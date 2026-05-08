                    nop                                     ;[0000] 00
                    nop                                     ;[0001] 00
                    nop                                     ;[0002] 00
                    nop                                     ;[0003] 00
                    nop                                     ;[0004] 00
                    nop                                     ;[0005] 00
                    nop                                     ;[0006] 00
                    nop                                     ;[0007] 00
                    ld        d,b                           ;[0008] 50
                    ld        c,h                           ;[0009] 4c
                    ld        d,l                           ;[000a] 55
                    ld        d,e                           ;[000b] 53
                    inc       sp                            ;[000c] 33
                    ld        b,h                           ;[000d] 44
                    ld        c,a                           ;[000e] 4f
                    ld        d,e                           ;[000f] 53
                    nop                                     ;[0010] 00
                    nop                                     ;[0011] 00
                    nop                                     ;[0012] 00
                    nop                                     ;[0013] 00
                    nop                                     ;[0014] 00
                    nop                                     ;[0015] 00
                    nop                                     ;[0016] 00
                    nop                                     ;[0017] 00
                    nop                                     ;[0018] 00
                    nop                                     ;[0019] 00
                    nop                                     ;[001a] 00
                    nop                                     ;[001b] 00
                    nop                                     ;[001c] 00
                    nop                                     ;[001d] 00
                    nop                                     ;[001e] 00
                    nop                                     ;[001f] 00
                    nop                                     ;[0020] 00
                    nop                                     ;[0021] 00
                    nop                                     ;[0022] 00
                    nop                                     ;[0023] 00
                    nop                                     ;[0024] 00
                    nop                                     ;[0025] 00
                    nop                                     ;[0026] 00
                    nop                                     ;[0027] 00
                    nop                                     ;[0028] 00
                    nop                                     ;[0029] 00
                    nop                                     ;[002a] 00
                    nop                                     ;[002b] 00
                    nop                                     ;[002c] 00
                    nop                                     ;[002d] 00
                    nop                                     ;[002e] 00
                    nop                                     ;[002f] 00
                    nop                                     ;[0030] 00
                    nop                                     ;[0031] 00
                    nop                                     ;[0032] 00
                    nop                                     ;[0033] 00
                    nop                                     ;[0034] 00
                    nop                                     ;[0035] 00
                    nop                                     ;[0036] 00
                    nop                                     ;[0037] 00
                    push      af                            ;[0038] f5
                    push      hl                            ;[0039] e5
                    ld        hl,($5c78)                    ;[003a] 2a 78 5c
                    inc       hl                            ;[003d] 23
                    ld        ($5c78),hl                    ;[003e] 22 78 5c
                    ld        a,h                           ;[0041] 7c
                    or        l                             ;[0042] b5
                    jr        nz,$0048                      ;[0043] 20 03
                    inc       (iy+$40)                      ;[0045] fd 34 40
                    push      bc                            ;[0048] c5
                    push      de                            ;[0049] d5
                    call      $2358                         ;[004a] cd 58 23
                    call      $0068                         ;[004d] cd 68 00
                    pop       de                            ;[0050] d1
                    pop       bc                            ;[0051] c1
                    pop       hl                            ;[0052] e1
                    pop       af                            ;[0053] f1
                    ei                                      ;[0054] fb
                    ret                                     ;[0055] c9

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
                    nop                                     ;[0060] 00
                    nop                                     ;[0061] 00
                    nop                                     ;[0062] 00
                    nop                                     ;[0063] 00
                    nop                                     ;[0064] 00
                    nop                                     ;[0065] 00
                    retn                                    ;[0066] ed 45

                    ld        bc,$7ffd                      ;[0068] 01 fd 7f
                    ld        a,($5b5c)                     ;[006b] 3a 5c 5b
                    or        $07                           ;[006e] f6 07
                    out       (c),a                         ;[0070] ed 79
                    ld        a,($e600)                     ;[0072] 3a 00 e6
                    or        a                             ;[0075] b7
                    jr        z,$0095                       ;[0076] 28 1d
                    ld        a,($5c78)                     ;[0078] 3a 78 5c
                    bit       0,a                           ;[007b] cb 47
                    jr        nz,$0095                      ;[007d] 20 16
                    ld        a,($e600)                     ;[007f] 3a 00 e6
                    dec       a                             ;[0082] 3d
                    ld        ($e600),a                     ;[0083] 32 00 e6
                    jr        nz,$0095                      ;[0086] 20 0d
                    ld        bc,$1ffd                      ;[0088] 01 fd 1f
                    ld        a,($5b67)                     ;[008b] 3a 67 5b
                    and       $f7                           ;[008e] e6 f7
                    ld        ($5b67),a                     ;[0090] 32 67 5b
                    out       (c),a                         ;[0093] ed 79
                    ld        bc,$7ffd                      ;[0095] 01 fd 7f
                    ld        a,($5b5c)                     ;[0098] 3a 5c 5b
                    out       (c),a                         ;[009b] ed 79
                    ret                                     ;[009d] c9

                    nop                                     ;[009e] 00
                    nop                                     ;[009f] 00
                    nop                                     ;[00a0] 00
                    nop                                     ;[00a1] 00
                    nop                                     ;[00a2] 00
                    nop                                     ;[00a3] 00
                    nop                                     ;[00a4] 00
                    nop                                     ;[00a5] 00
                    nop                                     ;[00a6] 00
                    nop                                     ;[00a7] 00
                    nop                                     ;[00a8] 00
                    nop                                     ;[00a9] 00
                    nop                                     ;[00aa] 00
                    nop                                     ;[00ab] 00
                    nop                                     ;[00ac] 00
                    nop                                     ;[00ad] 00
                    nop                                     ;[00ae] 00
                    nop                                     ;[00af] 00
                    nop                                     ;[00b0] 00
                    nop                                     ;[00b1] 00
                    nop                                     ;[00b2] 00
                    nop                                     ;[00b3] 00
                    nop                                     ;[00b4] 00
                    nop                                     ;[00b5] 00
                    nop                                     ;[00b6] 00
                    nop                                     ;[00b7] 00
                    nop                                     ;[00b8] 00
                    nop                                     ;[00b9] 00
                    nop                                     ;[00ba] 00
                    nop                                     ;[00bb] 00
                    nop                                     ;[00bc] 00
                    nop                                     ;[00bd] 00
                    nop                                     ;[00be] 00
                    nop                                     ;[00bf] 00
                    nop                                     ;[00c0] 00
                    nop                                     ;[00c1] 00
                    nop                                     ;[00c2] 00
                    nop                                     ;[00c3] 00
                    nop                                     ;[00c4] 00
                    nop                                     ;[00c5] 00
                    nop                                     ;[00c6] 00
                    nop                                     ;[00c7] 00
                    nop                                     ;[00c8] 00
                    nop                                     ;[00c9] 00
                    nop                                     ;[00ca] 00
                    nop                                     ;[00cb] 00
                    nop                                     ;[00cc] 00
                    nop                                     ;[00cd] 00
                    nop                                     ;[00ce] 00
                    nop                                     ;[00cf] 00
                    nop                                     ;[00d0] 00
                    nop                                     ;[00d1] 00
                    nop                                     ;[00d2] 00
                    nop                                     ;[00d3] 00
                    nop                                     ;[00d4] 00
                    nop                                     ;[00d5] 00
                    nop                                     ;[00d6] 00
                    nop                                     ;[00d7] 00
                    nop                                     ;[00d8] 00
                    nop                                     ;[00d9] 00
                    nop                                     ;[00da] 00
                    nop                                     ;[00db] 00
                    nop                                     ;[00dc] 00
                    nop                                     ;[00dd] 00
                    nop                                     ;[00de] 00
                    nop                                     ;[00df] 00
                    nop                                     ;[00e0] 00
                    nop                                     ;[00e1] 00
                    nop                                     ;[00e2] 00
                    nop                                     ;[00e3] 00
                    nop                                     ;[00e4] 00
                    nop                                     ;[00e5] 00
                    nop                                     ;[00e6] 00
                    nop                                     ;[00e7] 00
                    nop                                     ;[00e8] 00
                    nop                                     ;[00e9] 00
                    nop                                     ;[00ea] 00
                    nop                                     ;[00eb] 00
                    nop                                     ;[00ec] 00
                    nop                                     ;[00ed] 00
                    nop                                     ;[00ee] 00
                    nop                                     ;[00ef] 00
                    nop                                     ;[00f0] 00
                    nop                                     ;[00f1] 00
                    nop                                     ;[00f2] 00
                    nop                                     ;[00f3] 00
                    nop                                     ;[00f4] 00
                    nop                                     ;[00f5] 00
                    nop                                     ;[00f6] 00
                    nop                                     ;[00f7] 00
                    nop                                     ;[00f8] 00
                    nop                                     ;[00f9] 00
                    nop                                     ;[00fa] 00
                    nop                                     ;[00fb] 00
                    nop                                     ;[00fc] 00
                    nop                                     ;[00fd] 00
                    nop                                     ;[00fe] 00
                    nop                                     ;[00ff] 00
                    jp        $019f                         ;[0100] c3 9f 01
                    jp        $01cd                         ;[0103] c3 cd 01
                    jp        $062d                         ;[0106] c3 2d 06
                    jp        $0740                         ;[0109] c3 40 07
                    jp        $0761                         ;[010c] c3 61 07
                    jp        $08b1                         ;[010f] c3 b1 08
                    jp        $10ea                         ;[0112] c3 ea 10
                    jp        $11fe                         ;[0115] c3 fe 11
                    jp        $11a8                         ;[0118] c3 a8 11
                    jp        $1298                         ;[011b] c3 98 12
                    jp        $0a19                         ;[011e] c3 19 0a
                    jp        $08f2                         ;[0121] c3 f2 08
                    jp        $0924                         ;[0124] c3 24 09
                    jp        $096f                         ;[0127] c3 6f 09
                    jp        $1ace                         ;[012a] c3 ce 1a
                    jp        $090f                         ;[012d] c3 0f 09
                    jp        $08fc                         ;[0130] c3 fc 08
                    jp        $1070                         ;[0133] c3 70 10
                    jp        $108c                         ;[0136] c3 8c 10
                    jp        $1079                         ;[0139] c3 79 10
                    jp        $01d8                         ;[013c] c3 d8 01
                    jp        $01de                         ;[013f] c3 de 01
                    jp        $05c2                         ;[0142] c3 c2 05
                    jp        $08c3                         ;[0145] c3 c3 08
                    jp        $0959                         ;[0148] c3 59 09
                    jp        $0706                         ;[014b] c3 06 07
                    jp        $02e8                         ;[014e] c3 e8 02
                    jp        $1847                         ;[0151] c3 47 18
                    jp        $1943                         ;[0154] c3 43 19
                    jp        $1f27                         ;[0157] c3 27 1f
                    jp        $1f32                         ;[015a] c3 32 1f
                    jp        $1f47                         ;[015d] c3 47 1f
                    jp        $1e7c                         ;[0160] c3 7c 1e
                    jp        $1bff                         ;[0163] c3 ff 1b
                    jp        $1c0d                         ;[0166] c3 0d 1c
                    jp        $1c16                         ;[0169] c3 16 1c
                    jp        $1c24                         ;[016c] c3 24 1c
                    jp        $1c36                         ;[016f] c3 36 1c
                    jp        $1e65                         ;[0172] c3 65 1e
                    jp        $1c80                         ;[0175] c3 80 1c
                    jp        $1cdb                         ;[0178] c3 db 1c
                    jp        $1edd                         ;[017b] c3 dd 1e
                    jp        $1ee9                         ;[017e] c3 e9 1e
                    jp        $1e75                         ;[0181] c3 75 1e
                    jp        $1bda                         ;[0184] c3 da 1b
                    jp        $1cee                         ;[0187] c3 ee 1c
                    jp        $1d30                         ;[018a] c3 30 1d
                    jp        $1f76                         ;[018d] c3 76 1f
                    jp        $20c3                         ;[0190] c3 c3 20
                    jp        $20cc                         ;[0193] c3 cc 20
                    jp        $212b                         ;[0196] c3 2b 21
                    jp        $2150                         ;[0199] c3 50 21
                    jp        $2164                         ;[019c] c3 64 21
                    ld        hl,$db00                      ;[019f] 21 00 db
                    ld        de,$db01                      ;[01a2] 11 01 db
                    ld        bc,$09ff                      ;[01a5] 01 ff 09
                    ld        (hl),$00                      ;[01a8] 36 00
                    ldir                                    ;[01aa] ed b0
                    call      $1f27                         ;[01ac] cd 27 1f
                    ld        hl,$0080                      ;[01af] 21 80 00
                    ld        d,h                           ;[01b2] 54
                    ld        e,h                           ;[01b3] 5c
                    jr        nc,$01c2                      ;[01b4] 30 0c
                    call      $1f32                         ;[01b6] cd 32 1f
                    call      $17d0                         ;[01b9] cd d0 17
                    ld        hl,$0878                      ;[01bc] 21 78 08
                    ld        de,$0008                      ;[01bf] 11 08 00
                    push      de                            ;[01c2] d5
                    call      $1820                         ;[01c3] cd 20 18
                    pop       de                            ;[01c6] d1
                    call      $1539                         ;[01c7] cd 39 15
                    jp        $0500                         ;[01ca] c3 00 05
                    xor       a                             ;[01cd] af
                    ld        b,a                           ;[01ce] 47
                    ld        c,a                           ;[01cf] 4f
                    ld        de,$0100                      ;[01d0] 11 00 01
                    ld        hl,$0069                      ;[01d3] 21 69 00
                    scf                                     ;[01d6] 37
                    ret                                     ;[01d7] c9

                    call      $1a48                         ;[01d8] cd 48 1a
                    jp        $1530                         ;[01db] c3 30 15
                    push      de                            ;[01de] d5
                    ex        de,hl                         ;[01df] eb
                    call      $1a48                         ;[01e0] cd 48 1a
                    or        a                             ;[01e3] b7
                    sbc       hl,de                         ;[01e4] ed 52
                    ex        de,hl                         ;[01e6] eb
                    scf                                     ;[01e7] 37
                    call      nz,$1a52                      ;[01e8] c4 52 1a
                    pop       de                            ;[01eb] d1
                    ret       nc                            ;[01ec] d0
                    ex        de,hl                         ;[01ed] eb
                    call      $1530                         ;[01ee] cd 30 15
                    ex        de,hl                         ;[01f1] eb
                    or        a                             ;[01f2] b7
                    sbc       hl,de                         ;[01f3] ed 52
                    add       hl,de                         ;[01f5] 19
                    scf                                     ;[01f6] 37
                    call      nz,$1535                      ;[01f7] c4 35 15
                    ret                                     ;[01fa] c9

                    inc       c                             ;[01fb] 0c
                    dec       c                             ;[01fc] 0d
                    jr        nz,$0202                      ;[01fd] 20 03
                    inc       b                             ;[01ff] 04
                    dec       b                             ;[0200] 05
                    ret       z                             ;[0201] c8
                    call      $0207                         ;[0202] cd 07 02
                    ldir                                    ;[0205] ed b0
                    push      hl                            ;[0207] e5
                    push      bc                            ;[0208] c5
                    ld        b,a                           ;[0209] 47
                    ld        hl,$5b5c                      ;[020a] 21 5c 5b
                    ld        a,(hl)                        ;[020d] 7e
                    and       $07                           ;[020e] e6 07
                    push      af                            ;[0210] f5
                    cp        b                             ;[0211] b8
                    jr        z,$0227                       ;[0212] 28 13
                    ld        a,(hl)                        ;[0214] 7e
                    and       $f8                           ;[0215] e6 f8
                    or        b                             ;[0217] b0
                    ld        b,a                           ;[0218] 47
                    ld        a,r                           ;[0219] ed 5f
                    ld        a,b                           ;[021b] 78
                    ld        bc,$7ffd                      ;[021c] 01 fd 7f
                    di                                      ;[021f] f3
                    ld        (hl),a                        ;[0220] 77
                    out       (c),a                         ;[0221] ed 79
                    jp        po,$0227                      ;[0223] e2 27 02
                    ei                                      ;[0226] fb
                    pop       af                            ;[0227] f1
                    pop       bc                            ;[0228] c1
                    pop       hl                            ;[0229] e1
                    ret                                     ;[022a] c9

                    add       a                             ;[022b] 87
                    ld        l,a                           ;[022c] 6f
                    or        $c0                           ;[022d] f6 c0
                    ld        h,a                           ;[022f] 67
                    ld        a,l                           ;[0230] 7d
                    ld        l,$00                         ;[0231] 2e 00
                    rlca                                    ;[0233] 07
                    rlca                                    ;[0234] 07
                    rlca                                    ;[0235] 07
                    and       $06                           ;[0236] e6 06
                    cp        $04                           ;[0238] fe 04
                    ret       nc                            ;[023a] d0
                    inc       a                             ;[023b] 3c
                    ret                                     ;[023c] c9

                    ld        a,c                           ;[023d] 79
                    cp        b                             ;[023e] b8
                    jr        nz,$0247                      ;[023f] 20 06
                    push      ix                            ;[0241] dd e5
                    pop       bc                            ;[0243] c1
                    jp        $01fb                         ;[0244] c3 fb 01
                    push      bc                            ;[0247] c5
                    call      $02c4                         ;[0248] cd c4 02
                    call      $01fb                         ;[024b] cd fb 01
                    pop       bc                            ;[024e] c1
                    push      bc                            ;[024f] c5
                    ld        a,b                           ;[0250] 78
                    ex        de,hl                         ;[0251] eb
                    call      $02c4                         ;[0252] cd c4 02
                    ex        de,hl                         ;[0255] eb
                    call      $01fb                         ;[0256] cd fb 01
                    push      ix                            ;[0259] dd e5
                    pop       bc                            ;[025b] c1
                    ld        a,b                           ;[025c] 78
                    or        c                             ;[025d] b1
                    pop       bc                            ;[025e] c1
                    ret       z                             ;[025f] c8
                    ld        a,r                           ;[0260] ed 5f
                    di                                      ;[0262] f3
                    push      af                            ;[0263] f5
                    or        a                             ;[0264] b7
                    call      $0273                         ;[0265] cd 73 02
                    call      $0288                         ;[0268] cd 88 02
                    scf                                     ;[026b] 37
                    call      $0273                         ;[026c] cd 73 02
                    pop       af                            ;[026f] f1
                    ret       po                            ;[0270] e0
                    ei                                      ;[0271] fb
                    ret                                     ;[0272] c9

                    push      hl                            ;[0273] e5
                    push      de                            ;[0274] d5
                    push      bc                            ;[0275] c5
                    ld        bc,$0020                      ;[0276] 01 20 00
                    ld        de,$db00                      ;[0279] 11 00 db
                    ld        hl,$bfe0                      ;[027c] 21 e0 bf
                    jr        nc,$0282                      ;[027f] 30 01
                    ex        de,hl                         ;[0281] eb
                    ldir                                    ;[0282] ed b0
                    pop       bc                            ;[0284] c1
                    pop       de                            ;[0285] d1
                    pop       hl                            ;[0286] e1
                    ret                                     ;[0287] c9

                    push      ix                            ;[0288] dd e5
                    ex        (sp),hl                       ;[028a] e3
                    ld        a,h                           ;[028b] 7c
                    or        a                             ;[028c] b7
                    jr        nz,$0294                      ;[028d] 20 05
                    ld        a,l                           ;[028f] 7d
                    cp        $20                           ;[0290] fe 20
                    jr        c,$0296                       ;[0292] 38 02
                    ld        a,$20                         ;[0294] 3e 20
                    push      bc                            ;[0296] c5
                    ld        c,a                           ;[0297] 4f
                    ld        b,$00                         ;[0298] 06 00
                    or        a                             ;[029a] b7
                    sbc       hl,bc                         ;[029b] ed 42
                    pop       bc                            ;[029d] c1
                    ex        (sp),hl                       ;[029e] e3
                    pop       ix                            ;[029f] dd e1
                    or        a                             ;[02a1] b7
                    ret       z                             ;[02a2] c8
                    push      de                            ;[02a3] d5
                    push      bc                            ;[02a4] c5
                    push      af                            ;[02a5] f5
                    ld        b,a                           ;[02a6] 47
                    ld        a,c                           ;[02a7] 79
                    ld        c,b                           ;[02a8] 48
                    ld        b,$00                         ;[02a9] 06 00
                    ld        de,$bfe0                      ;[02ab] 11 e0 bf
                    call      $01fb                         ;[02ae] cd fb 01
                    pop       af                            ;[02b1] f1
                    pop       bc                            ;[02b2] c1
                    pop       de                            ;[02b3] d1
                    push      hl                            ;[02b4] e5
                    push      bc                            ;[02b5] c5
                    ld        c,a                           ;[02b6] 4f
                    ld        a,b                           ;[02b7] 78
                    ld        b,$00                         ;[02b8] 06 00
                    ld        hl,$bfe0                      ;[02ba] 21 e0 bf
                    call      $01fb                         ;[02bd] cd fb 01
                    pop       bc                            ;[02c0] c1
                    pop       hl                            ;[02c1] e1
                    jr        $0288                         ;[02c2] 18 c4
                    push      hl                            ;[02c4] e5
                    ld        bc,$0000                      ;[02c5] 01 00 00
                    ld        hl,$c000                      ;[02c8] 21 00 c0
                    or        a                             ;[02cb] b7
                    sbc       hl,de                         ;[02cc] ed 52
                    jr        c,$02e6                       ;[02ce] 38 16
                    jr        z,$02e6                       ;[02d0] 28 14
                    push      ix                            ;[02d2] dd e5
                    pop       bc                            ;[02d4] c1
                    or        a                             ;[02d5] b7
                    sbc       hl,bc                         ;[02d6] ed 42
                    add       hl,bc                         ;[02d8] 09
                    jr        nc,$02dd                      ;[02d9] 30 02
                    ld        b,h                           ;[02db] 44
                    ld        c,l                           ;[02dc] 4d
                    push      ix                            ;[02dd] dd e5
                    pop       hl                            ;[02df] e1
                    or        a                             ;[02e0] b7
                    sbc       hl,bc                         ;[02e1] ed 42
                    push      hl                            ;[02e3] e5
                    pop       ix                            ;[02e4] dd e1
                    pop       hl                            ;[02e6] e1
                    ret                                     ;[02e7] c9

                    or        a                             ;[02e8] b7
                    jr        nz,$02ee                      ;[02e9] 20 03
                    ld        hl,$0000                      ;[02eb] 21 00 00
                    ld        de,($db20)                    ;[02ee] ed 5b 20 db
                    ld        ($db20),hl                    ;[02f2] 22 20 db
                    ex        de,hl                         ;[02f5] eb
                    ret                                     ;[02f6] c9

                    ld        b,a                           ;[02f7] 47
                    ld        hl,($db20)                    ;[02f8] 2a 20 db
                    ld        a,h                           ;[02fb] 7c
                    or        l                             ;[02fc] b5
                    ld        a,b                           ;[02fd] 78
                    jr        nz,$0302                      ;[02fe] 20 02
                    inc       l                             ;[0300] 2c
                    ret                                     ;[0301] c9

                    push      bc                            ;[0302] c5
                    call      $033a                         ;[0303] cd 3a 03
                    push      hl                            ;[0306] e5
                    ld        de,$db22                      ;[0307] 11 22 db
                    push      de                            ;[030a] d5
                    ld        hl,$04cc                      ;[030b] 21 cc 04
                    ld        bc,$0007                      ;[030e] 01 07 00
                    ldir                                    ;[0311] ed b0
                    pop       de                            ;[0313] d1
                    pop       hl                            ;[0314] e1
                    push      de                            ;[0315] d5
                    call      $0332                         ;[0316] cd 32 03
                    pop       de                            ;[0319] d1
                    ld        hl,$04d3                      ;[031a] 21 d3 04
                    ld        b,a                           ;[031d] 47
                    ld        a,(de)                        ;[031e] 1a
                    cp        $ff                           ;[031f] fe ff
                    jr        z,$032b                       ;[0321] 28 08
                    cp        b                             ;[0323] b8
                    ld        a,(hl)                        ;[0324] 7e
                    jr        z,$032c                       ;[0325] 28 05
                    inc       de                            ;[0327] 13
                    inc       hl                            ;[0328] 23
                    jr        $031e                         ;[0329] 18 f3
                    xor       a                             ;[032b] af
                    sub       $01                           ;[032c] d6 01
                    ccf                                     ;[032e] 3f
                    pop       bc                            ;[032f] c1
                    ld        a,b                           ;[0330] 78
                    ret                                     ;[0331] c9

                    push      hl                            ;[0332] e5
                    ld        hl,($db20)                    ;[0333] 2a 20 db
                    ex        (sp),hl                       ;[0336] e3
                    ret                                     ;[0337] c9

                    ld        a,$0a                         ;[0338] 3e 0a
                    ld        ix,$db29                      ;[033a] dd 21 29 db
                    push      ix                            ;[033e] dd e5
                    call      $0349                         ;[0340] cd 49 03
                    ld        (ix+$00),$ff                  ;[0343] dd 36 00 ff
                    pop       hl                            ;[0347] e1
                    ret                                     ;[0348] c9

                    and       $7f                           ;[0349] e6 7f
                    ld        hl,$03ae                      ;[034b] 21 ae 03
                    ld        b,a                           ;[034e] 47
                    inc       b                             ;[034f] 04
                    jr        $0357                         ;[0350] 18 05
                    ld        a,(hl)                        ;[0352] 7e
                    inc       hl                            ;[0353] 23
                    inc       a                             ;[0354] 3c
                    jr        nz,$0352                      ;[0355] 20 fb
                    djnz      $0352                         ;[0357] 10 f9
                    ld        a,(hl)                        ;[0359] 7e
                    inc       hl                            ;[035a] 23
                    cp        $ff                           ;[035b] fe ff
                    ret       z                             ;[035d] c8
                    push      hl                            ;[035e] e5
                    call      $0365                         ;[035f] cd 65 03
                    pop       hl                            ;[0362] e1
                    jr        $0359                         ;[0363] 18 f4
                    or        a                             ;[0365] b7
                    jp        p,$03a8                       ;[0366] f2 a8 03
                    cp        $fe                           ;[0369] fe fe
                    jr        z,$03a7                       ;[036b] 28 3a
                    cp        $fd                           ;[036d] fe fd
                    jr        z,$0378                       ;[036f] 28 07
                    cp        $fc                           ;[0371] fe fc
                    jr        nz,$0349                      ;[0373] 20 d4
                    ld        a,e                           ;[0375] 7b
                    jr        $0379                         ;[0376] 18 01
                    ld        a,d                           ;[0378] 7a
                    push      de                            ;[0379] d5
                    push      bc                            ;[037a] c5
                    ld        l,a                           ;[037b] 6f
                    ld        h,$00                         ;[037c] 26 00
                    ld        d,h                           ;[037e] 54
                    ld        bc,$ff9c                      ;[037f] 01 9c ff
                    call      $0392                         ;[0382] cd 92 03
                    ld        bc,$fff6                      ;[0385] 01 f6 ff
                    call      $0392                         ;[0388] cd 92 03
                    ld        a,l                           ;[038b] 7d
                    add       $30                           ;[038c] c6 30
                    pop       bc                            ;[038e] c1
                    pop       de                            ;[038f] d1
                    jr        $03a8                         ;[0390] 18 16
                    ld        a,$ff                         ;[0392] 3e ff
                    push      hl                            ;[0394] e5
                    inc       a                             ;[0395] 3c
                    add       hl,bc                         ;[0396] 09
                    jr        nc,$039d                      ;[0397] 30 04
                    ex        (sp),hl                       ;[0399] e3
                    pop       hl                            ;[039a] e1
                    jr        $0394                         ;[039b] 18 f7
                    pop       hl                            ;[039d] e1
                    or        a                             ;[039e] b7
                    jr        z,$03a3                       ;[039f] 28 02
                    ld        d,$30                         ;[03a1] 16 30
                    add       d                             ;[03a3] 82
                    ret       z                             ;[03a4] c8
                    jr        $03a8                         ;[03a5] 18 01
                    ld        a,c                           ;[03a7] 79
                    ld        (ix+$00),a                    ;[03a8] dd 77 00
                    inc       ix                            ;[03ab] dd 23
                    ret                                     ;[03ad] c9

                    adc       e                             ;[03ae] 8b
                    ld        l,(hl)                        ;[03af] 6e
                    ld        l,a                           ;[03b0] 6f
                    ld        (hl),h                        ;[03b1] 74
                    jr        nz,$0426                      ;[03b2] 20 72
                    ld        h,l                           ;[03b4] 65
                    ld        h,c                           ;[03b5] 61
                    ld        h,h                           ;[03b6] 64
                    ld        a,c                           ;[03b7] 79
                    adc       a                             ;[03b8] 8f
                    rst       $38                           ;[03b9] ff
                    adc       h                             ;[03ba] 8c
                    ld        (hl),a                        ;[03bb] 77
                    ld        (hl),d                        ;[03bc] 72
                    ld        l,c                           ;[03bd] 69
                    ld        (hl),h                        ;[03be] 74
                    ld        h,l                           ;[03bf] 65
                    jr        nz,$0432                      ;[03c0] 20 70
                    ld        (hl),d                        ;[03c2] 72
                    ld        l,a                           ;[03c3] 6f
                    ld        (hl),h                        ;[03c4] 74
                    ld        h,l                           ;[03c5] 65
                    ld        h,e                           ;[03c6] 63
                    ld        (hl),h                        ;[03c7] 74
                    ld        h,l                           ;[03c8] 65
                    ld        h,h                           ;[03c9] 64
                    adc       a                             ;[03ca] 8f
                    rst       $38                           ;[03cb] ff
                    adc       l                             ;[03cc] 8d
                    ld        (hl),e                        ;[03cd] 73
                    ld        h,l                           ;[03ce] 65
                    ld        h,l                           ;[03cf] 65
                    ld        l,e                           ;[03d0] 6b
                    jr        nz,$0439                      ;[03d1] 20 66
                    ld        h,c                           ;[03d3] 61
                    ld        l,c                           ;[03d4] 69
                    ld        l,h                           ;[03d5] 6c
                    adc       a                             ;[03d6] 8f
                    rst       $38                           ;[03d7] ff
                    adc       (hl)                          ;[03d8] 8e
                    ld        h,h                           ;[03d9] 64
                    ld        h,c                           ;[03da] 61
                    ld        (hl),h                        ;[03db] 74
                    ld        h,c                           ;[03dc] 61
                    jr        nz,$0444                      ;[03dd] 20 65
                    ld        (hl),d                        ;[03df] 72
                    ld        (hl),d                        ;[03e0] 72
                    ld        l,a                           ;[03e1] 6f
                    ld        (hl),d                        ;[03e2] 72
                    adc       a                             ;[03e3] 8f
                    rst       $38                           ;[03e4] ff
                    adc       (hl)                          ;[03e5] 8e
                    ld        l,(hl)                        ;[03e6] 6e
                    ld        l,a                           ;[03e7] 6f
                    jr        nz,$044e                      ;[03e8] 20 64
                    ld        h,c                           ;[03ea] 61
                    ld        (hl),h                        ;[03eb] 74
                    ld        h,c                           ;[03ec] 61
                    adc       a                             ;[03ed] 8f
                    rst       $38                           ;[03ee] ff
                    adc       (hl)                          ;[03ef] 8e
                    ld        l,l                           ;[03f0] 6d
                    ld        l,c                           ;[03f1] 69
                    ld        (hl),e                        ;[03f2] 73
                    ld        (hl),e                        ;[03f3] 73
                    ld        l,c                           ;[03f4] 69
                    ld        l,(hl)                        ;[03f5] 6e
                    ld        h,a                           ;[03f6] 67
                    jr        nz,$045a                      ;[03f7] 20 61
                    ld        h,h                           ;[03f9] 64
                    ld        h,h                           ;[03fa] 64
                    ld        (hl),d                        ;[03fb] 72
                    ld        h,l                           ;[03fc] 65
                    ld        (hl),e                        ;[03fd] 73
                    ld        (hl),e                        ;[03fe] 73
                    jr        nz,$046e                      ;[03ff] 20 6d
                    ld        h,c                           ;[0401] 61
                    ld        (hl),d                        ;[0402] 72
                    ld        l,e                           ;[0403] 6b
                    adc       a                             ;[0404] 8f
                    rst       $38                           ;[0405] ff
                    adc       e                             ;[0406] 8b
                    ld        h,d                           ;[0407] 62
                    ld        h,c                           ;[0408] 61
                    ld        h,h                           ;[0409] 64
                    jr        nz,$0472                      ;[040a] 20 66
                    ld        l,a                           ;[040c] 6f
                    ld        (hl),d                        ;[040d] 72
                    ld        l,l                           ;[040e] 6d
                    ld        h,c                           ;[040f] 61
                    ld        (hl),h                        ;[0410] 74
                    adc       a                             ;[0411] 8f
                    rst       $38                           ;[0412] ff
                    adc       (hl)                          ;[0413] 8e
                    ld        (hl),l                        ;[0414] 75
                    ld        l,(hl)                        ;[0415] 6e
                    ld        l,e                           ;[0416] 6b
                    ld        l,(hl)                        ;[0417] 6e
                    ld        l,a                           ;[0418] 6f
                    ld        (hl),a                        ;[0419] 77
                    ld        l,(hl)                        ;[041a] 6e
                    jr        nz,$0482                      ;[041b] 20 65
                    ld        (hl),d                        ;[041d] 72
                    ld        (hl),d                        ;[041e] 72
                    ld        l,a                           ;[041f] 6f
                    ld        (hl),d                        ;[0420] 72
                    adc       a                             ;[0421] 8f
                    rst       $38                           ;[0422] ff
                    adc       h                             ;[0423] 8c
                    ld        h,e                           ;[0424] 63
                    ld        l,b                           ;[0425] 68
                    ld        h,c                           ;[0426] 61
                    ld        l,(hl)                        ;[0427] 6e
                    ld        h,a                           ;[0428] 67
                    ld        h,l                           ;[0429] 65
                    ld        h,h                           ;[042a] 64
                    inc       l                             ;[042b] 2c
                    jr        nz,$049e                      ;[042c] 20 70
                    ld        l,h                           ;[042e] 6c
                    ld        h,l                           ;[042f] 65
                    ld        h,c                           ;[0430] 61
                    ld        (hl),e                        ;[0431] 73
                    ld        h,l                           ;[0432] 65
                    jr        nz,$04a7                      ;[0433] 20 72
                    ld        h,l                           ;[0435] 65
                    ld        (hl),b                        ;[0436] 70
                    ld        l,h                           ;[0437] 6c
                    ld        h,c                           ;[0438] 61
                    ld        h,e                           ;[0439] 63
                    ld        h,l                           ;[043a] 65
                    adc       a                             ;[043b] 8f
                    rst       $38                           ;[043c] ff
                    adc       h                             ;[043d] 8c
                    ld        (hl),l                        ;[043e] 75
                    ld        l,(hl)                        ;[043f] 6e
                    ld        (hl),e                        ;[0440] 73
                    ld        (hl),l                        ;[0441] 75
                    ld        l,c                           ;[0442] 69
                    ld        (hl),h                        ;[0443] 74
                    ld        h,c                           ;[0444] 61
                    ld        h,d                           ;[0445] 62
                    ld        l,h                           ;[0446] 6c
                    ld        h,l                           ;[0447] 65
                    adc       a                             ;[0448] 8f
                    rst       $38                           ;[0449] ff
                    ld        d,b                           ;[044a] 50
                    ld        l,h                           ;[044b] 6c
                    ld        h,l                           ;[044c] 65
                    ld        h,c                           ;[044d] 61
                    ld        (hl),e                        ;[044e] 73
                    ld        h,l                           ;[044f] 65
                    jr        nz,$04c2                      ;[0450] 20 70
                    ld        (hl),l                        ;[0452] 75
                    ld        (hl),h                        ;[0453] 74
                    jr        nz,$04ca                      ;[0454] 20 74
                    ld        l,b                           ;[0456] 68
                    ld        h,l                           ;[0457] 65
                    jr        nz,$04be                      ;[0458] 20 64
                    ld        l,c                           ;[045a] 69
                    ld        (hl),e                        ;[045b] 73
                    ld        l,e                           ;[045c] 6b
                    jr        nz,$04c5                      ;[045d] 20 66
                    ld        l,a                           ;[045f] 6f
                    ld        (hl),d                        ;[0460] 72
                    jr        nz,$0461                      ;[0461] 20 fe
                    ld        a,($6920)                     ;[0463] 3a 20 69
                    ld        l,(hl)                        ;[0466] 6e
                    ld        (hl),h                        ;[0467] 74
                    ld        l,a                           ;[0468] 6f
                    jr        nz,$04df                      ;[0469] 20 74
                    ld        l,b                           ;[046b] 68
                    ld        h,l                           ;[046c] 65
                    jr        nz,$04d3                      ;[046d] 20 64
                    ld        (hl),d                        ;[046f] 72
                    ld        l,c                           ;[0470] 69
                    halt                                    ;[0471] 76
                    ld        h,l                           ;[0472] 65
                    jr        nz,$04e9                      ;[0473] 20 74
                    ld        l,b                           ;[0475] 68
                    ld        h,l                           ;[0476] 65
                    ld        l,(hl)                        ;[0477] 6e
                    jr        nz,$04ea                      ;[0478] 20 70
                    ld        (hl),d                        ;[047a] 72
                    ld        h,l                           ;[047b] 65
                    ld        (hl),e                        ;[047c] 73
                    ld        (hl),e                        ;[047d] 73
                    jr        nz,$04e1                      ;[047e] 20 61
                    ld        l,(hl)                        ;[0480] 6e
                    ld        a,c                           ;[0481] 79
                    jr        nz,$04ef                      ;[0482] 20 6b
                    ld        h,l                           ;[0484] 65
                    ld        a,c                           ;[0485] 79
                    rst       $38                           ;[0486] ff
                    ld        b,h                           ;[0487] 44
                    ld        (hl),d                        ;[0488] 72
                    ld        l,c                           ;[0489] 69
                    halt                                    ;[048a] 76
                    ld        h,l                           ;[048b] 65
                    jr        nz,$048c                      ;[048c] 20 fe
                    ld        a,($ff20)                     ;[048e] 3a 20 ff
                    adc       e                             ;[0491] 8b
                    ld        h,h                           ;[0492] 64
                    ld        l,c                           ;[0493] 69
                    ld        (hl),e                        ;[0494] 73
                    ld        l,e                           ;[0495] 6b
                    jr        nz,$0497                      ;[0496] 20 ff
                    adc       e                             ;[0498] 8b
                    ld        (hl),h                        ;[0499] 74
                    ld        (hl),d                        ;[049a] 72
                    ld        h,c                           ;[049b] 61
                    ld        h,e                           ;[049c] 63
                    ld        l,e                           ;[049d] 6b
                    jr        nz,$049d                      ;[049e] 20 fd
                    inc       l                             ;[04a0] 2c
                    jr        nz,$04a2                      ;[04a1] 20 ff
                    adc       l                             ;[04a3] 8d
                    ld        (hl),e                        ;[04a4] 73
                    ld        h,l                           ;[04a5] 65
                    ld        h,e                           ;[04a6] 63
                    ld        (hl),h                        ;[04a7] 74
                    ld        l,a                           ;[04a8] 6f
                    ld        (hl),d                        ;[04a9] 72
                    jr        nz,$04a8                      ;[04aa] 20 fc
                    inc       l                             ;[04ac] 2c
                    jr        nz,$04ae                      ;[04ad] 20 ff
                    jr        nz,$04de                      ;[04af] 20 2d
                    jr        nz,$0505                      ;[04b1] 20 52
                    ld        h,l                           ;[04b3] 65
                    ld        (hl),h                        ;[04b4] 74
                    ld        (hl),d                        ;[04b5] 72
                    ld        a,c                           ;[04b6] 79
                    inc       l                             ;[04b7] 2c
                    jr        nz,$0503                      ;[04b8] 20 49
                    ld        h,a                           ;[04ba] 67
                    ld        l,(hl)                        ;[04bb] 6e
                    ld        l,a                           ;[04bc] 6f
                    ld        (hl),d                        ;[04bd] 72
                    ld        h,l                           ;[04be] 65
                    jr        nz,$0530                      ;[04bf] 20 6f
                    ld        (hl),d                        ;[04c1] 72
                    jr        nz,$0507                      ;[04c2] 20 43
                    ld        h,c                           ;[04c4] 61
                    ld        l,(hl)                        ;[04c5] 6e
                    ld        h,e                           ;[04c6] 63
                    ld        h,l                           ;[04c7] 65
                    ld        l,h                           ;[04c8] 6c
                    ccf                                     ;[04c9] 3f
                    jr        nz,$04cb                      ;[04ca] 20 ff
                    ld        (hl),d                        ;[04cc] 72
                    ld        d,d                           ;[04cd] 52
                    ld        l,c                           ;[04ce] 69
                    ld        c,c                           ;[04cf] 49
                    ld        h,e                           ;[04d0] 63
                    ld        b,e                           ;[04d1] 43
                    rst       $38                           ;[04d2] ff
                    ld        bc,$0201                      ;[04d3] 01 01 02
                    ld        (bc),a                        ;[04d6] 02
                    nop                                     ;[04d7] 00
                    nop                                     ;[04d8] 00
                    or        a                             ;[04d9] b7
                    ret       z                             ;[04da] c8
                    srl       d                             ;[04db] cb 3a
                    rr        e                             ;[04dd] cb 1b
                    dec       a                             ;[04df] 3d
                    jr        nz,$04db                      ;[04e0] 20 f9
                    ret                                     ;[04e2] c9

                    or        a                             ;[04e3] b7
                    ret       z                             ;[04e4] c8
                    ex        de,hl                         ;[04e5] eb
                    add       hl,hl                         ;[04e6] 29
                    dec       a                             ;[04e7] 3d
                    jr        nz,$04e6                      ;[04e8] 20 fc
                    ex        de,hl                         ;[04ea] eb
                    ret                                     ;[04eb] c9

                    jp        (hl)                          ;[04ec] e9
                    cp        $61                           ;[04ed] fe 61
                    ret       c                             ;[04ef] d8
                    cp        $7b                           ;[04f0] fe 7b
                    ret       nc                            ;[04f2] d0
                    add       $e0                           ;[04f3] c6 e0
                    ret                                     ;[04f5] c9

                    nop                                     ;[04f6] 00
                    nop                                     ;[04f7] 00
                    nop                                     ;[04f8] 00
                    nop                                     ;[04f9] 00
                    nop                                     ;[04fa] 00
                    nop                                     ;[04fb] 00
                    nop                                     ;[04fc] 00
                    nop                                     ;[04fd] 00
                    nop                                     ;[04fe] 00
                    nop                                     ;[04ff] 00
                    ld        bc,$1041                      ;[0500] 01 41 10
                    ld        a,c                           ;[0503] 79
                    ld        ($df94),a                     ;[0504] 32 94 df
                    call      $184d                         ;[0507] cd 4d 18
                    ret       c                             ;[050a] d8
                    inc       c                             ;[050b] 0c
                    djnz      $0503                         ;[050c] 10 f5
                    ld        a,$41                         ;[050e] 3e 41
                    ld        ($df94),a                     ;[0510] 32 94 df
                    ret                                     ;[0513] c9

                    call      $0525                         ;[0514] cd 25 05
                    ret       nc                            ;[0517] d0
                    rra                                     ;[0518] 1f
                    ld        a,$1d                         ;[0519] 3e 1d
                    ret                                     ;[051b] c9

                    call      $0525                         ;[051c] cd 25 05
                    ret       nc                            ;[051f] d0
                    rra                                     ;[0520] 1f
                    rra                                     ;[0521] 1f
                    ld        a,$1d                         ;[0522] 3e 1d
                    ret                                     ;[0524] c9

                    call      $054b                         ;[0525] cd 4b 05
                    ret       nc                            ;[0528] d0
                    rlca                                    ;[0529] 07
                    rra                                     ;[052a] 1f
                    ret       c                             ;[052b] d8
                    ld        a,$1d                         ;[052c] 3e 1d
                    ret                                     ;[052e] c9

                    call      $054b                         ;[052f] cd 4b 05
                    ret       nc                            ;[0532] d0
                    rla                                     ;[0533] 17
                    ccf                                     ;[0534] 3f
                    ld        a,$1d                         ;[0535] 3e 1d
                    ret       nc                            ;[0537] d0
                    push      hl                            ;[0538] e5
                    push      de                            ;[0539] d5
                    push      bc                            ;[053a] c5
                    ld        h,b                           ;[053b] 60
                    ld        l,c                           ;[053c] 69
                    ld        (hl),$00                      ;[053d] 36 00
                    ld        d,b                           ;[053f] 50
                    ld        e,c                           ;[0540] 59
                    inc       de                            ;[0541] 13
                    ld        bc,$0038                      ;[0542] 01 38 00
                    ldir                                    ;[0545] ed b0
                    pop       bc                            ;[0547] c1
                    pop       de                            ;[0548] d1
                    pop       hl                            ;[0549] e1
                    ret                                     ;[054a] c9

                    push      hl                            ;[054b] e5
                    push      de                            ;[054c] d5
                    ld        a,b                           ;[054d] 78
                    cp        $10                           ;[054e] fe 10
                    ld        a,$15                         ;[0550] 3e 15
                    jr        nc,$0566                      ;[0552] 30 12
                    ld        hl,$db68                      ;[0554] 21 68 db
                    ld        de,$0038                      ;[0557] 11 38 00
                    inc       b                             ;[055a] 04
                    add       hl,de                         ;[055b] 19
                    djnz      $055b                         ;[055c] 10 fd
                    ld        b,h                           ;[055e] 44
                    ld        c,l                           ;[055f] 4d
                    ld        hl,$0020                      ;[0560] 21 20 00
                    add       hl,bc                         ;[0563] 09
                    ld        a,(hl)                        ;[0564] 7e
                    scf                                     ;[0565] 37
                    pop       de                            ;[0566] d1
                    pop       hl                            ;[0567] e1
                    ret                                     ;[0568] c9

                    ld        hl,$0020                      ;[0569] 21 20 00
                    add       hl,bc                         ;[056c] 09
                    ld        e,(hl)                        ;[056d] 5e
                    ld        (hl),$00                      ;[056e] 36 00
                    push      hl                            ;[0570] e5
                    push      de                            ;[0571] d5
                    call      $0579                         ;[0572] cd 79 05
                    pop       de                            ;[0575] d1
                    pop       hl                            ;[0576] e1
                    ld        (hl),e                        ;[0577] 73
                    ret                                     ;[0578] c9

                    ld        hl,$dba0                      ;[0579] 21 a0 db
                    ld        e,$12                         ;[057c] 1e 12
                    push      hl                            ;[057e] e5
                    push      bc                            ;[057f] c5
                    ld        bc,$0020                      ;[0580] 01 20 00
                    add       hl,bc                         ;[0583] 09
                    ld        d,(hl)                        ;[0584] 56
                    inc       hl                            ;[0585] 23
                    ld        a,(hl)                        ;[0586] 7e
                    pop       bc                            ;[0587] c1
                    pop       hl                            ;[0588] e1
                    bit       7,d                           ;[0589] cb 7a
                    jr        z,$05b7                       ;[058b] 28 2a
                    push      hl                            ;[058d] e5
                    ld        hl,$0021                      ;[058e] 21 21 00
                    add       hl,bc                         ;[0591] 09
                    cp        (hl)                          ;[0592] be
                    pop       hl                            ;[0593] e1
                    jr        nz,$05b7                      ;[0594] 20 21
                    ld        a,(bc)                        ;[0596] 0a
                    cp        $22                           ;[0597] fe 22
                    jr        z,$05a3                       ;[0599] 28 08
                    ld        a,(hl)                        ;[059b] 7e
                    cp        $22                           ;[059c] fe 22
                    call      nz,$0d8a                      ;[059e] c4 8a 0d
                    jr        nz,$05b7                      ;[05a1] 20 14
                    push      hl                            ;[05a3] e5
                    ld        hl,$0020                      ;[05a4] 21 20 00
                    add       hl,bc                         ;[05a7] 09
                    ld        a,(hl)                        ;[05a8] 7e
                    rrca                                    ;[05a9] 0f
                    rrca                                    ;[05aa] 0f
                    and       $03                           ;[05ab] e6 03
                    ld        h,a                           ;[05ad] 67
                    ld        a,d                           ;[05ae] 7a
                    and       $03                           ;[05af] e6 03
                    or        h                             ;[05b1] b4
                    xor       h                             ;[05b2] ac
                    ld        a,$1e                         ;[05b3] 3e 1e
                    pop       hl                            ;[05b5] e1
                    ret       nz                            ;[05b6] c0
                    push      de                            ;[05b7] d5
                    ld        de,$0038                      ;[05b8] 11 38 00
                    add       hl,de                         ;[05bb] 19
                    pop       de                            ;[05bc] d1
                    dec       e                             ;[05bd] 1d
                    jr        nz,$057e                      ;[05be] 20 be
                    scf                                     ;[05c0] 37
                    ret                                     ;[05c1] c9

                    call      $04ed                         ;[05c2] cd ed 04
                    call      $0c27                         ;[05c5] cd 27 0c
                    ret       nc                            ;[05c8] d0
                    push      bc                            ;[05c9] c5
                    ld        bc,$dba0                      ;[05ca] 01 a0 db
                    ld        e,$12                         ;[05cd] 1e 12
                    ld        hl,$0020                      ;[05cf] 21 20 00
                    add       hl,bc                         ;[05d2] 09
                    bit       7,(hl)                        ;[05d3] cb 7e
                    jr        z,$05e4                       ;[05d5] 28 0d
                    inc       hl                            ;[05d7] 23
                    ld        a,(hl)                        ;[05d8] 7e
                    cp        (ix+$1c)                      ;[05d9] dd be 1c
                    scf                                     ;[05dc] 37
                    push      de                            ;[05dd] d5
                    call      z,$074c                       ;[05de] cc 4c 07
                    pop       de                            ;[05e1] d1
                    jr        nc,$05ee                      ;[05e2] 30 0a
                    ld        hl,$0038                      ;[05e4] 21 38 00
                    add       hl,bc                         ;[05e7] 09
                    ld        b,h                           ;[05e8] 44
                    ld        c,l                           ;[05e9] 4d
                    dec       e                             ;[05ea] 1d
                    jr        nz,$05cf                      ;[05eb] 20 e2
                    scf                                     ;[05ed] 37
                    pop       bc                            ;[05ee] c1
                    ret                                     ;[05ef] c9

                    ld        bc,$dba0                      ;[05f0] 01 a0 db
                    ld        e,$12                         ;[05f3] 1e 12
                    push      hl                            ;[05f5] e5
                    push      de                            ;[05f6] d5
                    ld        a,d                           ;[05f7] 7a
                    ex        de,hl                         ;[05f8] eb
                    ld        hl,$0020                      ;[05f9] 21 20 00
                    add       hl,bc                         ;[05fc] 09
                    bit       7,(hl)                        ;[05fd] cb 7e
                    jr        z,$061f                       ;[05ff] 28 1e
                    inc       hl                            ;[0601] 23
                    cp        (hl)                          ;[0602] be
                    jr        nz,$061f                      ;[0603] 20 1a
                    inc       hl                            ;[0605] 23
                    bit       3,(hl)                        ;[0606] cb 5e
                    jr        z,$061f                       ;[0608] 28 15
                    ld        hl,$002b                      ;[060a] 21 2b 00
                    add       hl,bc                         ;[060d] 09
                    ld        a,e                           ;[060e] 7b
                    cp        (hl)                          ;[060f] be
                    jr        nz,$061f                      ;[0610] 20 0d
                    inc       hl                            ;[0612] 23
                    ld        a,d                           ;[0613] 7a
                    cp        (hl)                          ;[0614] be
                    jr        nz,$061f                      ;[0615] 20 08
                    call      $0c20                         ;[0617] cd 20 0c
                    call      c,$132a                       ;[061a] dc 2a 13
                    jr        nc,$0626                      ;[061d] 30 07
                    ld        hl,$0038                      ;[061f] 21 38 00
                    add       hl,bc                         ;[0622] 09
                    ld        b,h                           ;[0623] 44
                    ld        c,l                           ;[0624] 4d
                    scf                                     ;[0625] 37
                    pop       de                            ;[0626] d1
                    pop       hl                            ;[0627] e1
                    ret       nc                            ;[0628] d0
                    dec       e                             ;[0629] 1d
                    jr        nz,$05f5                      ;[062a] 20 c9
                    ret                                     ;[062c] c9

                    push      de                            ;[062d] d5
                    push      bc                            ;[062e] c5
                    call      $052f                         ;[062f] cd 2f 05
                    call      c,$0adf                       ;[0632] dc df 0a
                    call      c,$0c20                       ;[0635] dc 20 0c
                    pop       hl                            ;[0638] e1
                    pop       de                            ;[0639] d1
                    ret       nc                            ;[063a] d0
                    push      de                            ;[063b] d5
                    ld        a,l                           ;[063c] 7d
                    ld        hl,$0020                      ;[063d] 21 20 00
                    add       hl,bc                         ;[0640] 09
                    ld        (hl),a                        ;[0641] 77
                    call      $0579                         ;[0642] cd 79 05
                    ld        hl,$0d8a                      ;[0645] 21 8a 0d
                    call      c,$0dae                       ;[0648] dc ae 0d
                    pop       de                            ;[064b] d1
                    ret       nc                            ;[064c] d0
                    jr        nz,$067f                      ;[064d] 20 30
                    ld        a,e                           ;[064f] 7b
                    or        a                             ;[0650] b7
                    ld        a,$18                         ;[0651] 3e 18
                    ret       z                             ;[0653] c8
                    dec       e                             ;[0654] 1d
                    jr        nz,$065f                      ;[0655] 20 08
                    call      $06c4                         ;[0657] cd c4 06
                    call      c,$0801                       ;[065a] dc 01 08
                    jr        $069b                         ;[065d] 18 3c
                    dec       e                             ;[065f] 1d
                    jr        nz,$066a                      ;[0660] 20 08
                    call      $06c4                         ;[0662] cd c4 06
                    call      c,$0859                       ;[0665] dc 59 08
                    jr        $069b                         ;[0668] 18 31
                    push      de                            ;[066a] d5
                    dec       e                             ;[066b] 1d
                    jr        nz,$0676                      ;[066c] 20 08
                    call      $06e0                         ;[066e] cd e0 06
                    call      c,$0983                       ;[0671] dc 83 09
                    jr        $067d                         ;[0674] 18 07
                    or        a                             ;[0676] b7
                    ld        a,$15                         ;[0677] 3e 15
                    dec       e                             ;[0679] 1d
                    call      z,$092e                       ;[067a] cc 2e 09
                    pop       de                            ;[067d] d1
                    ret       nc                            ;[067e] d0
                    ld        a,d                           ;[067f] 7a
                    or        a                             ;[0680] b7
                    ld        a,$17                         ;[0681] 3e 17
                    ret       z                             ;[0683] c8
                    dec       d                             ;[0684] 15
                    jr        nz,$068f                      ;[0685] 20 08
                    call      $06a9                         ;[0687] cd a9 06
                    call      c,$07dc                       ;[068a] dc dc 07
                    jr        $0696                         ;[068d] 18 07
                    or        a                             ;[068f] b7
                    ld        a,$15                         ;[0690] 3e 15
                    dec       d                             ;[0692] 15
                    call      z,$06a9                       ;[0693] cc a9 06
                    ret       nc                            ;[0696] d0
                    xor       a                             ;[0697] af
                    scf                                     ;[0698] 37
                    jr        $069d                         ;[0699] 18 02
                    ret       nc                            ;[069b] d0
                    sbc       a                             ;[069c] 9f
                    push      af                            ;[069d] f5
                    ld        hl,$0020                      ;[069e] 21 20 00
                    add       hl,bc                         ;[06a1] 09
                    set       7,(hl)                        ;[06a2] cb fe
                    inc       (ix+$21)                      ;[06a4] dd 34 21
                    pop       af                            ;[06a7] f1
                    ret                                     ;[06a8] c9

                    ld        hl,$0020                      ;[06a9] 21 20 00
                    add       hl,bc                         ;[06ac] 09
                    ld        a,(hl)                        ;[06ad] 7e
                    rra                                     ;[06ae] 1f
                    rra                                     ;[06af] 1f
                    ld        a,$1e                         ;[06b0] 3e 1e
                    call      c,$18f3                       ;[06b2] dc f3 18
                    ld        hl,$0000                      ;[06b5] 21 00 00
                    call      c,$0cbe                       ;[06b8] dc be 0c
                    ret       nc                            ;[06bb] d0
                    ld        hl,$0022                      ;[06bc] 21 22 00
                    add       hl,bc                         ;[06bf] 09
                    set       0,(hl)                        ;[06c0] cb c6
                    scf                                     ;[06c2] 37
                    ret                                     ;[06c3] c9

                    call      $0d4a                         ;[06c4] cd 4a 0d
                    jr        nc,$06d8                      ;[06c7] 30 0f
                    ld        hl,$0020                      ;[06c9] 21 20 00
                    add       hl,bc                         ;[06cc] 09
                    bit       1,(hl)                        ;[06cd] cb 4e
                    scf                                     ;[06cf] 37
                    ret       z                             ;[06d0] c8
                    call      $0ebe                         ;[06d1] cd be 0e
                    call      c,$18f3                       ;[06d4] dc f3 18
                    ret                                     ;[06d7] c9

                    cp        $19                           ;[06d8] fe 19
                    scf                                     ;[06da] 37
                    ccf                                     ;[06db] 3f
                    ret       nz                            ;[06dc] c0
                    ld        a,$17                         ;[06dd] 3e 17
                    ret                                     ;[06df] c9

                    push      bc                            ;[06e0] c5
                    ld        h,b                           ;[06e1] 60
                    ld        l,c                           ;[06e2] 69
                    ld        de,$df20                      ;[06e3] 11 20 df
                    ld        bc,$0038                      ;[06e6] 01 38 00
                    ldir                                    ;[06e9] ed b0
                    pop       bc                            ;[06eb] c1
                    ld        a,$42                         ;[06ec] 3e 42
                    ld        ($df29),a                     ;[06ee] 32 29 df
                    ld        hl,$4b41                      ;[06f1] 21 41 4b
                    ld        ($df2a),hl                    ;[06f4] 22 2a df
                    push      bc                            ;[06f7] c5
                    ld        bc,$df20                      ;[06f8] 01 20 df
                    call      $092e                         ;[06fb] cd 2e 09
                    pop       bc                            ;[06fe] c1
                    ret       c                             ;[06ff] d8
                    cp        $17                           ;[0700] fe 17
                    scf                                     ;[0702] 37
                    ret       z                             ;[0703] c8
                    or        a                             ;[0704] b7
                    ret                                     ;[0705] c9

                    call      $04ed                         ;[0706] cd ed 04
                    ld        d,a                           ;[0709] 57
                    ld        e,c                           ;[070a] 59
                    call      $052f                         ;[070b] cd 2f 05
                    ret       nc                            ;[070e] d0
                    ld        a,$22                         ;[070f] 3e 22
                    ld        (bc),a                        ;[0711] 02
                    ld        hl,$0020                      ;[0712] 21 20 00
                    add       hl,bc                         ;[0715] 09
                    ld        (hl),e                        ;[0716] 73
                    inc       hl                            ;[0717] 23
                    ld        (hl),d                        ;[0718] 72
                    ld        a,d                           ;[0719] 7a
                    call      $184d                         ;[071a] cd 4d 18
                    call      c,$0579                       ;[071d] dc 79 05
                    ret       nc                            ;[0720] d0
                    ld        e,(ix+$05)                    ;[0721] dd 5e 05
                    ld        d,(ix+$06)                    ;[0724] dd 56 06
                    inc       de                            ;[0727] 13
                    ld        a,(ix+$02)                    ;[0728] dd 7e 02
                    call      $04e3                         ;[072b] cd e3 04
                    call      $19c0                         ;[072e] cd c0 19
                    sla       e                             ;[0731] cb 23
                    rl        d                             ;[0733] cb 12
                    ld        hl,$0024                      ;[0735] 21 24 00
                    add       hl,bc                         ;[0738] 09
                    ld        (hl),e                        ;[0739] 73
                    inc       hl                            ;[073a] 23
                    ld        (hl),d                        ;[073b] 72
                    scf                                     ;[073c] 37
                    jp        $069d                         ;[073d] c3 9d 06
                    call      $0525                         ;[0740] cd 25 05
                    call      c,$0c20                       ;[0743] dc 20 0c
                    call      c,$074c                       ;[0746] dc 4c 07
                    ret       nc                            ;[0749] d0
                    jr        $077f                         ;[074a] 18 33
                    ld        hl,$0020                      ;[074c] 21 20 00
                    add       hl,bc                         ;[074f] 09
                    bit       1,(hl)                        ;[0750] cb 4e
                    scf                                     ;[0752] 37
                    ret       z                             ;[0753] c8
                    call      $0797                         ;[0754] cd 97 07
                    call      c,$132a                       ;[0757] dc 2a 13
                    call      c,$1719                       ;[075a] dc 19 17
                    call      c,$0cca                       ;[075d] dc ca 0c
                    ret                                     ;[0760] c9

                    call      $0525                         ;[0761] cd 25 05
                    call      c,$0c20                       ;[0764] dc 20 0c
                    ret       nc                            ;[0767] d0
                    ld        hl,$0020                      ;[0768] 21 20 00
                    add       hl,bc                         ;[076b] 09
                    bit       1,(hl)                        ;[076c] cb 4e
                    jr        z,$077f                       ;[076e] 28 0f
                    inc       hl                            ;[0770] 23
                    inc       hl                            ;[0771] 23
                    bit       1,(hl)                        ;[0772] cb 4e
                    jr        z,$077c                       ;[0774] 28 06
                    call      $1038                         ;[0776] cd 38 10
                    call      $0f40                         ;[0779] cd 40 0f
                    call      $16c6                         ;[077c] cd c6 16
                    ld        hl,$0020                      ;[077f] 21 20 00
                    add       hl,bc                         ;[0782] 09
                    ld        (hl),$00                      ;[0783] 36 00
                    dec       (ix+$21)                      ;[0785] dd 35 21
                    call      z,$189d                       ;[0788] cc 9d 18
                    scf                                     ;[078b] 37
                    ret                                     ;[078c] c9

                    ld        d,b                           ;[078d] 50
                    ld        c,h                           ;[078e] 4c
                    ld        d,l                           ;[078f] 55
                    ld        d,e                           ;[0790] 53
                    inc       sp                            ;[0791] 33
                    ld        b,h                           ;[0792] 44
                    ld        c,a                           ;[0793] 4f
                    ld        d,e                           ;[0794] 53
                    ld        a,(de)                        ;[0795] 1a
                    ld        bc,$2221                      ;[0796] 01 21 22
                    nop                                     ;[0799] 00
                    add       hl,bc                         ;[079a] 09
                    bit       6,(hl)                        ;[079b] cb 76
                    scf                                     ;[079d] 37
                    ret       z                             ;[079e] c8
                    call      $1074                         ;[079f] cd 74 10
                    push      hl                            ;[07a2] e5
                    push      de                            ;[07a3] d5
                    call      $07af                         ;[07a4] cd af 07
                    pop       de                            ;[07a7] d1
                    pop       hl                            ;[07a8] e1
                    push      af                            ;[07a9] f5
                    call      $1090                         ;[07aa] cd 90 10
                    pop       af                            ;[07ad] f1
                    ret                                     ;[07ae] c9

                    ld        hl,$000b                      ;[07af] 21 0b 00
                    ld        e,h                           ;[07b2] 5c
                    call      $1090                         ;[07b3] cd 90 10
                    ld        hl,$0023                      ;[07b6] 21 23 00
                    add       hl,bc                         ;[07b9] 09
                    ld        e,$03                         ;[07ba] 1e 03
                    call      $07d2                         ;[07bc] cd d2 07
                    ret       nc                            ;[07bf] d0
                    xor       a                             ;[07c0] af
                    call      $12a5                         ;[07c1] cd a5 12
                    ret       nc                            ;[07c4] d0
                    ld        hl,$0030                      ;[07c5] 21 30 00
                    add       hl,bc                         ;[07c8] 09
                    ld        e,$08                         ;[07c9] 1e 08
                    call      $07d2                         ;[07cb] cd d2 07
                    ret       nc                            ;[07ce] d0
                    jp        $12e6                         ;[07cf] c3 e6 12
                    ld        a,(hl)                        ;[07d2] 7e
                    inc       hl                            ;[07d3] 23
                    call      $12a5                         ;[07d4] cd a5 12
                    ret       nc                            ;[07d7] d0
                    dec       e                             ;[07d8] 1d
                    jr        nz,$07d2                      ;[07d9] 20 f7
                    ret                                     ;[07db] c9

                    ld        e,$0a                         ;[07dc] 1e 0a
                    ld        hl,$078d                      ;[07de] 21 8d 07
                    call      $07d2                         ;[07e1] cd d2 07
                    ret       nc                            ;[07e4] d0
                    ld        a,$00                         ;[07e5] 3e 00
                    call      $12a5                         ;[07e7] cd a5 12
                    ret       nc                            ;[07ea] d0
                    ld        e,$74                         ;[07eb] 1e 74
                    xor       a                             ;[07ed] af
                    call      $12a5                         ;[07ee] cd a5 12
                    ret       nc                            ;[07f1] d0
                    dec       e                             ;[07f2] 1d
                    jr        nz,$07ed                      ;[07f3] 20 f8
                    call      $12e6                         ;[07f5] cd e6 12
                    ret       nc                            ;[07f8] d0
                    ld        hl,$0022                      ;[07f9] 21 22 00
                    add       hl,bc                         ;[07fc] 09
                    set       6,(hl)                        ;[07fd] cb f6
                    scf                                     ;[07ff] 37
                    ret                                     ;[0800] c9

                    call      $12de                         ;[0801] cd de 12
                    jr        nc,$084d                      ;[0804] 30 47
                    jr        nz,$0852                      ;[0806] 20 4a
                    ld        e,$0a                         ;[0808] 1e 0a
                    ld        hl,$078d                      ;[080a] 21 8d 07
                    call      $11cb                         ;[080d] cd cb 11
                    jr        nc,$084d                      ;[0810] 30 3b
                    cp        (hl)                          ;[0812] be
                    inc       hl                            ;[0813] 23
                    jr        nz,$0852                      ;[0814] 20 3c
                    dec       e                             ;[0816] 1d
                    jr        nz,$080d                      ;[0817] 20 f4
                    call      $11cb                         ;[0819] cd cb 11
                    jr        nc,$084d                      ;[081c] 30 2f
                    cp        $01                           ;[081e] fe 01
                    jr        nc,$0852                      ;[0820] 30 30
                    ld        hl,$0023                      ;[0822] 21 23 00
                    add       hl,bc                         ;[0825] 09
                    ld        e,$03                         ;[0826] 1e 03
                    call      $11cb                         ;[0828] cd cb 11
                    ret       nc                            ;[082b] d0
                    ld        (hl),a                        ;[082c] 77
                    inc       hl                            ;[082d] 23
                    dec       e                             ;[082e] 1d
                    jr        nz,$0828                      ;[082f] 20 f7
                    call      $11cb                         ;[0831] cd cb 11
                    ret       nc                            ;[0834] d0
                    ld        hl,$0030                      ;[0835] 21 30 00
                    add       hl,bc                         ;[0838] 09
                    ld        e,$08                         ;[0839] 1e 08
                    call      $11cb                         ;[083b] cd cb 11
                    ret       nc                            ;[083e] d0
                    ld        (hl),a                        ;[083f] 77
                    inc       hl                            ;[0840] 23
                    dec       e                             ;[0841] 1d
                    jr        nz,$083b                      ;[0842] 20 f7
                    ld        hl,$0080                      ;[0844] 21 80 00
                    ld        e,h                           ;[0847] 5c
                    call      $1090                         ;[0848] cd 90 10
                    jr        $07f9                         ;[084b] 18 ac
                    cp        $19                           ;[084d] fe 19
                    scf                                     ;[084f] 37
                    ccf                                     ;[0850] 3f
                    ret       nz                            ;[0851] c0
                    ld        hl,$0000                      ;[0852] 21 00 00
                    ld        e,l                           ;[0855] 5d
                    call      $1090                         ;[0856] cd 90 10
                    ld        hl,$0000                      ;[0859] 21 00 00
                    ld        ($df91),hl                    ;[085c] 22 91 df
                    xor       a                             ;[085f] af
                    ld        ($df92),a                     ;[0860] 32 92 df
                    ld        hl,$088d                      ;[0863] 21 8d 08
                    call      $0dae                         ;[0866] cd ae 0d
                    ret       nc                            ;[0869] d0
                    ld        de,($df91)                    ;[086a] ed 5b 91 df
                    ld        hl,($df8f)                    ;[086e] 2a 8f df
                    ld        l,$00                         ;[0871] 2e 00
                    srl       d                             ;[0873] cb 3a
                    rr        e                             ;[0875] cb 1b
                    rr        h                             ;[0877] cb 1c
                    rr        l                             ;[0879] cb 1d
                    ld        a,d                           ;[087b] 7a
                    or        a                             ;[087c] b7
                    ld        a,$22                         ;[087d] 3e 22
                    ret       nz                            ;[087f] c0
                    push      hl                            ;[0880] e5
                    ld        hl,$0025                      ;[0881] 21 25 00
                    add       hl,bc                         ;[0884] 09
                    ld        (hl),e                        ;[0885] 73
                    pop       de                            ;[0886] d1
                    dec       hl                            ;[0887] 2b
                    ld        (hl),d                        ;[0888] 72
                    dec       hl                            ;[0889] 2b
                    ld        (hl),e                        ;[088a] 73
                    scf                                     ;[088b] 37
                    ret                                     ;[088c] c9

                    call      $0d8a                         ;[088d] cd 8a 0d
                    ret       nz                            ;[0890] c0
                    push      bc                            ;[0891] c5
                    ld        b,h                           ;[0892] 44
                    ld        c,l                           ;[0893] 4d
                    call      $14c2                         ;[0894] cd c2 14
                    ld        b,a                           ;[0897] 47
                    ex        de,hl                         ;[0898] eb
                    ld        hl,($df90)                    ;[0899] 2a 90 df
                    or        a                             ;[089c] b7
                    sbc       hl,de                         ;[089d] ed 52
                    ld        a,($df92)                     ;[089f] 3a 92 df
                    sbc       b                             ;[08a2] 98
                    jr        nc,$08ad                      ;[08a3] 30 08
                    ld        ($df90),de                    ;[08a5] ed 53 90 df
                    ld        a,b                           ;[08a9] 78
                    ld        ($df92),a                     ;[08aa] 32 92 df
                    pop       bc                            ;[08ad] c1
                    scf                                     ;[08ae] 37
                    sbc       a                             ;[08af] 9f
                    ret                                     ;[08b0] c9

                    call      $0525                         ;[08b1] cd 25 05
                    ret       nc                            ;[08b4] d0
                    ld        ix,$0030                      ;[08b5] dd 21 30 00
                    add       ix,bc                         ;[08b9] dd 09
                    ld        hl,$0022                      ;[08bb] 21 22 00
                    add       hl,bc                         ;[08be] 09
                    bit       6,(hl)                        ;[08bf] cb 76
                    scf                                     ;[08c1] 37
                    ret                                     ;[08c2] c9

                    ld        e,c                           ;[08c3] 59
                    push      de                            ;[08c4] d5
                    call      $0525                         ;[08c5] cd 25 05
                    call      c,$0c20                       ;[08c8] dc 20 0c
                    call      c,$074c                       ;[08cb] dc 4c 07
                    pop       de                            ;[08ce] d1
                    ret       nc                            ;[08cf] d0
                    ld        hl,$0020                      ;[08d0] 21 20 00
                    add       hl,bc                         ;[08d3] 09
                    ld        d,(hl)                        ;[08d4] 56
                    ld        (hl),e                        ;[08d5] 73
                    push      hl                            ;[08d6] e5
                    push      de                            ;[08d7] d5
                    call      $0579                         ;[08d8] cd 79 05
                    pop       de                            ;[08db] d1
                    pop       hl                            ;[08dc] e1
                    jr        nc,$08ef                      ;[08dd] 30 10
                    bit       1,e                           ;[08df] cb 4b
                    jr        z,$08eb                       ;[08e1] 28 08
                    call      $0ebe                         ;[08e3] cd be 0e
                    call      c,$18f3                       ;[08e6] dc f3 18
                    jr        nc,$08ef                      ;[08e9] 30 04
                    set       7,(hl)                        ;[08eb] cb fe
                    scf                                     ;[08ed] 37
                    ret                                     ;[08ee] c9

                    ld        (hl),d                        ;[08ef] 72
                    or        a                             ;[08f0] b7
                    ret                                     ;[08f1] c9

                    call      $04ed                         ;[08f2] cd ed 04
                    call      $0c27                         ;[08f5] cd 27 0c
                    ret       nc                            ;[08f8] d0
                    jp        $0fa9                         ;[08f9] c3 a9 0f
                    cp        $ff                           ;[08fc] fe ff
                    jr        z,$090a                       ;[08fe] 28 0a
                    cp        $10                           ;[0900] fe 10
                    ld        b,a                           ;[0902] 47
                    ld        a,$15                         ;[0903] 3e 15
                    ret       nc                            ;[0905] d0
                    ld        a,b                           ;[0906] 78
                    ld        ($df93),a                     ;[0907] 32 93 df
                    ld        a,($df93)                     ;[090a] 3a 93 df
                    scf                                     ;[090d] 37
                    ret                                     ;[090e] c9

                    call      $04ed                         ;[090f] cd ed 04
                    cp        $ff                           ;[0912] fe ff
                    jr        z,$091f                       ;[0914] 28 09
                    ld        b,a                           ;[0916] 47
                    call      $184d                         ;[0917] cd 4d 18
                    ret       nc                            ;[091a] d0
                    ld        a,b                           ;[091b] 78
                    ld        ($df94),a                     ;[091c] 32 94 df
                    ld        a,($df94)                     ;[091f] 3a 94 df
                    scf                                     ;[0922] 37
                    ret                                     ;[0923] c9

                    ld        bc,$df20                      ;[0924] 01 20 df
                    call      $0af5                         ;[0927] cd f5 0a
                    call      c,$0c20                       ;[092a] dc 20 0c
                    ret       nc                            ;[092d] d0
                    call      $0569                         ;[092e] cd 69 05
                    call      c,$18f3                       ;[0931] dc f3 18
                    ret       nc                            ;[0934] d0
                    ld        hl,$093b                      ;[0935] 21 3b 09
                    jp        $09ad                         ;[0938] c3 ad 09
                    call      $0d84                         ;[093b] cd 84 0d
                    ret       nz                            ;[093e] c0
                    call      $0ebe                         ;[093f] cd be 0e
                    ret       nc                            ;[0942] d0
                    push      hl                            ;[0943] e5
                    push      de                            ;[0944] d5
                    xor       a                             ;[0945] af
                    call      $0f43                         ;[0946] cd 43 0f
                    pop       de                            ;[0949] d1
                    pop       hl                            ;[094a] e1
                    ld        (hl),$e5                      ;[094b] 36 e5
                    call      $0e34                         ;[094d] cd 34 0e
                    ret       nc                            ;[0950] d0
                    call      $1040                         ;[0951] cd 40 10
                    sbc       a                             ;[0954] 9f
                    ld        ($df95),a                     ;[0955] 32 95 df
                    ret                                     ;[0958] c9

                    ld        ($df96),de                    ;[0959] ed 53 96 df
                    ld        bc,$df20                      ;[095d] 01 20 df
                    call      $0af5                         ;[0960] cd f5 0a
                    call      c,$0c20                       ;[0963] dc 20 0c
                    call      c,$18f3                       ;[0966] dc f3 18
                    ret       nc                            ;[0969] d0
                    ld        hl,$09bf                      ;[096a] 21 bf 09
                    jr        $09a7                         ;[096d] 18 38
                    push      de                            ;[096f] d5
                    ld        bc,$df58                      ;[0970] 01 58 df
                    call      $0adf                         ;[0973] cd df 0a
                    call      c,$0c20                       ;[0976] dc 20 0c
                    pop       hl                            ;[0979] e1
                    push      bc                            ;[097a] c5
                    ld        bc,$df20                      ;[097b] 01 20 df
                    call      c,$0adf                       ;[097e] dc df 0a
                    pop       bc                            ;[0981] c1
                    ret       nc                            ;[0982] d0
                    ld        hl,$0021                      ;[0983] 21 21 00
                    add       hl,bc                         ;[0986] 09
                    ld        a,($df41)                     ;[0987] 3a 41 df
                    xor       (hl)                          ;[098a] ae
                    ld        a,$1f                         ;[098b] 3e 1f
                    ret       nz                            ;[098d] c0
                    call      $18f3                         ;[098e] cd f3 18
                    push      bc                            ;[0991] c5
                    ld        bc,$df20                      ;[0992] 01 20 df
                    call      c,$0569                       ;[0995] dc 69 05
                    ld        hl,$0d84                      ;[0998] 21 84 0d
                    call      c,$0dae                       ;[099b] dc ae 0d
                    pop       bc                            ;[099e] c1
                    ret       nc                            ;[099f] d0
                    ccf                                     ;[09a0] 3f
                    ld        a,$18                         ;[09a1] 3e 18
                    ret       z                             ;[09a3] c8
                    ld        hl,$09f4                      ;[09a4] 21 f4 09
                    push      hl                            ;[09a7] e5
                    call      $0569                         ;[09a8] cd 69 05
                    pop       hl                            ;[09ab] e1
                    ret       nc                            ;[09ac] d0
                    xor       a                             ;[09ad] af
                    ld        ($df95),a                     ;[09ae] 32 95 df
                    call      $0dae                         ;[09b1] cd ae 0d
                    ret       nc                            ;[09b4] d0
                    ld        a,($df95)                     ;[09b5] 3a 95 df
                    or        a                             ;[09b8] b7
                    ld        a,$17                         ;[09b9] 3e 17
                    call      nz,$1719                      ;[09bb] c4 19 17
                    ret                                     ;[09be] c9

                    call      $0d84                         ;[09bf] cd 84 0d
                    ret       nz                            ;[09c2] c0
                    push      bc                            ;[09c3] c5
                    ld        a,($df97)                     ;[09c4] 3a 97 df
                    ld        c,$ff                         ;[09c7] 0e ff
                    push      hl                            ;[09c9] e5
                    call      $09d8                         ;[09ca] cd d8 09
                    pop       hl                            ;[09cd] e1
                    ld        a,($df96)                     ;[09ce] 3a 96 df
                    inc       c                             ;[09d1] 0c
                    call      $09d8                         ;[09d2] cd d8 09
                    pop       bc                            ;[09d5] c1
                    jr        $0a10                         ;[09d6] 18 38
                    rla                                     ;[09d8] 17
                    ld        b,$04                         ;[09d9] 06 04
                    inc       hl                            ;[09db] 23
                    call      $09e5                         ;[09dc] cd e5 09
                    inc       hl                            ;[09df] 23
                    inc       hl                            ;[09e0] 23
                    inc       hl                            ;[09e1] 23
                    inc       hl                            ;[09e2] 23
                    ld        b,$03                         ;[09e3] 06 03
                    rla                                     ;[09e5] 17
                    jr        nc,$09f0                      ;[09e6] 30 08
                    res       7,(hl)                        ;[09e8] cb be
                    inc       c                             ;[09ea] 0c
                    dec       c                             ;[09eb] 0d
                    jr        z,$09f0                       ;[09ec] 28 02
                    set       7,(hl)                        ;[09ee] cb fe
                    inc       hl                            ;[09f0] 23
                    djnz      $09e5                         ;[09f1] 10 f2
                    ret                                     ;[09f3] c9

                    call      $0d84                         ;[09f4] cd 84 0d
                    ret       nz                            ;[09f7] c0
                    call      $0ebe                         ;[09f8] cd be 0e
                    ret       nc                            ;[09fb] d0
                    push      de                            ;[09fc] d5
                    ex        de,hl                         ;[09fd] eb
                    ld        hl,$df20                      ;[09fe] 21 20 df
                    ld        a,(de)                        ;[0a01] 1a
                    and       $10                           ;[0a02] e6 10
                    or        (hl)                          ;[0a04] b6
                    ld        (de),a                        ;[0a05] 12
                    inc       de                            ;[0a06] 13
                    inc       hl                            ;[0a07] 23
                    push      bc                            ;[0a08] c5
                    ld        bc,$000b                      ;[0a09] 01 0b 00
                    ldir                                    ;[0a0c] ed b0
                    pop       bc                            ;[0a0e] c1
                    pop       de                            ;[0a0f] d1
                    call      $0e34                         ;[0a10] cd 34 0e
                    ret       nc                            ;[0a13] d0
                    sbc       a                             ;[0a14] 9f
                    ld        ($df95),a                     ;[0a15] 32 95 df
                    ret                                     ;[0a18] c9

                    ld        ($df98),de                    ;[0a19] ed 53 98 df
                    ld        ($df9a),bc                    ;[0a1d] ed 43 9a df
                    ld        a,$01                         ;[0a21] 3e 01
                    ld        ($df9c),a                     ;[0a23] 32 9c df
                    ld        bc,$df20                      ;[0a26] 01 20 df
                    call      $0af5                         ;[0a29] cd f5 0a
                    call      c,$0c20                       ;[0a2c] dc 20 0c
                    call      c,$05c9                       ;[0a2f] dc c9 05
                    ld        hl,$0a3d                      ;[0a32] 21 3d 0a
                    call      c,$0dae                       ;[0a35] dc ae 0d
                    ld        bc,($df9b)                    ;[0a38] ed 4b 9b df
                    ret                                     ;[0a3c] c9

                    push      bc                            ;[0a3d] c5
                    call      $0a45                         ;[0a3e] cd 45 0a
                    pop       bc                            ;[0a41] c1
                    scf                                     ;[0a42] 37
                    sbc       a                             ;[0a43] 9f
                    ret                                     ;[0a44] c9

                    call      $0d8a                         ;[0a45] cd 8a 0d
                    ret       nz                            ;[0a48] c0
                    ld        a,($df9a)                     ;[0a49] 3a 9a df
                    rra                                     ;[0a4c] 1f
                    jr        c,$0a58                       ;[0a4d] 38 09
                    push      hl                            ;[0a4f] e5
                    ld        bc,$000a                      ;[0a50] 01 0a 00
                    add       hl,bc                         ;[0a53] 09
                    bit       7,(hl)                        ;[0a54] cb 7e
                    pop       hl                            ;[0a56] e1
                    ret       nz                            ;[0a57] c0
                    ld        de,($df98)                    ;[0a58] ed 5b 98 df
                    call      $0ac9                         ;[0a5c] cd c9 0a
                    ret       nc                            ;[0a5f] d0
                    ld        bc,($df9b)                    ;[0a60] ed 4b 9b df
                    push      hl                            ;[0a64] e5
                    ld        hl,$000d                      ;[0a65] 21 0d 00
                    add       hl,de                         ;[0a68] 19
                    ex        de,hl                         ;[0a69] eb
                    pop       hl                            ;[0a6a] e1
                    dec       c                             ;[0a6b] 0d
                    djnz      $0a71                         ;[0a6c] 10 03
                    ret       z                             ;[0a6e] c8
                    jr        $0aa1                         ;[0a6f] 18 30
                    call      $0ac9                         ;[0a71] cd c9 0a
                    jr        c,$0a64                       ;[0a74] 38 ee
                    jr        z,$0ab9                       ;[0a76] 28 41
                    push      hl                            ;[0a78] e5
                    push      de                            ;[0a79] d5
                    ld        hl,($df9b)                    ;[0a7a] 2a 9b df
                    ld        h,$00                         ;[0a7d] 26 00
                    dec       hl                            ;[0a7f] 2b
                    ld        b,h                           ;[0a80] 44
                    ld        c,l                           ;[0a81] 4d
                    add       hl,hl                         ;[0a82] 29
                    add       hl,bc                         ;[0a83] 09
                    add       hl,hl                         ;[0a84] 29
                    add       hl,hl                         ;[0a85] 29
                    add       hl,bc                         ;[0a86] 09
                    ld        bc,($df98)                    ;[0a87] ed 4b 98 df
                    add       hl,bc                         ;[0a8b] 09
                    ld        a,l                           ;[0a8c] 7d
                    sub       e                             ;[0a8d] 93
                    ld        c,a                           ;[0a8e] 4f
                    ld        a,h                           ;[0a8f] 7c
                    sbc       d                             ;[0a90] 9a
                    ld        b,a                           ;[0a91] 47
                    dec       hl                            ;[0a92] 2b
                    ld        de,$000d                      ;[0a93] 11 0d 00
                    ex        de,hl                         ;[0a96] eb
                    add       hl,de                         ;[0a97] 19
                    ex        de,hl                         ;[0a98] eb
                    ld        a,b                           ;[0a99] 78
                    or        c                             ;[0a9a] b1
                    jr        z,$0a9f                       ;[0a9b] 28 02
                    lddr                                    ;[0a9d] ed b8
                    pop       de                            ;[0a9f] d1
                    pop       hl                            ;[0aa0] e1
                    push      hl                            ;[0aa1] e5
                    push      de                            ;[0aa2] d5
                    inc       hl                            ;[0aa3] 23
                    ld        bc,$000b                      ;[0aa4] 01 0b 00
                    ldir                                    ;[0aa7] ed b0
                    xor       a                             ;[0aa9] af
                    ld        (de),a                        ;[0aaa] 12
                    inc       de                            ;[0aab] 13
                    ld        (de),a                        ;[0aac] 12
                    ld        hl,($df9b)                    ;[0aad] 2a 9b df
                    ld        a,h                           ;[0ab0] 7c
                    cp        l                             ;[0ab1] bd
                    adc       $00                           ;[0ab2] ce 00
                    ld        ($df9c),a                     ;[0ab4] 32 9c df
                    pop       de                            ;[0ab7] d1
                    pop       hl                            ;[0ab8] e1
                    call      $0f75                         ;[0ab9] cd 75 0f
                    ex        de,hl                         ;[0abc] eb
                    ld        bc,$000b                      ;[0abd] 01 0b 00
                    add       hl,bc                         ;[0ac0] 09
                    ld        a,(hl)                        ;[0ac1] 7e
                    add       e                             ;[0ac2] 83
                    ld        (hl),a                        ;[0ac3] 77
                    inc       hl                            ;[0ac4] 23
                    ld        a,(hl)                        ;[0ac5] 7e
                    adc       d                             ;[0ac6] 8a
                    ld        (hl),a                        ;[0ac7] 77
                    ret                                     ;[0ac8] c9

                    push      hl                            ;[0ac9] e5
                    push      de                            ;[0aca] d5
                    push      bc                            ;[0acb] c5
                    ld        b,$0b                         ;[0acc] 06 0b
                    inc       hl                            ;[0ace] 23
                    ld        a,(hl)                        ;[0acf] 7e
                    add       a                             ;[0ad0] 87
                    ld        c,a                           ;[0ad1] 4f
                    ld        a,(de)                        ;[0ad2] 1a
                    add       a                             ;[0ad3] 87
                    cp        c                             ;[0ad4] b9
                    jr        nz,$0adb                      ;[0ad5] 20 04
                    inc       de                            ;[0ad7] 13
                    inc       hl                            ;[0ad8] 23
                    djnz      $0acf                         ;[0ad9] 10 f4
                    pop       bc                            ;[0adb] c1
                    pop       de                            ;[0adc] d1
                    pop       hl                            ;[0add] e1
                    ret                                     ;[0ade] c9

                    call      $0b3f                         ;[0adf] cd 3f 0b
                    ret       nc                            ;[0ae2] d0
                    ld        hl,$0001                      ;[0ae3] 21 01 00
                    add       hl,bc                         ;[0ae6] 09
                    ld        e,$0b                         ;[0ae7] 1e 0b
                    ld        a,(hl)                        ;[0ae9] 7e
                    inc       hl                            ;[0aea] 23
                    cp        $3f                           ;[0aeb] fe 3f
                    ld        a,$14                         ;[0aed] 3e 14
                    ret       z                             ;[0aef] c8
                    dec       e                             ;[0af0] 1d
                    jr        nz,$0ae9                      ;[0af1] 20 f6
                    scf                                     ;[0af3] 37
                    ret                                     ;[0af4] c9

                    jp        $0b3f                         ;[0af5] c3 3f 0b
                    call      $0b02                         ;[0af8] cd 02 0b
                    jr        nc,$0b21                      ;[0afb] 30 24
                    call      $0b21                         ;[0afd] cd 21 0b
                    scf                                     ;[0b00] 37
                    ret                                     ;[0b01] c9

                    call      $0b38                         ;[0b02] cd 38 0b
                    ret       nc                            ;[0b05] d0
                    ld        e,a                           ;[0b06] 5f
                    call      $0bd3                         ;[0b07] cd d3 0b
                    call      c,$0b38                       ;[0b0a] dc 38 0b
                    jr        nc,$0b1b                      ;[0b0d] 30 0c
                    ld        d,a                           ;[0b0f] 57
                    ld        a,e                           ;[0b10] 7b
                    add       a                             ;[0b11] 87
                    ld        e,a                           ;[0b12] 5f
                    add       a                             ;[0b13] 87
                    add       a                             ;[0b14] 87
                    add       e                             ;[0b15] 83
                    add       d                             ;[0b16] 82
                    ld        e,a                           ;[0b17] 5f
                    call      $0bd3                         ;[0b18] cd d3 0b
                    ld        a,e                           ;[0b1b] 7b
                    cp        $10                           ;[0b1c] fe 10
                    ret       nc                            ;[0b1e] d0
                    ld        (bc),a                        ;[0b1f] 02
                    ret                                     ;[0b20] c9

                    call      $0bc9                         ;[0b21] cd c9 0b
                    ret       nc                            ;[0b24] d0
                    cp        $41                           ;[0b25] fe 41
                    ccf                                     ;[0b27] 3f
                    ret       nc                            ;[0b28] d0
                    cp        $51                           ;[0b29] fe 51
                    ret       nc                            ;[0b2b] d0
                    push      hl                            ;[0b2c] e5
                    ld        hl,$0021                      ;[0b2d] 21 21 00
                    add       hl,bc                         ;[0b30] 09
                    ld        (hl),a                        ;[0b31] 77
                    pop       hl                            ;[0b32] e1
                    call      $0bd3                         ;[0b33] cd d3 0b
                    scf                                     ;[0b36] 37
                    ret                                     ;[0b37] c9

                    sub       $30                           ;[0b38] d6 30
                    ccf                                     ;[0b3a] 3f
                    ret       nc                            ;[0b3b] d0
                    cp        $0a                           ;[0b3c] fe 0a
                    ret                                     ;[0b3e] c9

                    push      bc                            ;[0b3f] c5
                    call      $0b47                         ;[0b40] cd 47 0b
                    pop       bc                            ;[0b43] c1
                    ld        a,$14                         ;[0b44] 3e 14
                    ret                                     ;[0b46] c9

                    push      hl                            ;[0b47] e5
                    ld        hl,$0021                      ;[0b48] 21 21 00
                    add       hl,bc                         ;[0b4b] 09
                    ld        a,($df94)                     ;[0b4c] 3a 94 df
                    ld        (hl),a                        ;[0b4f] 77
                    pop       hl                            ;[0b50] e1
                    ld        a,($df93)                     ;[0b51] 3a 93 df
                    ld        (bc),a                        ;[0b54] 02
                    call      $0bc9                         ;[0b55] cd c9 0b
                    jr        nc,$0b7a                      ;[0b58] 30 20
                    ld        e,a                           ;[0b5a] 5f
                    push      hl                            ;[0b5b] e5
                    cp        $3a                           ;[0b5c] fe 3a
                    scf                                     ;[0b5e] 37
                    jr        z,$0b66                       ;[0b5f] 28 05
                    call      $0bd3                         ;[0b61] cd d3 0b
                    jr        c,$0b5c                       ;[0b64] 38 f6
                    pop       hl                            ;[0b66] e1
                    ld        a,e                           ;[0b67] 7b
                    jr        nc,$0b80                      ;[0b68] 30 16
                    call      $0af8                         ;[0b6a] cd f8 0a
                    ret       nc                            ;[0b6d] d0
                    call      $0bc9                         ;[0b6e] cd c9 0b
                    ret       nc                            ;[0b71] d0
                    xor       $3a                           ;[0b72] ee 3a
                    ret       nz                            ;[0b74] c0
                    call      $0bc5                         ;[0b75] cd c5 0b
                    jr        c,$0b80                       ;[0b78] 38 06
                    inc       bc                            ;[0b7a] 03
                    ld        e,$0b                         ;[0b7b] 1e 0b
                    scf                                     ;[0b7d] 37
                    jr        $0bbc                         ;[0b7e] 18 3c
                    inc       bc                            ;[0b80] 03
                    cp        $2e                           ;[0b81] fe 2e
                    ret       z                             ;[0b83] c8
                    ld        e,$08                         ;[0b84] 1e 08
                    call      $0b96                         ;[0b86] cd 96 0b
                    ccf                                     ;[0b89] 3f
                    ld        e,$03                         ;[0b8a] 1e 03
                    jr        nc,$0bb3                      ;[0b8c] 30 25
                    xor       $2e                           ;[0b8e] ee 2e
                    ret       nz                            ;[0b90] c0
                    call      $0bc5                         ;[0b91] cd c5 0b
                    jr        nc,$0bb3                      ;[0b94] 30 1d
                    push      hl                            ;[0b96] e5
                    cp        $20                           ;[0b97] fe 20
                    ld        hl,$0bef                      ;[0b99] 21 ef 0b
                    call      nc,$0be5                      ;[0b9c] d4 e5 0b
                    pop       hl                            ;[0b9f] e1
                    jr        c,$0bb3                       ;[0ba0] 38 11
                    dec       e                             ;[0ba2] 1d
                    ret       m                             ;[0ba3] f8
                    cp        $2a                           ;[0ba4] fe 2a
                    call      z,$0bbc                       ;[0ba6] cc bc 0b
                    ld        (bc),a                        ;[0ba9] 02
                    inc       bc                            ;[0baa] 03
                    call      $0bd3                         ;[0bab] cd d3 0b
                    jr        nz,$0b96                      ;[0bae] 20 e6
                    call      c,$0bc5                       ;[0bb0] dc c5 0b
                    push      af                            ;[0bb3] f5
                    ld        a,$20                         ;[0bb4] 3e 20
                    call      $0bbe                         ;[0bb6] cd be 0b
                    pop       af                            ;[0bb9] f1
                    ccf                                     ;[0bba] 3f
                    ret                                     ;[0bbb] c9

                    ld        a,$3f                         ;[0bbc] 3e 3f
                    inc       e                             ;[0bbe] 1c
                    dec       e                             ;[0bbf] 1d
                    ret       z                             ;[0bc0] c8
                    ld        (bc),a                        ;[0bc1] 02
                    inc       bc                            ;[0bc2] 03
                    jr        $0bbf                         ;[0bc3] 18 fa
                    call      $0bd3                         ;[0bc5] cd d3 0b
                    ret       nc                            ;[0bc8] d0
                    call      $0bd8                         ;[0bc9] cd d8 0b
                    ret       nz                            ;[0bcc] c0
                    call      $0bd3                         ;[0bcd] cd d3 0b
                    jr        c,$0bcc                       ;[0bd0] 38 fa
                    ret                                     ;[0bd2] c9

                    ld        a,(hl)                        ;[0bd3] 7e
                    cp        $ff                           ;[0bd4] fe ff
                    ret       z                             ;[0bd6] c8
                    inc       hl                            ;[0bd7] 23
                    ld        a,(hl)                        ;[0bd8] 7e
                    cp        $ff                           ;[0bd9] fe ff
                    ret       z                             ;[0bdb] c8
                    and       $7f                           ;[0bdc] e6 7f
                    call      $04ed                         ;[0bde] cd ed 04
                    cp        $20                           ;[0be1] fe 20
                    scf                                     ;[0be3] 37
                    ret                                     ;[0be4] c9

                    cp        (hl)                          ;[0be5] be
                    scf                                     ;[0be6] 37
                    ret       z                             ;[0be7] c8
                    inc       hl                            ;[0be8] 23
                    bit       7,(hl)                        ;[0be9] cb 7e
                    jr        z,$0be5                       ;[0beb] 28 f8
                    or        a                             ;[0bed] b7
                    ret                                     ;[0bee] c9

                    ld        hl,$2826                      ;[0bef] 21 26 28
                    add       hl,hl                         ;[0bf2] 29
                    dec       hl                            ;[0bf3] 2b
                    inc       l                             ;[0bf4] 2c
                    dec       l                             ;[0bf5] 2d
                    ld        l,$2f                         ;[0bf6] 2e 2f
                    ld        a,($3c3b)                     ;[0bf8] 3a 3b 3c
                    dec       a                             ;[0bfb] 3d
                    ld        a,$5b                         ;[0bfc] 3e 5b
                    ld        e,h                           ;[0bfe] 5c
                    ld        e,l                           ;[0bff] 5d
                    ld        a,h                           ;[0c00] 7c
                    add       b                             ;[0c01] 80
                    nop                                     ;[0c02] 00
                    nop                                     ;[0c03] 00
                    nop                                     ;[0c04] 00
                    nop                                     ;[0c05] 00
                    nop                                     ;[0c06] 00
                    nop                                     ;[0c07] 00
                    nop                                     ;[0c08] 00
                    nop                                     ;[0c09] 00
                    nop                                     ;[0c0a] 00
                    nop                                     ;[0c0b] 00
                    nop                                     ;[0c0c] 00
                    nop                                     ;[0c0d] 00
                    nop                                     ;[0c0e] 00
                    nop                                     ;[0c0f] 00
                    nop                                     ;[0c10] 00
                    nop                                     ;[0c11] 00
                    nop                                     ;[0c12] 00
                    nop                                     ;[0c13] 00
                    nop                                     ;[0c14] 00
                    nop                                     ;[0c15] 00
                    nop                                     ;[0c16] 00
                    nop                                     ;[0c17] 00
                    nop                                     ;[0c18] 00
                    nop                                     ;[0c19] 00
                    nop                                     ;[0c1a] 00
                    nop                                     ;[0c1b] 00
                    nop                                     ;[0c1c] 00
                    nop                                     ;[0c1d] 00
                    nop                                     ;[0c1e] 00
                    nop                                     ;[0c1f] 00
                    push      hl                            ;[0c20] e5
                    ld        hl,$0021                      ;[0c21] 21 21 00
                    add       hl,bc                         ;[0c24] 09
                    ld        a,(hl)                        ;[0c25] 7e
                    pop       hl                            ;[0c26] e1
                    call      $1871                         ;[0c27] cd 71 18
                    ret       nc                            ;[0c2a] d0
                    bit       0,(ix+$1b)                    ;[0c2b] dd cb 1b 46
                    scf                                     ;[0c2f] 37
                    ret       nz                            ;[0c30] c0
                    push      hl                            ;[0c31] e5
                    push      de                            ;[0c32] d5
                    push      bc                            ;[0c33] c5
                    call      $0ecc                         ;[0c34] cd cc 0e
                    set       1,(ix+$1b)                    ;[0c37] dd cb 1b ce
                    xor       a                             ;[0c3b] af
                    ld        (ix+$22),a                    ;[0c3c] dd 77 22
                    ld        (ix+$23),a                    ;[0c3f] dd 77 23
                    call      $0c74                         ;[0c42] cd 74 0c
                    ld        bc,$0000                      ;[0c45] 01 00 00
                    ld        hl,$0c61                      ;[0c48] 21 61 0c
                    call      $0dae                         ;[0c4b] cd ae 0d
                    ld        (ix+$24),c                    ;[0c4e] dd 71 24
                    ld        (ix+$25),b                    ;[0c51] dd 70 25
                    pop       bc                            ;[0c54] c1
                    pop       de                            ;[0c55] d1
                    pop       hl                            ;[0c56] e1
                    ret       nc                            ;[0c57] d0
                    set       0,(ix+$1b)                    ;[0c58] dd cb 1b c6
                    res       1,(ix+$1b)                    ;[0c5c] dd cb 1b 8e
                    ret                                     ;[0c60] c9

                    call      $0c67                         ;[0c61] cd 67 0c
                    scf                                     ;[0c64] 37
                    sbc       a                             ;[0c65] 9f
                    ret                                     ;[0c66] c9

                    ld        a,(hl)                        ;[0c67] 7e
                    cp        $e5                           ;[0c68] fe e5
                    jp        z,$1040                       ;[0c6a] ca 40 10
                    ld        b,d                           ;[0c6d] 42
                    ld        c,e                           ;[0c6e] 4b
                    ld        a,$ff                         ;[0c6f] 3e ff
                    jp        $0f43                         ;[0c71] c3 43 0f
                    ld        a,(ix+$07)                    ;[0c74] dd 7e 07
                    ld        (ix+$24),a                    ;[0c77] dd 77 24
                    ld        a,(ix+$08)                    ;[0c7a] dd 7e 08
                    ld        (ix+$25),a                    ;[0c7d] dd 77 25
                    ret                                     ;[0c80] c9

                    call      $0d19                         ;[0c81] cd 19 0d
                    ret       c                             ;[0c84] d8
                    push      hl                            ;[0c85] e5
                    call      $0cca                         ;[0c86] cd ca 0c
                    pop       hl                            ;[0c89] e1
                    ret       nc                            ;[0c8a] d0
                    ex        de,hl                         ;[0c8b] eb
                    ld        hl,$000c                      ;[0c8c] 21 0c 00
                    add       hl,bc                         ;[0c8f] 09
                    ld        (hl),d                        ;[0c90] 72
                    inc       hl                            ;[0c91] 23
                    inc       hl                            ;[0c92] 23
                    ld        (hl),e                        ;[0c93] 73
                    ld        e,$11                         ;[0c94] 1e 11
                    xor       a                             ;[0c96] af
                    inc       hl                            ;[0c97] 23
                    ld        (hl),a                        ;[0c98] 77
                    dec       e                             ;[0c99] 1d
                    jr        nz,$0c97                      ;[0c9a] 20 fb
                    call      $0d4a                         ;[0c9c] cd 4a 0d
                    ret       c                             ;[0c9f] d8
                    ld        hl,$0022                      ;[0ca0] 21 22 00
                    add       hl,bc                         ;[0ca3] 09
                    set       2,(hl)                        ;[0ca4] cb d6
                    or        a                             ;[0ca6] b7
                    ret                                     ;[0ca7] c9

                    call      $0d19                         ;[0ca8] cd 19 0d
                    jr        nc,$0cb8                      ;[0cab] 30 0b
                    ld        hl,$0022                      ;[0cad] 21 22 00
                    add       hl,bc                         ;[0cb0] 09
                    bit       2,(hl)                        ;[0cb1] cb 56
                    jp        nz,$0fd6                      ;[0cb3] c2 d6 0f
                    scf                                     ;[0cb6] 37
                    ret                                     ;[0cb7] c9

                    push      hl                            ;[0cb8] e5
                    call      $0cca                         ;[0cb9] cd ca 0c
                    pop       hl                            ;[0cbc] e1
                    ret       nc                            ;[0cbd] d0
                    call      $0c8b                         ;[0cbe] cd 8b 0c
                    ret       c                             ;[0cc1] d8
                    cp        $19                           ;[0cc2] fe 19
                    scf                                     ;[0cc4] 37
                    ccf                                     ;[0cc5] 3f
                    ret       nz                            ;[0cc6] c0
                    jp        $0fd6                         ;[0cc7] c3 d6 0f
                    ld        hl,$0022                      ;[0cca] 21 22 00
                    add       hl,bc                         ;[0ccd] 09
                    ld        a,(hl)                        ;[0cce] 7e
                    and       $03                           ;[0ccf] e6 03
                    scf                                     ;[0cd1] 37
                    ret       z                             ;[0cd2] c8
                    cp        $02                           ;[0cd3] fe 02
                    jp        z,$1038                       ;[0cd5] ca 38 10
                    and       $02                           ;[0cd8] e6 02
                    jr        nz,$0ce9                      ;[0cda] 20 0d
                    ld        hl,$0d60                      ;[0cdc] 21 60 0d
                    call      $0dae                         ;[0cdf] cd ae 0d
                    ret       nc                            ;[0ce2] d0
                    ld        a,$20                         ;[0ce3] 3e 20
                    ccf                                     ;[0ce5] 3f
                    ret       nz                            ;[0ce6] c0
                    jr        $0cff                         ;[0ce7] 18 16
                    call      $0ff6                         ;[0ce9] cd f6 0f
                    ret       nc                            ;[0cec] d0
                    push      hl                            ;[0ced] e5
                    push      de                            ;[0cee] d5
                    push      bc                            ;[0cef] c5
                    call      $1049                         ;[0cf0] cd 49 10
                    jr        nz,$0cfc                      ;[0cf3] 20 07
                    ld        b,$0a                         ;[0cf5] 06 0a
                    ld        (hl),$00                      ;[0cf7] 36 00
                    inc       hl                            ;[0cf9] 23
                    djnz      $0cf7                         ;[0cfa] 10 fb
                    pop       bc                            ;[0cfc] c1
                    pop       de                            ;[0cfd] d1
                    pop       hl                            ;[0cfe] e1
                    push      de                            ;[0cff] d5
                    push      bc                            ;[0d00] c5
                    ex        de,hl                         ;[0d01] eb
                    ld        h,b                           ;[0d02] 60
                    ld        l,c                           ;[0d03] 69
                    ld        bc,$0020                      ;[0d04] 01 20 00
                    ldir                                    ;[0d07] ed b0
                    pop       bc                            ;[0d09] c1
                    pop       de                            ;[0d0a] d1
                    call      $0e34                         ;[0d0b] cd 34 0e
                    call      c,$1719                       ;[0d0e] dc 19 17
                    ld        hl,$0022                      ;[0d11] 21 22 00
                    add       hl,bc                         ;[0d14] 09
                    res       0,(hl)                        ;[0d15] cb 86
                    scf                                     ;[0d17] 37
                    ret                                     ;[0d18] c9

                    push      bc                            ;[0d19] c5
                    ld        a,(ix+$04)                    ;[0d1a] dd 7e 04
                    cpl                                     ;[0d1d] 2f
                    and       $1f                           ;[0d1e] e6 1f
                    ld        b,a                           ;[0d20] 47
                    ld        a,d                           ;[0d21] 7a
                    rra                                     ;[0d22] 1f
                    rra                                     ;[0d23] 1f
                    rra                                     ;[0d24] 1f
                    rra                                     ;[0d25] 1f
                    and       $0f                           ;[0d26] e6 0f
                    ld        l,a                           ;[0d28] 6f
                    ld        a,e                           ;[0d29] 7b
                    add       a                             ;[0d2a] 87
                    ld        a,d                           ;[0d2b] 7a
                    adc       a                             ;[0d2c] 8f
                    and       b                             ;[0d2d] a0
                    ld        h,a                           ;[0d2e] 67
                    ld        a,b                           ;[0d2f] 78
                    pop       bc                            ;[0d30] c1
                    push      hl                            ;[0d31] e5
                    push      de                            ;[0d32] d5
                    push      bc                            ;[0d33] c5
                    ex        de,hl                         ;[0d34] eb
                    ld        hl,$000e                      ;[0d35] 21 0e 00
                    add       hl,bc                         ;[0d38] 09
                    ld        b,a                           ;[0d39] 47
                    ld        a,(hl)                        ;[0d3a] 7e
                    xor       e                             ;[0d3b] ab
                    jr        nz,$0d46                      ;[0d3c] 20 08
                    dec       hl                            ;[0d3e] 2b
                    dec       hl                            ;[0d3f] 2b
                    ld        a,(hl)                        ;[0d40] 7e
                    xor       d                             ;[0d41] aa
                    and       b                             ;[0d42] a0
                    jr        nz,$0d46                      ;[0d43] 20 01
                    scf                                     ;[0d45] 37
                    pop       bc                            ;[0d46] c1
                    pop       de                            ;[0d47] d1
                    pop       hl                            ;[0d48] e1
                    ret                                     ;[0d49] c9

                    ld        hl,$0d60                      ;[0d4a] 21 60 0d
                    call      $0dae                         ;[0d4d] cd ae 0d
                    ret       nc                            ;[0d50] d0
                    ccf                                     ;[0d51] 3f
                    ld        a,$19                         ;[0d52] 3e 19
                    ret       nz                            ;[0d54] c0
                    push      bc                            ;[0d55] c5
                    ld        d,b                           ;[0d56] 50
                    ld        e,c                           ;[0d57] 59
                    ld        bc,$0020                      ;[0d58] 01 20 00
                    ldir                                    ;[0d5b] ed b0
                    pop       bc                            ;[0d5d] c1
                    scf                                     ;[0d5e] 37
                    ret                                     ;[0d5f] c9

                    push      hl                            ;[0d60] e5
                    push      de                            ;[0d61] d5
                    push      bc                            ;[0d62] c5
                    ld        a,(bc)                        ;[0d63] 0a
                    xor       (hl)                          ;[0d64] ae
                    call      z,$0d97                       ;[0d65] cc 97 0d
                    jr        nz,$0d82                      ;[0d68] 20 18
                    ld        a,(de)                        ;[0d6a] 1a
                    inc       a                             ;[0d6b] 3c
                    jr        z,$0d78                       ;[0d6c] 28 0a
                    ld        a,(ix+$04)                    ;[0d6e] dd 7e 04
                    cpl                                     ;[0d71] 2f
                    ld        b,a                           ;[0d72] 47
                    ld        a,(de)                        ;[0d73] 1a
                    xor       (hl)                          ;[0d74] ae
                    and       b                             ;[0d75] a0
                    jr        nz,$0d82                      ;[0d76] 20 0a
                    inc       de                            ;[0d78] 13
                    inc       hl                            ;[0d79] 23
                    inc       de                            ;[0d7a] 13
                    inc       hl                            ;[0d7b] 23
                    ld        a,(de)                        ;[0d7c] 1a
                    cp        $ff                           ;[0d7d] fe ff
                    jr        z,$0d82                       ;[0d7f] 28 01
                    xor       (hl)                          ;[0d81] ae
                    jr        $0d92                         ;[0d82] 18 0e
                    ld        a,(bc)                        ;[0d84] 0a
                    xor       (hl)                          ;[0d85] ae
                    and       $ef                           ;[0d86] e6 ef
                    jr        $0d8c                         ;[0d88] 18 02
                    ld        a,(bc)                        ;[0d8a] 0a
                    xor       (hl)                          ;[0d8b] ae
                    push      hl                            ;[0d8c] e5
                    push      de                            ;[0d8d] d5
                    push      bc                            ;[0d8e] c5
                    call      z,$0d97                       ;[0d8f] cc 97 0d
                    pop       bc                            ;[0d92] c1
                    pop       de                            ;[0d93] d1
                    pop       hl                            ;[0d94] e1
                    scf                                     ;[0d95] 37
                    ret                                     ;[0d96] c9

                    push      bc                            ;[0d97] c5
                    ld        d,b                           ;[0d98] 50
                    ld        e,c                           ;[0d99] 59
                    inc       de                            ;[0d9a] 13
                    inc       hl                            ;[0d9b] 23
                    ld        b,$0b                         ;[0d9c] 06 0b
                    ld        a,(de)                        ;[0d9e] 1a
                    cp        $3f                           ;[0d9f] fe 3f
                    jr        z,$0da8                       ;[0da1] 28 05
                    xor       (hl)                          ;[0da3] ae
                    and       $7f                           ;[0da4] e6 7f
                    jr        nz,$0dac                      ;[0da6] 20 04
                    inc       de                            ;[0da8] 13
                    inc       hl                            ;[0da9] 23
                    djnz      $0d9e                         ;[0daa] 10 f2
                    pop       bc                            ;[0dac] c1
                    ret                                     ;[0dad] c9

                    ld        ($dfa0),hl                    ;[0dae] 22 a0 df
                    call      $1653                         ;[0db1] cd 53 16
                    ld        de,$0000                      ;[0db4] 11 00 00
                    push      af                            ;[0db7] f5
                    ld        a,e                           ;[0db8] 7b
                    and       $0f                           ;[0db9] e6 0f
                    jr        nz,$0dc3                      ;[0dbb] 20 06
                    pop       af                            ;[0dbd] f1
                    call      $0dff                         ;[0dbe] cd ff 0d
                    ret       nc                            ;[0dc1] d0
                    push      af                            ;[0dc2] f5
                    pop       af                            ;[0dc3] f1
                    push      af                            ;[0dc4] f5
                    push      hl                            ;[0dc5] e5
                    push      ix                            ;[0dc6] dd e5
                    push      de                            ;[0dc8] d5
                    push      bc                            ;[0dc9] c5
                    ld        c,a                           ;[0dca] 4f
                    ld        b,$07                         ;[0dcb] 06 07
                    ld        a,e                           ;[0dcd] 7b
                    call      $0e6a                         ;[0dce] cd 6a 0e
                    call      $023d                         ;[0dd1] cd 3d 02
                    pop       bc                            ;[0dd4] c1
                    pop       de                            ;[0dd5] d1
                    pop       ix                            ;[0dd6] dd e1
                    push      de                            ;[0dd8] d5
                    call      $0ded                         ;[0dd9] cd ed 0d
                    pop       de                            ;[0ddc] d1
                    pop       hl                            ;[0ddd] e1
                    jr        nc,$0de7                      ;[0dde] 30 07
                    jr        z,$0de7                       ;[0de0] 28 05
                    call      $0df5                         ;[0de2] cd f5 0d
                    jr        nc,$0db8                      ;[0de5] 30 d1
                    ld        hl,$dfa2                      ;[0de7] 21 a2 df
                    inc       sp                            ;[0dea] 33
                    inc       sp                            ;[0deb] 33
                    ret                                     ;[0dec] c9

                    ld        hl,($dfa0)                    ;[0ded] 2a a0 df
                    push      hl                            ;[0df0] e5
                    ld        hl,$dfa2                      ;[0df1] 21 a2 df
                    ret                                     ;[0df4] c9

                    inc       de                            ;[0df5] 13
                    ld        a,(ix+$24)                    ;[0df6] dd 7e 24
                    sub       e                             ;[0df9] 93
                    ld        a,(ix+$25)                    ;[0dfa] dd 7e 25
                    sbc       d                             ;[0dfd] 9a
                    ret                                     ;[0dfe] c9

                    push      bc                            ;[0dff] c5
                    push      de                            ;[0e00] d5
                    ld        a,$02                         ;[0e01] 3e 02
                    call      $04d9                         ;[0e03] cd d9 04
                    call      $19c0                         ;[0e06] cd c0 19
                    call      $15f4                         ;[0e09] cd f4 15
                    jr        nc,$0e31                      ;[0e0c] 30 23
                    ld        b,a                           ;[0e0e] 47
                    push      hl                            ;[0e0f] e5
                    call      $0e7d                         ;[0e10] cd 7d 0e
                    jr        c,$0e2d                       ;[0e13] 38 18
                    bit       1,(ix+$1b)                    ;[0e15] dd cb 1b 4e
                    jr        z,$0e1c                       ;[0e19] 28 01
                    ld        (hl),a                        ;[0e1b] 77
                    cp        (hl)                          ;[0e1c] be
                    scf                                     ;[0e1d] 37
                    jr        z,$0e2d                       ;[0e1e] 28 0d
                    call      $166c                         ;[0e20] cd 6c 16
                    ld        a,$08                         ;[0e23] 3e 08
                    call      $1a34                         ;[0e25] cd 34 1a
                    jr        nz,$0e2d                      ;[0e28] 20 03
                    pop       hl                            ;[0e2a] e1
                    jr        $0e09                         ;[0e2b] 18 dc
                    pop       hl                            ;[0e2d] e1
                    jr        nc,$0e31                      ;[0e2e] 30 01
                    ld        a,b                           ;[0e30] 78
                    pop       de                            ;[0e31] d1
                    pop       bc                            ;[0e32] c1
                    ret                                     ;[0e33] c9

                    push      hl                            ;[0e34] e5
                    push      de                            ;[0e35] d5
                    push      bc                            ;[0e36] c5
                    ld        c,e                           ;[0e37] 4b
                    ld        a,$02                         ;[0e38] 3e 02
                    call      $04d9                         ;[0e3a] cd d9 04
                    call      $19c0                         ;[0e3d] cd c0 19
                    push      bc                            ;[0e40] c5
                    ld        bc,$0001                      ;[0e41] 01 01 00
                    call      $1624                         ;[0e44] cd 24 16
                    pop       bc                            ;[0e47] c1
                    jr        nc,$0e66                      ;[0e48] 30 1c
                    ld        b,a                           ;[0e4a] 47
                    push      ix                            ;[0e4b] dd e5
                    push      hl                            ;[0e4d] e5
                    push      de                            ;[0e4e] d5
                    push      bc                            ;[0e4f] c5
                    ld        a,c                           ;[0e50] 79
                    call      $0e6a                         ;[0e51] cd 6a 0e
                    ex        de,hl                         ;[0e54] eb
                    ld        c,$07                         ;[0e55] 0e 07
                    call      $023d                         ;[0e57] cd 3d 02
                    pop       bc                            ;[0e5a] c1
                    pop       de                            ;[0e5b] d1
                    pop       hl                            ;[0e5c] e1
                    pop       ix                            ;[0e5d] dd e1
                    call      $0e7d                         ;[0e5f] cd 7d 0e
                    jr        c,$0e66                       ;[0e62] 38 02
                    ld        (hl),a                        ;[0e64] 77
                    scf                                     ;[0e65] 37
                    pop       bc                            ;[0e66] c1
                    pop       de                            ;[0e67] d1
                    pop       hl                            ;[0e68] e1
                    ret                                     ;[0e69] c9

                    and       $0f                           ;[0e6a] e6 0f
                    jr        z,$0e75                       ;[0e6c] 28 07
                    ld        de,$0020                      ;[0e6e] 11 20 00
                    add       hl,de                         ;[0e71] 19
                    dec       a                             ;[0e72] 3d
                    jr        nz,$0e71                      ;[0e73] 20 fc
                    ld        de,$dfa2                      ;[0e75] 11 a2 df
                    ld        ix,$0020                      ;[0e78] dd 21 20 00
                    ret                                     ;[0e7c] c9

                    push      hl                            ;[0e7d] e5
                    push      de                            ;[0e7e] d5
                    ex        de,hl                         ;[0e7f] eb
                    ld        e,(ix+$0b)                    ;[0e80] dd 5e 0b
                    ld        a,(ix+$0c)                    ;[0e83] dd 7e 0c
                    and       $7f                           ;[0e86] e6 7f
                    ld        d,a                           ;[0e88] 57
                    call      $19c0                         ;[0e89] cd c0 19
                    sbc       hl,de                         ;[0e8c] ed 52
                    ccf                                     ;[0e8e] 3f
                    pop       de                            ;[0e8f] d1
                    pop       hl                            ;[0e90] e1
                    ret       c                             ;[0e91] d8
                    push      bc                            ;[0e92] c5
                    ld        a,b                           ;[0e93] 78
                    call      $0207                         ;[0e94] cd 07 02
                    push      af                            ;[0e97] f5
                    xor       a                             ;[0e98] af
                    ld        bc,$0002                      ;[0e99] 01 02 00
                    add       (hl)                          ;[0e9c] 86
                    inc       hl                            ;[0e9d] 23
                    djnz      $0e9c                         ;[0e9e] 10 fc
                    dec       c                             ;[0ea0] 0d
                    jr        nz,$0e9c                      ;[0ea1] 20 f9
                    ld        b,a                           ;[0ea3] 47
                    pop       af                            ;[0ea4] f1
                    call      $0207                         ;[0ea5] cd 07 02
                    ld        l,(ix+$26)                    ;[0ea8] dd 6e 26
                    ld        h,(ix+$27)                    ;[0eab] dd 66 27
                    add       hl,de                         ;[0eae] 19
                    push      de                            ;[0eaf] d5
                    ld        de,$0000                      ;[0eb0] 11 00 00
                    call      $19c0                         ;[0eb3] cd c0 19
                    or        a                             ;[0eb6] b7
                    sbc       hl,de                         ;[0eb7] ed 52
                    pop       de                            ;[0eb9] d1
                    ld        a,b                           ;[0eba] 78
                    or        a                             ;[0ebb] b7
                    pop       bc                            ;[0ebc] c1
                    ret                                     ;[0ebd] c9

                    push      de                            ;[0ebe] d5
                    ex        de,hl                         ;[0ebf] eb
                    ld        hl,$0009                      ;[0ec0] 21 09 00
                    add       hl,de                         ;[0ec3] 19
                    ld        a,(hl)                        ;[0ec4] 7e
                    add       a                             ;[0ec5] 87
                    ex        de,hl                         ;[0ec6] eb
                    pop       de                            ;[0ec7] d1
                    ccf                                     ;[0ec8] 3f
                    ld        a,$1c                         ;[0ec9] 3e 1c
                    ret                                     ;[0ecb] c9

                    call      $0fc9                         ;[0ecc] cd c9 0f
                    ld        a,$03                         ;[0ecf] 3e 03
                    call      $04d9                         ;[0ed1] cd d9 04
                    inc       de                            ;[0ed4] 13
                    push      hl                            ;[0ed5] e5
                    ld        (hl),$00                      ;[0ed6] 36 00
                    inc       hl                            ;[0ed8] 23
                    dec       de                            ;[0ed9] 1b
                    ld        a,d                           ;[0eda] 7a
                    or        e                             ;[0edb] b3
                    jr        nz,$0ed6                      ;[0edc] 20 f8
                    pop       hl                            ;[0ede] e1
                    ld        a,(ix+$09)                    ;[0edf] dd 7e 09
                    ld        (hl),a                        ;[0ee2] 77
                    inc       hl                            ;[0ee3] 23
                    ld        a,(ix+$0a)                    ;[0ee4] dd 7e 0a
                    ld        (hl),a                        ;[0ee7] 77
                    ret                                     ;[0ee8] c9

                    push      bc                            ;[0ee9] c5
                    push      hl                            ;[0eea] e5
                    push      de                            ;[0eeb] d5
                    ld        a,$03                         ;[0eec] 3e 03
                    call      $04d9                         ;[0eee] cd d9 04
                    push      de                            ;[0ef1] d5
                    call      $0fc9                         ;[0ef2] cd c9 0f
                    pop       de                            ;[0ef5] d1
                    add       hl,de                         ;[0ef6] 19
                    pop       de                            ;[0ef7] d1
                    ld        a,e                           ;[0ef8] 7b
                    and       $07                           ;[0ef9] e6 07
                    ld        b,a                           ;[0efb] 47
                    ld        a,$01                         ;[0efc] 3e 01
                    inc       b                             ;[0efe] 04
                    rrca                                    ;[0eff] 0f
                    djnz      $0eff                         ;[0f00] 10 fd
                    ld        b,a                           ;[0f02] 47
                    and       c                             ;[0f03] a1
                    ld        c,a                           ;[0f04] 4f
                    ld        a,b                           ;[0f05] 78
                    cpl                                     ;[0f06] 2f
                    and       (hl)                          ;[0f07] a6
                    or        c                             ;[0f08] b1
                    ld        (hl),a                        ;[0f09] 77
                    pop       hl                            ;[0f0a] e1
                    pop       bc                            ;[0f0b] c1
                    ret                                     ;[0f0c] c9

                    push      hl                            ;[0f0d] e5
                    push      bc                            ;[0f0e] c5
                    call      $0fc9                         ;[0f0f] cd c9 0f
                    ld        bc,$0880                      ;[0f12] 01 80 08
                    ld        a,(hl)                        ;[0f15] 7e
                    and       c                             ;[0f16] a1
                    jr        z,$0f27                       ;[0f17] 28 0e
                    rrc       c                             ;[0f19] cb 09
                    ld        a,d                           ;[0f1b] 7a
                    or        e                             ;[0f1c] b3
                    ld        a,$1a                         ;[0f1d] 3e 1a
                    jr        z,$0f3d                       ;[0f1f] 28 1c
                    dec       de                            ;[0f21] 1b
                    djnz      $0f15                         ;[0f22] 10 f1
                    inc       hl                            ;[0f24] 23
                    jr        $0f12                         ;[0f25] 18 eb
                    ld        a,(hl)                        ;[0f27] 7e
                    or        c                             ;[0f28] b1
                    ld        (hl),a                        ;[0f29] 77
                    ld        a,(ix+$05)                    ;[0f2a] dd 7e 05
                    sub       e                             ;[0f2d] 93
                    ld        e,a                           ;[0f2e] 5f
                    ld        a,(ix+$06)                    ;[0f2f] dd 7e 06
                    sbc       d                             ;[0f32] 9a
                    ld        d,a                           ;[0f33] 57
                    pop       bc                            ;[0f34] c1
                    push      bc                            ;[0f35] c5
                    ld        hl,$0022                      ;[0f36] 21 22 00
                    add       hl,bc                         ;[0f39] 09
                    set       0,(hl)                        ;[0f3a] cb c6
                    scf                                     ;[0f3c] 37
                    pop       bc                            ;[0f3d] c1
                    pop       hl                            ;[0f3e] e1
                    ret                                     ;[0f3f] c9

                    ld        h,b                           ;[0f40] 60
                    ld        l,c                           ;[0f41] 69
                    xor       a                             ;[0f42] af
                    push      bc                            ;[0f43] c5
                    ld        c,a                           ;[0f44] 4f
                    ld        a,$0f                         ;[0f45] 3e 0f
                    cp        (hl)                          ;[0f47] be
                    jr        c,$0f72                       ;[0f48] 38 28
                    ld        de,$0010                      ;[0f4a] 11 10 00
                    add       hl,de                         ;[0f4d] 19
                    ld        b,$10                         ;[0f4e] 06 10
                    inc       b                             ;[0f50] 04
                    jr        $0f70                         ;[0f51] 18 1d
                    ld        e,(hl)                        ;[0f53] 5e
                    inc       hl                            ;[0f54] 23
                    ld        a,(ix+$06)                    ;[0f55] dd 7e 06
                    or        a                             ;[0f58] b7
                    ld        d,a                           ;[0f59] 57
                    jr        z,$0f5f                       ;[0f5a] 28 03
                    dec       b                             ;[0f5c] 05
                    ld        d,(hl)                        ;[0f5d] 56
                    inc       hl                            ;[0f5e] 23
                    ld        a,d                           ;[0f5f] 7a
                    or        e                             ;[0f60] b3
                    jr        z,$0f70                       ;[0f61] 28 0d
                    push      hl                            ;[0f63] e5
                    ld        a,(ix+$05)                    ;[0f64] dd 7e 05
                    sub       e                             ;[0f67] 93
                    ld        a,(ix+$06)                    ;[0f68] dd 7e 06
                    sbc       d                             ;[0f6b] 9a
                    call      nc,$0ee9                      ;[0f6c] d4 e9 0e
                    pop       hl                            ;[0f6f] e1
                    djnz      $0f53                         ;[0f70] 10 e1
                    pop       bc                            ;[0f72] c1
                    scf                                     ;[0f73] 37
                    ret                                     ;[0f74] c9

                    push      de                            ;[0f75] d5
                    ex        de,hl                         ;[0f76] eb
                    ld        a,(de)                        ;[0f77] 1a
                    cp        $10                           ;[0f78] fe 10
                    ld        hl,$0000                      ;[0f7a] 21 00 00
                    jr        nc,$0f99                      ;[0f7d] 30 1a
                    ld        hl,$0010                      ;[0f7f] 21 10 00
                    add       hl,de                         ;[0f82] 19
                    ld        de,$1000                      ;[0f83] 11 00 10
                    ld        a,(ix+$06)                    ;[0f86] dd 7e 06
                    or        a                             ;[0f89] b7
                    ld        a,(hl)                        ;[0f8a] 7e
                    inc       hl                            ;[0f8b] 23
                    jr        z,$0f91                       ;[0f8c] 28 03
                    or        (hl)                          ;[0f8e] b6
                    dec       d                             ;[0f8f] 15
                    inc       hl                            ;[0f90] 23
                    or        a                             ;[0f91] b7
                    jr        z,$0f95                       ;[0f92] 28 01
                    inc       e                             ;[0f94] 1c
                    dec       d                             ;[0f95] 15
                    jr        nz,$0f86                      ;[0f96] 20 ee
                    ex        de,hl                         ;[0f98] eb
                    pop       de                            ;[0f99] d1
                    ld        a,(ix+$02)                    ;[0f9a] dd 7e 02
                    dec       a                             ;[0f9d] 3d
                    dec       a                             ;[0f9e] 3d
                    dec       a                             ;[0f9f] 3d
                    jr        z,$0fa5                       ;[0fa0] 28 03
                    add       hl,hl                         ;[0fa2] 29
                    jr        $0f9f                         ;[0fa3] 18 fa
                    ld        a,h                           ;[0fa5] 7c
                    or        l                             ;[0fa6] b5
                    scf                                     ;[0fa7] 37
                    ret                                     ;[0fa8] c9

                    ld        hl,$0000                      ;[0fa9] 21 00 00
                    push      hl                            ;[0fac] e5
                    call      $0fc9                         ;[0fad] cd c9 0f
                    ld        bc,$0880                      ;[0fb0] 01 80 08
                    ld        a,(hl)                        ;[0fb3] 7e
                    and       c                             ;[0fb4] a1
                    jr        nz,$0fba                      ;[0fb5] 20 03
                    ex        (sp),hl                       ;[0fb7] e3
                    inc       hl                            ;[0fb8] 23
                    ex        (sp),hl                       ;[0fb9] e3
                    rrc       c                             ;[0fba] cb 09
                    ld        a,d                           ;[0fbc] 7a
                    or        e                             ;[0fbd] b3
                    jr        z,$0fc6                       ;[0fbe] 28 06
                    dec       de                            ;[0fc0] 1b
                    djnz      $0fb3                         ;[0fc1] 10 f0
                    inc       hl                            ;[0fc3] 23
                    jr        $0fb0                         ;[0fc4] 18 ea
                    pop       hl                            ;[0fc6] e1
                    jr        $0f9a                         ;[0fc7] 18 d1
                    ld        l,(ix+$28)                    ;[0fc9] dd 6e 28
                    ld        h,(ix+$29)                    ;[0fcc] dd 66 29
                    ld        e,(ix+$05)                    ;[0fcf] dd 5e 05
                    ld        d,(ix+$06)                    ;[0fd2] dd 56 06
                    ret                                     ;[0fd5] c9

                    ld        a,(ix+$22)                    ;[0fd6] dd 7e 22
                    or        (ix+$23)                      ;[0fd9] dd b6 23
                    ld        a,$1b                         ;[0fdc] 3e 1b
                    ret       z                             ;[0fde] c8
                    ld        hl,$0022                      ;[0fdf] 21 22 00
                    add       hl,bc                         ;[0fe2] 09
                    set       1,(hl)                        ;[0fe3] cb ce
                    res       2,(hl)                        ;[0fe5] cb 96
                    ld        a,(ix+$22)                    ;[0fe7] dd 7e 22
                    sub       $01                           ;[0fea] d6 01
                    ld        (ix+$22),a                    ;[0fec] dd 77 22
                    jr        nc,$0ff4                      ;[0fef] 30 03
                    dec       (ix+$23)                      ;[0ff1] dd 35 23
                    scf                                     ;[0ff4] 37
                    ret                                     ;[0ff5] c9

                    push      bc                            ;[0ff6] c5
                    ld        c,(ix+$24)                    ;[0ff7] dd 4e 24
                    ld        b,(ix+$25)                    ;[0ffa] dd 46 25
                    call      $0c74                         ;[0ffd] cd 74 0c
                    ld        hl,$1033                      ;[1000] 21 33 10
                    call      $0dae                         ;[1003] cd ae 0d
                    jr        nc,$1031                      ;[1006] 30 29
                    ex        (sp),hl                       ;[1008] e3
                    push      hl                            ;[1009] e5
                    push      de                            ;[100a] d5
                    push      af                            ;[100b] f5
                    ld        de,$0022                      ;[100c] 11 22 00
                    add       hl,de                         ;[100f] 19
                    res       1,(hl)                        ;[1010] cb 8e
                    pop       af                            ;[1012] f1
                    pop       de                            ;[1013] d1
                    pop       hl                            ;[1014] e1
                    ex        (sp),hl                       ;[1015] e3
                    jr        nz,$102b                      ;[1016] 20 13
                    ex        de,hl                         ;[1018] eb
                    or        a                             ;[1019] b7
                    sbc       hl,bc                         ;[101a] ed 42
                    add       hl,bc                         ;[101c] 09
                    ex        de,hl                         ;[101d] eb
                    jr        c,$1022                       ;[101e] 38 02
                    ld        b,d                           ;[1020] 42
                    ld        c,e                           ;[1021] 4b
                    ld        (ix+$24),c                    ;[1022] dd 71 24
                    ld        (ix+$25),b                    ;[1025] dd 70 25
                    scf                                     ;[1028] 37
                    jr        $1031                         ;[1029] 18 06
                    call      $1040                         ;[102b] cd 40 10
                    ld        a,$20                         ;[102e] 3e 20
                    or        a                             ;[1030] b7
                    pop       bc                            ;[1031] c1
                    ret                                     ;[1032] c9

                    ld        a,(hl)                        ;[1033] 7e
                    xor       $e5                           ;[1034] ee e5
                    scf                                     ;[1036] 37
                    ret                                     ;[1037] c9

                    push      hl                            ;[1038] e5
                    ld        hl,$0022                      ;[1039] 21 22 00
                    add       hl,bc                         ;[103c] 09
                    res       1,(hl)                        ;[103d] cb 8e
                    pop       hl                            ;[103f] e1
                    scf                                     ;[1040] 37
                    inc       (ix+$22)                      ;[1041] dd 34 22
                    ret       nz                            ;[1044] c0
                    inc       (ix+$23)                      ;[1045] dd 34 23
                    ret                                     ;[1048] c9

                    ld        a,e                           ;[1049] 7b
                    and       $03                           ;[104a] e6 03
                    cpl                                     ;[104c] 2f
                    add       $04                           ;[104d] c6 04
                    jr        z,$1058                       ;[104f] 28 07
                    ld        bc,$0020                      ;[1051] 01 20 00
                    add       hl,bc                         ;[1054] 09
                    dec       a                             ;[1055] 3d
                    jr        nz,$1054                      ;[1056] 20 fc
                    ld        a,(hl)                        ;[1058] 7e
                    cp        $21                           ;[1059] fe 21
                    ret       nz                            ;[105b] c0
                    ld        a,e                           ;[105c] 7b
                    and       $03                           ;[105d] e6 03
                    jr        z,$1068                       ;[105f] 28 07
                    ld        bc,$000a                      ;[1061] 01 0a 00
                    add       hl,bc                         ;[1064] 09
                    dec       a                             ;[1065] 3d
                    jr        nz,$1064                      ;[1066] 20 fc
                    inc       hl                            ;[1068] 23
                    ret                                     ;[1069] c9

                    nop                                     ;[106a] 00
                    nop                                     ;[106b] 00
                    nop                                     ;[106c] 00
                    nop                                     ;[106d] 00
                    nop                                     ;[106e] 00
                    nop                                     ;[106f] 00
                    call      $0525                         ;[1070] cd 25 05
                    ret       nc                            ;[1073] d0
                    ld        hl,$0026                      ;[1074] 21 26 00
                    jr        $1080                         ;[1077] 18 07
                    call      $0525                         ;[1079] cd 25 05
                    ret       nc                            ;[107c] d0
                    ld        hl,$0023                      ;[107d] 21 23 00
                    add       hl,bc                         ;[1080] 09
                    ld        e,(hl)                        ;[1081] 5e
                    inc       hl                            ;[1082] 23
                    ld        d,(hl)                        ;[1083] 56
                    inc       hl                            ;[1084] 23
                    ld        a,(hl)                        ;[1085] 7e
                    ex        de,hl                         ;[1086] eb
                    ld        e,a                           ;[1087] 5f
                    ld        d,$00                         ;[1088] 16 00
                    scf                                     ;[108a] 37
                    ret                                     ;[108b] c9

                    call      $0525                         ;[108c] cd 25 05
                    ret       nc                            ;[108f] d0
                    ld        a,l                           ;[1090] 7d
                    ld        d,e                           ;[1091] 53
                    ld        e,h                           ;[1092] 5c
                    ld        hl,$0026                      ;[1093] 21 26 00
                    add       hl,bc                         ;[1096] 09
                    jr        $10ad                         ;[1097] 18 14
                    ld        a,$80                         ;[1099] 3e 80
                    jr        $109f                         ;[109b] 18 02
                    ld        a,$01                         ;[109d] 3e 01
                    ld        hl,$0026                      ;[109f] 21 26 00
                    add       hl,bc                         ;[10a2] 09
                    add       (hl)                          ;[10a3] 86
                    inc       hl                            ;[10a4] 23
                    ld        e,(hl)                        ;[10a5] 5e
                    inc       hl                            ;[10a6] 23
                    ld        d,(hl)                        ;[10a7] 56
                    jr        nc,$10ab                      ;[10a8] 30 01
                    inc       de                            ;[10aa] 13
                    dec       hl                            ;[10ab] 2b
                    dec       hl                            ;[10ac] 2b
                    push      hl                            ;[10ad] e5
                    push      af                            ;[10ae] f5
                    xor       (hl)                          ;[10af] ae
                    jp        m,$10bd                       ;[10b0] fa bd 10
                    inc       hl                            ;[10b3] 23
                    ld        a,(hl)                        ;[10b4] 7e
                    cp        e                             ;[10b5] bb
                    jr        nz,$10bd                      ;[10b6] 20 05
                    inc       hl                            ;[10b8] 23
                    ld        a,(hl)                        ;[10b9] 7e
                    cp        d                             ;[10ba] ba
                    jr        z,$10c3                       ;[10bb] 28 06
                    ld        hl,$0022                      ;[10bd] 21 22 00
                    add       hl,bc                         ;[10c0] 09
                    res       5,(hl)                        ;[10c1] cb ae
                    pop       af                            ;[10c3] f1
                    pop       hl                            ;[10c4] e1
                    ld        (hl),a                        ;[10c5] 77
                    inc       hl                            ;[10c6] 23
                    ld        (hl),e                        ;[10c7] 73
                    inc       hl                            ;[10c8] 23
                    ld        (hl),d                        ;[10c9] 72
                    scf                                     ;[10ca] 37
                    ret                                     ;[10cb] c9

                    push      bc                            ;[10cc] c5
                    push      af                            ;[10cd] f5
                    ld        hl,$0022                      ;[10ce] 21 22 00
                    add       hl,bc                         ;[10d1] 09
                    ex        de,hl                         ;[10d2] eb
                    ld        hl,$0025                      ;[10d3] 21 25 00
                    add       hl,bc                         ;[10d6] 09
                    ld        b,$03                         ;[10d7] 06 03
                    or        a                             ;[10d9] b7
                    inc       de                            ;[10da] 13
                    inc       hl                            ;[10db] 23
                    ld        a,(de)                        ;[10dc] 1a
                    sbc       (hl)                          ;[10dd] 9e
                    djnz      $10da                         ;[10de] 10 fa
                    jr        nc,$10e7                      ;[10e0] 30 05
                    ld        bc,$0003                      ;[10e2] 01 03 00
                    lddr                                    ;[10e5] ed b8
                    pop       af                            ;[10e7] f1
                    pop       bc                            ;[10e8] c1
                    ret                                     ;[10e9] c9

                    ld        a,c                           ;[10ea] 79
                    ld        ($dfd0),a                     ;[10eb] 32 d0 df
                    ld        ($dfd1),hl                    ;[10ee] 22 d1 df
                    call      $0514                         ;[10f1] cd 14 05
                    ret       nc                            ;[10f4] d0
                    add       hl,de                         ;[10f5] 19
                    push      hl                            ;[10f6] e5
                    call      $1107                         ;[10f7] cd 07 11
                    pop       hl                            ;[10fa] e1
                    ret       c                             ;[10fb] d8
                    push      af                            ;[10fc] f5
                    ld        de,($dfd1)                    ;[10fd] ed 5b d1 df
                    or        a                             ;[1101] b7
                    sbc       hl,de                         ;[1102] ed 52
                    ex        de,hl                         ;[1104] eb
                    pop       af                            ;[1105] f1
                    ret                                     ;[1106] c9

                    push      bc                            ;[1107] c5
                    push      de                            ;[1108] d5
                    ld        hl,$0023                      ;[1109] 21 23 00
                    add       hl,bc                         ;[110c] 09
                    ld        e,(hl)                        ;[110d] 5e
                    inc       hl                            ;[110e] 23
                    ld        d,(hl)                        ;[110f] 56
                    inc       hl                            ;[1110] 23
                    ld        b,(hl)                        ;[1111] 46
                    inc       hl                            ;[1112] 23
                    ld        a,e                           ;[1113] 7b
                    scf                                     ;[1114] 37
                    sbc       (hl)                          ;[1115] 9e
                    ld        e,a                           ;[1116] 5f
                    inc       hl                            ;[1117] 23
                    ld        a,d                           ;[1118] 7a
                    sbc       (hl)                          ;[1119] 9e
                    ld        d,a                           ;[111a] 57
                    inc       hl                            ;[111b] 23
                    ld        a,b                           ;[111c] 78
                    sbc       (hl)                          ;[111d] 9e
                    ex        de,hl                         ;[111e] eb
                    pop       de                            ;[111f] d1
                    pop       bc                            ;[1120] c1
                    jr        c,$1130                       ;[1121] 38 0d
                    dec       de                            ;[1123] 1b
                    sbc       hl,de                         ;[1124] ed 52
                    add       hl,de                         ;[1126] 19
                    sbc       $00                           ;[1127] de 00
                    jr        nc,$1134                      ;[1129] 30 09
                    ex        de,hl                         ;[112b] eb
                    call      $1134                         ;[112c] cd 34 11
                    ret       nc                            ;[112f] d0
                    ld        a,$19                         ;[1130] 3e 19
                    or        a                             ;[1132] b7
                    ret                                     ;[1133] c9

                    ld        hl,$0026                      ;[1134] 21 26 00
                    add       hl,bc                         ;[1137] 09
                    ld        a,(hl)                        ;[1138] 7e
                    and       $7f                           ;[1139] e6 7f
                    jr        z,$1144                       ;[113b] 28 07
                    call      $1158                         ;[113d] cd 58 11
                    ret       nc                            ;[1140] d0
                    ret       z                             ;[1141] c8
                    jr        $1134                         ;[1142] 18 f0
                    ld        hl,$ff81                      ;[1144] 21 81 ff
                    add       hl,de                         ;[1147] 19
                    jr        nc,$1151                      ;[1148] 30 07
                    call      $1175                         ;[114a] cd 75 11
                    ret       nc                            ;[114d] d0
                    ret       z                             ;[114e] c8
                    jr        $1144                         ;[114f] 18 f3
                    call      $1158                         ;[1151] cd 58 11
                    ret       nc                            ;[1154] d0
                    ret       z                             ;[1155] c8
                    jr        $1151                         ;[1156] 18 f9
                    call      $11cb                         ;[1158] cd cb 11
                    ret       nc                            ;[115b] d0
                    push      de                            ;[115c] d5
                    ld        e,a                           ;[115d] 5f
                    ld        a,($dfd0)                     ;[115e] 3a d0 df
                    ld        hl,($dfd1)                    ;[1161] 2a d1 df
                    call      $0207                         ;[1164] cd 07 02
                    ld        (hl),e                        ;[1167] 73
                    call      $0207                         ;[1168] cd 07 02
                    inc       hl                            ;[116b] 23
                    ld        ($dfd1),hl                    ;[116c] 22 d1 df
                    pop       de                            ;[116f] d1
                    ld        a,d                           ;[1170] 7a
                    or        e                             ;[1171] b3
                    dec       de                            ;[1172] 1b
                    scf                                     ;[1173] 37
                    ret                                     ;[1174] c9

                    push      de                            ;[1175] d5
                    call      $137d                         ;[1176] cd 7d 13
                    pop       de                            ;[1179] d1
                    ret       nc                            ;[117a] d0
                    push      de                            ;[117b] d5
                    call      $1099                         ;[117c] cd 99 10
                    ld        hl,$002d                      ;[117f] 21 2d 00
                    add       hl,bc                         ;[1182] 09
                    ld        e,(hl)                        ;[1183] 5e
                    inc       hl                            ;[1184] 23
                    ld        d,(hl)                        ;[1185] 56
                    inc       hl                            ;[1186] 23
                    push      bc                            ;[1187] c5
                    ld        c,(hl)                        ;[1188] 4e
                    ld        a,($dfd0)                     ;[1189] 3a d0 df
                    ld        b,a                           ;[118c] 47
                    ld        hl,($dfd1)                    ;[118d] 2a d1 df
                    ex        de,hl                         ;[1190] eb
                    ld        ix,$0080                      ;[1191] dd 21 80 00
                    call      $023d                         ;[1195] cd 3d 02
                    pop       bc                            ;[1198] c1
                    ld        ($dfd1),de                    ;[1199] ed 53 d1 df
                    pop       de                            ;[119d] d1
                    ld        hl,$ff81                      ;[119e] 21 81 ff
                    add       hl,de                         ;[11a1] 19
                    ex        de,hl                         ;[11a2] eb
                    ld        a,d                           ;[11a3] 7a
                    or        e                             ;[11a4] b3
                    dec       de                            ;[11a5] 1b
                    scf                                     ;[11a6] 37
                    ret                                     ;[11a7] c9

                    call      $0514                         ;[11a8] cd 14 05
                    ret       nc                            ;[11ab] d0
                    ld        hl,$0026                      ;[11ac] 21 26 00
                    add       hl,bc                         ;[11af] 09
                    ex        de,hl                         ;[11b0] eb
                    ld        hl,$0023                      ;[11b1] 21 23 00
                    add       hl,bc                         ;[11b4] 09
                    push      bc                            ;[11b5] c5
                    ld        b,$03                         ;[11b6] 06 03
                    or        a                             ;[11b8] b7
                    ld        a,(de)                        ;[11b9] 1a
                    sbc       (hl)                          ;[11ba] 9e
                    inc       de                            ;[11bb] 13
                    inc       hl                            ;[11bc] 23
                    djnz      $11b9                         ;[11bd] 10 fa
                    pop       bc                            ;[11bf] c1
                    ld        a,$19                         ;[11c0] 3e 19
                    call      c,$11cb                       ;[11c2] dc cb 11
                    ret       nc                            ;[11c5] d0
                    ld        c,a                           ;[11c6] 4f
                    cp        $1a                           ;[11c7] fe 1a
                    scf                                     ;[11c9] 37
                    ret                                     ;[11ca] c9

                    push      hl                            ;[11cb] e5
                    push      de                            ;[11cc] d5
                    ld        hl,$0022                      ;[11cd] 21 22 00
                    add       hl,bc                         ;[11d0] 09
                    bit       5,(hl)                        ;[11d1] cb 6e
                    jr        nz,$11da                      ;[11d3] 20 05
                    call      $137d                         ;[11d5] cd 7d 13
                    jr        nc,$11fb                      ;[11d8] 30 21
                    ld        hl,$0026                      ;[11da] 21 26 00
                    add       hl,bc                         ;[11dd] 09
                    ld        a,(hl)                        ;[11de] 7e
                    and       $7f                           ;[11df] e6 7f
                    ld        hl,$002d                      ;[11e1] 21 2d 00
                    add       hl,bc                         ;[11e4] 09
                    add       (hl)                          ;[11e5] 86
                    ld        e,a                           ;[11e6] 5f
                    inc       hl                            ;[11e7] 23
                    adc       (hl)                          ;[11e8] 8e
                    sub       e                             ;[11e9] 93
                    ld        d,a                           ;[11ea] 57
                    inc       hl                            ;[11eb] 23
                    ld        a,(hl)                        ;[11ec] 7e
                    ex        de,hl                         ;[11ed] eb
                    call      $0207                         ;[11ee] cd 07 02
                    ld        d,(hl)                        ;[11f1] 56
                    call      $0207                         ;[11f2] cd 07 02
                    push      de                            ;[11f5] d5
                    call      $109d                         ;[11f6] cd 9d 10
                    pop       af                            ;[11f9] f1
                    scf                                     ;[11fa] 37
                    pop       de                            ;[11fb] d1
                    pop       hl                            ;[11fc] e1
                    ret                                     ;[11fd] c9

                    ld        a,c                           ;[11fe] 79
                    ld        ($dfd0),a                     ;[11ff] 32 d0 df
                    ld        ($dfd1),hl                    ;[1202] 22 d1 df
                    call      $051c                         ;[1205] cd 1c 05
                    ret       nc                            ;[1208] d0
                    add       hl,de                         ;[1209] 19
                    push      hl                            ;[120a] e5
                    call      $121e                         ;[120b] cd 1e 12
                    call      $10cc                         ;[120e] cd cc 10
                    pop       hl                            ;[1211] e1
                    ret       c                             ;[1212] d8
                    push      af                            ;[1213] f5
                    ld        de,($dfd1)                    ;[1214] ed 5b d1 df
                    or        a                             ;[1218] b7
                    sbc       hl,de                         ;[1219] ed 52
                    ex        de,hl                         ;[121b] eb
                    pop       af                            ;[121c] f1
                    ret                                     ;[121d] c9

                    dec       de                            ;[121e] 1b
                    ld        hl,$0026                      ;[121f] 21 26 00
                    add       hl,bc                         ;[1222] 09
                    ld        a,(hl)                        ;[1223] 7e
                    and       $7f                           ;[1224] e6 7f
                    jr        z,$122f                       ;[1226] 28 07
                    call      $1243                         ;[1228] cd 43 12
                    ret       nc                            ;[122b] d0
                    ret       z                             ;[122c] c8
                    jr        $121f                         ;[122d] 18 f0
                    ld        hl,$ff81                      ;[122f] 21 81 ff
                    add       hl,de                         ;[1232] 19
                    jr        nc,$123c                      ;[1233] 30 07
                    call      $1261                         ;[1235] cd 61 12
                    ret       nc                            ;[1238] d0
                    ret       z                             ;[1239] c8
                    jr        $122f                         ;[123a] 18 f3
                    call      $1243                         ;[123c] cd 43 12
                    ret       nc                            ;[123f] d0
                    ret       z                             ;[1240] c8
                    jr        $123c                         ;[1241] 18 f9
                    ld        a,($dfd0)                     ;[1243] 3a d0 df
                    ld        hl,($dfd1)                    ;[1246] 2a d1 df
                    call      $0207                         ;[1249] cd 07 02
                    ld        l,(hl)                        ;[124c] 6e
                    call      $0207                         ;[124d] cd 07 02
                    ld        a,l                           ;[1250] 7d
                    call      $12a5                         ;[1251] cd a5 12
                    ret       nc                            ;[1254] d0
                    ld        hl,($dfd1)                    ;[1255] 2a d1 df
                    inc       hl                            ;[1258] 23
                    ld        ($dfd1),hl                    ;[1259] 22 d1 df
                    ld        a,d                           ;[125c] 7a
                    or        e                             ;[125d] b3
                    dec       de                            ;[125e] 1b
                    scf                                     ;[125f] 37
                    ret                                     ;[1260] c9

                    push      de                            ;[1261] d5
                    call      $134e                         ;[1262] cd 4e 13
                    pop       de                            ;[1265] d1
                    ret       nc                            ;[1266] d0
                    ld        hl,$0022                      ;[1267] 21 22 00
                    add       hl,bc                         ;[126a] 09
                    set       4,(hl)                        ;[126b] cb e6
                    push      de                            ;[126d] d5
                    call      $1099                         ;[126e] cd 99 10
                    ld        hl,$002d                      ;[1271] 21 2d 00
                    add       hl,bc                         ;[1274] 09
                    ld        e,(hl)                        ;[1275] 5e
                    inc       hl                            ;[1276] 23
                    ld        d,(hl)                        ;[1277] 56
                    inc       hl                            ;[1278] 23
                    push      bc                            ;[1279] c5
                    ld        b,(hl)                        ;[127a] 46
                    ld        a,($dfd0)                     ;[127b] 3a d0 df
                    ld        c,a                           ;[127e] 4f
                    ld        hl,($dfd1)                    ;[127f] 2a d1 df
                    ld        ix,$0080                      ;[1282] dd 21 80 00
                    call      $023d                         ;[1286] cd 3d 02
                    pop       bc                            ;[1289] c1
                    ld        ($dfd1),hl                    ;[128a] 22 d1 df
                    pop       de                            ;[128d] d1
                    ld        hl,$ff81                      ;[128e] 21 81 ff
                    add       hl,de                         ;[1291] 19
                    ex        de,hl                         ;[1292] eb
                    ld        a,d                           ;[1293] 7a
                    or        e                             ;[1294] b3
                    dec       de                            ;[1295] 1b
                    scf                                     ;[1296] 37
                    ret                                     ;[1297] c9

                    ld        e,c                           ;[1298] 59
                    call      $051c                         ;[1299] cd 1c 05
                    ret       nc                            ;[129c] d0
                    ld        a,e                           ;[129d] 7b
                    call      $12a5                         ;[129e] cd a5 12
                    call      c,$10cc                       ;[12a1] dc cc 10
                    ret                                     ;[12a4] c9

                    push      hl                            ;[12a5] e5
                    push      de                            ;[12a6] d5
                    ld        e,a                           ;[12a7] 5f
                    ld        hl,$0022                      ;[12a8] 21 22 00
                    add       hl,bc                         ;[12ab] 09
                    bit       5,(hl)                        ;[12ac] cb 6e
                    jr        nz,$12b9                      ;[12ae] 20 09
                    push      hl                            ;[12b0] e5
                    push      de                            ;[12b1] d5
                    call      $134e                         ;[12b2] cd 4e 13
                    pop       de                            ;[12b5] d1
                    pop       hl                            ;[12b6] e1
                    jr        nc,$12db                      ;[12b7] 30 22
                    set       4,(hl)                        ;[12b9] cb e6
                    ld        hl,$0026                      ;[12bb] 21 26 00
                    add       hl,bc                         ;[12be] 09
                    ld        a,(hl)                        ;[12bf] 7e
                    and       $7f                           ;[12c0] e6 7f
                    ld        hl,$002d                      ;[12c2] 21 2d 00
                    add       hl,bc                         ;[12c5] 09
                    push      de                            ;[12c6] d5
                    add       (hl)                          ;[12c7] 86
                    ld        e,a                           ;[12c8] 5f
                    inc       hl                            ;[12c9] 23
                    adc       (hl)                          ;[12ca] 8e
                    sub       e                             ;[12cb] 93
                    ld        d,a                           ;[12cc] 57
                    inc       hl                            ;[12cd] 23
                    ld        a,(hl)                        ;[12ce] 7e
                    ex        de,hl                         ;[12cf] eb
                    pop       de                            ;[12d0] d1
                    call      $0207                         ;[12d1] cd 07 02
                    ld        (hl),e                        ;[12d4] 73
                    call      $0207                         ;[12d5] cd 07 02
                    call      $109d                         ;[12d8] cd 9d 10
                    pop       de                            ;[12db] d1
                    pop       hl                            ;[12dc] e1
                    ret                                     ;[12dd] c9

                    call      $12f7                         ;[12de] cd f7 12
                    ret       nc                            ;[12e1] d0
                    ld        a,d                           ;[12e2] 7a
                    cp        e                             ;[12e3] bb
                    scf                                     ;[12e4] 37
                    ret                                     ;[12e5] c9

                    call      $12f7                         ;[12e6] cd f7 12
                    ret       nc                            ;[12e9] d0
                    call      $0207                         ;[12ea] cd 07 02
                    ld        (hl),e                        ;[12ed] 73
                    call      $0207                         ;[12ee] cd 07 02
                    call      $1099                         ;[12f1] cd 99 10
                    jp        $10cc                         ;[12f4] c3 cc 10
                    ld        hl,$0000                      ;[12f7] 21 00 00
                    ld        e,h                           ;[12fa] 5c
                    call      $1090                         ;[12fb] cd 90 10
                    ld        hl,$0022                      ;[12fe] 21 22 00
                    add       hl,bc                         ;[1301] 09
                    bit       5,(hl)                        ;[1302] cb 6e
                    jr        nz,$130a                      ;[1304] 20 04
                    call      $137d                         ;[1306] cd 7d 13
                    ret       nc                            ;[1309] d0
                    ld        hl,$002d                      ;[130a] 21 2d 00
                    add       hl,bc                         ;[130d] 09
                    ld        e,(hl)                        ;[130e] 5e
                    inc       hl                            ;[130f] 23
                    ld        d,(hl)                        ;[1310] 56
                    inc       hl                            ;[1311] 23
                    ld        a,(hl)                        ;[1312] 7e
                    ex        de,hl                         ;[1313] eb
                    push      af                            ;[1314] f5
                    call      $0207                         ;[1315] cd 07 02
                    push      af                            ;[1318] f5
                    xor       a                             ;[1319] af
                    ld        e,$7f                         ;[131a] 1e 7f
                    add       (hl)                          ;[131c] 86
                    inc       hl                            ;[131d] 23
                    dec       e                             ;[131e] 1d
                    jr        nz,$131c                      ;[131f] 20 fb
                    ld        e,a                           ;[1321] 5f
                    ld        d,(hl)                        ;[1322] 56
                    pop       af                            ;[1323] f1
                    call      $0207                         ;[1324] cd 07 02
                    pop       af                            ;[1327] f1
                    scf                                     ;[1328] 37
                    ret                                     ;[1329] c9

                    push      hl                            ;[132a] e5
                    ld        hl,$0022                      ;[132b] 21 22 00
                    add       hl,bc                         ;[132e] 09
                    bit       3,(hl)                        ;[132f] cb 5e
                    jr        z,$1347                       ;[1331] 28 14
                    bit       4,(hl)                        ;[1333] cb 66
                    jr        z,$1347                       ;[1335] 28 10
                    push      hl                            ;[1337] e5
                    push      de                            ;[1338] d5
                    ld        hl,$002b                      ;[1339] 21 2b 00
                    add       hl,bc                         ;[133c] 09
                    ld        e,(hl)                        ;[133d] 5e
                    inc       hl                            ;[133e] 23
                    ld        d,(hl)                        ;[133f] 56
                    call      $1624                         ;[1340] cd 24 16
                    pop       de                            ;[1343] d1
                    pop       hl                            ;[1344] e1
                    jr        nc,$134c                      ;[1345] 30 05
                    ld        a,(hl)                        ;[1347] 7e
                    and       $c7                           ;[1348] e6 c7
                    ld        (hl),a                        ;[134a] 77
                    scf                                     ;[134b] 37
                    pop       hl                            ;[134c] e1
                    ret                                     ;[134d] c9

                    ld        a,(bc)                        ;[134e] 0a
                    cp        $22                           ;[134f] fe 22
                    ld        hl,$135b                      ;[1351] 21 5b 13
                    jr        nz,$1388                      ;[1354] 20 32
                    ld        hl,$14eb                      ;[1356] 21 eb 14
                    jr        $1388                         ;[1359] 18 2d
                    push      de                            ;[135b] d5
                    call      $0ca8                         ;[135c] cd a8 0c
                    pop       de                            ;[135f] d1
                    call      c,$13f0                       ;[1360] dc f0 13
                    ret       nc                            ;[1363] d0
                    push      hl                            ;[1364] e5
                    push      de                            ;[1365] d5
                    push      af                            ;[1366] f5
                    call      $13d2                         ;[1367] cd d2 13
                    call      $14b0                         ;[136a] cd b0 14
                    call      nc,$1493                      ;[136d] d4 93 14
                    pop       af                            ;[1370] f1
                    pop       de                            ;[1371] d1
                    pop       hl                            ;[1372] e1
                    ret                                     ;[1373] c9

                    push      de                            ;[1374] d5
                    call      $0c81                         ;[1375] cd 81 0c
                    pop       de                            ;[1378] d1
                    call      c,$1423                       ;[1379] dc 23 14
                    ret                                     ;[137c] c9

                    ld        a,(bc)                        ;[137d] 0a
                    cp        $22                           ;[137e] fe 22
                    ld        hl,$1374                      ;[1380] 21 74 13
                    jr        nz,$1388                      ;[1383] 20 03
                    ld        hl,$14eb                      ;[1385] 21 eb 14
                    call      $13dc                         ;[1388] cd dc 13
                    ret       nc                            ;[138b] d0
                    push      hl                            ;[138c] e5
                    ld        hl,$0022                      ;[138d] 21 22 00
                    add       hl,bc                         ;[1390] 09
                    bit       3,(hl)                        ;[1391] cb 5e
                    pop       hl                            ;[1393] e1
                    jr        z,$13a2                       ;[1394] 28 0c
                    push      hl                            ;[1396] e5
                    ex        de,hl                         ;[1397] eb
                    call      $13d2                         ;[1398] cd d2 13
                    ex        de,hl                         ;[139b] eb
                    or        a                             ;[139c] b7
                    sbc       hl,de                         ;[139d] ed 52
                    pop       hl                            ;[139f] e1
                    jr        z,$13c8                       ;[13a0] 28 26
                    call      $0c20                         ;[13a2] cd 20 0c
                    call      c,$132a                       ;[13a5] dc 2a 13
                    ret       nc                            ;[13a8] d0
                    push      hl                            ;[13a9] e5
                    ld        hl,$0029                      ;[13aa] 21 29 00
                    add       hl,bc                         ;[13ad] 09
                    ld        (hl),e                        ;[13ae] 73
                    inc       hl                            ;[13af] 23
                    ld        (hl),d                        ;[13b0] 72
                    pop       hl                            ;[13b1] e1
                    call      $13d2                         ;[13b2] cd d2 13
                    call      $04ec                         ;[13b5] cd ec 04
                    ret       nc                            ;[13b8] d0
                    push      hl                            ;[13b9] e5
                    ld        hl,$002b                      ;[13ba] 21 2b 00
                    add       hl,bc                         ;[13bd] 09
                    ld        (hl),e                        ;[13be] 73
                    inc       hl                            ;[13bf] 23
                    ld        (hl),d                        ;[13c0] 72
                    pop       de                            ;[13c1] d1
                    inc       hl                            ;[13c2] 23
                    ld        (hl),e                        ;[13c3] 73
                    inc       hl                            ;[13c4] 23
                    ld        (hl),d                        ;[13c5] 72
                    inc       hl                            ;[13c6] 23
                    ld        (hl),a                        ;[13c7] 77
                    ld        hl,$0022                      ;[13c8] 21 22 00
                    add       hl,bc                         ;[13cb] 09
                    ld        a,(hl)                        ;[13cc] 7e
                    or        $28                           ;[13cd] f6 28
                    ld        (hl),a                        ;[13cf] 77
                    scf                                     ;[13d0] 37
                    ret                                     ;[13d1] c9

                    push      hl                            ;[13d2] e5
                    ld        hl,$0029                      ;[13d3] 21 29 00
                    add       hl,bc                         ;[13d6] 09
                    ld        e,(hl)                        ;[13d7] 5e
                    inc       hl                            ;[13d8] 23
                    ld        d,(hl)                        ;[13d9] 56
                    pop       hl                            ;[13da] e1
                    ret                                     ;[13db] c9

                    push      hl                            ;[13dc] e5
                    ld        hl,$0026                      ;[13dd] 21 26 00
                    add       hl,bc                         ;[13e0] 09
                    ld        a,(hl)                        ;[13e1] 7e
                    inc       hl                            ;[13e2] 23
                    ld        e,(hl)                        ;[13e3] 5e
                    inc       hl                            ;[13e4] 23
                    ld        d,(hl)                        ;[13e5] 56
                    ex        de,hl                         ;[13e6] eb
                    add       a                             ;[13e7] 87
                    adc       hl,hl                         ;[13e8] ed 6a
                    ex        de,hl                         ;[13ea] eb
                    ccf                                     ;[13eb] 3f
                    ld        a,$22                         ;[13ec] 3e 22
                    pop       hl                            ;[13ee] e1
                    ret                                     ;[13ef] c9

                    push      de                            ;[13f0] d5
                    call      $1459                         ;[13f1] cd 59 14
                    ex        de,hl                         ;[13f4] eb
                    ex        (sp),hl                       ;[13f5] e3
                    ex        de,hl                         ;[13f6] eb
                    jr        c,$140a                       ;[13f7] 38 11
                    call      $0f0d                         ;[13f9] cd 0d 0f
                    jr        nc,$1413                      ;[13fc] 30 15
                    ld        (hl),e                        ;[13fe] 73
                    ld        a,(ix+$06)                    ;[13ff] dd 7e 06
                    or        a                             ;[1402] b7
                    jr        z,$1407                       ;[1403] 28 02
                    inc       hl                            ;[1405] 23
                    ld        (hl),d                        ;[1406] 72
                    ex        de,hl                         ;[1407] eb
                    jr        $1411                         ;[1408] 18 07
                    ld        a,e                           ;[140a] 7b
                    and       $03                           ;[140b] e6 03
                    scf                                     ;[140d] 37
                    call      z,$14b0                       ;[140e] cc b0 14
                    sbc       a                             ;[1411] 9f
                    scf                                     ;[1412] 37
                    pop       de                            ;[1413] d1
                    call      c,$143c                       ;[1414] dc 3c 14
                    push      hl                            ;[1417] e5
                    call      c,$141d                       ;[1418] dc 1d 14
                    jr        $1435                         ;[141b] 18 18
                    jp        z,$160c                       ;[141d] ca 0c 16
                    jp        $15f4                         ;[1420] c3 f4 15
                    push      de                            ;[1423] d5
                    call      $1459                         ;[1424] cd 59 14
                    ex        de,hl                         ;[1427] eb
                    ex        (sp),hl                       ;[1428] e3
                    ex        de,hl                         ;[1429] eb
                    call      c,$14b0                       ;[142a] dc b0 14
                    pop       de                            ;[142d] d1
                    call      c,$143c                       ;[142e] dc 3c 14
                    push      hl                            ;[1431] e5
                    call      c,$15f4                       ;[1432] dc f4 15
                    ex        de,hl                         ;[1435] eb
                    ex        (sp),hl                       ;[1436] e3
                    push      af                            ;[1437] f5
                    add       hl,de                         ;[1438] 19
                    pop       af                            ;[1439] f1
                    pop       de                            ;[143a] d1
                    ret                                     ;[143b] c9

                    push      bc                            ;[143c] c5
                    push      af                            ;[143d] f5
                    ex        de,hl                         ;[143e] eb
                    ld        a,(ix+$02)                    ;[143f] dd 7e 02
                    call      $04e3                         ;[1442] cd e3 04
                    call      $19c0                         ;[1445] cd c0 19
                    ex        de,hl                         ;[1448] eb
                    ld        b,d                           ;[1449] 42
                    ld        a,d                           ;[144a] 7a
                    and       $01                           ;[144b] e6 01
                    ld        d,a                           ;[144d] 57
                    ex        de,hl                         ;[144e] eb
                    xor       b                             ;[144f] a8
                    rrca                                    ;[1450] 0f
                    add       e                             ;[1451] 83
                    ld        e,a                           ;[1452] 5f
                    adc       d                             ;[1453] 8a
                    sub       e                             ;[1454] 93
                    ld        d,a                           ;[1455] 57
                    pop       af                            ;[1456] f1
                    pop       bc                            ;[1457] c1
                    ret                                     ;[1458] c9

                    push      bc                            ;[1459] c5
                    ld        h,b                           ;[145a] 60
                    ld        l,c                           ;[145b] 69
                    ld        a,(ix+$03)                    ;[145c] dd 7e 03
                    and       e                             ;[145f] a3
                    rra                                     ;[1460] 1f
                    ld        b,a                           ;[1461] 47
                    ld        a,$00                         ;[1462] 3e 00
                    rra                                     ;[1464] 1f
                    ld        c,a                           ;[1465] 4f
                    ld        a,(ix+$02)                    ;[1466] dd 7e 02
                    call      $04d9                         ;[1469] cd d9 04
                    ld        d,$00                         ;[146c] 16 00
                    ld        a,(ix+$06)                    ;[146e] dd 7e 06
                    or        a                             ;[1471] b7
                    ld        a,e                           ;[1472] 7b
                    jr        z,$1480                       ;[1473] 28 0b
                    and       $07                           ;[1475] e6 07
                    add       a                             ;[1477] 87
                    add       $11                           ;[1478] c6 11
                    ld        e,a                           ;[147a] 5f
                    add       hl,de                         ;[147b] 19
                    ld        d,(hl)                        ;[147c] 56
                    dec       hl                            ;[147d] 2b
                    jr        $1486                         ;[147e] 18 06
                    and       $0f                           ;[1480] e6 0f
                    add       $10                           ;[1482] c6 10
                    ld        e,a                           ;[1484] 5f
                    add       hl,de                         ;[1485] 19
                    ld        e,(hl)                        ;[1486] 5e
                    ld        a,d                           ;[1487] 7a
                    or        e                             ;[1488] b3
                    ld        a,$19                         ;[1489] 3e 19
                    jr        z,$148f                       ;[148b] 28 02
                    ex        de,hl                         ;[148d] eb
                    scf                                     ;[148e] 37
                    ld        d,b                           ;[148f] 50
                    ld        e,c                           ;[1490] 59
                    pop       bc                            ;[1491] c1
                    ret                                     ;[1492] c9

                    push      hl                            ;[1493] e5
                    ld        a,e                           ;[1494] 7b
                    and       $7f                           ;[1495] e6 7f
                    inc       a                             ;[1497] 3c
                    ld        hl,$000f                      ;[1498] 21 0f 00
                    add       hl,bc                         ;[149b] 09
                    ld        (hl),a                        ;[149c] 77
                    ld        a,e                           ;[149d] 7b
                    rla                                     ;[149e] 17
                    ld        a,d                           ;[149f] 7a
                    rla                                     ;[14a0] 17
                    and       $1f                           ;[14a1] e6 1f
                    dec       hl                            ;[14a3] 2b
                    dec       hl                            ;[14a4] 2b
                    dec       hl                            ;[14a5] 2b
                    ld        (hl),a                        ;[14a6] 77
                    ld        hl,$0022                      ;[14a7] 21 22 00
                    add       hl,bc                         ;[14aa] 09
                    set       0,(hl)                        ;[14ab] cb c6
                    scf                                     ;[14ad] 37
                    pop       hl                            ;[14ae] e1
                    ret                                     ;[14af] c9

                    push      de                            ;[14b0] d5
                    push      hl                            ;[14b1] e5
                    call      $14c2                         ;[14b2] cd c2 14
                    or        a                             ;[14b5] b7
                    ld        a,$22                         ;[14b6] 3e 22
                    jr        nz,$14bf                      ;[14b8] 20 05
                    ex        de,hl                         ;[14ba] eb
                    sbc       hl,de                         ;[14bb] ed 52
                    ld        a,$19                         ;[14bd] 3e 19
                    pop       hl                            ;[14bf] e1
                    pop       de                            ;[14c0] d1
                    ret                                     ;[14c1] c9

                    push      de                            ;[14c2] d5
                    ld        hl,$000c                      ;[14c3] 21 0c 00
                    add       hl,bc                         ;[14c6] 09
                    ld        d,(hl)                        ;[14c7] 56
                    ld        e,$00                         ;[14c8] 1e 00
                    srl       d                             ;[14ca] cb 3a
                    rr        e                             ;[14cc] cb 1b
                    inc       hl                            ;[14ce] 23
                    inc       hl                            ;[14cf] 23
                    inc       hl                            ;[14d0] 23
                    ld        a,(hl)                        ;[14d1] 7e
                    or        a                             ;[14d2] b7
                    jp        p,$14d8                       ;[14d3] f2 d8 14
                    ld        a,$80                         ;[14d6] 3e 80
                    add       e                             ;[14d8] 83
                    ld        e,a                           ;[14d9] 5f
                    adc       d                             ;[14da] 8a
                    sub       e                             ;[14db] 93
                    dec       hl                            ;[14dc] 2b
                    ld        l,(hl)                        ;[14dd] 6e
                    ld        h,$00                         ;[14de] 26 00
                    add       hl,hl                         ;[14e0] 29
                    add       hl,hl                         ;[14e1] 29
                    add       hl,hl                         ;[14e2] 29
                    add       hl,hl                         ;[14e3] 29
                    add       l                             ;[14e4] 85
                    ld        d,a                           ;[14e5] 57
                    adc       h                             ;[14e6] 8c
                    sub       d                             ;[14e7] 92
                    ex        de,hl                         ;[14e8] eb
                    pop       de                            ;[14e9] d1
                    ret                                     ;[14ea] c9

                    ld        a,e                           ;[14eb] 7b
                    and       $03                           ;[14ec] e6 03
                    ld        h,a                           ;[14ee] 67
                    ld        l,$00                         ;[14ef] 2e 00
                    srl       h                             ;[14f1] cb 3c
                    rr        l                             ;[14f3] cb 1d
                    ld        a,$02                         ;[14f5] 3e 02
                    call      $04d9                         ;[14f7] cd d9 04
                    push      hl                            ;[14fa] e5
                    push      de                            ;[14fb] d5
                    ex        de,hl                         ;[14fc] eb
                    ld        a,(ix+$02)                    ;[14fd] dd 7e 02
                    ld        e,(ix+$05)                    ;[1500] dd 5e 05
                    ld        d,(ix+$06)                    ;[1503] dd 56 06
                    inc       de                            ;[1506] 13
                    call      $04e3                         ;[1507] cd e3 04
                    call      $19c0                         ;[150a] cd c0 19
                    or        a                             ;[150d] b7
                    sbc       hl,de                         ;[150e] ed 52
                    pop       de                            ;[1510] d1
                    pop       hl                            ;[1511] e1
                    ld        a,$19                         ;[1512] 3e 19
                    jp        $1431                         ;[1514] c3 31 14
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
                    ld        de,($e290)                    ;[1530] ed 5b 90 e2
                    ret                                     ;[1534] c9

                    call      $1706                         ;[1535] cd 06 17
                    ret       nc                            ;[1538] d0
                    ld        hl,$0000                      ;[1539] 21 00 00
                    ld        ($e292),hl                    ;[153c] 22 92 e2
                    ld        ($e294),hl                    ;[153f] 22 94 e2
                    ld        h,d                           ;[1542] 62
                    ld        ($e290),hl                    ;[1543] 22 90 e2
                    ld        ix,$dfe0                      ;[1546] dd 21 e0 df
                    ld        b,$10                         ;[154a] 06 10
                    ld        a,$07                         ;[154c] 3e 07
                    ld        hl,$e090                      ;[154e] 21 90 e0
                    jr        $1561                         ;[1551] 18 0e
                    ld        a,e                           ;[1553] 7b
                    or        a                             ;[1554] b7
                    scf                                     ;[1555] 37
                    ret       z                             ;[1556] c8
                    ld        hl,$e290                      ;[1557] 21 90 e2
                    inc       (hl)                          ;[155a] 34
                    ld        a,d                           ;[155b] 7a
                    inc       d                             ;[155c] 14
                    dec       e                             ;[155d] 1d
                    call      $022b                         ;[155e] cd 2b 02
                    ld        (ix+$08),l                    ;[1561] dd 75 08
                    ld        (ix+$09),h                    ;[1564] dd 74 09
                    ld        (ix+$0a),a                    ;[1567] dd 77 0a
                    ld        hl,($e294)                    ;[156a] 2a 94 e2
                    ld        (ix+$00),l                    ;[156d] dd 75 00
                    ld        (ix+$01),h                    ;[1570] dd 74 01
                    ld        ($e294),ix                    ;[1573] dd 22 94 e2
                    ex        de,hl                         ;[1577] eb
                    ld        de,$000b                      ;[1578] 11 0b 00
                    add       ix,de                         ;[157b] dd 19
                    ex        de,hl                         ;[157d] eb
                    djnz      $1553                         ;[157e] 10 d3
                    scf                                     ;[1580] 37
                    ret                                     ;[1581] c9

                    ld        de,$e292                      ;[1582] 11 92 e2
                    ex        de,hl                         ;[1585] eb
                    call      $179b                         ;[1586] cd 9b 17
                    ccf                                     ;[1589] 3f
                    ld        a,$21                         ;[158a] 3e 21
                    ret       z                             ;[158c] c8
                    push      hl                            ;[158d] e5
                    ld        hl,$0005                      ;[158e] 21 05 00
                    add       hl,de                         ;[1591] 19
                    ld        a,(hl)                        ;[1592] 7e
                    cp        (ix+$1c)                      ;[1593] dd be 1c
                    jr        nz,$15a0                      ;[1596] 20 08
                    inc       hl                            ;[1598] 23
                    ld        a,(hl)                        ;[1599] 7e
                    inc       hl                            ;[159a] 23
                    ld        h,(hl)                        ;[159b] 66
                    ld        l,a                           ;[159c] 6f
                    or        a                             ;[159d] b7
                    sbc       hl,bc                         ;[159e] ed 42
                    pop       hl                            ;[15a0] e1
                    jr        nz,$1585                      ;[15a1] 20 e2
                    scf                                     ;[15a3] 37
                    ret                                     ;[15a4] c9

                    call      $1798                         ;[15a5] cd 98 17
                    jr        nz,$15c4                      ;[15a8] 20 1a
                    ld        de,$e292                      ;[15aa] 11 92 e2
                    ex        de,hl                         ;[15ad] eb
                    call      $179b                         ;[15ae] cd 9b 17
                    ccf                                     ;[15b1] 3f
                    ld        a,$21                         ;[15b2] 3e 21
                    ret       z                             ;[15b4] c8
                    call      $17a3                         ;[15b5] cd a3 17
                    jr        nz,$15ad                      ;[15b8] 20 f3
                    call      $15db                         ;[15ba] cd db 15
                    call      $16ec                         ;[15bd] cd ec 16
                    ret       nc                            ;[15c0] d0
                    call      $17aa                         ;[15c1] cd aa 17
                    push      hl                            ;[15c4] e5
                    ld        hl,$0002                      ;[15c5] 21 02 00
                    add       hl,de                         ;[15c8] 19
                    xor       a                             ;[15c9] af
                    ld        (hl),a                        ;[15ca] 77
                    inc       hl                            ;[15cb] 23
                    ld        (hl),a                        ;[15cc] 77
                    inc       hl                            ;[15cd] 23
                    ld        (hl),a                        ;[15ce] 77
                    inc       hl                            ;[15cf] 23
                    ld        a,(ix+$1c)                    ;[15d0] dd 7e 1c
                    ld        (hl),a                        ;[15d3] 77
                    inc       hl                            ;[15d4] 23
                    ld        (hl),c                        ;[15d5] 71
                    inc       hl                            ;[15d6] 23
                    ld        (hl),b                        ;[15d7] 70
                    pop       hl                            ;[15d8] e1
                    scf                                     ;[15d9] 37
                    ret                                     ;[15da] c9

                    push      ix                            ;[15db] dd e5
                    push      hl                            ;[15dd] e5
                    push      de                            ;[15de] d5
                    push      bc                            ;[15df] c5
                    ld        hl,$0005                      ;[15e0] 21 05 00
                    add       hl,de                         ;[15e3] 19
                    ld        a,(hl)                        ;[15e4] 7e
                    inc       hl                            ;[15e5] 23
                    ld        e,(hl)                        ;[15e6] 5e
                    inc       hl                            ;[15e7] 23
                    ld        d,(hl)                        ;[15e8] 56
                    ex        de,hl                         ;[15e9] eb
                    ld        d,a                           ;[15ea] 57
                    call      $05f0                         ;[15eb] cd f0 05
                    pop       bc                            ;[15ee] c1
                    pop       de                            ;[15ef] d1
                    pop       hl                            ;[15f0] e1
                    pop       ix                            ;[15f1] dd e1
                    ret                                     ;[15f3] c9

                    bit       3,(ix+$1b)                    ;[15f4] dd cb 1b 5e
                    jp        nz,$1ab6                      ;[15f8] c2 b6 1a
                    push      de                            ;[15fb] d5
                    push      bc                            ;[15fc] c5
                    ld        b,d                           ;[15fd] 42
                    ld        c,e                           ;[15fe] 4b
                    call      $1582                         ;[15ff] cd 82 15
                    jr        c,$160a                       ;[1602] 38 06
                    call      $15a5                         ;[1604] cd a5 15
                    call      c,$1778                       ;[1607] dc 78 17
                    jr        $161d                         ;[160a] 18 11
                    bit       3,(ix+$1b)                    ;[160c] dd cb 1b 5e
                    jp        nz,$1ab6                      ;[1610] c2 b6 1a
                    push      de                            ;[1613] d5
                    push      bc                            ;[1614] c5
                    ld        b,d                           ;[1615] 42
                    ld        c,e                           ;[1616] 4b
                    call      $1582                         ;[1617] cd 82 15
                    call      nc,$15a5                      ;[161a] d4 a5 15
                    push      af                            ;[161d] f5
                    call      c,$17b2                       ;[161e] dc b2 17
                    pop       af                            ;[1621] f1
                    jr        $1643                         ;[1622] 18 1f
                    bit       3,(ix+$1b)                    ;[1624] dd cb 1b 5e
                    jp        nz,$1ab6                      ;[1628] c2 b6 1a
                    push      de                            ;[162b] d5
                    push      bc                            ;[162c] c5
                    push      bc                            ;[162d] c5
                    ld        b,d                           ;[162e] 42
                    ld        c,e                           ;[162f] 4b
                    call      $1582                         ;[1630] cd 82 15
                    pop       bc                            ;[1633] c1
                    jr        nc,$1643                      ;[1634] 30 0d
                    push      hl                            ;[1636] e5
                    ld        hl,$0002                      ;[1637] 21 02 00
                    add       hl,de                         ;[163a] 19
                    set       0,(hl)                        ;[163b] cb c6
                    inc       hl                            ;[163d] 23
                    ld        (hl),c                        ;[163e] 71
                    inc       hl                            ;[163f] 23
                    ld        (hl),b                        ;[1640] 70
                    pop       hl                            ;[1641] e1
                    scf                                     ;[1642] 37
                    jr        nc,$1650                      ;[1643] 30 0b
                    ld        hl,$0008                      ;[1645] 21 08 00
                    add       hl,de                         ;[1648] 19
                    ld        e,(hl)                        ;[1649] 5e
                    inc       hl                            ;[164a] 23
                    ld        d,(hl)                        ;[164b] 56
                    inc       hl                            ;[164c] 23
                    ld        a,(hl)                        ;[164d] 7e
                    ex        de,hl                         ;[164e] eb
                    scf                                     ;[164f] 37
                    pop       bc                            ;[1650] c1
                    pop       de                            ;[1651] d1
                    ret                                     ;[1652] c9

                    call      $18ab                         ;[1653] cd ab 18
                    ret       c                             ;[1656] d8
                    push      hl                            ;[1657] e5
                    push      de                            ;[1658] d5
                    push      bc                            ;[1659] c5
                    ld        e,(ix+$07)                    ;[165a] dd 5e 07
                    ld        d,(ix+$08)                    ;[165d] dd 56 08
                    inc       de                            ;[1660] 13
                    ld        a,$02                         ;[1661] 3e 02
                    call      $04d9                         ;[1663] cd d9 04
                    call      $19c0                         ;[1666] cd c0 19
                    dec       de                            ;[1669] 1b
                    jr        $1672                         ;[166a] 18 06
                    push      hl                            ;[166c] e5
                    push      de                            ;[166d] d5
                    push      bc                            ;[166e] c5
                    ld        de,$ffff                      ;[166f] 11 ff ff
                    ld        b,d                           ;[1672] 42
                    ld        c,e                           ;[1673] 4b
                    ld        de,$e292                      ;[1674] 11 92 e2
                    ex        de,hl                         ;[1677] eb
                    call      $179b                         ;[1678] cd 9b 17
                    jr        z,$16e8                       ;[167b] 28 6b
                    push      hl                            ;[167d] e5
                    ld        hl,$0005                      ;[167e] 21 05 00
                    add       hl,de                         ;[1681] 19
                    ld        a,(hl)                        ;[1682] 7e
                    cp        (ix+$1c)                      ;[1683] dd be 1c
                    jr        nz,$16a4                      ;[1686] 20 1c
                    inc       hl                            ;[1688] 23
                    ld        a,c                           ;[1689] 79
                    sub       (hl)                          ;[168a] 96
                    inc       hl                            ;[168b] 23
                    ld        a,b                           ;[168c] 78
                    sbc       (hl)                          ;[168d] 9e
                    jr        c,$16a4                       ;[168e] 38 14
                    call      $15db                         ;[1690] cd db 15
                    dec       hl                            ;[1693] 2b
                    dec       hl                            ;[1694] 2b
                    dec       hl                            ;[1695] 2b
                    dec       hl                            ;[1696] 2b
                    dec       hl                            ;[1697] 2b
                    bit       0,(hl)                        ;[1698] cb 46
                    jr        nz,$16a4                      ;[169a] 20 08
                    pop       hl                            ;[169c] e1
                    push      hl                            ;[169d] e5
                    call      $17aa                         ;[169e] cd aa 17
                    pop       hl                            ;[16a1] e1
                    jr        $1678                         ;[16a2] 18 d4
                    pop       hl                            ;[16a4] e1
                    jr        $1677                         ;[16a5] 18 d0
                    push      hl                            ;[16a7] e5
                    push      de                            ;[16a8] d5
                    push      bc                            ;[16a9] c5
                    ld        de,$e292                      ;[16aa] 11 92 e2
                    ex        de,hl                         ;[16ad] eb
                    call      $179b                         ;[16ae] cd 9b 17
                    jr        z,$16e8                       ;[16b1] 28 35
                    push      hl                            ;[16b3] e5
                    ld        hl,$0005                      ;[16b4] 21 05 00
                    add       hl,de                         ;[16b7] 19
                    ld        a,(hl)                        ;[16b8] 7e
                    pop       hl                            ;[16b9] e1
                    cp        (ix+$1c)                      ;[16ba] dd be 1c
                    jr        nz,$16ad                      ;[16bd] 20 ee
                    push      hl                            ;[16bf] e5
                    call      $17aa                         ;[16c0] cd aa 17
                    pop       hl                            ;[16c3] e1
                    jr        $16ae                         ;[16c4] 18 e8
                    push      hl                            ;[16c6] e5
                    push      de                            ;[16c7] d5
                    push      bc                            ;[16c8] c5
                    ld        de,$e292                      ;[16c9] 11 92 e2
                    ex        de,hl                         ;[16cc] eb
                    call      $179b                         ;[16cd] cd 9b 17
                    jr        z,$16e8                       ;[16d0] 28 16
                    push      hl                            ;[16d2] e5
                    ld        hl,$0003                      ;[16d3] 21 03 00
                    add       hl,de                         ;[16d6] 19
                    ld        a,(hl)                        ;[16d7] 7e
                    inc       hl                            ;[16d8] 23
                    ld        h,(hl)                        ;[16d9] 66
                    ld        l,a                           ;[16da] 6f
                    or        a                             ;[16db] b7
                    sbc       hl,bc                         ;[16dc] ed 42
                    pop       hl                            ;[16de] e1
                    jr        nz,$16cc                      ;[16df] 20 eb
                    push      hl                            ;[16e1] e5
                    call      $17aa                         ;[16e2] cd aa 17
                    pop       hl                            ;[16e5] e1
                    jr        $16cd                         ;[16e6] 18 e5
                    pop       bc                            ;[16e8] c1
                    pop       de                            ;[16e9] d1
                    pop       hl                            ;[16ea] e1
                    ret                                     ;[16eb] c9

                    push      ix                            ;[16ec] dd e5
                    push      hl                            ;[16ee] e5
                    ld        hl,$0002                      ;[16ef] 21 02 00
                    add       hl,de                         ;[16f2] 19
                    bit       0,(hl)                        ;[16f3] cb 46
                    scf                                     ;[16f5] 37
                    jr        z,$1702                       ;[16f6] 28 0a
                    inc       hl                            ;[16f8] 23
                    inc       hl                            ;[16f9] 23
                    inc       hl                            ;[16fa] 23
                    ld        a,(hl)                        ;[16fb] 7e
                    call      $184d                         ;[16fc] cd 4d 18
                    call      $1719                         ;[16ff] cd 19 17
                    pop       hl                            ;[1702] e1
                    pop       ix                            ;[1703] dd e1
                    ret                                     ;[1705] c9

                    push      hl                            ;[1706] e5
                    push      de                            ;[1707] d5
                    ld        de,$e292                      ;[1708] 11 92 e2
                    ex        de,hl                         ;[170b] eb
                    call      $179b                         ;[170c] cd 9b 17
                    jr        z,$1716                       ;[170f] 28 05
                    call      $16ec                         ;[1711] cd ec 16
                    jr        c,$170b                       ;[1714] 38 f5
                    pop       de                            ;[1716] d1
                    pop       hl                            ;[1717] e1
                    ret                                     ;[1718] c9

                    push      hl                            ;[1719] e5
                    push      de                            ;[171a] d5
                    push      bc                            ;[171b] c5
                    ld        de,$e292                      ;[171c] 11 92 e2
                    ld        bc,$ffff                      ;[171f] 01 ff ff
                    call      $173a                         ;[1722] cd 3a 17
                    jr        z,$1736                       ;[1725] 28 0f
                    push      de                            ;[1727] d5
                    call      $173a                         ;[1728] cd 3a 17
                    jr        z,$1730                       ;[172b] 28 03
                    pop       af                            ;[172d] f1
                    jr        $1727                         ;[172e] 18 f7
                    pop       de                            ;[1730] d1
                    call      $1762                         ;[1731] cd 62 17
                    jr        c,$171c                       ;[1734] 38 e6
                    pop       bc                            ;[1736] c1
                    pop       de                            ;[1737] d1
                    pop       hl                            ;[1738] e1
                    ret                                     ;[1739] c9

                    ex        de,hl                         ;[173a] eb
                    call      $179b                         ;[173b] cd 9b 17
                    ret       z                             ;[173e] c8
                    ld        hl,$0002                      ;[173f] 21 02 00
                    add       hl,de                         ;[1742] 19
                    bit       0,(hl)                        ;[1743] cb 46
                    jr        z,$173a                       ;[1745] 28 f3
                    inc       hl                            ;[1747] 23
                    inc       hl                            ;[1748] 23
                    inc       hl                            ;[1749] 23
                    ld        a,(hl)                        ;[174a] 7e
                    cp        (ix+$1c)                      ;[174b] dd be 1c
                    jr        nz,$173a                      ;[174e] 20 ea
                    inc       hl                            ;[1750] 23
                    ld        a,(hl)                        ;[1751] 7e
                    inc       hl                            ;[1752] 23
                    ld        h,(hl)                        ;[1753] 66
                    ld        l,a                           ;[1754] 6f
                    or        a                             ;[1755] b7
                    sbc       hl,bc                         ;[1756] ed 42
                    add       hl,bc                         ;[1758] 09
                    jr        z,$175d                       ;[1759] 28 02
                    jr        nc,$173a                      ;[175b] 30 dd
                    ld        b,h                           ;[175d] 44
                    ld        c,l                           ;[175e] 4d
                    scf                                     ;[175f] 37
                    sbc       a                             ;[1760] 9f
                    ret                                     ;[1761] c9

                    push      de                            ;[1762] d5
                    call      $1785                         ;[1763] cd 85 17
                    call      $1a13                         ;[1766] cd 13 1a
                    pop       de                            ;[1769] d1
                    ret       nc                            ;[176a] d0
                    ld        hl,$0002                      ;[176b] 21 02 00
                    add       hl,de                         ;[176e] 19
                    res       0,(hl)                        ;[176f] cb 86
                    inc       hl                            ;[1771] 23
                    xor       a                             ;[1772] af
                    ld        (hl),a                        ;[1773] 77
                    inc       hl                            ;[1774] 23
                    ld        (hl),a                        ;[1775] 77
                    scf                                     ;[1776] 37
                    ret                                     ;[1777] c9

                    push      hl                            ;[1778] e5
                    push      de                            ;[1779] d5
                    push      bc                            ;[177a] c5
                    call      $1785                         ;[177b] cd 85 17
                    call      $1a0a                         ;[177e] cd 0a 1a
                    pop       bc                            ;[1781] c1
                    pop       de                            ;[1782] d1
                    pop       hl                            ;[1783] e1
                    ret                                     ;[1784] c9

                    ld        hl,$000a                      ;[1785] 21 0a 00
                    add       hl,de                         ;[1788] 19
                    ld        b,(hl)                        ;[1789] 46
                    dec       hl                            ;[178a] 2b
                    ld        d,(hl)                        ;[178b] 56
                    dec       hl                            ;[178c] 2b
                    ld        e,(hl)                        ;[178d] 5e
                    push      de                            ;[178e] d5
                    dec       hl                            ;[178f] 2b
                    ld        d,(hl)                        ;[1790] 56
                    dec       hl                            ;[1791] 2b
                    ld        e,(hl)                        ;[1792] 5e
                    call      $19df                         ;[1793] cd df 19
                    pop       hl                            ;[1796] e1
                    ret                                     ;[1797] c9

                    ld        hl,$e294                      ;[1798] 21 94 e2
                    ld        e,(hl)                        ;[179b] 5e
                    inc       hl                            ;[179c] 23
                    ld        d,(hl)                        ;[179d] 56
                    dec       hl                            ;[179e] 2b
                    ld        a,d                           ;[179f] 7a
                    or        e                             ;[17a0] b3
                    scf                                     ;[17a1] 37
                    ret                                     ;[17a2] c9

                    ex        de,hl                         ;[17a3] eb
                    ld        a,(hl)                        ;[17a4] 7e
                    inc       hl                            ;[17a5] 23
                    or        (hl)                          ;[17a6] b6
                    dec       hl                            ;[17a7] 2b
                    ex        de,hl                         ;[17a8] eb
                    ret                                     ;[17a9] c9

                    call      $17c3                         ;[17aa] cd c3 17
                    ld        hl,$e294                      ;[17ad] 21 94 e2
                    jr        $17b8                         ;[17b0] 18 06
                    call      $17c3                         ;[17b2] cd c3 17
                    ld        hl,$e292                      ;[17b5] 21 92 e2
                    ld        a,(hl)                        ;[17b8] 7e
                    ld        (de),a                        ;[17b9] 12
                    inc       hl                            ;[17ba] 23
                    inc       de                            ;[17bb] 13
                    ld        a,(hl)                        ;[17bc] 7e
                    ld        (de),a                        ;[17bd] 12
                    dec       de                            ;[17be] 1b
                    ld        (hl),d                        ;[17bf] 72
                    dec       hl                            ;[17c0] 2b
                    ld        (hl),e                        ;[17c1] 73
                    ret                                     ;[17c2] c9

                    ld        a,(de)                        ;[17c3] 1a
                    ld        (hl),a                        ;[17c4] 77
                    inc       hl                            ;[17c5] 23
                    inc       de                            ;[17c6] 13
                    ld        a,(de)                        ;[17c7] 1a
                    ld        (hl),a                        ;[17c8] 77
                    dec       de                            ;[17c9] 1b
                    ret                                     ;[17ca] c9

                    nop                                     ;[17cb] 00
                    nop                                     ;[17cc] 00
                    nop                                     ;[17cd] 00
                    nop                                     ;[17ce] 00
                    nop                                     ;[17cf] 00
                    ld        a,$41                         ;[17d0] 3e 41
                    ld        ($e3ea),a                     ;[17d2] 32 ea e3
                    ld        hl,$17f6                      ;[17d5] 21 f6 17
                    ld        de,$e2db                      ;[17d8] 11 db e2
                    ld        bc,$0015                      ;[17db] 01 15 00
                    ldir                                    ;[17de] ed b0
                    ld        hl,$e2c0                      ;[17e0] 21 c0 e2
                    ld        ($e2a0),hl                    ;[17e3] 22 a0 e2
                    ld        hl,$180b                      ;[17e6] 21 0b 18
                    ld        de,$e348                      ;[17e9] 11 48 e3
                    ld        bc,$0015                      ;[17ec] 01 15 00
                    ldir                                    ;[17ef] ed b0
                    ld        c,$01                         ;[17f1] 0e 01
                    jp        $1943                         ;[17f3] c3 43 19
                    inc       b                             ;[17f6] 04
                    ld        b,c                           ;[17f7] 41
                    nop                                     ;[17f8] 00
                    nop                                     ;[17f9] 00
                    nop                                     ;[17fa] 00
                    nop                                     ;[17fb] 00
                    nop                                     ;[17fc] 00
                    nop                                     ;[17fd] 00
                    nop                                     ;[17fe] 00
                    nop                                     ;[17ff] 00
                    nop                                     ;[1800] 00
                    ret       p                             ;[1801] f0
                    jp        po,$e300                      ;[1802] e2 00 e3
                    adc       b                             ;[1805] 88
                    add       hl,de                         ;[1806] 19
                    ld        a,h                           ;[1807] 7c
                    add       hl,de                         ;[1808] 19
                    add       d                             ;[1809] 82
                    add       hl,de                         ;[180a] 19
                    inc       b                             ;[180b] 04
                    ld        b,d                           ;[180c] 42
                    ld        bc,$0000                      ;[180d] 01 00 00
                    nop                                     ;[1810] 00
                    nop                                     ;[1811] 00
                    nop                                     ;[1812] 00
                    nop                                     ;[1813] 00
                    nop                                     ;[1814] 00
                    nop                                     ;[1815] 00
                    ld        e,l                           ;[1816] 5d
                    ex        (sp),hl                       ;[1817] e3
                    ld        l,l                           ;[1818] 6d
                    ex        (sp),hl                       ;[1819] e3
                    adc       b                             ;[181a] 88
                    add       hl,de                         ;[181b] 19
                    ld        a,h                           ;[181c] 7c
                    add       hl,de                         ;[181d] 19
                    add       d                             ;[181e] 82
                    add       hl,de                         ;[181f] 19
                    push      hl                            ;[1820] e5
                    ld        hl,$1830                      ;[1821] 21 30 18
                    ld        de,$e3b5                      ;[1824] 11 b5 e3
                    ld        bc,$0015                      ;[1827] 01 15 00
                    ldir                                    ;[182a] ed b0
                    pop       hl                            ;[182c] e1
                    jp        $1a5d                         ;[182d] c3 5d 1a
                    ex        af,af'                        ;[1830] 08
                    ld        c,l                           ;[1831] 4d
                    rst       $38                           ;[1832] ff
                    nop                                     ;[1833] 00
                    nop                                     ;[1834] 00
                    nop                                     ;[1835] 00
                    nop                                     ;[1836] 00
                    nop                                     ;[1837] 00
                    nop                                     ;[1838] 00
                    nop                                     ;[1839] 00
                    nop                                     ;[183a] 00
                    nop                                     ;[183b] 00
                    nop                                     ;[183c] 00
                    jp        z,$45e3                       ;[183d] ca e3 45
                    jr        $1887                         ;[1840] 18 45
                    jr        $1889                         ;[1842] 18 45
                    jr        $187d                         ;[1844] 18 37
                    ret                                     ;[1846] c9

                    call      $04ed                         ;[1847] cd ed 04
                    ld        hl,$e2a0                      ;[184a] 21 a0 e2
                    sub       $41                           ;[184d] d6 41
                    jr        c,$186d                       ;[184f] 38 1c
                    cp        $10                           ;[1851] fe 10
                    jr        nc,$186d                      ;[1853] 30 18
                    push      hl                            ;[1855] e5
                    add       a                             ;[1856] 87
                    add       $a0                           ;[1857] c6 a0
                    ld        l,a                           ;[1859] 6f
                    adc       $e2                           ;[185a] ce e2
                    sub       l                             ;[185c] 95
                    ld        h,a                           ;[185d] 67
                    ld        a,(hl)                        ;[185e] 7e
                    inc       hl                            ;[185f] 23
                    ld        h,(hl)                        ;[1860] 66
                    ld        l,a                           ;[1861] 6f
                    push      hl                            ;[1862] e5
                    pop       ix                            ;[1863] dd e1
                    ld        a,h                           ;[1865] 7c
                    or        l                             ;[1866] b5
                    add       $ff                           ;[1867] c6 ff
                    pop       hl                            ;[1869] e1
                    ld        a,$16                         ;[186a] 3e 16
                    ret                                     ;[186c] c9

                    ld        a,$15                         ;[186d] 3e 15
                    or        a                             ;[186f] b7
                    ret                                     ;[1870] c9

                    call      $184d                         ;[1871] cd 4d 18
                    ret       nc                            ;[1874] d0
                    push      hl                            ;[1875] e5
                    push      de                            ;[1876] d5
                    push      bc                            ;[1877] c5
                    call      $188f                         ;[1878] cd 8f 18
                    bit       0,(ix+$1b)                    ;[187b] dd cb 1b 46
                    scf                                     ;[187f] 37
                    call      z,$1887                       ;[1880] cc 87 18
                    pop       bc                            ;[1883] c1
                    pop       de                            ;[1884] d1
                    pop       hl                            ;[1885] e1
                    ret                                     ;[1886] c9

                    ld        a,(ix+$1a)                    ;[1887] dd 7e 1a
                    rla                                     ;[188a] 17
                    ret       c                             ;[188b] d8
                    jp        $19fe                         ;[188c] c3 fe 19
                    bit       0,(ix+$1b)                    ;[188f] dd cb 1b 46
                    ret       z                             ;[1893] c8
                    ld        a,(ix+$21)                    ;[1894] dd 7e 21
                    or        a                             ;[1897] b7
                    ret       nz                            ;[1898] c0
                    call      $18ab                         ;[1899] cd ab 18
                    ret       c                             ;[189c] d8
                    ld        a,(ix+$21)                    ;[189d] dd 7e 21
                    or        a                             ;[18a0] b7
                    ld        a,$24                         ;[18a1] 3e 24
                    ret       nz                            ;[18a3] c0
                    res       0,(ix+$1b)                    ;[18a4] dd cb 1b 86
                    jp        $16a7                         ;[18a8] c3 a7 16
                    bit       7,(ix+$0c)                    ;[18ab] dd cb 0c 7e
                    scf                                     ;[18af] 37
                    ret       nz                            ;[18b0] c0
                    push      hl                            ;[18b1] e5
                    push      de                            ;[18b2] d5
                    push      bc                            ;[18b3] c5
                    ld        a,r                           ;[18b4] ed 5f
                    di                                      ;[18b6] f3
                    ld        a,($5c78)                     ;[18b7] 3a 78 5c
                    ld        hl,($5c79)                    ;[18ba] 2a 79 5c
                    jp        po,$18c1                      ;[18bd] e2 c1 18
                    ei                                      ;[18c0] fb
                    ld        b,a                           ;[18c1] 47
                    ld        a,(ix+$1e)                    ;[18c2] dd 7e 1e
                    ld        e,(ix+$1f)                    ;[18c5] dd 5e 1f
                    ld        d,(ix+$20)                    ;[18c8] dd 56 20
                    add       $64                           ;[18cb] c6 64
                    jr        nc,$18d0                      ;[18cd] 30 01
                    inc       de                            ;[18cf] 13
                    ld        c,a                           ;[18d0] 4f
                    ld        a,b                           ;[18d1] 78
                    sub       c                             ;[18d2] 91
                    sbc       hl,de                         ;[18d3] ed 52
                    push      af                            ;[18d5] f5
                    ld        hl,$5c78                      ;[18d6] 21 78 5c
                    ld        a,r                           ;[18d9] ed 5f
                    di                                      ;[18db] f3
                    ld        a,(hl)                        ;[18dc] 7e
                    ld        (ix+$1e),a                    ;[18dd] dd 77 1e
                    inc       hl                            ;[18e0] 23
                    ld        a,(hl)                        ;[18e1] 7e
                    ld        (ix+$1f),a                    ;[18e2] dd 77 1f
                    inc       hl                            ;[18e5] 23
                    ld        a,(hl)                        ;[18e6] 7e
                    ld        (ix+$20),a                    ;[18e7] dd 77 20
                    jp        po,$18ee                      ;[18ea] e2 ee 18
                    ei                                      ;[18ed] fb
                    pop       af                            ;[18ee] f1
                    pop       bc                            ;[18ef] c1
                    pop       de                            ;[18f0] d1
                    pop       hl                            ;[18f1] e1
                    ret                                     ;[18f2] c9

                    push      hl                            ;[18f3] e5
                    push      de                            ;[18f4] d5
                    push      bc                            ;[18f5] c5
                    call      $18fd                         ;[18f6] cd fd 18
                    pop       bc                            ;[18f9] c1
                    pop       de                            ;[18fa] d1
                    pop       hl                            ;[18fb] e1
                    ret                                     ;[18fc] c9

                    bit       2,(ix+$1b)                    ;[18fd] dd cb 1b 56
                    scf                                     ;[1901] 37
                    ret       z                             ;[1902] c8
                    call      $1918                         ;[1903] cd 18 19
                    call      $1e65                         ;[1906] cd 65 1e
                    ret       nc                            ;[1909] d0
                    call      $1ee9                         ;[190a] cd e9 1e
                    ld        c,a                           ;[190d] 4f
                    and       $20                           ;[190e] e6 20
                    ret       z                             ;[1910] c8
                    bit       6,c                           ;[1911] cb 71
                    ld        a,$01                         ;[1913] 3e 01
                    ret       nz                            ;[1915] c0
                    scf                                     ;[1916] 37
                    ret                                     ;[1917] c9

                    push      hl                            ;[1918] e5
                    ld        c,(ix+$1d)                    ;[1919] dd 4e 1d
                    ld        a,c                           ;[191c] 79
                    or        a                             ;[191d] b7
                    jr        nz,$1935                      ;[191e] 20 15
                    ld        hl,$e3ea                      ;[1920] 21 ea e3
                    ld        a,(ix+$1c)                    ;[1923] dd 7e 1c
                    cp        (hl)                          ;[1926] be
                    jr        z,$1935                       ;[1927] 28 0c
                    ld        (hl),a                        ;[1929] 77
                    push      ix                            ;[192a] dd e5
                    push      de                            ;[192c] d5
                    push      bc                            ;[192d] c5
                    call      $1937                         ;[192e] cd 37 19
                    pop       bc                            ;[1931] c1
                    pop       de                            ;[1932] d1
                    pop       ix                            ;[1933] dd e1
                    pop       hl                            ;[1935] e1
                    ret                                     ;[1936] c9

                    push      af                            ;[1937] f5
                    ld        c,a                           ;[1938] 4f
                    call      $0338                         ;[1939] cd 38 03
                    pop       af                            ;[193c] f1
                    push      hl                            ;[193d] e5
                    ld        hl,($e3eb)                    ;[193e] 2a eb e3
                    ex        (sp),hl                       ;[1941] e3
                    ret                                     ;[1942] c9

                    ld        a,$42                         ;[1943] 3e 42
                    call      $184d                         ;[1945] cd 4d 18
                    ccf                                     ;[1948] 3f
                    call      nc,$189d                      ;[1949] d4 9d 18
                    ret       nc                            ;[194c] d0
                    ld        a,c                           ;[194d] 79
                    or        a                             ;[194e] b7
                    jr        z,$1954                       ;[194f] 28 03
                    ld        hl,$0000                      ;[1951] 21 00 00
                    ld        de,($e3eb)                    ;[1954] ed 5b eb e3
                    ld        ($e3eb),hl                    ;[1958] 22 eb e3
                    ld        hl,$0000                      ;[195b] 21 00 00
                    ld        ($e2a2),hl                    ;[195e] 22 a2 e2
                    ld        ix,$e32d                      ;[1961] dd 21 2d e3
                    ld        (ix+$1d),c                    ;[1965] dd 71 1d
                    call      $1f27                         ;[1968] cd 27 1f
                    jr        nc,$1979                      ;[196b] 30 0c
                    ld        a,c                           ;[196d] 79
                    or        a                             ;[196e] b7
                    scf                                     ;[196f] 37
                    call      nz,$1edd                      ;[1970] c4 dd 1e
                    jr        nc,$1979                      ;[1973] 30 04
                    ld        ($e2a2),ix                    ;[1975] dd 22 a2 e2
                    scf                                     ;[1979] 37
                    ex        de,hl                         ;[197a] eb
                    ret                                     ;[197b] c9

                    call      $1918                         ;[197c] cd 18 19
                    jp        $1bff                         ;[197f] c3 ff 1b
                    call      $1918                         ;[1982] cd 18 19
                    jp        $1c0d                         ;[1985] c3 0d 1c
                    call      $1918                         ;[1988] cd 18 19
                    call      $1c80                         ;[198b] cd 80 1c
                    ret       nc                            ;[198e] d0
                    ld        a,(ix+$0f)                    ;[198f] dd 7e 0f
                    xor       $02                           ;[1992] ee 02
                    ld        a,$06                         ;[1994] 3e 06
                    ret       nz                            ;[1996] c0
                    rr        d                             ;[1997] cb 1a
                    rr        e                             ;[1999] cb 1b
                    ld        hl,$ffd2                      ;[199b] 21 d2 ff
                    add       hl,de                         ;[199e] 19
                    ccf                                     ;[199f] 3f
                    ret       nc                            ;[19a0] d0
                    ld        e,(ix+$0b)                    ;[19a1] dd 5e 0b
                    ld        a,(ix+$0c)                    ;[19a4] dd 7e 0c
                    and       $7f                           ;[19a7] e6 7f
                    ld        d,a                           ;[19a9] 57
                    ld        hl,$ffbf                      ;[19aa] 21 bf ff
                    add       hl,de                         ;[19ad] 19
                    ccf                                     ;[19ae] 3f
                    ret       c                             ;[19af] d8
                    ld        (ix+$0b),$40                  ;[19b0] dd 36 0b 40
                    ld        a,(ix+$0c)                    ;[19b4] dd 7e 0c
                    and       $80                           ;[19b7] e6 80
                    or        $00                           ;[19b9] f6 00
                    ld        (ix+$0c),a                    ;[19bb] dd 77 0c
                    scf                                     ;[19be] 37
                    ret                                     ;[19bf] c9

                    push      hl                            ;[19c0] e5
                    push      bc                            ;[19c1] c5
                    ld        c,(ix+$0d)                    ;[19c2] dd 4e 0d
                    ld        b,(ix+$0e)                    ;[19c5] dd 46 0e
                    ex        de,hl                         ;[19c8] eb
                    ld        e,(ix+$00)                    ;[19c9] dd 5e 00
                    ld        d,(ix+$01)                    ;[19cc] dd 56 01
                    jr        $19d3                         ;[19cf] 18 02
                    add       hl,de                         ;[19d1] 19
                    dec       bc                            ;[19d2] 0b
                    ld        a,b                           ;[19d3] 78
                    or        c                             ;[19d4] b1
                    jr        nz,$19d1                      ;[19d5] 20 fa
                    ex        de,hl                         ;[19d7] eb
                    pop       bc                            ;[19d8] c1
                    pop       hl                            ;[19d9] e1
                    ld        a,$02                         ;[19da] 3e 02
                    jp        $04d9                         ;[19dc] c3 d9 04
                    push      hl                            ;[19df] e5
                    push      bc                            ;[19e0] c5
                    ex        de,hl                         ;[19e1] eb
                    add       hl,hl                         ;[19e2] 29
                    add       hl,hl                         ;[19e3] 29
                    ld        e,(ix+$00)                    ;[19e4] dd 5e 00
                    ld        d,(ix+$01)                    ;[19e7] dd 56 01
                    ld        bc,$ffff                      ;[19ea] 01 ff ff
                    or        a                             ;[19ed] b7
                    inc       bc                            ;[19ee] 03
                    sbc       hl,de                         ;[19ef] ed 52
                    jr        nc,$19ee                      ;[19f1] 30 fb
                    add       hl,de                         ;[19f3] 19
                    ex        de,hl                         ;[19f4] eb
                    ld        a,$02                         ;[19f5] 3e 02
                    call      $04d9                         ;[19f7] cd d9 04
                    ld        d,c                           ;[19fa] 51
                    pop       bc                            ;[19fb] c1
                    pop       hl                            ;[19fc] e1
                    ret                                     ;[19fd] c9

                    push      hl                            ;[19fe] e5
                    ld        l,(ix+$2a)                    ;[19ff] dd 6e 2a
                    ld        h,(ix+$2b)                    ;[1a02] dd 66 2b
                    ld        de,$0000                      ;[1a05] 11 00 00
                    jr        $1a1a                         ;[1a08] 18 10
                    push      hl                            ;[1a0a] e5
                    ld        l,(ix+$2c)                    ;[1a0b] dd 6e 2c
                    ld        h,(ix+$2d)                    ;[1a0e] dd 66 2d
                    jr        $1a1a                         ;[1a11] 18 07
                    push      hl                            ;[1a13] e5
                    ld        l,(ix+$2e)                    ;[1a14] dd 6e 2e
                    ld        h,(ix+$2f)                    ;[1a17] dd 66 2f
                    ld        ($e3ed),hl                    ;[1a1a] 22 ed e3
                    pop       hl                            ;[1a1d] e1
                    push      hl                            ;[1a1e] e5
                    push      de                            ;[1a1f] d5
                    push      bc                            ;[1a20] c5
                    call      $1a2e                         ;[1a21] cd 2e 1a
                    pop       bc                            ;[1a24] c1
                    pop       de                            ;[1a25] d1
                    pop       hl                            ;[1a26] e1
                    ret       c                             ;[1a27] d8
                    call      $1a34                         ;[1a28] cd 34 1a
                    jr        z,$1a1e                       ;[1a2b] 28 f1
                    ret                                     ;[1a2d] c9

                    push      hl                            ;[1a2e] e5
                    ld        hl,($e3ed)                    ;[1a2f] 2a ed e3
                    ex        (sp),hl                       ;[1a32] e3
                    ret                                     ;[1a33] c9

                    push      ix                            ;[1a34] dd e5
                    push      hl                            ;[1a36] e5
                    push      de                            ;[1a37] d5
                    push      bc                            ;[1a38] c5
                    call      $2164                         ;[1a39] cd 64 21
                    ld        c,(ix+$1c)                    ;[1a3c] dd 4e 1c
                    call      $02f7                         ;[1a3f] cd f7 02
                    pop       bc                            ;[1a42] c1
                    pop       de                            ;[1a43] d1
                    pop       hl                            ;[1a44] e1
                    pop       ix                            ;[1a45] dd e1
                    ret                                     ;[1a47] c9

                    ld        a,($e3f4)                     ;[1a48] 3a f4 e3
                    ld        h,a                           ;[1a4b] 67
                    ld        a,($e3f1)                     ;[1a4c] 3a f1 e3
                    sub       h                             ;[1a4f] 94
                    ld        l,a                           ;[1a50] 6f
                    ret                                     ;[1a51] c9

                    ld        a,$4d                         ;[1a52] 3e 4d
                    call      $184d                         ;[1a54] cd 4d 18
                    jr        nc,$1a5d                      ;[1a57] 30 04
                    call      $189d                         ;[1a59] cd 9d 18
                    ret       nc                            ;[1a5c] d0
                    push      hl                            ;[1a5d] e5
                    ld        hl,$1aae                      ;[1a5e] 21 ae 1a
                    ld        de,$e3ef                      ;[1a61] 11 ef e3
                    ld        bc,$0008                      ;[1a64] 01 08 00
                    ldir                                    ;[1a67] ed b0
                    pop       de                            ;[1a69] d1
                    ld        hl,$0000                      ;[1a6a] 21 00 00
                    ld        ($e2b8),hl                    ;[1a6d] 22 b8 e2
                    ld        a,e                           ;[1a70] 7b
                    cp        $04                           ;[1a71] fe 04
                    ret       c                             ;[1a73] d8
                    add       d                             ;[1a74] 82
                    ld        ($e3f1),a                     ;[1a75] 32 f1 e3
                    ld        a,d                           ;[1a78] 7a
                    ld        ($e3f4),a                     ;[1a79] 32 f4 e3
                    ld        a,d                           ;[1a7c] 7a
                    push      de                            ;[1a7d] d5
                    call      $022b                         ;[1a7e] cd 2b 02
                    call      $0207                         ;[1a81] cd 07 02
                    ld        d,h                           ;[1a84] 54
                    ld        e,l                           ;[1a85] 5d
                    inc       de                            ;[1a86] 13
                    ld        (hl),$e5                      ;[1a87] 36 e5
                    ld        bc,$01ff                      ;[1a89] 01 ff 01
                    ldir                                    ;[1a8c] ed b0
                    call      $0207                         ;[1a8e] cd 07 02
                    pop       de                            ;[1a91] d1
                    inc       d                             ;[1a92] 14
                    dec       e                             ;[1a93] 1d
                    jr        nz,$1a7c                      ;[1a94] 20 e6
                    ld        ix,$e39a                      ;[1a96] dd 21 9a e3
                    ld        hl,$e3ef                      ;[1a9a] 21 ef e3
                    call      $1d30                         ;[1a9d] cd 30 1d
                    ld        (ix+$0b),$00                  ;[1aa0] dd 36 0b 00
                    ld        (ix+$0c),$80                  ;[1aa4] dd 36 0c 80
                    ld        ($e2b8),ix                    ;[1aa8] dd 22 b8 e2
                    scf                                     ;[1aac] 37
                    ret                                     ;[1aad] c9

                    nop                                     ;[1aae] 00
                    nop                                     ;[1aaf] 00
                    nop                                     ;[1ab0] 00
                    ld        bc,$0002                      ;[1ab1] 01 02 00
                    inc       bc                            ;[1ab4] 03
                    nop                                     ;[1ab5] 00
                    push      de                            ;[1ab6] d5
                    call      $19df                         ;[1ab7] cd df 19
                    ld        a,e                           ;[1aba] 7b
                    or        a                             ;[1abb] b7
                    jr        nz,$1ac9                      ;[1abc] 20 0b
                    ld        a,d                           ;[1abe] 7a
                    ld        hl,$e3f1                      ;[1abf] 21 f1 e3
                    cp        (hl)                          ;[1ac2] be
                    jr        nc,$1ac9                      ;[1ac3] 30 04
                    call      $022b                         ;[1ac5] cd 2b 02
                    scf                                     ;[1ac8] 37
                    pop       de                            ;[1ac9] d1
                    ret       c                             ;[1aca] d8
                    ld        a,$02                         ;[1acb] 3e 02
                    ret                                     ;[1acd] c9

                    ld        a,$41                         ;[1ace] 3e 41
                    call      $1871                         ;[1ad0] cd 71 18
                    call      c,$189d                       ;[1ad3] dc 9d 18
                    ld        de,$0000                      ;[1ad6] 11 00 00
                    call      c,$15f4                       ;[1ad9] dc f4 15
                    ret       nc                            ;[1adc] d0
                    ld        c,a                           ;[1add] 4f
                    push      hl                            ;[1ade] e5
                    call      $0207                         ;[1adf] cd 07 02
                    push      af                            ;[1ae2] f5
                    xor       a                             ;[1ae3] af
                    ld        b,a                           ;[1ae4] 47
                    ld        e,$02                         ;[1ae5] 1e 02
                    add       (hl)                          ;[1ae7] 86
                    inc       hl                            ;[1ae8] 23
                    djnz      $1ae7                         ;[1ae9] 10 fc
                    dec       e                             ;[1aeb] 1d
                    jr        nz,$1ae7                      ;[1aec] 20 f9
                    ld        e,a                           ;[1aee] 5f
                    pop       af                            ;[1aef] f1
                    call      $0207                         ;[1af0] cd 07 02
                    pop       hl                            ;[1af3] e1
                    ld        a,e                           ;[1af4] 7b
                    xor       $03                           ;[1af5] ee 03
                    ld        a,$23                         ;[1af7] 3e 23
                    ret       nz                            ;[1af9] c0
                    di                                      ;[1afa] f3
                    ld        b,$03                         ;[1afb] 06 03
                    ld        de,$fe00                      ;[1afd] 11 00 fe
                    ld        ix,$0200                      ;[1b00] dd 21 00 02
                    call      $023d                         ;[1b04] cd 3d 02
                    ld        a,$03                         ;[1b07] 3e 03
                    call      $0207                         ;[1b09] cd 07 02
                    ld        hl,$1b22                      ;[1b0c] 21 22 1b
                    ld        de,$fdfb                      ;[1b0f] 11 fb fd
                    ld        bc,$0005                      ;[1b12] 01 05 00
                    ldir                                    ;[1b15] ed b0
                    ld        bc,$1ffd                      ;[1b17] 01 fd 1f
                    ld        a,$07                         ;[1b1a] 3e 07
                    ld        sp,$fe00                      ;[1b1c] 31 00 fe
                    jp        $fdfb                         ;[1b1f] c3 fb fd
                    out       (c),a                         ;[1b22] ed 79
                    jp        $fe10                         ;[1b24] c3 10 fe
                    nop                                     ;[1b27] 00
                    nop                                     ;[1b28] 00
                    nop                                     ;[1b29] 00
                    nop                                     ;[1b2a] 00
                    nop                                     ;[1b2b] 00
                    nop                                     ;[1b2c] 00
                    nop                                     ;[1b2d] 00
                    nop                                     ;[1b2e] 00
                    nop                                     ;[1b2f] 00
                    ld        a,(ix+$19)                    ;[1b30] dd 7e 19
                    and       $40                           ;[1b33] e6 40
                    or        $0d                           ;[1b35] f6 0d
                    call      $1b9c                         ;[1b37] cd 9c 1b
                    ld        l,(ix+$0f)                    ;[1b3a] dd 6e 0f
                    ld        h,(ix+$13)                    ;[1b3d] dd 66 13
                    ld        ($e408),hl                    ;[1b40] 22 08 e4
                    ld        h,e                           ;[1b43] 63
                    ld        l,(ix+$18)                    ;[1b44] dd 6e 18
                    ld        ($e40a),hl                    ;[1b47] 22 0a e4
                    ld        a,$06                         ;[1b4a] 3e 06
                    ld        ($e405),a                     ;[1b4c] 32 05 e4
                    ret                                     ;[1b4f] c9

                    ld        a,(ix+$19)                    ;[1b50] dd 7e 19
                    or        $11                           ;[1b53] f6 11
                    call      $1b69                         ;[1b55] cd 69 1b
                    ld        (hl),$01                      ;[1b58] 36 01
                    ret                                     ;[1b5a] c9

                    ld        a,(ix+$19)                    ;[1b5b] dd 7e 19
                    or        $06                           ;[1b5e] f6 06
                    jr        $1b69                         ;[1b60] 18 07
                    ld        a,(ix+$19)                    ;[1b62] dd 7e 19
                    and       $c0                           ;[1b65] e6 c0
                    or        $05                           ;[1b67] f6 05
                    call      $1b9c                         ;[1b69] cd 9c 1b
                    ld        a,e                           ;[1b6c] 7b
                    add       (ix+$14)                      ;[1b6d] dd 86 14
                    ld        e,a                           ;[1b70] 5f
                    push      de                            ;[1b71] d5
                    ld        hl,($e419)                    ;[1b72] 2a 19 e4
                    ld        a,h                           ;[1b75] 7c
                    or        l                             ;[1b76] b5
                    call      nz,$1ef2                      ;[1b77] c4 f2 1e
                    ld        a,e                           ;[1b7a] 7b
                    ld        ($e40a),a                     ;[1b7b] 32 0a e4
                    ld        l,(ix+$0f)                    ;[1b7e] dd 6e 0f
                    ld        h,e                           ;[1b81] 63
                    ld        ($e40b),hl                    ;[1b82] 22 0b e4
                    ld        a,(ix+$17)                    ;[1b85] dd 7e 17
                    ld        ($e40d),a                     ;[1b88] 32 0d e4
                    ld        h,b                           ;[1b8b] 60
                    ld        l,d                           ;[1b8c] 6a
                    ld        ($e408),hl                    ;[1b8d] 22 08 e4
                    ld        a,$09                         ;[1b90] 3e 09
                    ld        ($e405),a                     ;[1b92] 32 05 e4
                    ld        hl,$e40e                      ;[1b95] 21 0e e4
                    ld        (hl),$ff                      ;[1b98] 36 ff
                    pop       de                            ;[1b9a] d1
                    ret                                     ;[1b9b] c9

                    ld        ($e401),hl                    ;[1b9c] 22 01 e4
                    ld        l,a                           ;[1b9f] 6f
                    ld        a,b                           ;[1ba0] 78
                    ld        ($e400),a                     ;[1ba1] 32 00 e4
                    call      $1bb5                         ;[1ba4] cd b5 1b
                    ld        h,c                           ;[1ba7] 61
                    ld        ($e406),hl                    ;[1ba8] 22 06 e4
                    ld        l,(ix+$15)                    ;[1bab] dd 6e 15
                    ld        h,(ix+$16)                    ;[1bae] dd 66 16
                    ld        ($e403),hl                    ;[1bb1] 22 03 e4
                    ret                                     ;[1bb4] c9

                    ld        a,(ix+$11)                    ;[1bb5] dd 7e 11
                    and       $7f                           ;[1bb8] e6 7f
                    ld        b,$00                         ;[1bba] 06 00
                    ret       z                             ;[1bbc] c8
                    dec       a                             ;[1bbd] 3d
                    jr        nz,$1bc8                      ;[1bbe] 20 08
                    ld        a,d                           ;[1bc0] 7a
                    rra                                     ;[1bc1] 1f
                    ld        d,a                           ;[1bc2] 57
                    ld        a,b                           ;[1bc3] 78
                    rla                                     ;[1bc4] 17
                    ld        b,a                           ;[1bc5] 47
                    jr        $1bd4                         ;[1bc6] 18 0c
                    ld        a,d                           ;[1bc8] 7a
                    sub       (ix+$12)                      ;[1bc9] dd 96 12
                    jr        c,$1bd4                       ;[1bcc] 38 06
                    sub       (ix+$12)                      ;[1bce] dd 96 12
                    cpl                                     ;[1bd1] 2f
                    ld        d,a                           ;[1bd2] 57
                    inc       b                             ;[1bd3] 04
                    ld        a,b                           ;[1bd4] 78
                    add       a                             ;[1bd5] 87
                    add       a                             ;[1bd6] 87
                    or        c                             ;[1bd7] b1
                    ld        c,a                           ;[1bd8] 4f
                    ret                                     ;[1bd9] c9

                    or        a                             ;[1bda] b7
                    jr        nz,$1be0                      ;[1bdb] 20 03
                    ld        hl,$0000                      ;[1bdd] 21 00 00
                    ld        de,($e419)                    ;[1be0] ed 5b 19 e4
                    ld        ($e419),hl                    ;[1be4] 22 19 e4
                    ex        de,hl                         ;[1be7] eb
                    ret                                     ;[1be8] c9

                    push      af                            ;[1be9] f5
                    call      $1b5b                         ;[1bea] cd 5b 1b
                    pop       af                            ;[1bed] f1
                    ld        l,a                           ;[1bee] 6f
                    ld        h,$00                         ;[1bef] 26 00
                    ld        ($e403),hl                    ;[1bf1] 22 03 e4
                    ld        hl,$1bf9                      ;[1bf4] 21 f9 1b
                    jr        $1c4f                         ;[1bf7] 18 56
                    ld        hl,$e400                      ;[1bf9] 21 00 e4
                    jp        $20ba                         ;[1bfc] c3 ba 20
                    call      $1b5b                         ;[1bff] cd 5b 1b
                    ld        hl,$1c07                      ;[1c02] 21 07 1c
                    jr        $1c4f                         ;[1c05] 18 48
                    ld        hl,$e400                      ;[1c07] 21 00 e4
                    jp        $20c3                         ;[1c0a] c3 c3 20
                    call      $1e65                         ;[1c0d] cd 65 1e
                    ret       nc                            ;[1c10] d0
                    call      $1b62                         ;[1c11] cd 62 1b
                    jr        $1c2b                         ;[1c14] 18 15
                    call      $1b50                         ;[1c16] cd 50 1b
                    call      $1c2b                         ;[1c19] cd 2b 1c
                    ret       nc                            ;[1c1c] d0
                    ld        a,($e433)                     ;[1c1d] 3a 33 e4
                    cp        $08                           ;[1c20] fe 08
                    scf                                     ;[1c22] 37
                    ret                                     ;[1c23] c9

                    call      $1e65                         ;[1c24] cd 65 1e
                    ret       nc                            ;[1c27] d0
                    call      $1b30                         ;[1c28] cd 30 1b
                    ld        hl,$1c30                      ;[1c2b] 21 30 1c
                    jr        $1c4f                         ;[1c2e] 18 1f
                    ld        hl,$e400                      ;[1c30] 21 00 e4
                    jp        $20cc                         ;[1c33] c3 cc 20
                    call      $1c41                         ;[1c36] cd 41 1c
                    ld        hl,$e430                      ;[1c39] 21 30 e4
                    ret       nc                            ;[1c3c] d0
                    ld        a,($e436)                     ;[1c3d] 3a 36 e4
                    ret                                     ;[1c40] c9

                    call      $1bb5                         ;[1c41] cd b5 1b
                    ld        hl,$1c49                      ;[1c44] 21 49 1c
                    jr        $1c4f                         ;[1c47] 18 06
                    ld        a,(ix+$19)                    ;[1c49] dd 7e 19
                    jp        $2103                         ;[1c4c] c3 03 21
                    call      $212b                         ;[1c4f] cd 2b 21
                    call      $1e80                         ;[1c52] cd 80 1e
                    jp        $2150                         ;[1c55] c3 50 21
                    nop                                     ;[1c58] 00
                    nop                                     ;[1c59] 00
                    jr        z,$1c65                       ;[1c5a] 28 09
                    ld        (bc),a                        ;[1c5c] 02
                    ld        bc,$0203                      ;[1c5d] 01 03 02
                    ld        hl,($0152)                    ;[1c60] 2a 52 01
                    nop                                     ;[1c63] 00
                    jr        z,$1c6f                       ;[1c64] 28 09
                    ld        (bc),a                        ;[1c66] 02
                    ld        (bc),a                        ;[1c67] 02
                    inc       bc                            ;[1c68] 03
                    ld        (bc),a                        ;[1c69] 02
                    ld        hl,($0252)                    ;[1c6a] 2a 52 02
                    nop                                     ;[1c6d] 00
                    jr        z,$1c79                       ;[1c6e] 28 09
                    ld        (bc),a                        ;[1c70] 02
                    nop                                     ;[1c71] 00
                    inc       bc                            ;[1c72] 03
                    ld        (bc),a                        ;[1c73] 02
                    ld        hl,($0352)                    ;[1c74] 2a 52 03
                    add       c                             ;[1c77] 81
                    ld        d,b                           ;[1c78] 50
                    add       hl,bc                         ;[1c79] 09
                    ld        (bc),a                        ;[1c7a] 02
                    ld        bc,$0404                      ;[1c7b] 01 04 04
                    ld        hl,($af52)                    ;[1c7e] 2a 52 af
                    call      $1cdb                         ;[1c81] cd db 1c
                    ld        d,$00                         ;[1c84] 16 00
                    push      bc                            ;[1c86] c5
                    call      c,$1c36                       ;[1c87] dc 36 1c
                    pop       bc                            ;[1c8a] c1
                    ret       nc                            ;[1c8b] d0
                    and       $c0                           ;[1c8c] e6 c0
                    ld        e,$01                         ;[1c8e] 1e 01
                    cp        $40                           ;[1c90] fe 40
                    jr        z,$1c99                       ;[1c92] 28 05
                    inc       e                             ;[1c94] 1c
                    cp        $c0                           ;[1c95] fe c0
                    jr        nz,$1c9f                      ;[1c97] 20 06
                    ld        a,e                           ;[1c99] 7b
                    call      $1cdb                         ;[1c9a] cd db 1c
                    jr        $1cd3                         ;[1c9d] 18 34
                    push      bc                            ;[1c9f] c5
                    ld        hl,$e40f                      ;[1ca0] 21 0f e4
                    ld        de,$0000                      ;[1ca3] 11 00 00
                    ld        b,$07                         ;[1ca6] 06 07
                    ld        a,$0a                         ;[1ca8] 3e 0a
                    push      hl                            ;[1caa] e5
                    call      $1be9                         ;[1cab] cd e9 1b
                    pop       hl                            ;[1cae] e1
                    pop       bc                            ;[1caf] c1
                    jr        c,$1cba                       ;[1cb0] 38 08
                    cp        $08                           ;[1cb2] fe 08
                    scf                                     ;[1cb4] 37
                    ccf                                     ;[1cb5] 3f
                    ret       nz                            ;[1cb6] c0
                    ld        a,$06                         ;[1cb7] 3e 06
                    ret                                     ;[1cb9] c9

                    push      bc                            ;[1cba] c5
                    ld        d,h                           ;[1cbb] 54
                    ld        e,l                           ;[1cbc] 5d
                    ld        c,(hl)                        ;[1cbd] 4e
                    ld        b,$0a                         ;[1cbe] 06 0a
                    ld        a,(de)                        ;[1cc0] 1a
                    inc       de                            ;[1cc1] 13
                    cp        c                             ;[1cc2] b9
                    jr        nz,$1cca                      ;[1cc3] 20 05
                    djnz      $1cc0                         ;[1cc5] 10 f9
                    ld        hl,$1c58                      ;[1cc7] 21 58 1c
                    pop       bc                            ;[1cca] c1
                    ld        a,(hl)                        ;[1ccb] 7e
                    cp        $04                           ;[1ccc] fe 04
                    ld        a,$06                         ;[1cce] 3e 06
                    call      c,$1cee                       ;[1cd0] dc ee 1c
                    push      hl                            ;[1cd3] e5
                    push      bc                            ;[1cd4] c5
                    call      c,$1dee                       ;[1cd5] dc ee 1d
                    pop       bc                            ;[1cd8] c1
                    pop       hl                            ;[1cd9] e1
                    ret                                     ;[1cda] c9

                    ld        e,a                           ;[1cdb] 5f
                    cp        $04                           ;[1cdc] fe 04
                    ld        a,$06                         ;[1cde] 3e 06
                    ret       nc                            ;[1ce0] d0
                    ld        a,e                           ;[1ce1] 7b
                    add       a                             ;[1ce2] 87
                    ld        e,a                           ;[1ce3] 5f
                    add       a                             ;[1ce4] 87
                    add       a                             ;[1ce5] 87
                    add       e                             ;[1ce6] 83
                    adc       $58                           ;[1ce7] ce 58
                    ld        l,a                           ;[1ce9] 6f
                    adc       $1c                           ;[1cea] ce 1c
                    sub       l                             ;[1cec] 95
                    ld        h,a                           ;[1ced] 67
                    push      hl                            ;[1cee] e5
                    push      bc                            ;[1cef] c5
                    ld        a,(hl)                        ;[1cf0] 7e
                    ld        b,$41                         ;[1cf1] 06 41
                    dec       a                             ;[1cf3] 3d
                    jr        z,$1cfd                       ;[1cf4] 28 07
                    ld        b,$c1                         ;[1cf6] 06 c1
                    dec       a                             ;[1cf8] 3d
                    jr        z,$1cfd                       ;[1cf9] 28 02
                    ld        b,$01                         ;[1cfb] 06 01
                    ld        (ix+$14),b                    ;[1cfd] dd 70 14
                    inc       hl                            ;[1d00] 23
                    ld        a,(hl)                        ;[1d01] 7e
                    ld        (ix+$11),a                    ;[1d02] dd 77 11
                    inc       hl                            ;[1d05] 23
                    ld        a,(hl)                        ;[1d06] 7e
                    ld        (ix+$12),a                    ;[1d07] dd 77 12
                    inc       hl                            ;[1d0a] 23
                    ld        a,(hl)                        ;[1d0b] 7e
                    ld        (ix+$13),a                    ;[1d0c] dd 77 13
                    inc       hl                            ;[1d0f] 23
                    ld        b,(hl)                        ;[1d10] 46
                    inc       hl                            ;[1d11] 23
                    inc       hl                            ;[1d12] 23
                    inc       hl                            ;[1d13] 23
                    inc       hl                            ;[1d14] 23
                    ld        a,(hl)                        ;[1d15] 7e
                    ld        (ix+$17),a                    ;[1d16] dd 77 17
                    inc       hl                            ;[1d19] 23
                    ld        a,(hl)                        ;[1d1a] 7e
                    ld        (ix+$18),a                    ;[1d1b] dd 77 18
                    ld        hl,$0080                      ;[1d1e] 21 80 00
                    call      $1efd                         ;[1d21] cd fd 1e
                    ld        (ix+$15),l                    ;[1d24] dd 75 15
                    ld        (ix+$16),h                    ;[1d27] dd 74 16
                    ld        (ix+$19),$60                  ;[1d2a] dd 36 19 60
                    pop       bc                            ;[1d2e] c1
                    pop       hl                            ;[1d2f] e1
                    push      bc                            ;[1d30] c5
                    push      hl                            ;[1d31] e5
                    ex        de,hl                         ;[1d32] eb
                    ld        hl,$0004                      ;[1d33] 21 04 00
                    add       hl,de                         ;[1d36] 19
                    ld        a,(hl)                        ;[1d37] 7e
                    ld        (ix+$0f),a                    ;[1d38] dd 77 0f
                    push      af                            ;[1d3b] f5
                    call      $1ef3                         ;[1d3c] cd f3 1e
                    ld        (ix+$10),a                    ;[1d3f] dd 77 10
                    dec       hl                            ;[1d42] 2b
                    ld        l,(hl)                        ;[1d43] 6e
                    ld        h,$00                         ;[1d44] 26 00
                    pop       bc                            ;[1d46] c1
                    call      $1efd                         ;[1d47] cd fd 1e
                    ld        (ix+$00),l                    ;[1d4a] dd 75 00
                    ld        (ix+$01),h                    ;[1d4d] dd 74 01
                    ld        hl,$0006                      ;[1d50] 21 06 00
                    add       hl,de                         ;[1d53] 19
                    ld        a,(hl)                        ;[1d54] 7e
                    ld        (ix+$02),a                    ;[1d55] dd 77 02
                    ld        c,a                           ;[1d58] 4f
                    push      hl                            ;[1d59] e5
                    call      $1ef3                         ;[1d5a] cd f3 1e
                    ld        (ix+$03),a                    ;[1d5d] dd 77 03
                    dec       hl                            ;[1d60] 2b
                    ld        e,(hl)                        ;[1d61] 5e
                    ld        (ix+$0d),e                    ;[1d62] dd 73 0d
                    ld        (ix+$0e),$00                  ;[1d65] dd 36 0e 00
                    dec       hl                            ;[1d69] 2b
                    dec       hl                            ;[1d6a] 2b
                    ld        b,(hl)                        ;[1d6b] 46
                    dec       hl                            ;[1d6c] 2b
                    ld        d,(hl)                        ;[1d6d] 56
                    dec       hl                            ;[1d6e] 2b
                    ld        a,(hl)                        ;[1d6f] 7e
                    ld        l,d                           ;[1d70] 6a
                    ld        h,$00                         ;[1d71] 26 00
                    ld        d,h                           ;[1d73] 54
                    and       $7f                           ;[1d74] e6 7f
                    jr        z,$1d79                       ;[1d76] 28 01
                    add       hl,hl                         ;[1d78] 29
                    sbc       hl,de                         ;[1d79] ed 52
                    ex        de,hl                         ;[1d7b] eb
                    ld        hl,$0000                      ;[1d7c] 21 00 00
                    add       hl,de                         ;[1d7f] 19
                    djnz      $1d7f                         ;[1d80] 10 fd
                    ld        a,c                           ;[1d82] 79
                    sub       (ix+$0f)                      ;[1d83] dd 96 0f
                    ld        b,a                           ;[1d86] 47
                    call      $1f04                         ;[1d87] cd 04 1f
                    dec       hl                            ;[1d8a] 2b
                    ld        (ix+$05),l                    ;[1d8b] dd 75 05
                    ld        (ix+$06),h                    ;[1d8e] dd 74 06
                    ld        b,$03                         ;[1d91] 06 03
                    ld        a,h                           ;[1d93] 7c
                    or        a                             ;[1d94] b7
                    jr        z,$1d98                       ;[1d95] 28 01
                    inc       b                             ;[1d97] 04
                    ld        a,c                           ;[1d98] 79
                    sub       b                             ;[1d99] 90
                    call      $1ef3                         ;[1d9a] cd f3 1e
                    ld        (ix+$04),a                    ;[1d9d] dd 77 04
                    pop       de                            ;[1da0] d1
                    push      hl                            ;[1da1] e5
                    ld        b,$02                         ;[1da2] 06 02
                    call      $1f04                         ;[1da4] cd 04 1f
                    inc       hl                            ;[1da7] 23
                    inc       hl                            ;[1da8] 23
                    ex        (sp),hl                       ;[1da9] e3
                    inc       de                            ;[1daa] 13
                    ld        a,(de)                        ;[1dab] 1a
                    or        a                             ;[1dac] b7
                    jr        nz,$1db7                      ;[1dad] 20 08
                    add       hl,hl                         ;[1daf] 29
                    ld        a,h                           ;[1db0] 7c
                    inc       a                             ;[1db1] 3c
                    cp        $02                           ;[1db2] fe 02
                    jr        nc,$1db7                      ;[1db4] 30 01
                    inc       a                             ;[1db6] 3c
                    ld        b,a                           ;[1db7] 47
                    ld        hl,$0000                      ;[1db8] 21 00 00
                    scf                                     ;[1dbb] 37
                    rr        h                             ;[1dbc] cb 1c
                    rr        l                             ;[1dbe] cb 1d
                    djnz      $1dbb                         ;[1dc0] 10 f9
                    ld        (ix+$09),h                    ;[1dc2] dd 74 09
                    ld        (ix+$0a),l                    ;[1dc5] dd 75 0a
                    ld        h,$00                         ;[1dc8] 26 00
                    ld        l,a                           ;[1dca] 6f
                    ld        b,c                           ;[1dcb] 41
                    inc       b                             ;[1dcc] 04
                    inc       b                             ;[1dcd] 04
                    call      $1efd                         ;[1dce] cd fd 1e
                    push      hl                            ;[1dd1] e5
                    dec       hl                            ;[1dd2] 2b
                    ld        (ix+$07),l                    ;[1dd3] dd 75 07
                    ld        (ix+$08),h                    ;[1dd6] dd 74 08
                    ld        b,$02                         ;[1dd9] 06 02
                    call      $1f04                         ;[1ddb] cd 04 1f
                    inc       hl                            ;[1dde] 23
                    ld        (ix+$0b),l                    ;[1ddf] dd 75 0b
                    ld        (ix+$0c),h                    ;[1de2] dd 74 0c
                    pop       hl                            ;[1de5] e1
                    add       hl,hl                         ;[1de6] 29
                    add       hl,hl                         ;[1de7] 29
                    pop       de                            ;[1de8] d1
                    pop       bc                            ;[1de9] c1
                    ld        a,(bc)                        ;[1dea] 0a
                    scf                                     ;[1deb] 37
                    pop       bc                            ;[1dec] c1
                    ret                                     ;[1ded] c9

                    ld        b,a                           ;[1dee] 47
                    push      de                            ;[1def] d5
                    push      bc                            ;[1df0] c5
                    call      $1e10                         ;[1df1] cd 10 1e
                    pop       bc                            ;[1df4] c1
                    pop       de                            ;[1df5] d1
                    ret       nc                            ;[1df6] d0
                    ld        a,(ix+$11)                    ;[1df7] dd 7e 11
                    and       $03                           ;[1dfa] e6 03
                    jr        z,$1e02                       ;[1dfc] 28 04
                    bit       1,(hl)                        ;[1dfe] cb 4e
                    jr        z,$1e0c                       ;[1e00] 28 0a
                    ld        a,b                           ;[1e02] 78
                    scf                                     ;[1e03] 37
                    bit       7,(ix+$11)                    ;[1e04] dd cb 11 7e
                    ret       z                             ;[1e08] c8
                    bit       3,(hl)                        ;[1e09] cb 5e
                    ret       nz                            ;[1e0b] c0
                    ld        a,$09                         ;[1e0c] 3e 09
                    or        a                             ;[1e0e] b7
                    ret                                     ;[1e0f] c9

                    call      $1f6a                         ;[1e10] cd 6a 1f
                    ld        a,(hl)                        ;[1e13] 7e
                    and       $0c                           ;[1e14] e6 0c
                    jr        z,$1e24                       ;[1e16] 28 0c
                    ld        a,(hl)                        ;[1e18] 7e
                    and       $03                           ;[1e19] e6 03
                    scf                                     ;[1e1b] 37
                    ret       nz                            ;[1e1c] c0
                    ld        a,(ix+$11)                    ;[1e1d] dd 7e 11
                    and       $03                           ;[1e20] e6 03
                    scf                                     ;[1e22] 37
                    ret       z                             ;[1e23] c8
                    ld        a,(ix+$11)                    ;[1e24] dd 7e 11
                    and       $03                           ;[1e27] e6 03
                    ld        d,$02                         ;[1e29] 16 02
                    jr        z,$1e39                       ;[1e2b] 28 0c
                    dec       a                             ;[1e2d] 3d
                    ld        d,$05                         ;[1e2e] 16 05
                    jr        z,$1e39                       ;[1e30] 28 07
                    ld        a,(ix+$12)                    ;[1e32] dd 7e 12
                    add       a                             ;[1e35] 87
                    sub       $03                           ;[1e36] d6 03
                    ld        d,a                           ;[1e38] 57
                    push      hl                            ;[1e39] e5
                    call      $1c36                         ;[1e3a] cd 36 1c
                    pop       hl                            ;[1e3d] e1
                    ret       nc                            ;[1e3e] d0
                    ld        de,($e434)                    ;[1e3f] ed 5b 34 e4
                    ld        a,(ix+$11)                    ;[1e43] dd 7e 11
                    and       $03                           ;[1e46] e6 03
                    jr        z,$1e53                       ;[1e48] 28 09
                    dec       d                             ;[1e4a] 15
                    jr        z,$1e51                       ;[1e4b] 28 04
                    set       0,(hl)                        ;[1e4d] cb c6
                    jr        $1e53                         ;[1e4f] 18 02
                    set       1,(hl)                        ;[1e51] cb ce
                    ld        a,(ix+$11)                    ;[1e53] dd 7e 11
                    dec       e                             ;[1e56] 1d
                    dec       e                             ;[1e57] 1d
                    jr        z,$1e5b                       ;[1e58] 28 01
                    cpl                                     ;[1e5a] 2f
                    rla                                     ;[1e5b] 17
                    jr        nc,$1e61                      ;[1e5c] 30 03
                    set       3,(hl)                        ;[1e5e] cb de
                    ret                                     ;[1e60] c9

                    set       2,(hl)                        ;[1e61] cb d6
                    scf                                     ;[1e63] 37
                    ret                                     ;[1e64] c9

                    push      hl                            ;[1e65] e5
                    call      $1f6a                         ;[1e66] cd 6a 1f
                    bit       3,(hl)                        ;[1e69] cb 5e
                    pop       hl                            ;[1e6b] e1
                    scf                                     ;[1e6c] 37
                    ret       z                             ;[1e6d] c8
                    ld        a,(ix+$11)                    ;[1e6e] dd 7e 11
                    rla                                     ;[1e71] 17
                    ld        a,$09                         ;[1e72] 3e 09
                    ret                                     ;[1e74] c9

                    call      $1f6a                         ;[1e75] cd 6a 1f
                    ld        a,(hl)                        ;[1e78] 7e
                    and       $0f                           ;[1e79] e6 0f
                    ret                                     ;[1e7b] c9

                    ld        ($e42d),a                     ;[1e7c] 32 2d e4
                    ret                                     ;[1e7f] c9

                    ld        a,($e42d)                     ;[1e80] 3a 2d e4
                    ld        b,a                           ;[1e83] 47
                    push      bc                            ;[1e84] c5
                    call      $1eb0                         ;[1e85] cd b0 1e
                    pop       bc                            ;[1e88] c1
                    ret       z                             ;[1e89] c8
                    cp        $04                           ;[1e8a] fe 04
                    jr        nz,$1eac                      ;[1e8c] 20 1e
                    push      hl                            ;[1e8e] e5
                    push      de                            ;[1e8f] d5
                    push      bc                            ;[1e90] c5
                    ld        a,(ix+$19)                    ;[1e91] dd 7e 19
                    call      $2103                         ;[1e94] cd 03 21
                    call      $204a                         ;[1e97] cd 4a 20
                    pop       bc                            ;[1e9a] c1
                    pop       de                            ;[1e9b] d1
                    pop       hl                            ;[1e9c] e1
                    jr        nz,$1eac                      ;[1e9d] 20 0d
                    ret       nc                            ;[1e9f] d0
                    ld        a,($e436)                     ;[1ea0] 3a 36 e4
                    xor       (ix+$14)                      ;[1ea3] dd ae 14
                    and       $c0                           ;[1ea6] e6 c0
                    ld        a,$08                         ;[1ea8] 3e 08
                    ret       nz                            ;[1eaa] c0
                    rra                                     ;[1eab] 1f
                    djnz      $1e84                         ;[1eac] 10 d6
                    or        a                             ;[1eae] b7
                    ret                                     ;[1eaf] c9

                    ld        a,b                           ;[1eb0] 78
                    and       $07                           ;[1eb1] e6 07
                    jr        z,$1ec2                       ;[1eb3] 28 0d
                    and       $03                           ;[1eb5] e6 03
                    jr        nz,$1ecc                      ;[1eb7] 20 13
                    push      hl                            ;[1eb9] e5
                    call      $1f6a                         ;[1eba] cd 6a 1f
                    res       6,(hl)                        ;[1ebd] cb b6
                    pop       hl                            ;[1ebf] e1
                    jr        $1ecc                         ;[1ec0] 18 0a
                    push      de                            ;[1ec2] d5
                    ld        d,(ix+$12)                    ;[1ec3] dd 56 12
                    dec       d                             ;[1ec6] 15
                    call      $1f76                         ;[1ec7] cd 76 1f
                    pop       de                            ;[1eca] d1
                    ret       nc                            ;[1ecb] d0
                    call      $1f76                         ;[1ecc] cd 76 1f
                    ret       nc                            ;[1ecf] d0
                    push      hl                            ;[1ed0] e5
                    push      de                            ;[1ed1] d5
                    push      bc                            ;[1ed2] c5
                    call      $1ef2                         ;[1ed3] cd f2 1e
                    pop       bc                            ;[1ed6] c1
                    pop       de                            ;[1ed7] d1
                    call      $204a                         ;[1ed8] cd 4a 20
                    pop       hl                            ;[1edb] e1
                    ret                                     ;[1edc] c9

                    push      bc                            ;[1edd] c5
                    ld        c,$01                         ;[1ede] 0e 01
                    call      $1ee9                         ;[1ee0] cd e9 1e
                    pop       bc                            ;[1ee3] c1
                    and       $60                           ;[1ee4] e6 60
                    ret       z                             ;[1ee6] c8
                    scf                                     ;[1ee7] 37
                    ret                                     ;[1ee8] c9

                    call      $212b                         ;[1ee9] cd 2b 21
                    call      $2087                         ;[1eec] cd 87 20
                    jp        $2150                         ;[1eef] c3 50 21
                    jp        (hl)                          ;[1ef2] e9
                    or        a                             ;[1ef3] b7
                    ret       z                             ;[1ef4] c8
                    ld        b,a                           ;[1ef5] 47
                    ld        a,$01                         ;[1ef6] 3e 01
                    add       a                             ;[1ef8] 87
                    djnz      $1ef8                         ;[1ef9] 10 fd
                    dec       a                             ;[1efb] 3d
                    ret                                     ;[1efc] c9

                    ld        a,b                           ;[1efd] 78
                    or        a                             ;[1efe] b7
                    ret       z                             ;[1eff] c8
                    add       hl,hl                         ;[1f00] 29
                    djnz      $1f00                         ;[1f01] 10 fd
                    ret                                     ;[1f03] c9

                    ld        a,b                           ;[1f04] 78
                    or        a                             ;[1f05] b7
                    ret       z                             ;[1f06] c8
                    srl       h                             ;[1f07] cb 3c
                    rr        l                             ;[1f09] cb 1d
                    djnz      $1f07                         ;[1f0b] 10 fa
                    ret                                     ;[1f0d] c9

                    nop                                     ;[1f0e] 00
                    nop                                     ;[1f0f] 00
                    nop                                     ;[1f10] 00
                    nop                                     ;[1f11] 00
                    nop                                     ;[1f12] 00
                    nop                                     ;[1f13] 00
                    nop                                     ;[1f14] 00
                    nop                                     ;[1f15] 00
                    nop                                     ;[1f16] 00
                    nop                                     ;[1f17] 00
                    nop                                     ;[1f18] 00
                    nop                                     ;[1f19] 00
                    nop                                     ;[1f1a] 00
                    nop                                     ;[1f1b] 00
                    nop                                     ;[1f1c] 00
                    nop                                     ;[1f1d] 00
                    nop                                     ;[1f1e] 00
                    nop                                     ;[1f1f] 00
                    ld        a,(bc)                        ;[1f20] 0a
                    ld        ($1eaf),a                     ;[1f21] 32 af 1e
                    inc       c                             ;[1f24] 0c
                    rrca                                    ;[1f25] 0f
                    inc       bc                            ;[1f26] 03
                    push      bc                            ;[1f27] c5
                    ld        bc,$2ffd                      ;[1f28] 01 fd 2f
                    in        a,(c)                         ;[1f2b] ed 78
                    add       $01                           ;[1f2d] c6 01
                    ccf                                     ;[1f2f] 3f
                    pop       bc                            ;[1f30] c1
                    ret                                     ;[1f31] c9

                    ld        hl,$e420                      ;[1f32] 21 20 e4
                    ld        b,$10                         ;[1f35] 06 10
                    ld        (hl),$00                      ;[1f37] 36 00
                    inc       hl                            ;[1f39] 23
                    djnz      $1f37                         ;[1f3a] 10 fb
                    ld        a,$0f                         ;[1f3c] 3e 0f
                    ld        ($e42d),a                     ;[1f3e] 32 2d e4
                    call      $2164                         ;[1f41] cd 64 21
                    ld        hl,$1f20                      ;[1f44] 21 20 1f
                    ld        de,$e428                      ;[1f47] 11 28 e4
                    ld        bc,$0005                      ;[1f4a] 01 05 00
                    ldir                                    ;[1f4d] ed b0
                    ld        a,($e42c)                     ;[1f4f] 3a 2c e4
                    dec       a                             ;[1f52] 3d
                    rlca                                    ;[1f53] 07
                    rlca                                    ;[1f54] 07
                    rlca                                    ;[1f55] 07
                    cpl                                     ;[1f56] 2f
                    and       $f0                           ;[1f57] e6 f0
                    or        (hl)                          ;[1f59] b6
                    inc       hl                            ;[1f5a] 23
                    ld        h,(hl)                        ;[1f5b] 66
                    ld        l,a                           ;[1f5c] 6f
                    ld        a,$03                         ;[1f5d] 3e 03
                    call      $2114                         ;[1f5f] cd 14 21
                    ld        a,l                           ;[1f62] 7d
                    call      $2114                         ;[1f63] cd 14 21
                    ld        a,h                           ;[1f66] 7c
                    jp        $2114                         ;[1f67] c3 14 21
                    ld        a,c                           ;[1f6a] 79
                    and       $03                           ;[1f6b] e6 03
                    add       a                             ;[1f6d] 87
                    add       $20                           ;[1f6e] c6 20
                    ld        l,a                           ;[1f70] 6f
                    adc       $e4                           ;[1f71] ce e4
                    sub       l                             ;[1f73] 95
                    ld        h,a                           ;[1f74] 67
                    ret                                     ;[1f75] c9

                    push      hl                            ;[1f76] e5
                    call      $1f6a                         ;[1f77] cd 6a 1f
                    call      $1f7f                         ;[1f7a] cd 7f 1f
                    pop       hl                            ;[1f7d] e1
                    ret                                     ;[1f7e] c9

                    ld        a,($e42d)                     ;[1f7f] 3a 2d e4
                    ld        b,a                           ;[1f82] 47
                    bit       6,(hl)                        ;[1f83] cb 76
                    jr        nz,$1f92                      ;[1f85] 20 0b
                    inc       hl                            ;[1f87] 23
                    ld        (hl),$00                      ;[1f88] 36 00
                    dec       hl                            ;[1f8a] 2b
                    call      $1fb7                         ;[1f8b] cd b7 1f
                    jr        nc,$1fa8                      ;[1f8e] 30 18
                    set       6,(hl)                        ;[1f90] cb f6
                    ld        a,d                           ;[1f92] 7a
                    inc       hl                            ;[1f93] 23
                    cp        (hl)                          ;[1f94] be
                    dec       hl                            ;[1f95] 2b
                    scf                                     ;[1f96] 37
                    ret       z                             ;[1f97] c8
                    or        a                             ;[1f98] b7
                    jr        nz,$1fa0                      ;[1f99] 20 05
                    call      $1fb7                         ;[1f9b] cd b7 1f
                    jr        $1fa3                         ;[1f9e] 18 03
                    call      $1fdb                         ;[1fa0] cd db 1f
                    jr        nc,$1fad                      ;[1fa3] 30 08
                    inc       hl                            ;[1fa5] 23
                    ld        (hl),d                        ;[1fa6] 72
                    ret                                     ;[1fa7] c9

                    push      de                            ;[1fa8] d5
                    call      nz,$1fd7                      ;[1fa9] c4 d7 1f
                    pop       de                            ;[1fac] d1
                    res       6,(hl)                        ;[1fad] cb b6
                    ret       z                             ;[1faf] c8
                    call      $206f                         ;[1fb0] cd 6f 20
                    djnz      $1f83                         ;[1fb3] 10 ce
                    cp        a                             ;[1fb5] bf
                    ret                                     ;[1fb6] c9

                    call      $1fbb                         ;[1fb7] cd bb 1f
                    ret       z                             ;[1fba] c8
                    push      bc                            ;[1fbb] c5
                    ld        b,(ix+$12)                    ;[1fbc] dd 46 12
                    dec       b                             ;[1fbf] 05
                    bit       7,(ix+$11)                    ;[1fc0] dd cb 11 7e
                    jr        nz,$1fcd                      ;[1fc4] 20 07
                    bit       3,(hl)                        ;[1fc6] cb 5e
                    jr        z,$1fcd                       ;[1fc8] 28 03
                    ld        a,b                           ;[1fca] 78
                    add       a                             ;[1fcb] 87
                    ld        b,a                           ;[1fcc] 47
                    ld        a,$07                         ;[1fcd] 3e 07
                    call      $2114                         ;[1fcf] cd 14 21
                    ld        a,c                           ;[1fd2] 79
                    and       $03                           ;[1fd3] e6 03
                    jr        $1ffe                         ;[1fd5] 18 27
                    ld        d,(ix+$12)                    ;[1fd7] dd 56 12
                    dec       d                             ;[1fda] 15
                    push      bc                            ;[1fdb] c5
                    ld        a,d                           ;[1fdc] 7a
                    inc       hl                            ;[1fdd] 23
                    sub       (hl)                          ;[1fde] 96
                    dec       hl                            ;[1fdf] 2b
                    jr        nc,$1fe4                      ;[1fe0] 30 02
                    cpl                                     ;[1fe2] 2f
                    inc       a                             ;[1fe3] 3c
                    ld        b,a                           ;[1fe4] 47
                    ld        a,$0f                         ;[1fe5] 3e 0f
                    call      $2114                         ;[1fe7] cd 14 21
                    ld        a,c                           ;[1fea] 79
                    call      $2114                         ;[1feb] cd 14 21
                    ld        a,d                           ;[1fee] 7a
                    bit       7,(ix+$11)                    ;[1fef] dd cb 11 7e
                    jr        nz,$1ffe                      ;[1ff3] 20 09
                    bit       3,(hl)                        ;[1ff5] cb 5e
                    jr        z,$1ffe                       ;[1ff7] 28 05
                    ld        a,b                           ;[1ff9] 78
                    add       a                             ;[1ffa] 87
                    ld        b,a                           ;[1ffb] 47
                    ld        a,d                           ;[1ffc] 7a
                    add       a                             ;[1ffd] 87
                    push      hl                            ;[1ffe] e5
                    call      $2114                         ;[1fff] cd 14 21
                    ld        a,($e42c)                     ;[2002] 3a 2c e4
                    call      $201c                         ;[2005] cd 1c 20
                    djnz      $2002                         ;[2008] 10 f8
                    ld        a,($e42b)                     ;[200a] 3a 2b e4
                    call      $201c                         ;[200d] cd 1c 20
                    ld        hl,$e430                      ;[2010] 21 30 e4
                    call      $2080                         ;[2013] cd 80 20
                    call      $2025                         ;[2016] cd 25 20
                    pop       hl                            ;[2019] e1
                    pop       bc                            ;[201a] c1
                    ret                                     ;[201b] c9

                    ld        l,$dc                         ;[201c] 2e dc
                    dec       l                             ;[201e] 2d
                    jr        nz,$201e                      ;[201f] 20 fd
                    dec       a                             ;[2021] 3d
                    jr        nz,$201c                      ;[2022] 20 f8
                    ret                                     ;[2024] c9

                    ld        a,c                           ;[2025] 79
                    or        $20                           ;[2026] f6 20
                    inc       hl                            ;[2028] 23
                    xor       (hl)                          ;[2029] ae
                    and       $fb                           ;[202a] e6 fb
                    scf                                     ;[202c] 37
                    ret       z                             ;[202d] c8
                    ld        a,(hl)                        ;[202e] 7e
                    and       $c0                           ;[202f] e6 c0
                    xor       $80                           ;[2031] ee 80
                    jr        z,$2046                       ;[2033] 28 11
                    ld        a,(hl)                        ;[2035] 7e
                    xor       c                             ;[2036] a9
                    and       $03                           ;[2037] e6 03
                    jr        z,$2040                       ;[2039] 28 05
                    call      $2080                         ;[203b] cd 80 20
                    jr        $2025                         ;[203e] 18 e5
                    ld        a,(hl)                        ;[2040] 7e
                    and       $08                           ;[2041] e6 08
                    xor       $08                           ;[2043] ee 08
                    ret       z                             ;[2045] c8
                    ld        a,$02                         ;[2046] 3e 02
                    or        a                             ;[2048] b7
                    ret                                     ;[2049] c9

                    inc       hl                            ;[204a] 23
                    ld        a,(hl)                        ;[204b] 7e
                    xor       c                             ;[204c] a9
                    scf                                     ;[204d] 37
                    ret       z                             ;[204e] c8
                    and       $08                           ;[204f] e6 08
                    xor       $08                           ;[2051] ee 08
                    ret       z                             ;[2053] c8
                    inc       hl                            ;[2054] 23
                    ld        a,(hl)                        ;[2055] 7e
                    cp        $80                           ;[2056] fe 80
                    scf                                     ;[2058] 37
                    ret       z                             ;[2059] c8
                    xor       $02                           ;[205a] ee 02
                    ld        a,$01                         ;[205c] 3e 01
                    ret       z                             ;[205e] c8
                    ld        a,$03                         ;[205f] 3e 03
                    bit       5,(hl)                        ;[2061] cb 6e
                    ret       nz                            ;[2063] c0
                    inc       a                             ;[2064] 3c
                    bit       2,(hl)                        ;[2065] cb 56
                    ret       nz                            ;[2067] c0
                    inc       a                             ;[2068] 3c
                    bit       0,(hl)                        ;[2069] cb 46
                    ret       nz                            ;[206b] c0
                    inc       a                             ;[206c] 3c
                    inc       a                             ;[206d] 3c
                    ret                                     ;[206e] c9

                    push      hl                            ;[206f] e5
                    push      af                            ;[2070] f5
                    ld        hl,$e430                      ;[2071] 21 30 e4
                    call      $2080                         ;[2074] cd 80 20
                    and       $c0                           ;[2077] e6 c0
                    cp        $80                           ;[2079] fe 80
                    jr        nz,$2074                      ;[207b] 20 f7
                    pop       af                            ;[207d] f1
                    pop       hl                            ;[207e] e1
                    ret                                     ;[207f] c9

                    ld        a,$08                         ;[2080] 3e 08
                    call      $2114                         ;[2082] cd 14 21
                    jr        $2093                         ;[2085] 18 0c
                    ld        a,$04                         ;[2087] 3e 04
                    call      $2114                         ;[2089] cd 14 21
                    ld        a,c                           ;[208c] 79
                    call      $2114                         ;[208d] cd 14 21
                    ld        hl,$e430                      ;[2090] 21 30 e4
                    push      de                            ;[2093] d5
                    push      bc                            ;[2094] c5
                    ld        bc,$2ffd                      ;[2095] 01 fd 2f
                    ld        d,$00                         ;[2098] 16 00
                    inc       hl                            ;[209a] 23
                    push      hl                            ;[209b] e5
                    in        a,(c)                         ;[209c] ed 78
                    add       a                             ;[209e] 87
                    jr        nc,$209c                      ;[209f] 30 fb
                    jp        p,$20b3                       ;[20a1] f2 b3 20
                    ld        b,$3f                         ;[20a4] 06 3f
                    in        a,(c)                         ;[20a6] ed 78
                    ld        b,$2f                         ;[20a8] 06 2f
                    ld        (hl),a                        ;[20aa] 77
                    inc       hl                            ;[20ab] 23
                    inc       d                             ;[20ac] 14
                    ex        (sp),hl                       ;[20ad] e3
                    ex        (sp),hl                       ;[20ae] e3
                    ex        (sp),hl                       ;[20af] e3
                    ex        (sp),hl                       ;[20b0] e3
                    jr        $209c                         ;[20b1] 18 e9
                    pop       hl                            ;[20b3] e1
                    ld        a,(hl)                        ;[20b4] 7e
                    dec       hl                            ;[20b5] 2b
                    ld        (hl),d                        ;[20b6] 72
                    pop       bc                            ;[20b7] c1
                    pop       de                            ;[20b8] d1
                    ret                                     ;[20b9] c9

                    call      $20de                         ;[20ba] cd de 20
                    call      $2185                         ;[20bd] cd 85 21
                    jp        $2090                         ;[20c0] c3 90 20
                    call      $20de                         ;[20c3] cd de 20
                    call      $21b7                         ;[20c6] cd b7 21
                    jp        $2090                         ;[20c9] c3 90 20
                    call      $20de                         ;[20cc] cd de 20
                    call      $21d4                         ;[20cf] cd d4 21
                    ld        a,($e42a)                     ;[20d2] 3a 2a e4
                    dec       a                             ;[20d5] 3d
                    inc       bc                            ;[20d6] 03
                    inc       bc                            ;[20d7] 03
                    inc       bc                            ;[20d8] 03
                    jr        nz,$20d5                      ;[20d9] 20 fa
                    jp        $2090                         ;[20db] c3 90 20
                    call      $206f                         ;[20de] cd 6f 20
                    ld        a,($5b5c)                     ;[20e1] 3a 5c 5b
                    and       $f8                           ;[20e4] e6 f8
                    or        (hl)                          ;[20e6] b6
                    ld        b,a                           ;[20e7] 47
                    inc       hl                            ;[20e8] 23
                    ld        e,(hl)                        ;[20e9] 5e
                    inc       hl                            ;[20ea] 23
                    ld        d,(hl)                        ;[20eb] 56
                    inc       hl                            ;[20ec] 23
                    ld        c,(hl)                        ;[20ed] 4e
                    push      bc                            ;[20ee] c5
                    inc       hl                            ;[20ef] 23
                    inc       hl                            ;[20f0] 23
                    ld        b,(hl)                        ;[20f1] 46
                    inc       hl                            ;[20f2] 23
                    dec       b                             ;[20f3] 05
                    ld        a,(hl)                        ;[20f4] 7e
                    inc       hl                            ;[20f5] 23
                    call      $2114                         ;[20f6] cd 14 21
                    djnz      $20f4                         ;[20f9] 10 f9
                    ld        a,(hl)                        ;[20fb] 7e
                    ex        de,hl                         ;[20fc] eb
                    pop       de                            ;[20fd] d1
                    ld        bc,$7ffd                      ;[20fe] 01 fd 7f
                    di                                      ;[2101] f3
                    ret                                     ;[2102] c9

                    call      $206f                         ;[2103] cd 6f 20
                    and       $40                           ;[2106] e6 40
                    or        $0a                           ;[2108] f6 0a
                    call      $2114                         ;[210a] cd 14 21
                    ld        a,c                           ;[210d] 79
                    call      $2114                         ;[210e] cd 14 21
                    jp        $2090                         ;[2111] c3 90 20
                    push      de                            ;[2114] d5
                    push      bc                            ;[2115] c5
                    ld        d,a                           ;[2116] 57
                    ld        bc,$2ffd                      ;[2117] 01 fd 2f
                    in        a,(c)                         ;[211a] ed 78
                    add       a                             ;[211c] 87
                    jr        nc,$211a                      ;[211d] 30 fb
                    add       a                             ;[211f] 87
                    jr        c,$2128                       ;[2120] 38 06
                    ld        b,$3f                         ;[2122] 06 3f
                    out       (c),d                         ;[2124] ed 51
                    ex        (sp),hl                       ;[2126] e3
                    ex        (sp),hl                       ;[2127] e3
                    pop       bc                            ;[2128] c1
                    pop       de                            ;[2129] d1
                    ret                                     ;[212a] c9

                    push      bc                            ;[212b] c5
                    push      af                            ;[212c] f5
                    xor       a                             ;[212d] af
                    ld        ($e600),a                     ;[212e] 32 00 e6
                    ld        a,($5b67)                     ;[2131] 3a 67 5b
                    bit       3,a                           ;[2134] cb 5f
                    jr        nz,$214d                      ;[2136] 20 15
                    or        $08                           ;[2138] f6 08
                    call      $2173                         ;[213a] cd 73 21
                    ld        a,($e428)                     ;[213d] 3a 28 e4
                    push      af                            ;[2140] f5
                    ld        bc,$3548                      ;[2141] 01 48 35
                    dec       bc                            ;[2144] 0b
                    ld        a,b                           ;[2145] 78
                    or        c                             ;[2146] b1
                    jr        nz,$2144                      ;[2147] 20 fb
                    pop       af                            ;[2149] f1
                    dec       a                             ;[214a] 3d
                    jr        nz,$2140                      ;[214b] 20 f3
                    pop       af                            ;[214d] f1
                    pop       bc                            ;[214e] c1
                    ret                                     ;[214f] c9

                    push      af                            ;[2150] f5
                    xor       a                             ;[2151] af
                    ld        ($e600),a                     ;[2152] 32 00 e6
                    ld        a,($5b67)                     ;[2155] 3a 67 5b
                    and       $08                           ;[2158] e6 08
                    jr        z,$2162                       ;[215a] 28 06
                    ld        a,($e429)                     ;[215c] 3a 29 e4
                    ld        ($e600),a                     ;[215f] 32 00 e6
                    pop       af                            ;[2162] f1
                    ret                                     ;[2163] c9

                    push      af                            ;[2164] f5
                    xor       a                             ;[2165] af
                    ld        ($e600),a                     ;[2166] 32 00 e6
                    ld        a,($5b67)                     ;[2169] 3a 67 5b
                    and       $f7                           ;[216c] e6 f7
                    call      $2173                         ;[216e] cd 73 21
                    pop       af                            ;[2171] f1
                    ret                                     ;[2172] c9

                    push      bc                            ;[2173] c5
                    ld        b,a                           ;[2174] 47
                    ld        a,r                           ;[2175] ed 5f
                    ld        a,b                           ;[2177] 78
                    ld        bc,$1ffd                      ;[2178] 01 fd 1f
                    di                                      ;[217b] f3
                    ld        ($5b67),a                     ;[217c] 32 67 5b
                    out       (c),a                         ;[217f] ed 79
                    pop       bc                            ;[2181] c1
                    ret       po                            ;[2182] e0
                    ei                                      ;[2183] fb
                    ret                                     ;[2184] c9

                    call      $2114                         ;[2185] cd 14 21
                    out       (c),d                         ;[2188] ed 51
                    ld        bc,$2ffd                      ;[218a] 01 fd 2f
                    ld        d,$20                         ;[218d] 16 20
                    jr        $219b                         ;[218f] 18 0a
                    ld        b,$3f                         ;[2191] 06 3f
                    ini                                     ;[2193] ed a2
                    ld        b,$2f                         ;[2195] 06 2f
                    dec       e                             ;[2197] 1d
                    jp        z,$21ac                       ;[2198] ca ac 21
                    in        a,(c)                         ;[219b] ed 78
                    jp        p,$219b                       ;[219d] f2 9b 21
                    and       d                             ;[21a0] a2
                    jp        nz,$2191                      ;[21a1] c2 91 21
                    jr        $21ef                         ;[21a4] 18 49
                    ld        b,$3f                         ;[21a6] 06 3f
                    in        a,(c)                         ;[21a8] ed 78
                    ld        b,$2f                         ;[21aa] 06 2f
                    in        a,(c)                         ;[21ac] ed 78
                    jp        p,$21ac                       ;[21ae] f2 ac 21
                    and       d                             ;[21b1] a2
                    jp        nz,$21a6                      ;[21b2] c2 a6 21
                    jr        $21ef                         ;[21b5] 18 38
                    call      $2114                         ;[21b7] cd 14 21
                    out       (c),d                         ;[21ba] ed 51
                    ld        bc,$2ffd                      ;[21bc] 01 fd 2f
                    ld        d,$20                         ;[21bf] 16 20
                    jr        $21c9                         ;[21c1] 18 06
                    ld        b,$3f                         ;[21c3] 06 3f
                    ini                                     ;[21c5] ed a2
                    ld        b,$2f                         ;[21c7] 06 2f
                    in        a,(c)                         ;[21c9] ed 78
                    jp        p,$21c9                       ;[21cb] f2 c9 21
                    and       d                             ;[21ce] a2
                    jp        nz,$21c3                      ;[21cf] c2 c3 21
                    jr        $21ef                         ;[21d2] 18 1b
                    call      $2114                         ;[21d4] cd 14 21
                    out       (c),d                         ;[21d7] ed 51
                    ld        bc,$2ffd                      ;[21d9] 01 fd 2f
                    ld        d,$20                         ;[21dc] 16 20
                    jr        $21e6                         ;[21de] 18 06
                    ld        b,$40                         ;[21e0] 06 40
                    outi                                    ;[21e2] ed a3
                    ld        b,$2f                         ;[21e4] 06 2f
                    in        a,(c)                         ;[21e6] ed 78
                    jp        p,$21e6                       ;[21e8] f2 e6 21
                    and       d                             ;[21eb] a2
                    jp        nz,$21e0                      ;[21ec] c2 e0 21
                    ld        a,($5b5c)                     ;[21ef] 3a 5c 5b
                    ld        bc,$7ffd                      ;[21f2] 01 fd 7f
                    out       (c),a                         ;[21f5] ed 79
                    ei                                      ;[21f7] fb
                    ret                                     ;[21f8] c9

                    nop                                     ;[21f9] 00
                    nop                                     ;[21fa] 00
                    nop                                     ;[21fb] 00
                    nop                                     ;[21fc] 00
                    nop                                     ;[21fd] 00
                    nop                                     ;[21fe] 00
                    nop                                     ;[21ff] 00
                    nop                                     ;[2200] 00
                    nop                                     ;[2201] 00
                    nop                                     ;[2202] 00
                    nop                                     ;[2203] 00
                    nop                                     ;[2204] 00
                    nop                                     ;[2205] 00
                    nop                                     ;[2206] 00
                    nop                                     ;[2207] 00
                    nop                                     ;[2208] 00
                    nop                                     ;[2209] 00
                    nop                                     ;[220a] 00
                    nop                                     ;[220b] 00
                    nop                                     ;[220c] 00
                    nop                                     ;[220d] 00
                    nop                                     ;[220e] 00
                    nop                                     ;[220f] 00
                    nop                                     ;[2210] 00
                    nop                                     ;[2211] 00
                    nop                                     ;[2212] 00
                    nop                                     ;[2213] 00
                    nop                                     ;[2214] 00
                    nop                                     ;[2215] 00
                    nop                                     ;[2216] 00
                    nop                                     ;[2217] 00
                    nop                                     ;[2218] 00
                    nop                                     ;[2219] 00
                    nop                                     ;[221a] 00
                    nop                                     ;[221b] 00
                    nop                                     ;[221c] 00
                    nop                                     ;[221d] 00
                    nop                                     ;[221e] 00
                    nop                                     ;[221f] 00
                    nop                                     ;[2220] 00
                    nop                                     ;[2221] 00
                    nop                                     ;[2222] 00
                    nop                                     ;[2223] 00
                    nop                                     ;[2224] 00
                    nop                                     ;[2225] 00
                    nop                                     ;[2226] 00
                    nop                                     ;[2227] 00
                    nop                                     ;[2228] 00
                    nop                                     ;[2229] 00
                    nop                                     ;[222a] 00
                    nop                                     ;[222b] 00
                    nop                                     ;[222c] 00
                    nop                                     ;[222d] 00
                    nop                                     ;[222e] 00
                    nop                                     ;[222f] 00
                    nop                                     ;[2230] 00
                    nop                                     ;[2231] 00
                    nop                                     ;[2232] 00
                    nop                                     ;[2233] 00
                    nop                                     ;[2234] 00
                    nop                                     ;[2235] 00
                    nop                                     ;[2236] 00
                    nop                                     ;[2237] 00
                    nop                                     ;[2238] 00
                    nop                                     ;[2239] 00
                    nop                                     ;[223a] 00
                    nop                                     ;[223b] 00
                    nop                                     ;[223c] 00
                    nop                                     ;[223d] 00
                    nop                                     ;[223e] 00
                    nop                                     ;[223f] 00
                    nop                                     ;[2240] 00
                    nop                                     ;[2241] 00
                    nop                                     ;[2242] 00
                    nop                                     ;[2243] 00
                    nop                                     ;[2244] 00
                    nop                                     ;[2245] 00
                    nop                                     ;[2246] 00
                    nop                                     ;[2247] 00
                    nop                                     ;[2248] 00
                    nop                                     ;[2249] 00
                    nop                                     ;[224a] 00
                    nop                                     ;[224b] 00
                    nop                                     ;[224c] 00
                    nop                                     ;[224d] 00
                    nop                                     ;[224e] 00
                    nop                                     ;[224f] 00
                    nop                                     ;[2250] 00
                    nop                                     ;[2251] 00
                    nop                                     ;[2252] 00
                    nop                                     ;[2253] 00
                    nop                                     ;[2254] 00
                    nop                                     ;[2255] 00
                    nop                                     ;[2256] 00
                    nop                                     ;[2257] 00
                    nop                                     ;[2258] 00
                    nop                                     ;[2259] 00
                    nop                                     ;[225a] 00
                    nop                                     ;[225b] 00
                    nop                                     ;[225c] 00
                    nop                                     ;[225d] 00
                    nop                                     ;[225e] 00
                    nop                                     ;[225f] 00
                    nop                                     ;[2260] 00
                    nop                                     ;[2261] 00
                    nop                                     ;[2262] 00
                    nop                                     ;[2263] 00
                    nop                                     ;[2264] 00
                    nop                                     ;[2265] 00
                    nop                                     ;[2266] 00
                    nop                                     ;[2267] 00
                    nop                                     ;[2268] 00
                    nop                                     ;[2269] 00
                    nop                                     ;[226a] 00
                    nop                                     ;[226b] 00
                    nop                                     ;[226c] 00
                    nop                                     ;[226d] 00
                    nop                                     ;[226e] 00
                    nop                                     ;[226f] 00
                    nop                                     ;[2270] 00
                    nop                                     ;[2271] 00
                    nop                                     ;[2272] 00
                    nop                                     ;[2273] 00
                    nop                                     ;[2274] 00
                    nop                                     ;[2275] 00
                    nop                                     ;[2276] 00
                    nop                                     ;[2277] 00
                    nop                                     ;[2278] 00
                    nop                                     ;[2279] 00
                    nop                                     ;[227a] 00
                    nop                                     ;[227b] 00
                    nop                                     ;[227c] 00
                    nop                                     ;[227d] 00
                    nop                                     ;[227e] 00
                    nop                                     ;[227f] 00
                    nop                                     ;[2280] 00
                    nop                                     ;[2281] 00
                    nop                                     ;[2282] 00
                    nop                                     ;[2283] 00
                    nop                                     ;[2284] 00
                    nop                                     ;[2285] 00
                    nop                                     ;[2286] 00
                    nop                                     ;[2287] 00
                    nop                                     ;[2288] 00
                    nop                                     ;[2289] 00
                    nop                                     ;[228a] 00
                    nop                                     ;[228b] 00
                    nop                                     ;[228c] 00
                    nop                                     ;[228d] 00
                    nop                                     ;[228e] 00
                    nop                                     ;[228f] 00
                    nop                                     ;[2290] 00
                    nop                                     ;[2291] 00
                    nop                                     ;[2292] 00
                    nop                                     ;[2293] 00
                    nop                                     ;[2294] 00
                    nop                                     ;[2295] 00
                    nop                                     ;[2296] 00
                    nop                                     ;[2297] 00
                    nop                                     ;[2298] 00
                    nop                                     ;[2299] 00
                    nop                                     ;[229a] 00
                    nop                                     ;[229b] 00
                    nop                                     ;[229c] 00
                    nop                                     ;[229d] 00
                    ld        b,d                           ;[229e] 42
                    ld        c,b                           ;[229f] 48
                    ld        e,c                           ;[22a0] 59
                    ld        (hl),$35                      ;[22a1] 36 35
                    ld        d,h                           ;[22a3] 54
                    ld        b,a                           ;[22a4] 47
                    ld        d,(hl)                        ;[22a5] 56
                    ld        c,(hl)                        ;[22a6] 4e
                    ld        c,d                           ;[22a7] 4a
                    ld        d,l                           ;[22a8] 55
                    scf                                     ;[22a9] 37
                    inc       (hl)                          ;[22aa] 34
                    ld        d,d                           ;[22ab] 52
                    ld        b,(hl)                        ;[22ac] 46
                    ld        b,e                           ;[22ad] 43
                    ld        c,l                           ;[22ae] 4d
                    ld        c,e                           ;[22af] 4b
                    ld        c,c                           ;[22b0] 49
                    jr        c,$22e6                       ;[22b1] 38 33
                    ld        b,l                           ;[22b3] 45
                    ld        b,h                           ;[22b4] 44
                    ld        e,b                           ;[22b5] 58
                    ld        c,$4c                         ;[22b6] 0e 4c
                    ld        c,a                           ;[22b8] 4f
                    add       hl,sp                         ;[22b9] 39
                    ld        ($5357),a                     ;[22ba] 32 57 53
                    ld        e,d                           ;[22bd] 5a
                    jr        nz,$22cd                      ;[22be] 20 0d
                    ld        d,b                           ;[22c0] 50
                    jr        nc,$22f4                      ;[22c1] 30 31
                    ld        d,c                           ;[22c3] 51
                    ld        b,c                           ;[22c4] 41
                    ex        (sp),hl                       ;[22c5] e3
                    call      nz,$e4e0                      ;[22c6] c4 e0 e4
                    or        h                             ;[22c9] b4
                    cp        h                             ;[22ca] bc
                    cp        l                             ;[22cb] bd
                    cp        e                             ;[22cc] bb
                    xor       a                             ;[22cd] af
                    or        b                             ;[22ce] b0
                    or        c                             ;[22cf] b1
                    ret       nz                            ;[22d0] c0
                    and       a                             ;[22d1] a7
                    and       (hl)                          ;[22d2] a6
                    cp        (hl)                          ;[22d3] be
                    xor       l                             ;[22d4] ad
                    or        d                             ;[22d5] b2
                    cp        d                             ;[22d6] ba
                    push      hl                            ;[22d7] e5
                    and       l                             ;[22d8] a5
                    jp        nz,$b3e1                      ;[22d9] c2 e1 b3
                    cp        c                             ;[22dc] b9
                    pop       bc                            ;[22dd] c1
                    cp        b                             ;[22de] b8
                    ld        a,(hl)                        ;[22df] 7e
                    call      c,$5cda                       ;[22e0] dc da 5c
                    or        a                             ;[22e3] b7
                    ld        a,e                           ;[22e4] 7b
                    ld        a,l                           ;[22e5] 7d
                    ret       c                             ;[22e6] d8
                    cp        a                             ;[22e7] bf
                    xor       (hl)                          ;[22e8] ae
                    xor       d                             ;[22e9] aa
                    xor       e                             ;[22ea] ab
                    sbc       $df                           ;[22eb] dd de df
                    ld        a,a                           ;[22ee] 7f
                    or        l                             ;[22ef] b5
                    sub       $7c                           ;[22f0] d6 7c
                    push      de                            ;[22f2] d5
                    ld        e,l                           ;[22f3] 5d
                    in        a,($b6)                       ;[22f4] db b6
                    exx                                     ;[22f6] d9
                    ld        e,e                           ;[22f7] 5b
                    rst       $10                           ;[22f8] d7
                    inc       c                             ;[22f9] 0c
                    rlca                                    ;[22fa] 07
                    ld        b,$04                         ;[22fb] 06 04
                    dec       b                             ;[22fd] 05
                    ex        af,af'                        ;[22fe] 08
                    ld        a,(bc)                        ;[22ff] 0a
                    dec       bc                            ;[2300] 0b
                    add       hl,bc                         ;[2301] 09
                    rrca                                    ;[2302] 0f
                    jp        po,$3f2a                      ;[2303] e2 2a 3f
                    call      $ccc8                         ;[2306] cd c8 cc
                    bit       3,(hl)                        ;[2309] cb 5e
                    xor       h                             ;[230b] ac
                    dec       l                             ;[230c] 2d
                    dec       hl                            ;[230d] 2b
                    dec       a                             ;[230e] 3d
                    ld        l,$2c                         ;[230f] 2e 2c
                    dec       sp                            ;[2311] 3b
                    ld        ($3cc7),hl                    ;[2312] 22 c7 3c
                    jp        $c53e                         ;[2315] c3 3e c5
                    cpl                                     ;[2318] 2f
                    ret                                     ;[2319] c9

                    ld        h,b                           ;[231a] 60
                    add       $3a                           ;[231b] c6 3a
                    ret       nc                            ;[231d] d0
                    adc       $a8                           ;[231e] ce a8
                    jp        z,$d4d3                       ;[2320] ca d3 d4
                    pop       de                            ;[2323] d1
                    jp        nc,$cfa9                      ;[2324] d2 a9 cf
                    ld        l,$2f                         ;[2327] 2e 2f
                    ld        de,$ffff                      ;[2329] 11 ff ff
                    ld        bc,$fefe                      ;[232c] 01 fe fe
                    in        a,(c)                         ;[232f] ed 78
                    cpl                                     ;[2331] 2f
                    and       $1f                           ;[2332] e6 1f
                    jr        z,$2344                       ;[2334] 28 0e
                    ld        h,a                           ;[2336] 67
                    ld        a,l                           ;[2337] 7d
                    inc       d                             ;[2338] 14
                    ret       nz                            ;[2339] c0
                    sub       $08                           ;[233a] d6 08
                    srl       h                             ;[233c] cb 3c
                    jr        nc,$233a                      ;[233e] 30 fa
                    ld        d,e                           ;[2340] 53
                    ld        e,a                           ;[2341] 5f
                    jr        nz,$2338                      ;[2342] 20 f4
                    dec       l                             ;[2344] 2d
                    rlc       b                             ;[2345] cb 00
                    jr        c,$232f                       ;[2347] 38 e6
                    ld        a,d                           ;[2349] 7a
                    inc       a                             ;[234a] 3c
                    ret       z                             ;[234b] c8
                    cp        $28                           ;[234c] fe 28
                    ret       z                             ;[234e] c8
                    cp        $19                           ;[234f] fe 19
                    ret       z                             ;[2351] c8
                    ld        a,e                           ;[2352] 7b
                    ld        e,d                           ;[2353] 5a
                    ld        d,a                           ;[2354] 57
                    cp        $18                           ;[2355] fe 18
                    ret                                     ;[2357] c9

                    call      $2327                         ;[2358] cd 27 23
                    ret       nz                            ;[235b] c0
                    ld        hl,$5c00                      ;[235c] 21 00 5c
                    bit       7,(hl)                        ;[235f] cb 7e
                    jr        nz,$236a                      ;[2361] 20 07
                    inc       hl                            ;[2363] 23
                    dec       (hl)                          ;[2364] 35
                    dec       hl                            ;[2365] 2b
                    jr        nz,$236a                      ;[2366] 20 02
                    ld        (hl),$ff                      ;[2368] 36 ff
                    ld        a,l                           ;[236a] 7d
                    ld        hl,$5c04                      ;[236b] 21 04 5c
                    cp        l                             ;[236e] bd
                    jr        nz,$235f                      ;[236f] 20 ee
                    call      $23b7                         ;[2371] cd b7 23
                    ret       nc                            ;[2374] d0
                    ld        hl,$5c00                      ;[2375] 21 00 5c
                    cp        (hl)                          ;[2378] be
                    jr        z,$23a9                       ;[2379] 28 2e
                    ex        de,hl                         ;[237b] eb
                    ld        hl,$5c04                      ;[237c] 21 04 5c
                    cp        (hl)                          ;[237f] be
                    jr        z,$23a9                       ;[2380] 28 27
                    bit       7,(hl)                        ;[2382] cb 7e
                    jr        nz,$238a                      ;[2384] 20 04
                    ex        de,hl                         ;[2386] eb
                    bit       7,(hl)                        ;[2387] cb 7e
                    ret       z                             ;[2389] c8
                    ld        e,a                           ;[238a] 5f
                    ld        (hl),a                        ;[238b] 77
                    inc       hl                            ;[238c] 23
                    ld        (hl),$05                      ;[238d] 36 05
                    inc       hl                            ;[238f] 23
                    ld        a,($5c09)                     ;[2390] 3a 09 5c
                    ld        (hl),a                        ;[2393] 77
                    inc       hl                            ;[2394] 23
                    ld        c,(iy+$07)                    ;[2395] fd 4e 07
                    ld        d,(iy+$01)                    ;[2398] fd 56 01
                    push      hl                            ;[239b] e5
                    call      $23cc                         ;[239c] cd cc 23
                    pop       hl                            ;[239f] e1
                    ld        (hl),a                        ;[23a0] 77
                    ld        ($5c08),a                     ;[23a1] 32 08 5c
                    set       5,(iy+$01)                    ;[23a4] fd cb 01 ee
                    ret                                     ;[23a8] c9

                    inc       hl                            ;[23a9] 23
                    ld        (hl),$05                      ;[23aa] 36 05
                    inc       hl                            ;[23ac] 23
                    dec       (hl)                          ;[23ad] 35
                    ret       nz                            ;[23ae] c0
                    ld        a,($5c0a)                     ;[23af] 3a 0a 5c
                    ld        (hl),a                        ;[23b2] 77
                    inc       hl                            ;[23b3] 23
                    ld        a,(hl)                        ;[23b4] 7e
                    jr        $23a1                         ;[23b5] 18 ea
                    ld        b,d                           ;[23b7] 42
                    ld        d,$00                         ;[23b8] 16 00
                    ld        a,e                           ;[23ba] 7b
                    cp        $27                           ;[23bb] fe 27
                    ret       nc                            ;[23bd] d0
                    cp        $18                           ;[23be] fe 18
                    jr        nz,$23c5                      ;[23c0] 20 03
                    bit       7,b                           ;[23c2] cb 78
                    ret       nz                            ;[23c4] c0
                    ld        hl,$229e                      ;[23c5] 21 9e 22
                    add       hl,de                         ;[23c8] 19
                    ld        a,(hl)                        ;[23c9] 7e
                    scf                                     ;[23ca] 37
                    ret                                     ;[23cb] c9

                    ld        a,e                           ;[23cc] 7b
                    cp        $3a                           ;[23cd] fe 3a
                    jr        c,$2400                       ;[23cf] 38 2f
                    dec       c                             ;[23d1] 0d
                    jp        m,$23e8                       ;[23d2] fa e8 23
                    jr        z,$23da                       ;[23d5] 28 03
                    add       $4f                           ;[23d7] c6 4f
                    ret                                     ;[23d9] c9

                    ld        hl,$2284                      ;[23da] 21 84 22
                    inc       b                             ;[23dd] 04
                    jr        z,$23e3                       ;[23de] 28 03
                    ld        hl,$229e                      ;[23e0] 21 9e 22
                    ld        d,$00                         ;[23e3] 16 00
                    add       hl,de                         ;[23e5] 19
                    ld        a,(hl)                        ;[23e6] 7e
                    ret                                     ;[23e7] c9

                    ld        hl,$22c2                      ;[23e8] 21 c2 22
                    bit       0,b                           ;[23eb] cb 40
                    jr        z,$23e3                       ;[23ed] 28 f4
                    bit       3,d                           ;[23ef] cb 5a
                    jr        z,$23fd                       ;[23f1] 28 0a
                    bit       3,(iy+$30)                    ;[23f3] fd cb 30 5e
                    ret       nz                            ;[23f7] c0
                    inc       b                             ;[23f8] 04
                    ret       nz                            ;[23f9] c0
                    add       $20                           ;[23fa] c6 20
                    ret                                     ;[23fc] c9

                    add       $a5                           ;[23fd] c6 a5
                    ret                                     ;[23ff] c9

                    cp        $30                           ;[2400] fe 30
                    ret       c                             ;[2402] d8
                    dec       c                             ;[2403] 0d
                    jp        m,$2436                       ;[2404] fa 36 24
                    jr        nz,$2422                      ;[2407] 20 19
                    ld        hl,$22ed                      ;[2409] 21 ed 22
                    bit       5,b                           ;[240c] cb 68
                    jr        z,$23e3                       ;[240e] 28 d3
                    cp        $38                           ;[2410] fe 38
                    jr        nc,$241b                      ;[2412] 30 07
                    sub       $20                           ;[2414] d6 20
                    inc       b                             ;[2416] 04
                    ret       z                             ;[2417] c8
                    add       $08                           ;[2418] c6 08
                    ret                                     ;[241a] c9

                    sub       $36                           ;[241b] d6 36
                    inc       b                             ;[241d] 04
                    ret       z                             ;[241e] c8
                    add       $fe                           ;[241f] c6 fe
                    ret                                     ;[2421] c9

                    ld        hl,$22c9                      ;[2422] 21 c9 22
                    cp        $39                           ;[2425] fe 39
                    jr        z,$23e3                       ;[2427] 28 ba
                    cp        $30                           ;[2429] fe 30
                    jr        z,$23e3                       ;[242b] 28 b6
                    and       $07                           ;[242d] e6 07
                    add       $80                           ;[242f] c6 80
                    inc       b                             ;[2431] 04
                    ret       z                             ;[2432] c8
                    xor       $0f                           ;[2433] ee 0f
                    ret                                     ;[2435] c9

                    inc       b                             ;[2436] 04
                    ret       z                             ;[2437] c8
                    bit       5,b                           ;[2438] cb 68
                    ld        hl,$22c9                      ;[243a] 21 c9 22
                    jr        nz,$23e3                      ;[243d] 20 a4
                    sub       $10                           ;[243f] d6 10
                    cp        $22                           ;[2441] fe 22
                    jr        z,$244b                       ;[2443] 28 06
                    cp        $20                           ;[2445] fe 20
                    ret       nz                            ;[2447] c0
                    ld        a,$5f                         ;[2448] 3e 5f
                    ret                                     ;[244a] c9

                    ld        a,$40                         ;[244b] 3e 40
                    ret                                     ;[244d] c9

                    push      hl                            ;[244e] e5
                    ld        hl,($5c51)                    ;[244f] 2a 51 5c
                    ex        (sp),hl                       ;[2452] e3
                    push      hl                            ;[2453] e5
                    ld        hl,($5c3b)                    ;[2454] 2a 3b 5c
                    ex        (sp),hl                       ;[2457] e3
                    push      hl                            ;[2458] e5
                    ld        hl,($5c6a)                    ;[2459] 2a 6a 5c
                    ex        (sp),hl                       ;[245c] e3
                    xor       a                             ;[245d] af
                    call      $3e00                         ;[245e] cd 00 3e
                    ret       p                             ;[2461] f0
                    ccf                                     ;[2462] 3f
                    pop       hl                            ;[2463] e1
                    ld        ($5c6a),hl                    ;[2464] 22 6a 5c
                    pop       hl                            ;[2467] e1
                    ld        ($5c3b),hl                    ;[2468] 22 3b 5c
                    pop       hl                            ;[246b] e1
                    ld        ($5c51),hl                    ;[246c] 22 51 5c
                    ret                                     ;[246f] c9

                    push      hl                            ;[2470] e5
                    ld        hl,($5c51)                    ;[2471] 2a 51 5c
                    ex        (sp),hl                       ;[2474] e3
                    push      hl                            ;[2475] e5
                    ld        hl,($5c3b)                    ;[2476] 2a 3b 5c
                    ex        (sp),hl                       ;[2479] e3
                    push      hl                            ;[247a] e5
                    ld        hl,($5c6a)                    ;[247b] 2a 6a 5c
                    ex        (sp),hl                       ;[247e] e3
                    or        a                             ;[247f] b7
                    call      $3e00                         ;[2480] cd 00 3e
                    ret       p                             ;[2483] f0
                    ccf                                     ;[2484] 3f
                    pop       hl                            ;[2485] e1
                    ld        ($5c6a),hl                    ;[2486] 22 6a 5c
                    pop       hl                            ;[2489] e1
                    ld        ($5c3b),hl                    ;[248a] 22 3b 5c
                    pop       hl                            ;[248d] e1
                    ld        ($5c51),hl                    ;[248e] 22 51 5c
                    ret                                     ;[2491] c9

                    call      $6827                         ;[2492] cd 27 68
                    rst       $28                           ;[2495] ef
                    ld        l,e                           ;[2496] 6b
                    dec       c                             ;[2497] 0d
                    ld        a,$02                         ;[2498] 3e 02
                    rst       $28                           ;[249a] ef
                    ld        bc,$cd16                      ;[249b] 01 16 cd
                    jr        c,$2500                       ;[249e] 38 60
                    push      af                            ;[24a0] f5
                    ld        hl,$6531                      ;[24a1] 21 31 65
                    jr        c,$24ae                       ;[24a4] 38 08
                    cp        $ff                           ;[24a6] fe ff
                    call      nz,$6575                      ;[24a8] c4 75 65
                    ld        hl,$654e                      ;[24ab] 21 4e 65
                    call      $6591                         ;[24ae] cd 91 65
                    ld        a,$0d                         ;[24b1] 3e 0d
                    call      $637e                         ;[24b3] cd 7e 63
                    pop       hl                            ;[24b6] e1
                    push      hl                            ;[24b7] e5
                    call      $67e6                         ;[24b8] cd e6 67
                    pop       hl                            ;[24bb] e1
                    and       $df                           ;[24bc] e6 df
                    cp        $52                           ;[24be] fe 52
                    jp        z,$6000                       ;[24c0] ca 00 60
                    cp        $51                           ;[24c3] fe 51
                    jr        nz,$24b7                      ;[24c5] 20 f0
                    push      hl                            ;[24c7] e5
                    pop       af                            ;[24c8] f1
                    ret                                     ;[24c9] c9

                    ld        a,$01                         ;[24ca] 3e 01
                    call      $637e                         ;[24cc] cd 7e 63
                    call      $6804                         ;[24cf] cd 04 68
                    call      $0100                         ;[24d2] cd 00 01
                    call      $6827                         ;[24d5] cd 27 68
                    ret       nc                            ;[24d8] d0
                    call      $6804                         ;[24d9] cd 04 68
                    call      $0157                         ;[24dc] cd 57 01
                    call      $6827                         ;[24df] cd 27 68
                    jr        c,$24ed                       ;[24e2] 38 09
                    ld        a,$03                         ;[24e4] 3e 03
                    call      $637e                         ;[24e6] cd 7e 63
                    xor       a                             ;[24e9] af
                    ld        a,$ff                         ;[24ea] 3e ff
                    ret                                     ;[24ec] c9

                    xor       a                             ;[24ed] af
                    ld        ($6a91),a                     ;[24ee] 32 91 6a
                    ld        a,$02                         ;[24f1] 3e 02
                    call      $637e                         ;[24f3] cd 7e 63
                    call      $6804                         ;[24f6] cd 04 68
                    call      $017b                         ;[24f9] cd 7b 01
                    call      $6827                         ;[24fc] cd 27 68
                    ld        a,$41                         ;[24ff] 3e 41
                    jr        nc,$250b                      ;[2501] 30 08
                    ld        a,$26                         ;[2503] 3e 26
                    rst       $10                           ;[2505] d7
                    ld        a,$42                         ;[2506] 3e 42
                    rst       $10                           ;[2508] d7
                    ld        a,$42                         ;[2509] 3e 42
                    ld        ($6a90),a                     ;[250b] 32 90 6a
                    ld        a,$0d                         ;[250e] 3e 0d
                    rst       $10                           ;[2510] d7
                    ld        ix,$6a4e                      ;[2511] dd 21 4e 6a
                    ld        a,$00                         ;[2515] 3e 00
                    call      $6804                         ;[2517] cd 04 68
                    call      $0178                         ;[251a] cd 78 01
                    call      $6827                         ;[251d] cd 27 68
                    ret       nc                            ;[2520] d0
                    call      $60a1                         ;[2521] cd a1 60
                    ret       nc                            ;[2524] d0
                    ld        a,$01                         ;[2525] 3e 01
                    ld        ($6a91),a                     ;[2527] 32 91 6a
                    ld        a,($6a90)                     ;[252a] 3a 90 6a
                    cp        $42                           ;[252d] fe 42
                    call      z,$60a1                       ;[252f] cc a1 60
                    ret                                     ;[2532] c9

                    ld        a,$04                         ;[2533] 3e 04
                    call      $637e                         ;[2535] cd 7e 63
                    ld        a,($6a91)                     ;[2538] 3a 91 6a
                    add       $41                           ;[253b] c6 41
                    rst       $10                           ;[253d] d7
                    ld        a,$0d                         ;[253e] 3e 0d
                    rst       $10                           ;[2540] d7
                    call      $6357                         ;[2541] cd 57 63
                    jr        z,$2550                       ;[2544] 28 0a
                    ld        a,$06                         ;[2546] 3e 06
                    call      $637e                         ;[2548] cd 7e 63
                    call      $6357                         ;[254b] cd 57 63
                    jr        nz,$254b                      ;[254e] 20 fb
                    ld        a,$05                         ;[2550] 3e 05
                    call      $637e                         ;[2552] cd 7e 63
                    call      $6357                         ;[2555] cd 57 63
                    jr        z,$2555                       ;[2558] 28 fb
                    call      $6804                         ;[255a] cd 04 68
                    call      $015a                         ;[255d] cd 5a 01
                    call      $6827                         ;[2560] cd 27 68
                    ld        a,$0e                         ;[2563] 3e 0e
                    call      $637e                         ;[2565] cd 7e 63
                    ld        a,$e5                         ;[2568] 3e e5
                    ld        ($684d),a                     ;[256a] 32 4d 68
                    ld        d,$13                         ;[256d] 16 13
                    call      $6295                         ;[256f] cd 95 62
                    ret       nc                            ;[2572] d0
                    ld        d,$00                         ;[2573] 16 00
                    call      $6295                         ;[2575] cd 95 62
                    ret       nc                            ;[2578] d0
                    ld        d,$27                         ;[2579] 16 27
                    call      $6295                         ;[257b] cd 95 62
                    ret       nc                            ;[257e] d0
                    ld        a,$aa                         ;[257f] 3e aa
                    ld        ($684d),a                     ;[2581] 32 4d 68
                    ld        d,$12                         ;[2584] 16 12
                    call      $6295                         ;[2586] cd 95 62
                    ret       nc                            ;[2589] d0
                    ld        d,$01                         ;[258a] 16 01
                    call      $6295                         ;[258c] cd 95 62
                    ret       nc                            ;[258f] d0
                    ld        d,$26                         ;[2590] 16 26
                    call      $6295                         ;[2592] cd 95 62
                    ret       nc                            ;[2595] d0
                    ld        a,$e5                         ;[2596] 3e e5
                    ld        ($684d),a                     ;[2598] 32 4d 68
                    ld        d,$00                         ;[259b] 16 00
                    call      $6256                         ;[259d] cd 56 62
                    ret       nc                            ;[25a0] d0
                    ld        d,$13                         ;[25a1] 16 13
                    call      $6256                         ;[25a3] cd 56 62
                    ret       nc                            ;[25a6] d0
                    ld        d,$27                         ;[25a7] 16 27
                    call      $6256                         ;[25a9] cd 56 62
                    ret       nc                            ;[25ac] d0
                    ld        a,$aa                         ;[25ad] 3e aa
                    ld        ($684d),a                     ;[25af] 32 4d 68
                    ld        d,$01                         ;[25b2] 16 01
                    call      $6256                         ;[25b4] cd 56 62
                    ret       nc                            ;[25b7] d0
                    ld        d,$12                         ;[25b8] 16 12
                    call      $6256                         ;[25ba] cd 56 62
                    ret       nc                            ;[25bd] d0
                    ld        d,$26                         ;[25be] 16 26
                    call      $6256                         ;[25c0] cd 56 62
                    ret       nc                            ;[25c3] d0
                    xor       a                             ;[25c4] af
                    ld        ($684d),a                     ;[25c5] 32 4d 68
                    ld        d,$00                         ;[25c8] 16 00
                    call      $6295                         ;[25ca] cd 95 62
                    ret       nc                            ;[25cd] d0
                    ld        d,$01                         ;[25ce] 16 01
                    call      $6295                         ;[25d0] cd 95 62
                    ret       nc                            ;[25d3] d0
                    ld        d,$12                         ;[25d4] 16 12
                    call      $6295                         ;[25d6] cd 95 62
                    ret       nc                            ;[25d9] d0
                    ld        d,$13                         ;[25da] 16 13
                    call      $6295                         ;[25dc] cd 95 62
                    ret       nc                            ;[25df] d0
                    ld        d,$26                         ;[25e0] 16 26
                    call      $6295                         ;[25e2] cd 95 62
                    ret       nc                            ;[25e5] d0
                    ld        d,$27                         ;[25e6] 16 27
                    call      $6295                         ;[25e8] cd 95 62
                    ret       nc                            ;[25eb] d0
                    ld        d,$00                         ;[25ec] 16 00
                    call      $6256                         ;[25ee] cd 56 62
                    ret       nc                            ;[25f1] d0
                    ld        d,$01                         ;[25f2] 16 01
                    call      $6256                         ;[25f4] cd 56 62
                    ret       nc                            ;[25f7] d0
                    ld        d,$12                         ;[25f8] 16 12
                    call      $6256                         ;[25fa] cd 56 62
                    ret       nc                            ;[25fd] d0
                    ld        d,$13                         ;[25fe] 16 13
                    call      $6256                         ;[2600] cd 56 62
                    ret       nc                            ;[2603] d0
                    ld        d,$26                         ;[2604] 16 26
                    call      $6256                         ;[2606] cd 56 62
                    ret       nc                            ;[2609] d0
                    ld        d,$27                         ;[260a] 16 27
                    call      $6256                         ;[260c] cd 56 62
                    ret       nc                            ;[260f] d0
                    ld        a,$06                         ;[2610] 3e 06
                    call      $637e                         ;[2612] cd 7e 63
                    call      $6357                         ;[2615] cd 57 63
                    jr        nz,$2615                      ;[2618] 20 fb
                    rst       $28                           ;[261a] ef
                    xor       a                             ;[261b] af
                    dec       c                             ;[261c] 0d
                    ld        a,$07                         ;[261d] 3e 07
                    call      $637e                         ;[261f] cd 7e 63
                    call      $6357                         ;[2622] cd 57 63
                    jr        z,$2622                       ;[2625] 28 fb
                    call      $6804                         ;[2627] cd 04 68
                    call      $015a                         ;[262a] cd 5a 01
                    call      $6827                         ;[262d] cd 27 68
                    ld        a,($6a91)                     ;[2630] 3a 91 6a
                    ld        c,a                           ;[2633] 4f
                    call      $6804                         ;[2634] cd 04 68
                    call      $017e                         ;[2637] cd 7e 01
                    call      $6827                         ;[263a] cd 27 68
                    and       $40                           ;[263d] e6 40
                    jr        nz,$264a                      ;[263f] 20 09
                    ld        a,$08                         ;[2641] 3e 08
                    call      $637e                         ;[2643] cd 7e 63
                    xor       a                             ;[2646] af
                    ld        a,$ff                         ;[2647] 3e ff
                    ret                                     ;[2649] c9

                    ld        a,$0a                         ;[264a] 3e 0a
                    call      $637e                         ;[264c] cd 7e 63
                    ld        b,$28                         ;[264f] 06 28
                    push      bc                            ;[2651] c5
                    ld        a,b                           ;[2652] 78
                    add       a                             ;[2653] 87
                    add       a                             ;[2654] 87
                    add       a                             ;[2655] 87
                    add       a                             ;[2656] 87
                    add       b                             ;[2657] 80
                    ld        ($684d),a                     ;[2658] 32 4d 68
                    dec       b                             ;[265b] 05
                    ld        d,b                           ;[265c] 50
                    call      $6256                         ;[265d] cd 56 62
                    pop       bc                            ;[2660] c1
                    ret       nc                            ;[2661] d0
                    djnz      $2651                         ;[2662] 10 ed
                    ld        a,$06                         ;[2664] 3e 06
                    call      $637e                         ;[2666] cd 7e 63
                    call      $6357                         ;[2669] cd 57 63
                    jr        nz,$2669                      ;[266c] 20 fb
                    rst       $28                           ;[266e] ef
                    xor       a                             ;[266f] af
                    dec       c                             ;[2670] 0d
                    xor       a                             ;[2671] af
                    scf                                     ;[2672] 37
                    ret                                     ;[2673] c9

                    ld        a,$0b                         ;[2674] 3e 0b
                    call      $637e                         ;[2676] cd 7e 63
                    call      $62cb                         ;[2679] cd cb 62
                    call      $62cb                         ;[267c] cd cb 62
                    push      hl                            ;[267f] e5
                    ld        hl,$3030                      ;[2680] 21 30 30
                    ld        ($650f),hl                    ;[2683] 22 0f 65
                    pop       hl                            ;[2686] e1
                    push      de                            ;[2687] d5
                    ld        ($684b),hl                    ;[2688] 22 4b 68
                    ld        de,$736f                      ;[268b] 11 6f 73
                    or        a                             ;[268e] b7
                    sbc       hl,de                         ;[268f] ed 52
                    ld        a,$2d                         ;[2691] 3e 2d
                    jr        nc,$2697                      ;[2693] 30 02
                    ld        a,$2b                         ;[2695] 3e 2b
                    ld        ($650e),a                     ;[2697] 32 0e 65
                    pop       de                            ;[269a] d1
                    ld        a,d                           ;[269b] 7a
                    push      af                            ;[269c] f5
                    or        e                             ;[269d] b3
                    jr        z,$26a6                       ;[269e] 28 06
                    pop       af                            ;[26a0] f1
                    ld        a,$63                         ;[26a1] 3e 63
                    jp        $622c                         ;[26a3] c3 2c 62
                    pop       af                            ;[26a6] f1
                    ld        hl,($684b)                    ;[26a7] 2a 4b 68
                    ld        de,$736f                      ;[26aa] 11 6f 73
                    jr        nc,$26b0                      ;[26ad] 30 01
                    ex        de,hl                         ;[26af] eb
                    xor       a                             ;[26b0] af
                    sbc       hl,de                         ;[26b1] ed 52
                    ld        de,$0127                      ;[26b3] 11 27 01
                    or        a                             ;[26b6] b7
                    sbc       hl,de                         ;[26b7] ed 52
                    jr        c,$26be                       ;[26b9] 38 03
                    inc       a                             ;[26bb] 3c
                    jr        $26b6                         ;[26bc] 18 f8
                    push      af                            ;[26be] f5
                    or        a                             ;[26bf] b7
                    jr        z,$26cb                       ;[26c0] 28 09
                    ld        b,a                           ;[26c2] 47
                    ld        hl,$6510                      ;[26c3] 21 10 65
                    call      $624b                         ;[26c6] cd 4b 62
                    djnz      $26c3                         ;[26c9] 10 f8
                    ld        a,$0f                         ;[26cb] 3e 0f
                    call      $637e                         ;[26cd] cd 7e 63
                    pop       af                            ;[26d0] f1
                    cp        $03                           ;[26d1] fe 03
                    ld        a,$10                         ;[26d3] 3e 10
                    push      af                            ;[26d5] f5
                    call      nc,$637e                      ;[26d6] d4 7e 63
                    pop       af                            ;[26d9] f1
                    ld        a,$ff                         ;[26da] 3e ff
                    ret                                     ;[26dc] c9

                    ld        a,(hl)                        ;[26dd] 7e
                    inc       a                             ;[26de] 3c
                    ld        (hl),a                        ;[26df] 77
                    cp        $3a                           ;[26e0] fe 3a
                    ret       nz                            ;[26e2] c0
                    ld        (hl),$30                      ;[26e3] 36 30
                    dec       hl                            ;[26e5] 2b
                    jr        $26dd                         ;[26e6] 18 f5
                    ld        b,$09                         ;[26e8] 06 09
                    push      bc                            ;[26ea] c5
                    dec       b                             ;[26eb] 05
                    ld        a,($6a91)                     ;[26ec] 3a 91 6a
                    ld        e,b                           ;[26ef] 58
                    ld        c,a                           ;[26f0] 4f
                    ld        b,$00                         ;[26f1] 06 00
                    ld        e,$00                         ;[26f3] 1e 00
                    ld        hl,$684e                      ;[26f5] 21 4e 68
                    ld        ix,$6a4e                      ;[26f8] dd 21 4e 6a
                    call      $6804                         ;[26fc] cd 04 68
                    call      $0163                         ;[26ff] cd 63 01
                    call      $6827                         ;[2702] cd 27 68
                    pop       bc                            ;[2705] c1
                    ret       nc                            ;[2706] d0
                    push      bc                            ;[2707] c5
                    ld        hl,$684e                      ;[2708] 21 4e 68
                    ld        bc,$0200                      ;[270b] 01 00 02
                    ld        a,($684d)                     ;[270e] 3a 4d 68
                    cp        (hl)                          ;[2711] be
                    jr        nz,$271d                      ;[2712] 20 09
                    inc       hl                            ;[2714] 23
                    dec       bc                            ;[2715] 0b
                    ld        a,b                           ;[2716] 78
                    or        c                             ;[2717] b1
                    jr        nz,$270e                      ;[2718] 20 f4
                    pop       bc                            ;[271a] c1
                    scf                                     ;[271b] 37
                    ret                                     ;[271c] c9

                    pop       bc                            ;[271d] c1
                    ld        a,$0c                         ;[271e] 3e 0c
                    call      $637e                         ;[2720] cd 7e 63
                    xor       a                             ;[2723] af
                    ld        a,$ff                         ;[2724] 3e ff
                    ret                                     ;[2726] c9

                    ld        a,($684d)                     ;[2727] 3a 4d 68
                    ld        e,a                           ;[272a] 5f
                    ld        b,$00                         ;[272b] 06 00
                    ld        a,($6a91)                     ;[272d] 3a 91 6a
                    ld        c,a                           ;[2730] 4f
                    ld        hl,$6a68                      ;[2731] 21 68 6a
                    call      $62b3                         ;[2734] cd b3 62
                    ld        ix,$6a4e                      ;[2737] dd 21 4e 6a
                    call      $6804                         ;[273b] cd 04 68
                    call      $016c                         ;[273e] cd 6c 01
                    call      $6827                         ;[2741] cd 27 68
                    ret                                     ;[2744] c9

                    push      af                            ;[2745] f5
                    push      bc                            ;[2746] c5
                    push      hl                            ;[2747] e5
                    ld        b,$09                         ;[2748] 06 09
                    ld        (hl),d                        ;[274a] 72
                    inc       hl                            ;[274b] 23
                    ld        (hl),$00                      ;[274c] 36 00
                    inc       hl                            ;[274e] 23
                    ld        a,$0a                         ;[274f] 3e 0a
                    sub       b                             ;[2751] 90
                    ld        (hl),a                        ;[2752] 77
                    inc       hl                            ;[2753] 23
                    ld        (hl),$02                      ;[2754] 36 02
                    inc       hl                            ;[2756] 23
                    djnz      $274a                         ;[2757] 10 f1
                    pop       hl                            ;[2759] e1
                    pop       bc                            ;[275a] c1
                    pop       af                            ;[275b] f1
                    ret                                     ;[275c] c9

                    call      $6357                         ;[275d] cd 57 63
                    jr        z,$275d                       ;[2760] 28 fb
                    call      $6804                         ;[2762] cd 04 68
                    call      $0196                         ;[2765] cd 96 01
                    call      $6827                         ;[2768] cd 27 68
                    di                                      ;[276b] f3
                    call      $62df                         ;[276c] cd df 62
                    ei                                      ;[276f] fb
                    ret                                     ;[2770] c9

                    ld        bc,$2ffd                      ;[2771] 01 fd 2f
                    in        a,(c)                         ;[2774] ed 78
                    bit       4,a                           ;[2776] cb 67
                    jr        nz,$2771                      ;[2778] 20 f7
                    ld        a,$66                         ;[277a] 3e 66
                    call      $6340                         ;[277c] cd 40 63
                    ld        a,($6a91)                     ;[277f] 3a 91 6a
                    call      $6340                         ;[2782] cd 40 63
                    xor       a                             ;[2785] af
                    call      $6340                         ;[2786] cd 40 63
                    xor       a                             ;[2789] af
                    call      $6340                         ;[278a] cd 40 63
                    ld        a,$fe                         ;[278d] 3e fe
                    call      $6340                         ;[278f] cd 40 63
                    ld        a,$03                         ;[2792] 3e 03
                    call      $6340                         ;[2794] cd 40 63
                    ld        a,$fe                         ;[2797] 3e fe
                    call      $6340                         ;[2799] cd 40 63
                    ld        a,$2a                         ;[279c] 3e 2a
                    call      $6340                         ;[279e] cd 40 63
                    ld        a,$ff                         ;[27a1] 3e ff
                    call      $6340                         ;[27a3] cd 40 63
                    ld        hl,$0000                      ;[27a6] 21 00 00
                    ld        de,$0000                      ;[27a9] 11 00 00
                    ld        bc,$2ffd                      ;[27ac] 01 fd 2f
                    in        a,(c)                         ;[27af] ed 78
                    bit       7,a                           ;[27b1] cb 7f
                    jr        nz,$27bd                      ;[27b3] 20 08
                    inc       hl                            ;[27b5] 23
                    ld        a,h                           ;[27b6] 7c
                    or        l                             ;[27b7] b5
                    jr        nz,$27af                      ;[27b8] 20 f5
                    inc       de                            ;[27ba] 13
                    jr        $27af                         ;[27bb] 18 f2
                    bit       5,a                           ;[27bd] cb 6f
                    jr        z,$27c8                       ;[27bf] 28 07
                    ld        bc,$3ffd                      ;[27c1] 01 fd 3f
                    in        a,(c)                         ;[27c4] ed 78
                    jr        $27ac                         ;[27c6] 18 e4
                    ld        bc,$2ffd                      ;[27c8] 01 fd 2f
                    in        a,(c)                         ;[27cb] ed 78
                    bit       7,a                           ;[27cd] cb 7f
                    jr        z,$27c8                       ;[27cf] 28 f7
                    ret                                     ;[27d1] c9

                    ld        d,a                           ;[27d2] 57
                    ld        bc,$2ffd                      ;[27d3] 01 fd 2f
                    in        a,(c)                         ;[27d6] ed 78
                    and       $e0                           ;[27d8] e6 e0
                    cp        $80                           ;[27da] fe 80
                    jr        nz,$27d6                      ;[27dc] 20 f8
                    ld        bc,$3ffd                      ;[27de] 01 fd 3f
                    ld        a,d                           ;[27e1] 7a
                    out       (c),a                         ;[27e2] ed 79
                    ex        (sp),hl                       ;[27e4] e3
                    ex        (sp),hl                       ;[27e5] e3
                    ex        (sp),hl                       ;[27e6] e3
                    ex        (sp),hl                       ;[27e7] e3
                    ret                                     ;[27e8] c9

                    ld        a,($6a91)                     ;[27e9] 3a 91 6a
                    ld        c,a                           ;[27ec] 4f
                    call      $6804                         ;[27ed] cd 04 68
                    call      $017e                         ;[27f0] cd 7e 01
                    call      $6827                         ;[27f3] cd 27 68
                    ld        bc,$4000                      ;[27f6] 01 00 40
                    push      bc                            ;[27f9] c5
                    pop       bc                            ;[27fa] c1
                    dec       bc                            ;[27fb] 0b
                    ld        a,b                           ;[27fc] 78
                    or        c                             ;[27fd] b1
                    jr        nz,$27f9                      ;[27fe] 20 f9
                    ld        a,($6a91)                     ;[2800] 3a 91 6a
                    ld        c,a                           ;[2803] 4f
                    call      $6804                         ;[2804] cd 04 68
                    call      $017e                         ;[2807] cd 7e 01
                    call      $6827                         ;[280a] cd 27 68
                    and       $20                           ;[280d] e6 20
                    ret                                     ;[280f] c9

                    push      af                            ;[2810] f5
                    ld        a,$ff                         ;[2811] 3e ff
                    ld        ($5c8c),a                     ;[2813] 32 8c 5c
                    pop       af                            ;[2816] f1
                    ld        hl,$638b                      ;[2817] 21 8b 63
                    jp        $6585                         ;[281a] c3 85 65
                    dec       c                             ;[281d] 0d
                    nop                                     ;[281e] 00
                    ld        c,c                           ;[281f] 49
                    ld        l,(hl)                        ;[2820] 6e
                    ld        (hl),h                        ;[2821] 74
                    ld        h,l                           ;[2822] 65
                    ld        h,a                           ;[2823] 67
                    ld        (hl),d                        ;[2824] 72
                    ld        h,c                           ;[2825] 61
                    ld        l,h                           ;[2826] 6c
                    jr        nz,$288d                      ;[2827] 20 64
                    ld        l,c                           ;[2829] 69
                    ld        (hl),e                        ;[282a] 73
                    ld        l,e                           ;[282b] 6b
                    jr        nz,$28a2                      ;[282c] 20 74
                    ld        h,l                           ;[282e] 65
                    ld        (hl),e                        ;[282f] 73
                    ld        (hl),h                        ;[2830] 74
                    jr        nz,$2853                      ;[2831] 20 20
                    ld        d,(hl)                        ;[2833] 56
                    ld        sp,$352e                      ;[2834] 31 2e 35
                    dec       c                             ;[2837] 0d
                    nop                                     ;[2838] 00
                    dec       c                             ;[2839] 0d
                    ld        b,(hl)                        ;[283a] 46
                    ld        l,a                           ;[283b] 6f
                    ld        (hl),l                        ;[283c] 75
                    ld        l,(hl)                        ;[283d] 6e
                    ld        h,h                           ;[283e] 64
                    jr        nz,$2885                      ;[283f] 20 44
                    ld        (hl),d                        ;[2841] 72
                    ld        l,c                           ;[2842] 69
                    halt                                    ;[2843] 76
                    ld        h,l                           ;[2844] 65
                    jr        z,$28ba                       ;[2845] 28 73
                    add       hl,hl                         ;[2847] 29
                    jr        nz,$2884                      ;[2848] 20 3a
                    ld        b,c                           ;[284a] 41
                    nop                                     ;[284b] 00
                    ld        c,(hl)                        ;[284c] 4e
                    ld        c,a                           ;[284d] 4f
                    jr        nz,$2894                      ;[284e] 20 44
                    ld        c,c                           ;[2850] 49
                    ld        d,e                           ;[2851] 53
                    ld        c,e                           ;[2852] 4b
                    jr        nz,$289e                      ;[2853] 20 49
                    ld        c,(hl)                        ;[2855] 4e
                    ld        d,h                           ;[2856] 54
                    ld        b,l                           ;[2857] 45
                    ld        d,d                           ;[2858] 52
                    ld        b,(hl)                        ;[2859] 46
                    ld        b,c                           ;[285a] 41
                    ld        b,e                           ;[285b] 43
                    ld        b,l                           ;[285c] 45
                    jr        nz,$2880                      ;[285d] 20 21
                    dec       c                             ;[285f] 0d
                    dec       c                             ;[2860] 0d
                    ld        d,h                           ;[2861] 54
                    ld        h,l                           ;[2862] 65
                    ld        (hl),e                        ;[2863] 73
                    ld        (hl),h                        ;[2864] 74
                    jr        nz,$28a8                      ;[2865] 20 41
                    ld        h,d                           ;[2867] 62
                    ld        l,a                           ;[2868] 6f
                    ld        (hl),d                        ;[2869] 72
                    ld        (hl),h                        ;[286a] 74
                    ld        h,l                           ;[286b] 65
                    ld        h,h                           ;[286c] 64
                    ld        l,$00                         ;[286d] 2e 00
                    dec       c                             ;[286f] 0d
                    ld        d,h                           ;[2870] 54
                    ld        h,l                           ;[2871] 65
                    ld        (hl),e                        ;[2872] 73
                    ld        (hl),h                        ;[2873] 74
                    ld        l,c                           ;[2874] 69
                    ld        l,(hl)                        ;[2875] 6e
                    ld        h,a                           ;[2876] 67
                    jr        nz,$28bd                      ;[2877] 20 44
                    ld        (hl),d                        ;[2879] 72
                    ld        l,c                           ;[287a] 69
                    halt                                    ;[287b] 76
                    ld        h,l                           ;[287c] 65
                    jr        nz,$287f                      ;[287d] 20 00
                    dec       c                             ;[287f] 0d
                    ld        c,c                           ;[2880] 49
                    ld        l,(hl)                        ;[2881] 6e
                    ld        (hl),e                        ;[2882] 73
                    ld        h,l                           ;[2883] 65
                    ld        (hl),d                        ;[2884] 72
                    ld        (hl),h                        ;[2885] 74
                    jr        nz,$28fb                      ;[2886] 20 73
                    ld        l,c                           ;[2888] 69
                    ld        h,h                           ;[2889] 64
                    ld        h,l                           ;[288a] 65
                    jr        nz,$28be                      ;[288b] 20 31
                    jr        nz,$28fe                      ;[288d] 20 6f
                    ld        h,(hl)                        ;[288f] 66
                    jr        nz,$2906                      ;[2890] 20 74
                    ld        h,l                           ;[2892] 65
                    ld        (hl),e                        ;[2893] 73
                    ld        (hl),h                        ;[2894] 74
                    jr        nz,$28fb                      ;[2895] 20 64
                    ld        l,c                           ;[2897] 69
                    ld        (hl),e                        ;[2898] 73
                    ld        l,e                           ;[2899] 6b
                    ld        l,$0d                         ;[289a] 2e 0d
                    nop                                     ;[289c] 00
                    dec       c                             ;[289d] 0d
                    ld        d,d                           ;[289e] 52
                    ld        h,l                           ;[289f] 65
                    ld        l,l                           ;[28a0] 6d
                    ld        l,a                           ;[28a1] 6f
                    halt                                    ;[28a2] 76
                    ld        h,l                           ;[28a3] 65
                    jr        nz,$290a                      ;[28a4] 20 64
                    ld        l,c                           ;[28a6] 69
                    ld        (hl),e                        ;[28a7] 73
                    ld        l,e                           ;[28a8] 6b
                    jr        nz,$2911                      ;[28a9] 20 66
                    ld        (hl),d                        ;[28ab] 72
                    ld        l,a                           ;[28ac] 6f
                    ld        l,l                           ;[28ad] 6d
                    jr        nz,$2914                      ;[28ae] 20 64
                    ld        (hl),d                        ;[28b0] 72
                    ld        l,c                           ;[28b1] 69
                    halt                                    ;[28b2] 76
                    ld        h,l                           ;[28b3] 65
                    dec       c                             ;[28b4] 0d
                    nop                                     ;[28b5] 00
                    dec       c                             ;[28b6] 0d
                    ld        c,c                           ;[28b7] 49
                    ld        l,(hl)                        ;[28b8] 6e
                    ld        (hl),e                        ;[28b9] 73
                    ld        h,l                           ;[28ba] 65
                    ld        (hl),d                        ;[28bb] 72
                    ld        (hl),h                        ;[28bc] 74
                    jr        nz,$2932                      ;[28bd] 20 73
                    ld        l,c                           ;[28bf] 69
                    ld        h,h                           ;[28c0] 64
                    ld        h,l                           ;[28c1] 65
                    jr        nz,$28f6                      ;[28c2] 20 32
                    jr        nz,$2935                      ;[28c4] 20 6f
                    ld        h,(hl)                        ;[28c6] 66
                    jr        nz,$293d                      ;[28c7] 20 74
                    ld        h,l                           ;[28c9] 65
                    ld        (hl),e                        ;[28ca] 73
                    ld        (hl),h                        ;[28cb] 74
                    jr        nz,$2932                      ;[28cc] 20 64
                    ld        l,c                           ;[28ce] 69
                    ld        (hl),e                        ;[28cf] 73
                    ld        l,e                           ;[28d0] 6b
                    ld        l,$00                         ;[28d1] 2e 00
                    dec       c                             ;[28d3] 0d
                    ld        b,h                           ;[28d4] 44
                    ld        l,c                           ;[28d5] 69
                    ld        (hl),e                        ;[28d6] 73
                    ld        l,e                           ;[28d7] 6b
                    jr        nz,$2943                      ;[28d8] 20 69
                    ld        (hl),e                        ;[28da] 73
                    jr        nz,$294b                      ;[28db] 20 6e
                    ld        l,a                           ;[28dd] 6f
                    ld        (hl),h                        ;[28de] 74
                    jr        nz,$2958                      ;[28df] 20 77
                    ld        (hl),d                        ;[28e1] 72
                    ld        l,c                           ;[28e2] 69
                    ld        (hl),h                        ;[28e3] 74
                    ld        h,l                           ;[28e4] 65
                    dec       l                             ;[28e5] 2d
                    ld        (hl),b                        ;[28e6] 70
                    ld        (hl),d                        ;[28e7] 72
                    ld        l,a                           ;[28e8] 6f
                    ld        (hl),h                        ;[28e9] 74
                    ld        h,l                           ;[28ea] 65
                    ld        h,e                           ;[28eb] 63
                    ld        (hl),h                        ;[28ec] 74
                    ld        h,l                           ;[28ed] 65
                    ld        h,h                           ;[28ee] 64
                    ld        l,$0d                         ;[28ef] 2e 0d
                    nop                                     ;[28f1] 00
                    dec       c                             ;[28f2] 0d
                    ld        d,e                           ;[28f3] 53
                    ld        (hl),b                        ;[28f4] 70
                    ld        l,c                           ;[28f5] 69
                    ld        l,(hl)                        ;[28f6] 6e
                    jr        nz,$296d                      ;[28f7] 20 74
                    ld        h,l                           ;[28f9] 65
                    ld        (hl),e                        ;[28fa] 73
                    ld        (hl),h                        ;[28fb] 74
                    jr        nz,$296c                      ;[28fc] 20 6e
                    ld        l,a                           ;[28fe] 6f
                    ld        (hl),h                        ;[28ff] 74
                    jr        nz,$296b                      ;[2900] 20 69
                    ld        l,l                           ;[2902] 6d
                    ld        (hl),b                        ;[2903] 70
                    ld        l,h                           ;[2904] 6c
                    ld        h,l                           ;[2905] 65
                    ld        l,l                           ;[2906] 6d
                    ld        h,l                           ;[2907] 65
                    ld        l,(hl)                        ;[2908] 6e
                    ld        (hl),h                        ;[2909] 74
                    ld        h,l                           ;[290a] 65
                    ld        h,h                           ;[290b] 64
                    ld        l,$0d                         ;[290c] 2e 0d
                    nop                                     ;[290e] 00
                    dec       c                             ;[290f] 0d
                    ld        b,e                           ;[2910] 43
                    ld        l,b                           ;[2911] 68
                    ld        h,l                           ;[2912] 65
                    ld        h,e                           ;[2913] 63
                    ld        l,e                           ;[2914] 6b
                    ld        l,c                           ;[2915] 69
                    ld        l,(hl)                        ;[2916] 6e
                    ld        h,a                           ;[2917] 67
                    jr        nz,$295e                      ;[2918] 20 44
                    ld        h,c                           ;[291a] 61
                    ld        (hl),h                        ;[291b] 74
                    ld        h,c                           ;[291c] 61
                    ld        l,$0d                         ;[291d] 2e 0d
                    nop                                     ;[291f] 00
                    dec       c                             ;[2920] 0d
                    ld        d,e                           ;[2921] 53
                    ld        (hl),h                        ;[2922] 74
                    ld        h,c                           ;[2923] 61
                    ld        (hl),d                        ;[2924] 72
                    ld        (hl),h                        ;[2925] 74
                    ld        l,c                           ;[2926] 69
                    ld        l,(hl)                        ;[2927] 6e
                    ld        h,a                           ;[2928] 67
                    jr        nz,$299e                      ;[2929] 20 73
                    ld        (hl),b                        ;[292b] 70
                    ld        l,c                           ;[292c] 69
                    ld        l,(hl)                        ;[292d] 6e
                    dec       l                             ;[292e] 2d
                    ld        (hl),e                        ;[292f] 73
                    ld        (hl),b                        ;[2930] 70
                    ld        h,l                           ;[2931] 65
                    ld        h,l                           ;[2932] 65
                    ld        h,h                           ;[2933] 64
                    jr        nz,$29aa                      ;[2934] 20 74
                    ld        h,l                           ;[2936] 65
                    ld        (hl),e                        ;[2937] 73
                    ld        (hl),h                        ;[2938] 74
                    ld        l,$0d                         ;[2939] 2e 0d
                    nop                                     ;[293b] 00
                    dec       c                             ;[293c] 0d
                    ld        b,h                           ;[293d] 44
                    ld        h,c                           ;[293e] 61
                    ld        (hl),h                        ;[293f] 74
                    ld        h,c                           ;[2940] 61
                    jr        nz,$29b1                      ;[2941] 20 6e
                    ld        l,a                           ;[2943] 6f
                    ld        (hl),h                        ;[2944] 74
                    jr        nz,$29b9                      ;[2945] 20 72
                    ld        h,l                           ;[2947] 65
                    ld        h,c                           ;[2948] 61
                    ld        h,h                           ;[2949] 64
                    jr        nz,$29af                      ;[294a] 20 63
                    ld        l,a                           ;[294c] 6f
                    ld        (hl),d                        ;[294d] 72
                    ld        (hl),d                        ;[294e] 72
                    ld        h,l                           ;[294f] 65
                    ld        h,e                           ;[2950] 63
                    ld        (hl),h                        ;[2951] 74
                    ld        l,h                           ;[2952] 6c
                    ld        a,c                           ;[2953] 79
                    ld        l,$0d                         ;[2954] 2e 0d
                    nop                                     ;[2956] 00
                    dec       c                             ;[2957] 0d
                    ld        d,b                           ;[2958] 50
                    ld        (hl),d                        ;[2959] 72
                    ld        h,l                           ;[295a] 65
                    ld        (hl),e                        ;[295b] 73
                    ld        (hl),e                        ;[295c] 73
                    jr        nz,$29b1                      ;[295d] 20 52
                    jr        nz,$29d5                      ;[295f] 20 74
                    ld        l,a                           ;[2961] 6f
                    jr        nz,$29d6                      ;[2962] 20 72
                    ld        h,l                           ;[2964] 65
                    ld        (hl),b                        ;[2965] 70
                    ld        h,l                           ;[2966] 65
                    ld        h,c                           ;[2967] 61
                    ld        (hl),h                        ;[2968] 74
                    inc       l                             ;[2969] 2c
                    jr        nz,$29bd                      ;[296a] 20 51
                    jr        nz,$29e2                      ;[296c] 20 74
                    ld        l,a                           ;[296e] 6f
                    jr        nz,$29e2                      ;[296f] 20 71
                    ld        (hl),l                        ;[2971] 75
                    ld        l,c                           ;[2972] 69
                    ld        (hl),h                        ;[2973] 74
                    dec       c                             ;[2974] 0d
                    nop                                     ;[2975] 00
                    ld        b,(hl)                        ;[2976] 46
                    ld        l,a                           ;[2977] 6f
                    ld        (hl),d                        ;[2978] 72
                    ld        l,l                           ;[2979] 6d
                    ld        h,c                           ;[297a] 61
                    ld        (hl),h                        ;[297b] 74
                    ld        (hl),h                        ;[297c] 74
                    ld        l,c                           ;[297d] 69
                    ld        l,(hl)                        ;[297e] 6e
                    ld        h,a                           ;[297f] 67
                    jr        nz,$29f6                      ;[2980] 20 74
                    ld        (hl),d                        ;[2982] 72
                    ld        h,c                           ;[2983] 61
                    ld        h,e                           ;[2984] 63
                    ld        l,e                           ;[2985] 6b
                    ld        (hl),e                        ;[2986] 73
                    ld        l,$0d                         ;[2987] 2e 0d
                    nop                                     ;[2989] 00
                    ld        d,e                           ;[298a] 53
                    ld        (hl),b                        ;[298b] 70
                    ld        l,c                           ;[298c] 69
                    ld        l,(hl)                        ;[298d] 6e
                    jr        nz,$2a03                      ;[298e] 20 73
                    ld        (hl),b                        ;[2990] 70
                    ld        h,l                           ;[2991] 65
                    ld        h,l                           ;[2992] 65
                    ld        h,h                           ;[2993] 64
                    jr        nz,$29fa                      ;[2994] 20 64
                    ld        l,c                           ;[2996] 69
                    ld        h,(hl)                        ;[2997] 66
                    ld        h,(hl)                        ;[2998] 66
                    ld        h,l                           ;[2999] 65
                    ld        (hl),d                        ;[299a] 72
                    ld        h,l                           ;[299b] 65
                    ld        l,(hl)                        ;[299c] 6e
                    ld        h,e                           ;[299d] 63
                    ld        h,l                           ;[299e] 65
                    jr        nz,$29cb                      ;[299f] 20 2a
                    jr        nc,$29d3                      ;[29a1] 30 30
                    jr        nz,$29ca                      ;[29a3] 20 25
                    dec       c                             ;[29a5] 0d
                    nop                                     ;[29a6] 00
                    dec       c                             ;[29a7] 0d
                    ld        d,e                           ;[29a8] 53
                    ld        (hl),b                        ;[29a9] 70
                    ld        l,c                           ;[29aa] 69
                    ld        l,(hl)                        ;[29ab] 6e
                    jr        nz,$2a21                      ;[29ac] 20 73
                    ld        (hl),b                        ;[29ae] 70
                    ld        h,l                           ;[29af] 65
                    ld        h,l                           ;[29b0] 65
                    ld        h,h                           ;[29b1] 64
                    jr        nz,$2a1d                      ;[29b2] 20 69
                    ld        (hl),e                        ;[29b4] 73
                    jr        nz,$2a00                      ;[29b5] 20 49
                    ld        c,(hl)                        ;[29b7] 4e
                    ld        b,e                           ;[29b8] 43
                    ld        c,a                           ;[29b9] 4f
                    ld        d,d                           ;[29ba] 52
                    ld        d,d                           ;[29bb] 52
                    ld        b,l                           ;[29bc] 45
                    ld        b,e                           ;[29bd] 43
                    ld        d,h                           ;[29be] 54
                    jr        nz,$29e2                      ;[29bf] 20 21
                    dec       c                             ;[29c1] 0d
                    nop                                     ;[29c2] 00
                    dec       c                             ;[29c3] 0d
                    ld        d,h                           ;[29c4] 54
                    ld        l,b                           ;[29c5] 68
                    ld        h,l                           ;[29c6] 65
                    jr        nz,$2a2d                      ;[29c7] 20 64
                    ld        l,c                           ;[29c9] 69
                    ld        (hl),e                        ;[29ca] 73
                    ld        l,e                           ;[29cb] 6b
                    jr        nz,$2a32                      ;[29cc] 20 64
                    ld        (hl),d                        ;[29ce] 72
                    ld        l,c                           ;[29cf] 69
                    halt                                    ;[29d0] 76
                    ld        h,l                           ;[29d1] 65
                    jr        nz,$2a48                      ;[29d2] 20 74
                    ld        h,l                           ;[29d4] 65
                    ld        (hl),e                        ;[29d5] 73
                    ld        (hl),h                        ;[29d6] 74
                    jr        nz,$2a49                      ;[29d7] 20 70
                    ld        h,c                           ;[29d9] 61
                    ld        (hl),e                        ;[29da] 73
                    ld        (hl),e                        ;[29db] 73
                    ld        h,l                           ;[29dc] 65
                    ld        h,h                           ;[29dd] 64
                    ld        l,$00                         ;[29de] 2e 00
                    dec       c                             ;[29e0] 0d
                    ld        d,h                           ;[29e1] 54
                    ld        l,b                           ;[29e2] 68
                    ld        h,l                           ;[29e3] 65
                    jr        nz,$2a4a                      ;[29e4] 20 64
                    ld        l,c                           ;[29e6] 69
                    ld        (hl),e                        ;[29e7] 73
                    ld        l,e                           ;[29e8] 6b
                    jr        nz,$2a4f                      ;[29e9] 20 64
                    ld        (hl),d                        ;[29eb] 72
                    ld        l,c                           ;[29ec] 69
                    halt                                    ;[29ed] 76
                    ld        h,l                           ;[29ee] 65
                    jr        nz,$2a65                      ;[29ef] 20 74
                    ld        h,l                           ;[29f1] 65
                    ld        (hl),e                        ;[29f2] 73
                    ld        (hl),h                        ;[29f3] 74
                    jr        nz,$2a5c                      ;[29f4] 20 66
                    ld        h,c                           ;[29f6] 61
                    ld        l,c                           ;[29f7] 69
                    ld        l,h                           ;[29f8] 6c
                    ld        h,l                           ;[29f9] 65
                    ld        h,h                           ;[29fa] 64
                    ld        l,$00                         ;[29fb] 2e 00
                    push      af                            ;[29fd] f5
                    ld        a,$16                         ;[29fe] 3e 16
                    rst       $10                           ;[2a00] d7
                    xor       a                             ;[2a01] af
                    rst       $10                           ;[2a02] d7
                    xor       a                             ;[2a03] af
                    rst       $10                           ;[2a04] d7
                    pop       af                            ;[2a05] f1
                    ret                                     ;[2a06] c9

                    ld        hl,$659d                      ;[2a07] 21 9d 65
                    cp        $0a                           ;[2a0a] fe 0a
                    jr        c,$2a10                       ;[2a0c] 38 02
                    sub       $0a                           ;[2a0e] d6 0a
                    call      $6585                         ;[2a10] cd 85 65
                    ld        a,$0d                         ;[2a13] 3e 0d
                    rst       $10                           ;[2a15] d7
                    ret                                     ;[2a16] c9

                    ld        b,a                           ;[2a17] 47
                    inc       b                             ;[2a18] 04
                    xor       a                             ;[2a19] af
                    push      hl                            ;[2a1a] e5
                    pop       de                            ;[2a1b] d1
                    cp        (hl)                          ;[2a1c] be
                    inc       hl                            ;[2a1d] 23
                    jr        nz,$2a1c                      ;[2a1e] 20 fc
                    djnz      $2a1a                         ;[2a20] 10 f8
                    ex        de,hl                         ;[2a22] eb
                    ld        a,(hl)                        ;[2a23] 7e
                    inc       hl                            ;[2a24] 23
                    or        a                             ;[2a25] b7
                    ret       z                             ;[2a26] c8
                    cp        $ff                           ;[2a27] fe ff
                    ret       z                             ;[2a29] c8
                    rst       $28                           ;[2a2a] ef
                    djnz      $2a2d                         ;[2a2b] 10 00
                    jr        $2a23                         ;[2a2d] 18 f4
                    ld        b,h                           ;[2a2f] 44
                    ld        (hl),d                        ;[2a30] 72
                    ld        l,c                           ;[2a31] 69
                    halt                                    ;[2a32] 76
                    ld        h,l                           ;[2a33] 65
                    jr        nz,$2aa4                      ;[2a34] 20 6e
                    ld        l,a                           ;[2a36] 6f
                    ld        (hl),h                        ;[2a37] 74
                    jr        nz,$2aac                      ;[2a38] 20 72
                    ld        h,l                           ;[2a3a] 65
                    ld        h,c                           ;[2a3b] 61
                    ld        h,h                           ;[2a3c] 64
                    ld        a,c                           ;[2a3d] 79
                    ld        l,$00                         ;[2a3e] 2e 00
                    ld        b,h                           ;[2a40] 44
                    ld        l,c                           ;[2a41] 69
                    ld        (hl),e                        ;[2a42] 73
                    ld        h,e                           ;[2a43] 63
                    jr        nz,$2aaf                      ;[2a44] 20 69
                    ld        (hl),e                        ;[2a46] 73
                    jr        nz,$2ac0                      ;[2a47] 20 77
                    ld        (hl),d                        ;[2a49] 72
                    ld        l,c                           ;[2a4a] 69
                    ld        (hl),h                        ;[2a4b] 74
                    ld        h,l                           ;[2a4c] 65
                    jr        nz,$2abf                      ;[2a4d] 20 70
                    ld        (hl),d                        ;[2a4f] 72
                    ld        l,a                           ;[2a50] 6f
                    ld        (hl),h                        ;[2a51] 74
                    ld        h,l                           ;[2a52] 65
                    ld        h,e                           ;[2a53] 63
                    ld        (hl),h                        ;[2a54] 74
                    ld        h,l                           ;[2a55] 65
                    ld        h,h                           ;[2a56] 64
                    ld        l,$00                         ;[2a57] 2e 00
                    ld        b,h                           ;[2a59] 44
                    ld        l,c                           ;[2a5a] 69
                    ld        (hl),e                        ;[2a5b] 73
                    ld        h,e                           ;[2a5c] 63
                    jr        nz,$2ad2                      ;[2a5d] 20 73
                    ld        h,l                           ;[2a5f] 65
                    ld        h,l                           ;[2a60] 65
                    ld        l,e                           ;[2a61] 6b
                    jr        nz,$2aca                      ;[2a62] 20 66
                    ld        h,c                           ;[2a64] 61
                    ld        l,c                           ;[2a65] 69
                    ld        l,h                           ;[2a66] 6c
                    ld        l,$00                         ;[2a67] 2e 00
                    ld        b,h                           ;[2a69] 44
                    ld        l,c                           ;[2a6a] 69
                    ld        (hl),e                        ;[2a6b] 73
                    ld        h,e                           ;[2a6c] 63
                    jr        nz,$2ab2                      ;[2a6d] 20 43
                    ld        d,d                           ;[2a6f] 52
                    ld        b,e                           ;[2a70] 43
                    jr        nz,$2ad7                      ;[2a71] 20 64
                    ld        h,c                           ;[2a73] 61
                    ld        (hl),h                        ;[2a74] 74
                    ld        h,c                           ;[2a75] 61
                    jr        nz,$2add                      ;[2a76] 20 65
                    ld        (hl),d                        ;[2a78] 72
                    ld        (hl),d                        ;[2a79] 72
                    ld        l,a                           ;[2a7a] 6f
                    ld        (hl),d                        ;[2a7b] 72
                    ld        l,$00                         ;[2a7c] 2e 00
                    ld        c,(hl)                        ;[2a7e] 4e
                    ld        l,a                           ;[2a7f] 6f
                    jr        nz,$2ae6                      ;[2a80] 20 64
                    ld        h,c                           ;[2a82] 61
                    ld        (hl),h                        ;[2a83] 74
                    ld        h,c                           ;[2a84] 61
                    jr        nz,$2af6                      ;[2a85] 20 6f
                    ld        l,(hl)                        ;[2a87] 6e
                    jr        nz,$2aee                      ;[2a88] 20 64
                    ld        l,c                           ;[2a8a] 69
                    ld        (hl),e                        ;[2a8b] 73
                    ld        h,e                           ;[2a8c] 63
                    ld        l,$00                         ;[2a8d] 2e 00
                    ld        b,c                           ;[2a8f] 41
                    ld        h,h                           ;[2a90] 64
                    ld        h,h                           ;[2a91] 64
                    ld        (hl),d                        ;[2a92] 72
                    ld        h,l                           ;[2a93] 65
                    ld        (hl),e                        ;[2a94] 73
                    ld        (hl),e                        ;[2a95] 73
                    jr        nz,$2b05                      ;[2a96] 20 6d
                    ld        h,c                           ;[2a98] 61
                    ld        (hl),d                        ;[2a99] 72
                    ld        l,e                           ;[2a9a] 6b
                    jr        nz,$2b0a                      ;[2a9b] 20 6d
                    ld        l,c                           ;[2a9d] 69
                    ld        (hl),e                        ;[2a9e] 73
                    ld        (hl),e                        ;[2a9f] 73
                    ld        l,c                           ;[2aa0] 69
                    ld        l,(hl)                        ;[2aa1] 6e
                    ld        h,a                           ;[2aa2] 67
                    ld        l,$00                         ;[2aa3] 2e 00
                    ld        d,l                           ;[2aa5] 55
                    ld        l,(hl)                        ;[2aa6] 6e
                    ld        (hl),d                        ;[2aa7] 72
                    ld        h,l                           ;[2aa8] 65
                    ld        h,e                           ;[2aa9] 63
                    ld        l,a                           ;[2aaa] 6f
                    ld        h,a                           ;[2aab] 67
                    ld        l,(hl)                        ;[2aac] 6e
                    ld        l,c                           ;[2aad] 69
                    ld        a,d                           ;[2aae] 7a
                    ld        h,l                           ;[2aaf] 65
                    ld        h,h                           ;[2ab0] 64
                    jr        nz,$2b17                      ;[2ab1] 20 64
                    ld        l,c                           ;[2ab3] 69
                    ld        (hl),e                        ;[2ab4] 73
                    ld        h,e                           ;[2ab5] 63
                    jr        nz,$2b1e                      ;[2ab6] 20 66
                    ld        l,a                           ;[2ab8] 6f
                    ld        (hl),d                        ;[2ab9] 72
                    ld        l,l                           ;[2aba] 6d
                    ld        h,c                           ;[2abb] 61
                    ld        (hl),h                        ;[2abc] 74
                    ld        l,$00                         ;[2abd] 2e 00
                    ld        d,l                           ;[2abf] 55
                    ld        l,(hl)                        ;[2ac0] 6e
                    ld        l,e                           ;[2ac1] 6b
                    ld        l,(hl)                        ;[2ac2] 6e
                    ld        l,a                           ;[2ac3] 6f
                    ld        (hl),a                        ;[2ac4] 77
                    ld        l,(hl)                        ;[2ac5] 6e
                    jr        nz,$2b2c                      ;[2ac6] 20 64
                    ld        l,c                           ;[2ac8] 69
                    ld        (hl),e                        ;[2ac9] 73
                    ld        h,e                           ;[2aca] 63
                    jr        nz,$2b32                      ;[2acb] 20 65
                    ld        (hl),d                        ;[2acd] 72
                    ld        (hl),d                        ;[2ace] 72
                    ld        l,a                           ;[2acf] 6f
                    ld        (hl),d                        ;[2ad0] 72
                    ld        l,$20                         ;[2ad1] 2e 20
                    dec       l                             ;[2ad3] 2d
                    jr        nz,$2b2d                      ;[2ad4] 20 57
                    ld        h,l                           ;[2ad6] 65
                    ld        l,h                           ;[2ad7] 6c
                    ld        l,h                           ;[2ad8] 6c
                    jr        nz,$2b3f                      ;[2ad9] 20 64
                    ld        l,a                           ;[2adb] 6f
                    ld        l,(hl)                        ;[2adc] 6e
                    ld        h,l                           ;[2add] 65
                    jr        nz,$2b01                      ;[2ade] 20 21
                    nop                                     ;[2ae0] 00
                    ld        b,h                           ;[2ae1] 44
                    ld        l,c                           ;[2ae2] 69
                    ld        (hl),e                        ;[2ae3] 73
                    ld        h,e                           ;[2ae4] 63
                    jr        nz,$2b4a                      ;[2ae5] 20 63
                    ld        l,b                           ;[2ae7] 68
                    ld        h,c                           ;[2ae8] 61
                    ld        l,(hl)                        ;[2ae9] 6e
                    ld        h,a                           ;[2aea] 67
                    ld        h,l                           ;[2aeb] 65
                    ld        h,h                           ;[2aec] 64
                    jr        nz,$2b66                      ;[2aed] 20 77
                    ld        l,b                           ;[2aef] 68
                    ld        l,c                           ;[2af0] 69
                    ld        l,h                           ;[2af1] 6c
                    ld        h,l                           ;[2af2] 65
                    jr        nz,$2b5e                      ;[2af3] 20 69
                    ld        l,(hl)                        ;[2af5] 6e
                    jr        nz,$2b6d                      ;[2af6] 20 75
                    ld        (hl),e                        ;[2af8] 73
                    ld        h,l                           ;[2af9] 65
                    ld        l,$00                         ;[2afa] 2e 00
                    ld        d,a                           ;[2afc] 57
                    ld        (hl),d                        ;[2afd] 72
                    ld        l,a                           ;[2afe] 6f
                    ld        l,(hl)                        ;[2aff] 6e
                    ld        h,a                           ;[2b00] 67
                    jr        nz,$2b77                      ;[2b01] 20 74
                    ld        a,c                           ;[2b03] 79
                    ld        (hl),b                        ;[2b04] 70
                    ld        h,l                           ;[2b05] 65
                    jr        nz,$2b77                      ;[2b06] 20 6f
                    ld        h,(hl)                        ;[2b08] 66
                    jr        nz,$2b6f                      ;[2b09] 20 64
                    ld        l,c                           ;[2b0b] 69
                    ld        (hl),e                        ;[2b0c] 73
                    ld        h,e                           ;[2b0d] 63
                    jr        nz,$2b79                      ;[2b0e] 20 69
                    ld        l,(hl)                        ;[2b10] 6e
                    jr        nz,$2b77                      ;[2b11] 20 64
                    ld        (hl),d                        ;[2b13] 72
                    ld        l,c                           ;[2b14] 69
                    halt                                    ;[2b15] 76
                    ld        h,l                           ;[2b16] 65
                    ld        l,$00                         ;[2b17] 2e 00
                    ld        b,d                           ;[2b19] 42
                    ld        h,c                           ;[2b1a] 61
                    ld        h,h                           ;[2b1b] 64
                    jr        nz,$2b84                      ;[2b1c] 20 66
                    ld        l,c                           ;[2b1e] 69
                    ld        l,h                           ;[2b1f] 6c
                    ld        h,l                           ;[2b20] 65
                    ld        l,(hl)                        ;[2b21] 6e
                    ld        h,c                           ;[2b22] 61
                    ld        l,l                           ;[2b23] 6d
                    ld        h,l                           ;[2b24] 65
                    ld        l,$00                         ;[2b25] 2e 00
                    ld        b,d                           ;[2b27] 42
                    ld        h,c                           ;[2b28] 61
                    ld        h,h                           ;[2b29] 64
                    jr        nz,$2b9c                      ;[2b2a] 20 70
                    ld        h,c                           ;[2b2c] 61
                    ld        (hl),d                        ;[2b2d] 72
                    ld        h,c                           ;[2b2e] 61
                    ld        l,l                           ;[2b2f] 6d
                    ld        h,l                           ;[2b30] 65
                    ld        (hl),h                        ;[2b31] 74
                    ld        h,l                           ;[2b32] 65
                    ld        (hl),d                        ;[2b33] 72
                    ld        l,$00                         ;[2b34] 2e 00
                    ld        b,h                           ;[2b36] 44
                    ld        (hl),d                        ;[2b37] 72
                    ld        l,c                           ;[2b38] 69
                    halt                                    ;[2b39] 76
                    ld        h,l                           ;[2b3a] 65
                    jr        nz,$2bab                      ;[2b3b] 20 6e
                    ld        l,a                           ;[2b3d] 6f
                    ld        (hl),h                        ;[2b3e] 74
                    jr        nz,$2ba7                      ;[2b3f] 20 66
                    ld        l,a                           ;[2b41] 6f
                    ld        (hl),l                        ;[2b42] 75
                    ld        l,(hl)                        ;[2b43] 6e
                    ld        h,h                           ;[2b44] 64
                    ld        l,$00                         ;[2b45] 2e 00
                    ld        b,(hl)                        ;[2b47] 46
                    ld        l,c                           ;[2b48] 69
                    ld        l,h                           ;[2b49] 6c
                    ld        h,l                           ;[2b4a] 65
                    jr        nz,$2bbb                      ;[2b4b] 20 6e
                    ld        l,a                           ;[2b4d] 6f
                    ld        (hl),h                        ;[2b4e] 74
                    jr        nz,$2bb7                      ;[2b4f] 20 66
                    ld        l,a                           ;[2b51] 6f
                    ld        (hl),l                        ;[2b52] 75
                    ld        l,(hl)                        ;[2b53] 6e
                    ld        h,h                           ;[2b54] 64
                    ld        l,$00                         ;[2b55] 2e 00
                    ld        b,(hl)                        ;[2b57] 46
                    ld        l,c                           ;[2b58] 69
                    ld        l,h                           ;[2b59] 6c
                    ld        h,l                           ;[2b5a] 65
                    jr        nz,$2bbe                      ;[2b5b] 20 61
                    ld        l,h                           ;[2b5d] 6c
                    ld        (hl),d                        ;[2b5e] 72
                    ld        h,l                           ;[2b5f] 65
                    ld        h,c                           ;[2b60] 61
                    ld        h,h                           ;[2b61] 64
                    ld        a,c                           ;[2b62] 79
                    jr        nz,$2bca                      ;[2b63] 20 65
                    ld        a,b                           ;[2b65] 78
                    ld        l,c                           ;[2b66] 69
                    ld        (hl),e                        ;[2b67] 73
                    ld        (hl),h                        ;[2b68] 74
                    ld        (hl),e                        ;[2b69] 73
                    ld        l,$00                         ;[2b6a] 2e 00
                    ld        b,l                           ;[2b6c] 45
                    ld        l,(hl)                        ;[2b6d] 6e
                    ld        h,h                           ;[2b6e] 64
                    jr        nz,$2be0                      ;[2b6f] 20 6f
                    ld        h,(hl)                        ;[2b71] 66
                    jr        nz,$2bda                      ;[2b72] 20 66
                    ld        l,c                           ;[2b74] 69
                    ld        l,h                           ;[2b75] 6c
                    ld        h,l                           ;[2b76] 65
                    ld        l,$00                         ;[2b77] 2e 00
                    ld        b,h                           ;[2b79] 44
                    ld        l,c                           ;[2b7a] 69
                    ld        (hl),e                        ;[2b7b] 73
                    ld        h,e                           ;[2b7c] 63
                    jr        nz,$2be5                      ;[2b7d] 20 66
                    ld        (hl),l                        ;[2b7f] 75
                    ld        l,h                           ;[2b80] 6c
                    ld        l,h                           ;[2b81] 6c
                    ld        l,$00                         ;[2b82] 2e 00
                    ld        b,h                           ;[2b84] 44
                    ld        l,c                           ;[2b85] 69
                    ld        (hl),d                        ;[2b86] 72
                    ld        h,l                           ;[2b87] 65
                    ld        h,e                           ;[2b88] 63
                    ld        (hl),h                        ;[2b89] 74
                    ld        l,a                           ;[2b8a] 6f
                    ld        (hl),d                        ;[2b8b] 72
                    ld        a,c                           ;[2b8c] 79
                    jr        nz,$2bf5                      ;[2b8d] 20 66
                    ld        (hl),l                        ;[2b8f] 75
                    ld        l,h                           ;[2b90] 6c
                    ld        l,h                           ;[2b91] 6c
                    ld        l,$00                         ;[2b92] 2e 00
                    ld        d,d                           ;[2b94] 52
                    ld        h,l                           ;[2b95] 65
                    ld        h,c                           ;[2b96] 61
                    ld        h,h                           ;[2b97] 64
                    dec       l                             ;[2b98] 2d
                    ld        l,a                           ;[2b99] 6f
                    ld        l,(hl)                        ;[2b9a] 6e
                    ld        l,h                           ;[2b9b] 6c
                    ld        a,c                           ;[2b9c] 79
                    jr        nz,$2c05                      ;[2b9d] 20 66
                    ld        l,c                           ;[2b9f] 69
                    ld        l,h                           ;[2ba0] 6c
                    ld        h,l                           ;[2ba1] 65
                    ld        l,$00                         ;[2ba2] 2e 00
                    ld        b,(hl)                        ;[2ba4] 46
                    ld        l,c                           ;[2ba5] 69
                    ld        l,h                           ;[2ba6] 6c
                    ld        h,l                           ;[2ba7] 65
                    jr        nz,$2c18                      ;[2ba8] 20 6e
                    ld        l,a                           ;[2baa] 6f
                    ld        (hl),h                        ;[2bab] 74
                    jr        nz,$2c1d                      ;[2bac] 20 6f
                    ld        (hl),b                        ;[2bae] 70
                    ld        h,l                           ;[2baf] 65
                    ld        l,(hl)                        ;[2bb0] 6e
                    inc       l                             ;[2bb1] 2c
                    jr        nz,$2c23                      ;[2bb2] 20 6f
                    ld        (hl),d                        ;[2bb4] 72
                    jr        nz,$2c2e                      ;[2bb5] 20 77
                    ld        (hl),d                        ;[2bb7] 72
                    ld        l,a                           ;[2bb8] 6f
                    ld        l,(hl)                        ;[2bb9] 6e
                    ld        h,a                           ;[2bba] 67
                    jr        nz,$2c1e                      ;[2bbb] 20 61
                    ld        h,e                           ;[2bbd] 63
                    ld        h,e                           ;[2bbe] 63
                    ld        h,l                           ;[2bbf] 65
                    ld        (hl),e                        ;[2bc0] 73
                    ld        (hl),e                        ;[2bc1] 73
                    ld        l,$00                         ;[2bc2] 2e 00
                    ld        b,c                           ;[2bc4] 41
                    ld        h,e                           ;[2bc5] 63
                    ld        h,e                           ;[2bc6] 63
                    ld        h,l                           ;[2bc7] 65
                    ld        (hl),e                        ;[2bc8] 73
                    ld        (hl),e                        ;[2bc9] 73
                    jr        nz,$2c30                      ;[2bca] 20 64
                    ld        h,l                           ;[2bcc] 65
                    ld        l,(hl)                        ;[2bcd] 6e
                    ld        l,c                           ;[2bce] 69
                    ld        h,l                           ;[2bcf] 65
                    ld        h,h                           ;[2bd0] 64
                    inc       l                             ;[2bd1] 2c
                    jr        nz,$2c3a                      ;[2bd2] 20 66
                    ld        l,c                           ;[2bd4] 69
                    ld        l,h                           ;[2bd5] 6c
                    ld        h,l                           ;[2bd6] 65
                    jr        nz,$2c42                      ;[2bd7] 20 69
                    ld        l,(hl)                        ;[2bd9] 6e
                    jr        nz,$2c51                      ;[2bda] 20 75
                    ld        (hl),e                        ;[2bdc] 73
                    ld        h,l                           ;[2bdd] 65
                    ld        l,$00                         ;[2bde] 2e 00
                    ld        b,e                           ;[2be0] 43
                    ld        h,c                           ;[2be1] 61
                    ld        l,(hl)                        ;[2be2] 6e
                    ld        l,(hl)                        ;[2be3] 6e
                    ld        l,a                           ;[2be4] 6f
                    ld        (hl),h                        ;[2be5] 74
                    jr        nz,$2c5a                      ;[2be6] 20 72
                    ld        h,l                           ;[2be8] 65
                    ld        l,(hl)                        ;[2be9] 6e
                    ld        h,c                           ;[2bea] 61
                    ld        l,l                           ;[2beb] 6d
                    ld        h,l                           ;[2bec] 65
                    jr        nz,$2c51                      ;[2bed] 20 62
                    ld        h,l                           ;[2bef] 65
                    ld        (hl),h                        ;[2bf0] 74
                    ld        (hl),a                        ;[2bf1] 77
                    ld        h,l                           ;[2bf2] 65
                    ld        h,l                           ;[2bf3] 65
                    ld        l,(hl)                        ;[2bf4] 6e
                    jr        nz,$2c5b                      ;[2bf5] 20 64
                    ld        (hl),d                        ;[2bf7] 72
                    ld        l,c                           ;[2bf8] 69
                    halt                                    ;[2bf9] 76
                    ld        h,l                           ;[2bfa] 65
                    ld        (hl),e                        ;[2bfb] 73
                    nop                                     ;[2bfc] 00
                    ld        b,l                           ;[2bfd] 45
                    ld        a,b                           ;[2bfe] 78
                    ld        (hl),h                        ;[2bff] 74
                    ld        h,l                           ;[2c00] 65
                    ld        l,(hl)                        ;[2c01] 6e
                    ld        (hl),h                        ;[2c02] 74
                    jr        nz,$2c6e                      ;[2c03] 20 69
                    ld        (hl),e                        ;[2c05] 73
                    jr        nz,$2c75                      ;[2c06] 20 6d
                    ld        l,c                           ;[2c08] 69
                    ld        (hl),e                        ;[2c09] 73
                    ld        (hl),e                        ;[2c0a] 73
                    ld        l,c                           ;[2c0b] 69
                    ld        l,(hl)                        ;[2c0c] 6e
                    ld        h,a                           ;[2c0d] 67
                    ld        l,$00                         ;[2c0e] 2e 00
                    ld        d,l                           ;[2c10] 55
                    ld        l,(hl)                        ;[2c11] 6e
                    ld        h,e                           ;[2c12] 63
                    ld        h,c                           ;[2c13] 61
                    ld        h,e                           ;[2c14] 63
                    ld        l,b                           ;[2c15] 68
                    ld        h,l                           ;[2c16] 65
                    ld        h,h                           ;[2c17] 64
                    ld        l,$20                         ;[2c18] 2e 20
                    jr        z,$2c8f                       ;[2c1a] 28 73
                    ld        l,a                           ;[2c1c] 6f
                    ld        h,(hl)                        ;[2c1d] 66
                    ld        (hl),h                        ;[2c1e] 74
                    ld        (hl),a                        ;[2c1f] 77
                    ld        h,c                           ;[2c20] 61
                    ld        (hl),d                        ;[2c21] 72
                    ld        h,l                           ;[2c22] 65
                    jr        nz,$2c8a                      ;[2c23] 20 65
                    ld        (hl),d                        ;[2c25] 72
                    ld        (hl),d                        ;[2c26] 72
                    ld        l,a                           ;[2c27] 6f
                    ld        (hl),d                        ;[2c28] 72
                    add       hl,hl                         ;[2c29] 29
                    nop                                     ;[2c2a] 00
                    ld        b,(hl)                        ;[2c2b] 46
                    ld        l,c                           ;[2c2c] 69
                    ld        l,h                           ;[2c2d] 6c
                    ld        h,l                           ;[2c2e] 65
                    jr        nz,$2ca5                      ;[2c2f] 20 74
                    ld        l,a                           ;[2c31] 6f
                    ld        l,a                           ;[2c32] 6f
                    jr        nz,$2c97                      ;[2c33] 20 62
                    ld        l,c                           ;[2c35] 69
                    ld        h,a                           ;[2c36] 67
                    ld        l,$00                         ;[2c37] 2e 00
                    ld        b,h                           ;[2c39] 44
                    ld        l,c                           ;[2c3a] 69
                    ld        (hl),e                        ;[2c3b] 73
                    ld        h,e                           ;[2c3c] 63
                    jr        nz,$2cad                      ;[2c3d] 20 6e
                    ld        l,a                           ;[2c3f] 6f
                    ld        (hl),h                        ;[2c40] 74
                    jr        nz,$2ca5                      ;[2c41] 20 62
                    ld        l,a                           ;[2c43] 6f
                    ld        l,a                           ;[2c44] 6f
                    ld        (hl),h                        ;[2c45] 74
                    ld        h,c                           ;[2c46] 61
                    ld        h,d                           ;[2c47] 62
                    ld        l,h                           ;[2c48] 6c
                    ld        h,l                           ;[2c49] 65
                    nop                                     ;[2c4a] 00
                    ld        b,h                           ;[2c4b] 44
                    ld        (hl),d                        ;[2c4c] 72
                    ld        l,c                           ;[2c4d] 69
                    halt                                    ;[2c4e] 76
                    ld        h,l                           ;[2c4f] 65
                    jr        nz,$2cc9                      ;[2c50] 20 77
                    ld        h,c                           ;[2c52] 61
                    ld        (hl),e                        ;[2c53] 73
                    jr        nz,$2cbf                      ;[2c54] 20 69
                    ld        l,(hl)                        ;[2c56] 6e
                    jr        nz,$2cce                      ;[2c57] 20 75
                    ld        (hl),e                        ;[2c59] 73
                    ld        h,l                           ;[2c5a] 65
                    ld        l,$00                         ;[2c5b] 2e 00
                    ld        e,c                           ;[2c5d] 59
                    ld        l,a                           ;[2c5e] 6f
                    ld        (hl),l                        ;[2c5f] 75
                    jr        nz,$2cd5                      ;[2c60] 20 73
                    ld        l,b                           ;[2c62] 68
                    ld        l,a                           ;[2c63] 6f
                    ld        (hl),l                        ;[2c64] 75
                    ld        l,h                           ;[2c65] 6c
                    ld        h,h                           ;[2c66] 64
                    jr        nz,$2cd7                      ;[2c67] 20 6e
                    ld        h,l                           ;[2c69] 65
                    halt                                    ;[2c6a] 76
                    ld        h,l                           ;[2c6b] 65
                    ld        (hl),d                        ;[2c6c] 72
                    jr        nz,$2ce2                      ;[2c6d] 20 73
                    ld        h,l                           ;[2c6f] 65
                    ld        h,l                           ;[2c70] 65
                    jr        nz,$2ce7                      ;[2c71] 20 74
                    ld        l,b                           ;[2c73] 68
                    ld        l,c                           ;[2c74] 69
                    ld        (hl),e                        ;[2c75] 73
                    ld        l,$00                         ;[2c76] 2e 00
                    ld        hl,$5c3b                      ;[2c78] 21 3b 5c
                    res       5,(hl)                        ;[2c7b] cb ae
                    bit       5,(hl)                        ;[2c7d] cb 6e
                    jr        z,$2c7d                       ;[2c7f] 28 fc
                    ld        a,($5c08)                     ;[2c81] 3a 08 5c
                    res       5,(hl)                        ;[2c84] cb ae
                    ret                                     ;[2c86] c9

                    di                                      ;[2c87] f3
                    ld        bc,$1ffd                      ;[2c88] 01 fd 1f
                    xor       a                             ;[2c8b] af
                    out       (c),a                         ;[2c8c] ed 79
                    ld        bc,$7ffd                      ;[2c8e] 01 fd 7f
                    out       (c),a                         ;[2c91] ed 79
                    jp        $0000                         ;[2c93] c3 00 00
                    di                                      ;[2c96] f3
                    push      af                            ;[2c97] f5
                    push      bc                            ;[2c98] c5
                    push      hl                            ;[2c99] e5
                    ld        bc,$7ffd                      ;[2c9a] 01 fd 7f
                    ld        hl,$5b5c                      ;[2c9d] 21 5c 5b
                    ld        a,(hl)                        ;[2ca0] 7e
                    or        $07                           ;[2ca1] f6 07
                    and       $ef                           ;[2ca3] e6 ef
                    ld        (hl),a                        ;[2ca5] 77
                    out       (c),a                         ;[2ca6] ed 79
                    ld        hl,$5b67                      ;[2ca8] 21 67 5b
                    ld        a,(hl)                        ;[2cab] 7e
                    or        $04                           ;[2cac] f6 04
                    ld        (hl),a                        ;[2cae] 77
                    ld        bc,$1ffd                      ;[2caf] 01 fd 1f
                    out       (c),a                         ;[2cb2] ed 79
                    pop       hl                            ;[2cb4] e1
                    pop       bc                            ;[2cb5] c1
                    pop       af                            ;[2cb6] f1
                    ei                                      ;[2cb7] fb
                    ret                                     ;[2cb8] c9

                    di                                      ;[2cb9] f3
                    push      af                            ;[2cba] f5
                    push      bc                            ;[2cbb] c5
                    push      hl                            ;[2cbc] e5
                    ld        bc,$7ffd                      ;[2cbd] 01 fd 7f
                    ld        hl,$5b5c                      ;[2cc0] 21 5c 5b
                    ld        a,(hl)                        ;[2cc3] 7e
                    or        $10                           ;[2cc4] f6 10
                    and       $f8                           ;[2cc6] e6 f8
                    ld        (hl),a                        ;[2cc8] 77
                    out       (c),a                         ;[2cc9] ed 79
                    ld        a,($5b67)                     ;[2ccb] 3a 67 5b
                    and       $fb                           ;[2cce] e6 fb
                    ld        ($5b67),a                     ;[2cd0] 32 67 5b
                    ld        bc,$1ffd                      ;[2cd3] 01 fd 1f
                    out       (c),a                         ;[2cd6] ed 79
                    pop       hl                            ;[2cd8] e1
                    pop       bc                            ;[2cd9] c1
                    pop       af                            ;[2cda] f1
                    ei                                      ;[2cdb] fb
                    ret                                     ;[2cdc] c9

                    nop                                     ;[2cdd] 00
                    nop                                     ;[2cde] 00
                    push      hl                            ;[2cdf] e5
                    nop                                     ;[2ce0] 00
                    nop                                     ;[2ce1] 00
                    nop                                     ;[2ce2] 00
                    nop                                     ;[2ce3] 00
                    nop                                     ;[2ce4] 00
                    nop                                     ;[2ce5] 00
                    nop                                     ;[2ce6] 00
                    nop                                     ;[2ce7] 00
                    nop                                     ;[2ce8] 00
                    nop                                     ;[2ce9] 00
                    nop                                     ;[2cea] 00
                    nop                                     ;[2ceb] 00
                    nop                                     ;[2cec] 00
                    nop                                     ;[2ced] 00
                    nop                                     ;[2cee] 00
                    nop                                     ;[2cef] 00
                    nop                                     ;[2cf0] 00
                    nop                                     ;[2cf1] 00
                    nop                                     ;[2cf2] 00
                    nop                                     ;[2cf3] 00
                    nop                                     ;[2cf4] 00
                    nop                                     ;[2cf5] 00
                    nop                                     ;[2cf6] 00
                    nop                                     ;[2cf7] 00
                    nop                                     ;[2cf8] 00
                    nop                                     ;[2cf9] 00
                    nop                                     ;[2cfa] 00
                    nop                                     ;[2cfb] 00
                    nop                                     ;[2cfc] 00
                    nop                                     ;[2cfd] 00
                    nop                                     ;[2cfe] 00
                    nop                                     ;[2cff] 00
                    nop                                     ;[2d00] 00
                    nop                                     ;[2d01] 00
                    nop                                     ;[2d02] 00
                    nop                                     ;[2d03] 00
                    nop                                     ;[2d04] 00
                    nop                                     ;[2d05] 00
                    nop                                     ;[2d06] 00
                    nop                                     ;[2d07] 00
                    nop                                     ;[2d08] 00
                    nop                                     ;[2d09] 00
                    nop                                     ;[2d0a] 00
                    nop                                     ;[2d0b] 00
                    nop                                     ;[2d0c] 00
                    nop                                     ;[2d0d] 00
                    nop                                     ;[2d0e] 00
                    nop                                     ;[2d0f] 00
                    nop                                     ;[2d10] 00
                    nop                                     ;[2d11] 00
                    nop                                     ;[2d12] 00
                    nop                                     ;[2d13] 00
                    nop                                     ;[2d14] 00
                    nop                                     ;[2d15] 00
                    nop                                     ;[2d16] 00
                    nop                                     ;[2d17] 00
                    nop                                     ;[2d18] 00
                    nop                                     ;[2d19] 00
                    nop                                     ;[2d1a] 00
                    nop                                     ;[2d1b] 00
                    nop                                     ;[2d1c] 00
                    nop                                     ;[2d1d] 00
                    nop                                     ;[2d1e] 00
                    nop                                     ;[2d1f] 00
                    nop                                     ;[2d20] 00
                    nop                                     ;[2d21] 00
                    nop                                     ;[2d22] 00
                    nop                                     ;[2d23] 00
                    nop                                     ;[2d24] 00
                    nop                                     ;[2d25] 00
                    nop                                     ;[2d26] 00
                    nop                                     ;[2d27] 00
                    nop                                     ;[2d28] 00
                    nop                                     ;[2d29] 00
                    nop                                     ;[2d2a] 00
                    nop                                     ;[2d2b] 00
                    nop                                     ;[2d2c] 00
                    nop                                     ;[2d2d] 00
                    nop                                     ;[2d2e] 00
                    nop                                     ;[2d2f] 00
                    nop                                     ;[2d30] 00
                    nop                                     ;[2d31] 00
                    nop                                     ;[2d32] 00
                    nop                                     ;[2d33] 00
                    nop                                     ;[2d34] 00
                    nop                                     ;[2d35] 00
                    nop                                     ;[2d36] 00
                    nop                                     ;[2d37] 00
                    nop                                     ;[2d38] 00
                    nop                                     ;[2d39] 00
                    nop                                     ;[2d3a] 00
                    nop                                     ;[2d3b] 00
                    nop                                     ;[2d3c] 00
                    nop                                     ;[2d3d] 00
                    nop                                     ;[2d3e] 00
                    nop                                     ;[2d3f] 00
                    nop                                     ;[2d40] 00
                    nop                                     ;[2d41] 00
                    nop                                     ;[2d42] 00
                    nop                                     ;[2d43] 00
                    nop                                     ;[2d44] 00
                    nop                                     ;[2d45] 00
                    nop                                     ;[2d46] 00
                    nop                                     ;[2d47] 00
                    nop                                     ;[2d48] 00
                    nop                                     ;[2d49] 00
                    nop                                     ;[2d4a] 00
                    nop                                     ;[2d4b] 00
                    nop                                     ;[2d4c] 00
                    nop                                     ;[2d4d] 00
                    nop                                     ;[2d4e] 00
                    nop                                     ;[2d4f] 00
                    nop                                     ;[2d50] 00
                    nop                                     ;[2d51] 00
                    nop                                     ;[2d52] 00
                    nop                                     ;[2d53] 00
                    nop                                     ;[2d54] 00
                    nop                                     ;[2d55] 00
                    nop                                     ;[2d56] 00
                    nop                                     ;[2d57] 00
                    nop                                     ;[2d58] 00
                    nop                                     ;[2d59] 00
                    nop                                     ;[2d5a] 00
                    nop                                     ;[2d5b] 00
                    nop                                     ;[2d5c] 00
                    nop                                     ;[2d5d] 00
                    nop                                     ;[2d5e] 00
                    nop                                     ;[2d5f] 00
                    nop                                     ;[2d60] 00
                    nop                                     ;[2d61] 00
                    nop                                     ;[2d62] 00
                    nop                                     ;[2d63] 00
                    nop                                     ;[2d64] 00
                    nop                                     ;[2d65] 00
                    nop                                     ;[2d66] 00
                    nop                                     ;[2d67] 00
                    nop                                     ;[2d68] 00
                    nop                                     ;[2d69] 00
                    nop                                     ;[2d6a] 00
                    nop                                     ;[2d6b] 00
                    nop                                     ;[2d6c] 00
                    nop                                     ;[2d6d] 00
                    nop                                     ;[2d6e] 00
                    nop                                     ;[2d6f] 00
                    nop                                     ;[2d70] 00
                    nop                                     ;[2d71] 00
                    nop                                     ;[2d72] 00
                    nop                                     ;[2d73] 00
                    nop                                     ;[2d74] 00
                    nop                                     ;[2d75] 00
                    nop                                     ;[2d76] 00
                    nop                                     ;[2d77] 00
                    nop                                     ;[2d78] 00
                    nop                                     ;[2d79] 00
                    nop                                     ;[2d7a] 00
                    nop                                     ;[2d7b] 00
                    nop                                     ;[2d7c] 00
                    nop                                     ;[2d7d] 00
                    nop                                     ;[2d7e] 00
                    nop                                     ;[2d7f] 00
                    nop                                     ;[2d80] 00
                    nop                                     ;[2d81] 00
                    nop                                     ;[2d82] 00
                    nop                                     ;[2d83] 00
                    nop                                     ;[2d84] 00
                    nop                                     ;[2d85] 00
                    nop                                     ;[2d86] 00
                    nop                                     ;[2d87] 00
                    nop                                     ;[2d88] 00
                    nop                                     ;[2d89] 00
                    nop                                     ;[2d8a] 00
                    nop                                     ;[2d8b] 00
                    nop                                     ;[2d8c] 00
                    nop                                     ;[2d8d] 00
                    nop                                     ;[2d8e] 00
                    nop                                     ;[2d8f] 00
                    nop                                     ;[2d90] 00
                    nop                                     ;[2d91] 00
                    nop                                     ;[2d92] 00
                    nop                                     ;[2d93] 00
                    nop                                     ;[2d94] 00
                    nop                                     ;[2d95] 00
                    nop                                     ;[2d96] 00
                    nop                                     ;[2d97] 00
                    nop                                     ;[2d98] 00
                    nop                                     ;[2d99] 00
                    nop                                     ;[2d9a] 00
                    nop                                     ;[2d9b] 00
                    nop                                     ;[2d9c] 00
                    nop                                     ;[2d9d] 00
                    nop                                     ;[2d9e] 00
                    nop                                     ;[2d9f] 00
                    nop                                     ;[2da0] 00
                    nop                                     ;[2da1] 00
                    nop                                     ;[2da2] 00
                    nop                                     ;[2da3] 00
                    nop                                     ;[2da4] 00
                    nop                                     ;[2da5] 00
                    nop                                     ;[2da6] 00
                    nop                                     ;[2da7] 00
                    nop                                     ;[2da8] 00
                    nop                                     ;[2da9] 00
                    nop                                     ;[2daa] 00
                    nop                                     ;[2dab] 00
                    nop                                     ;[2dac] 00
                    nop                                     ;[2dad] 00
                    nop                                     ;[2dae] 00
                    nop                                     ;[2daf] 00
                    nop                                     ;[2db0] 00
                    nop                                     ;[2db1] 00
                    nop                                     ;[2db2] 00
                    nop                                     ;[2db3] 00
                    nop                                     ;[2db4] 00
                    nop                                     ;[2db5] 00
                    nop                                     ;[2db6] 00
                    nop                                     ;[2db7] 00
                    nop                                     ;[2db8] 00
                    nop                                     ;[2db9] 00
                    nop                                     ;[2dba] 00
                    nop                                     ;[2dbb] 00
                    nop                                     ;[2dbc] 00
                    nop                                     ;[2dbd] 00
                    nop                                     ;[2dbe] 00
                    nop                                     ;[2dbf] 00
                    nop                                     ;[2dc0] 00
                    nop                                     ;[2dc1] 00
                    nop                                     ;[2dc2] 00
                    nop                                     ;[2dc3] 00
                    nop                                     ;[2dc4] 00
                    nop                                     ;[2dc5] 00
                    nop                                     ;[2dc6] 00
                    nop                                     ;[2dc7] 00
                    nop                                     ;[2dc8] 00
                    nop                                     ;[2dc9] 00
                    nop                                     ;[2dca] 00
                    nop                                     ;[2dcb] 00
                    nop                                     ;[2dcc] 00
                    nop                                     ;[2dcd] 00
                    nop                                     ;[2dce] 00
                    nop                                     ;[2dcf] 00
                    nop                                     ;[2dd0] 00
                    nop                                     ;[2dd1] 00
                    nop                                     ;[2dd2] 00
                    nop                                     ;[2dd3] 00
                    nop                                     ;[2dd4] 00
                    nop                                     ;[2dd5] 00
                    nop                                     ;[2dd6] 00
                    nop                                     ;[2dd7] 00
                    nop                                     ;[2dd8] 00
                    nop                                     ;[2dd9] 00
                    nop                                     ;[2dda] 00
                    nop                                     ;[2ddb] 00
                    nop                                     ;[2ddc] 00
                    nop                                     ;[2ddd] 00
                    nop                                     ;[2dde] 00
                    nop                                     ;[2ddf] 00
                    nop                                     ;[2de0] 00
                    nop                                     ;[2de1] 00
                    nop                                     ;[2de2] 00
                    nop                                     ;[2de3] 00
                    nop                                     ;[2de4] 00
                    nop                                     ;[2de5] 00
                    nop                                     ;[2de6] 00
                    nop                                     ;[2de7] 00
                    nop                                     ;[2de8] 00
                    nop                                     ;[2de9] 00
                    nop                                     ;[2dea] 00
                    nop                                     ;[2deb] 00
                    nop                                     ;[2dec] 00
                    nop                                     ;[2ded] 00
                    nop                                     ;[2dee] 00
                    nop                                     ;[2def] 00
                    nop                                     ;[2df0] 00
                    nop                                     ;[2df1] 00
                    nop                                     ;[2df2] 00
                    nop                                     ;[2df3] 00
                    nop                                     ;[2df4] 00
                    nop                                     ;[2df5] 00
                    nop                                     ;[2df6] 00
                    nop                                     ;[2df7] 00
                    nop                                     ;[2df8] 00
                    nop                                     ;[2df9] 00
                    nop                                     ;[2dfa] 00
                    nop                                     ;[2dfb] 00
                    nop                                     ;[2dfc] 00
                    nop                                     ;[2dfd] 00
                    nop                                     ;[2dfe] 00
                    nop                                     ;[2dff] 00
                    nop                                     ;[2e00] 00
                    nop                                     ;[2e01] 00
                    nop                                     ;[2e02] 00
                    nop                                     ;[2e03] 00
                    nop                                     ;[2e04] 00
                    nop                                     ;[2e05] 00
                    nop                                     ;[2e06] 00
                    nop                                     ;[2e07] 00
                    nop                                     ;[2e08] 00
                    nop                                     ;[2e09] 00
                    nop                                     ;[2e0a] 00
                    nop                                     ;[2e0b] 00
                    nop                                     ;[2e0c] 00
                    nop                                     ;[2e0d] 00
                    nop                                     ;[2e0e] 00
                    nop                                     ;[2e0f] 00
                    nop                                     ;[2e10] 00
                    nop                                     ;[2e11] 00
                    nop                                     ;[2e12] 00
                    nop                                     ;[2e13] 00
                    nop                                     ;[2e14] 00
                    nop                                     ;[2e15] 00
                    nop                                     ;[2e16] 00
                    nop                                     ;[2e17] 00
                    nop                                     ;[2e18] 00
                    nop                                     ;[2e19] 00
                    nop                                     ;[2e1a] 00
                    nop                                     ;[2e1b] 00
                    nop                                     ;[2e1c] 00
                    nop                                     ;[2e1d] 00
                    nop                                     ;[2e1e] 00
                    nop                                     ;[2e1f] 00
                    nop                                     ;[2e20] 00
                    nop                                     ;[2e21] 00
                    nop                                     ;[2e22] 00
                    nop                                     ;[2e23] 00
                    nop                                     ;[2e24] 00
                    nop                                     ;[2e25] 00
                    nop                                     ;[2e26] 00
                    nop                                     ;[2e27] 00
                    nop                                     ;[2e28] 00
                    nop                                     ;[2e29] 00
                    nop                                     ;[2e2a] 00
                    nop                                     ;[2e2b] 00
                    nop                                     ;[2e2c] 00
                    nop                                     ;[2e2d] 00
                    nop                                     ;[2e2e] 00
                    nop                                     ;[2e2f] 00
                    nop                                     ;[2e30] 00
                    nop                                     ;[2e31] 00
                    nop                                     ;[2e32] 00
                    nop                                     ;[2e33] 00
                    nop                                     ;[2e34] 00
                    nop                                     ;[2e35] 00
                    nop                                     ;[2e36] 00
                    nop                                     ;[2e37] 00
                    nop                                     ;[2e38] 00
                    nop                                     ;[2e39] 00
                    nop                                     ;[2e3a] 00
                    nop                                     ;[2e3b] 00
                    nop                                     ;[2e3c] 00
                    nop                                     ;[2e3d] 00
                    nop                                     ;[2e3e] 00
                    nop                                     ;[2e3f] 00
                    nop                                     ;[2e40] 00
                    nop                                     ;[2e41] 00
                    nop                                     ;[2e42] 00
                    nop                                     ;[2e43] 00
                    nop                                     ;[2e44] 00
                    nop                                     ;[2e45] 00
                    nop                                     ;[2e46] 00
                    nop                                     ;[2e47] 00
                    nop                                     ;[2e48] 00
                    nop                                     ;[2e49] 00
                    nop                                     ;[2e4a] 00
                    nop                                     ;[2e4b] 00
                    nop                                     ;[2e4c] 00
                    nop                                     ;[2e4d] 00
                    nop                                     ;[2e4e] 00
                    nop                                     ;[2e4f] 00
                    nop                                     ;[2e50] 00
                    nop                                     ;[2e51] 00
                    nop                                     ;[2e52] 00
                    nop                                     ;[2e53] 00
                    nop                                     ;[2e54] 00
                    nop                                     ;[2e55] 00
                    nop                                     ;[2e56] 00
                    nop                                     ;[2e57] 00
                    nop                                     ;[2e58] 00
                    nop                                     ;[2e59] 00
                    nop                                     ;[2e5a] 00
                    nop                                     ;[2e5b] 00
                    nop                                     ;[2e5c] 00
                    nop                                     ;[2e5d] 00
                    nop                                     ;[2e5e] 00
                    nop                                     ;[2e5f] 00
                    nop                                     ;[2e60] 00
                    nop                                     ;[2e61] 00
                    nop                                     ;[2e62] 00
                    nop                                     ;[2e63] 00
                    nop                                     ;[2e64] 00
                    nop                                     ;[2e65] 00
                    nop                                     ;[2e66] 00
                    nop                                     ;[2e67] 00
                    nop                                     ;[2e68] 00
                    nop                                     ;[2e69] 00
                    nop                                     ;[2e6a] 00
                    nop                                     ;[2e6b] 00
                    nop                                     ;[2e6c] 00
                    nop                                     ;[2e6d] 00
                    nop                                     ;[2e6e] 00
                    nop                                     ;[2e6f] 00
                    nop                                     ;[2e70] 00
                    nop                                     ;[2e71] 00
                    nop                                     ;[2e72] 00
                    nop                                     ;[2e73] 00
                    nop                                     ;[2e74] 00
                    nop                                     ;[2e75] 00
                    nop                                     ;[2e76] 00
                    nop                                     ;[2e77] 00
                    nop                                     ;[2e78] 00
                    nop                                     ;[2e79] 00
                    nop                                     ;[2e7a] 00
                    nop                                     ;[2e7b] 00
                    nop                                     ;[2e7c] 00
                    nop                                     ;[2e7d] 00
                    nop                                     ;[2e7e] 00
                    nop                                     ;[2e7f] 00
                    nop                                     ;[2e80] 00
                    nop                                     ;[2e81] 00
                    nop                                     ;[2e82] 00
                    nop                                     ;[2e83] 00
                    nop                                     ;[2e84] 00
                    nop                                     ;[2e85] 00
                    nop                                     ;[2e86] 00
                    nop                                     ;[2e87] 00
                    nop                                     ;[2e88] 00
                    nop                                     ;[2e89] 00
                    nop                                     ;[2e8a] 00
                    nop                                     ;[2e8b] 00
                    nop                                     ;[2e8c] 00
                    nop                                     ;[2e8d] 00
                    nop                                     ;[2e8e] 00
                    nop                                     ;[2e8f] 00
                    nop                                     ;[2e90] 00
                    nop                                     ;[2e91] 00
                    nop                                     ;[2e92] 00
                    nop                                     ;[2e93] 00
                    nop                                     ;[2e94] 00
                    nop                                     ;[2e95] 00
                    nop                                     ;[2e96] 00
                    nop                                     ;[2e97] 00
                    nop                                     ;[2e98] 00
                    nop                                     ;[2e99] 00
                    nop                                     ;[2e9a] 00
                    nop                                     ;[2e9b] 00
                    nop                                     ;[2e9c] 00
                    nop                                     ;[2e9d] 00
                    nop                                     ;[2e9e] 00
                    nop                                     ;[2e9f] 00
                    nop                                     ;[2ea0] 00
                    nop                                     ;[2ea1] 00
                    nop                                     ;[2ea2] 00
                    nop                                     ;[2ea3] 00
                    nop                                     ;[2ea4] 00
                    nop                                     ;[2ea5] 00
                    nop                                     ;[2ea6] 00
                    nop                                     ;[2ea7] 00
                    nop                                     ;[2ea8] 00
                    nop                                     ;[2ea9] 00
                    nop                                     ;[2eaa] 00
                    nop                                     ;[2eab] 00
                    nop                                     ;[2eac] 00
                    nop                                     ;[2ead] 00
                    nop                                     ;[2eae] 00
                    nop                                     ;[2eaf] 00
                    nop                                     ;[2eb0] 00
                    nop                                     ;[2eb1] 00
                    nop                                     ;[2eb2] 00
                    nop                                     ;[2eb3] 00
                    nop                                     ;[2eb4] 00
                    nop                                     ;[2eb5] 00
                    nop                                     ;[2eb6] 00
                    nop                                     ;[2eb7] 00
                    nop                                     ;[2eb8] 00
                    nop                                     ;[2eb9] 00
                    nop                                     ;[2eba] 00
                    nop                                     ;[2ebb] 00
                    nop                                     ;[2ebc] 00
                    nop                                     ;[2ebd] 00
                    nop                                     ;[2ebe] 00
                    nop                                     ;[2ebf] 00
                    nop                                     ;[2ec0] 00
                    nop                                     ;[2ec1] 00
                    nop                                     ;[2ec2] 00
                    nop                                     ;[2ec3] 00
                    nop                                     ;[2ec4] 00
                    nop                                     ;[2ec5] 00
                    nop                                     ;[2ec6] 00
                    nop                                     ;[2ec7] 00
                    nop                                     ;[2ec8] 00
                    nop                                     ;[2ec9] 00
                    nop                                     ;[2eca] 00
                    nop                                     ;[2ecb] 00
                    nop                                     ;[2ecc] 00
                    nop                                     ;[2ecd] 00
                    nop                                     ;[2ece] 00
                    nop                                     ;[2ecf] 00
                    nop                                     ;[2ed0] 00
                    nop                                     ;[2ed1] 00
                    nop                                     ;[2ed2] 00
                    nop                                     ;[2ed3] 00
                    nop                                     ;[2ed4] 00
                    nop                                     ;[2ed5] 00
                    nop                                     ;[2ed6] 00
                    nop                                     ;[2ed7] 00
                    nop                                     ;[2ed8] 00
                    nop                                     ;[2ed9] 00
                    nop                                     ;[2eda] 00
                    nop                                     ;[2edb] 00
                    nop                                     ;[2edc] 00
                    nop                                     ;[2edd] 00
                    nop                                     ;[2ede] 00
                    nop                                     ;[2edf] 00
                    nop                                     ;[2ee0] 00
                    nop                                     ;[2ee1] 00
                    nop                                     ;[2ee2] 00
                    nop                                     ;[2ee3] 00
                    nop                                     ;[2ee4] 00
                    nop                                     ;[2ee5] 00
                    nop                                     ;[2ee6] 00
                    nop                                     ;[2ee7] 00
                    nop                                     ;[2ee8] 00
                    nop                                     ;[2ee9] 00
                    nop                                     ;[2eea] 00
                    nop                                     ;[2eeb] 00
                    nop                                     ;[2eec] 00
                    nop                                     ;[2eed] 00
                    nop                                     ;[2eee] 00
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
                    nop                                     ;[2f0f] 00
                    nop                                     ;[2f10] 00
                    nop                                     ;[2f11] 00
                    nop                                     ;[2f12] 00
                    nop                                     ;[2f13] 00
                    nop                                     ;[2f14] 00
                    nop                                     ;[2f15] 00
                    nop                                     ;[2f16] 00
                    nop                                     ;[2f17] 00
                    nop                                     ;[2f18] 00
                    nop                                     ;[2f19] 00
                    nop                                     ;[2f1a] 00
                    nop                                     ;[2f1b] 00
                    nop                                     ;[2f1c] 00
                    nop                                     ;[2f1d] 00
                    nop                                     ;[2f1e] 00
                    nop                                     ;[2f1f] 00
                    nop                                     ;[2f20] 00
                    nop                                     ;[2f21] 00
                    nop                                     ;[2f22] 00
                    nop                                     ;[2f23] 00
                    nop                                     ;[2f24] 00
                    nop                                     ;[2f25] 00
                    nop                                     ;[2f26] 00
                    nop                                     ;[2f27] 00
                    nop                                     ;[2f28] 00
                    nop                                     ;[2f29] 00
                    nop                                     ;[2f2a] 00
                    nop                                     ;[2f2b] 00
                    nop                                     ;[2f2c] 00
                    nop                                     ;[2f2d] 00
                    nop                                     ;[2f2e] 00
                    nop                                     ;[2f2f] 00
                    nop                                     ;[2f30] 00
                    nop                                     ;[2f31] 00
                    nop                                     ;[2f32] 00
                    nop                                     ;[2f33] 00
                    nop                                     ;[2f34] 00
                    nop                                     ;[2f35] 00
                    nop                                     ;[2f36] 00
                    nop                                     ;[2f37] 00
                    nop                                     ;[2f38] 00
                    nop                                     ;[2f39] 00
                    nop                                     ;[2f3a] 00
                    nop                                     ;[2f3b] 00
                    nop                                     ;[2f3c] 00
                    nop                                     ;[2f3d] 00
                    nop                                     ;[2f3e] 00
                    nop                                     ;[2f3f] 00
                    nop                                     ;[2f40] 00
                    nop                                     ;[2f41] 00
                    nop                                     ;[2f42] 00
                    nop                                     ;[2f43] 00
                    nop                                     ;[2f44] 00
                    nop                                     ;[2f45] 00
                    nop                                     ;[2f46] 00
                    nop                                     ;[2f47] 00
                    nop                                     ;[2f48] 00
                    nop                                     ;[2f49] 00
                    nop                                     ;[2f4a] 00
                    nop                                     ;[2f4b] 00
                    nop                                     ;[2f4c] 00
                    nop                                     ;[2f4d] 00
                    nop                                     ;[2f4e] 00
                    nop                                     ;[2f4f] 00
                    nop                                     ;[2f50] 00
                    nop                                     ;[2f51] 00
                    nop                                     ;[2f52] 00
                    nop                                     ;[2f53] 00
                    nop                                     ;[2f54] 00
                    nop                                     ;[2f55] 00
                    nop                                     ;[2f56] 00
                    nop                                     ;[2f57] 00
                    nop                                     ;[2f58] 00
                    nop                                     ;[2f59] 00
                    nop                                     ;[2f5a] 00
                    nop                                     ;[2f5b] 00
                    nop                                     ;[2f5c] 00
                    nop                                     ;[2f5d] 00
                    nop                                     ;[2f5e] 00
                    nop                                     ;[2f5f] 00
                    nop                                     ;[2f60] 00
                    nop                                     ;[2f61] 00
                    nop                                     ;[2f62] 00
                    nop                                     ;[2f63] 00
                    nop                                     ;[2f64] 00
                    nop                                     ;[2f65] 00
                    nop                                     ;[2f66] 00
                    nop                                     ;[2f67] 00
                    nop                                     ;[2f68] 00
                    nop                                     ;[2f69] 00
                    nop                                     ;[2f6a] 00
                    nop                                     ;[2f6b] 00
                    nop                                     ;[2f6c] 00
                    nop                                     ;[2f6d] 00
                    nop                                     ;[2f6e] 00
                    nop                                     ;[2f6f] 00
                    nop                                     ;[2f70] 00
                    nop                                     ;[2f71] 00
                    nop                                     ;[2f72] 00
                    nop                                     ;[2f73] 00
                    nop                                     ;[2f74] 00
                    nop                                     ;[2f75] 00
                    nop                                     ;[2f76] 00
                    nop                                     ;[2f77] 00
                    nop                                     ;[2f78] 00
                    nop                                     ;[2f79] 00
                    nop                                     ;[2f7a] 00
                    nop                                     ;[2f7b] 00
                    nop                                     ;[2f7c] 00
                    nop                                     ;[2f7d] 00
                    nop                                     ;[2f7e] 00
                    nop                                     ;[2f7f] 00
                    nop                                     ;[2f80] 00
                    nop                                     ;[2f81] 00
                    nop                                     ;[2f82] 00
                    nop                                     ;[2f83] 00
                    nop                                     ;[2f84] 00
                    nop                                     ;[2f85] 00
                    nop                                     ;[2f86] 00
                    nop                                     ;[2f87] 00
                    nop                                     ;[2f88] 00
                    nop                                     ;[2f89] 00
                    nop                                     ;[2f8a] 00
                    nop                                     ;[2f8b] 00
                    nop                                     ;[2f8c] 00
                    nop                                     ;[2f8d] 00
                    nop                                     ;[2f8e] 00
                    nop                                     ;[2f8f] 00
                    nop                                     ;[2f90] 00
                    nop                                     ;[2f91] 00
                    nop                                     ;[2f92] 00
                    nop                                     ;[2f93] 00
                    nop                                     ;[2f94] 00
                    nop                                     ;[2f95] 00
                    nop                                     ;[2f96] 00
                    nop                                     ;[2f97] 00
                    nop                                     ;[2f98] 00
                    nop                                     ;[2f99] 00
                    nop                                     ;[2f9a] 00
                    nop                                     ;[2f9b] 00
                    nop                                     ;[2f9c] 00
                    nop                                     ;[2f9d] 00
                    nop                                     ;[2f9e] 00
                    nop                                     ;[2f9f] 00
                    nop                                     ;[2fa0] 00
                    nop                                     ;[2fa1] 00
                    nop                                     ;[2fa2] 00
                    nop                                     ;[2fa3] 00
                    nop                                     ;[2fa4] 00
                    nop                                     ;[2fa5] 00
                    nop                                     ;[2fa6] 00
                    nop                                     ;[2fa7] 00
                    nop                                     ;[2fa8] 00
                    nop                                     ;[2fa9] 00
                    nop                                     ;[2faa] 00
                    nop                                     ;[2fab] 00
                    nop                                     ;[2fac] 00
                    nop                                     ;[2fad] 00
                    nop                                     ;[2fae] 00
                    nop                                     ;[2faf] 00
                    nop                                     ;[2fb0] 00
                    nop                                     ;[2fb1] 00
                    nop                                     ;[2fb2] 00
                    nop                                     ;[2fb3] 00
                    nop                                     ;[2fb4] 00
                    nop                                     ;[2fb5] 00
                    nop                                     ;[2fb6] 00
                    nop                                     ;[2fb7] 00
                    nop                                     ;[2fb8] 00
                    nop                                     ;[2fb9] 00
                    nop                                     ;[2fba] 00
                    nop                                     ;[2fbb] 00
                    nop                                     ;[2fbc] 00
                    nop                                     ;[2fbd] 00
                    nop                                     ;[2fbe] 00
                    nop                                     ;[2fbf] 00
                    nop                                     ;[2fc0] 00
                    nop                                     ;[2fc1] 00
                    nop                                     ;[2fc2] 00
                    nop                                     ;[2fc3] 00
                    nop                                     ;[2fc4] 00
                    nop                                     ;[2fc5] 00
                    nop                                     ;[2fc6] 00
                    nop                                     ;[2fc7] 00
                    nop                                     ;[2fc8] 00
                    nop                                     ;[2fc9] 00
                    nop                                     ;[2fca] 00
                    nop                                     ;[2fcb] 00
                    nop                                     ;[2fcc] 00
                    nop                                     ;[2fcd] 00
                    nop                                     ;[2fce] 00
                    nop                                     ;[2fcf] 00
                    nop                                     ;[2fd0] 00
                    nop                                     ;[2fd1] 00
                    nop                                     ;[2fd2] 00
                    nop                                     ;[2fd3] 00
                    nop                                     ;[2fd4] 00
                    nop                                     ;[2fd5] 00
                    nop                                     ;[2fd6] 00
                    nop                                     ;[2fd7] 00
                    nop                                     ;[2fd8] 00
                    nop                                     ;[2fd9] 00
                    nop                                     ;[2fda] 00
                    nop                                     ;[2fdb] 00
                    nop                                     ;[2fdc] 00
                    nop                                     ;[2fdd] 00
                    nop                                     ;[2fde] 00
                    nop                                     ;[2fdf] 00
                    nop                                     ;[2fe0] 00
                    nop                                     ;[2fe1] 00
                    nop                                     ;[2fe2] 00
                    nop                                     ;[2fe3] 00
                    nop                                     ;[2fe4] 00
                    nop                                     ;[2fe5] 00
                    nop                                     ;[2fe6] 00
                    nop                                     ;[2fe7] 00
                    nop                                     ;[2fe8] 00
                    nop                                     ;[2fe9] 00
                    nop                                     ;[2fea] 00
                    nop                                     ;[2feb] 00
                    nop                                     ;[2fec] 00
                    nop                                     ;[2fed] 00
                    nop                                     ;[2fee] 00
                    nop                                     ;[2fef] 00
                    nop                                     ;[2ff0] 00
                    nop                                     ;[2ff1] 00
                    nop                                     ;[2ff2] 00
                    nop                                     ;[2ff3] 00
                    nop                                     ;[2ff4] 00
                    nop                                     ;[2ff5] 00
                    nop                                     ;[2ff6] 00
                    nop                                     ;[2ff7] 00
                    nop                                     ;[2ff8] 00
                    nop                                     ;[2ff9] 00
                    nop                                     ;[2ffa] 00
                    nop                                     ;[2ffb] 00
                    nop                                     ;[2ffc] 00
                    nop                                     ;[2ffd] 00
                    nop                                     ;[2ffe] 00
                    nop                                     ;[2fff] 00
                    nop                                     ;[3000] 00
                    nop                                     ;[3001] 00
                    nop                                     ;[3002] 00
                    nop                                     ;[3003] 00
                    nop                                     ;[3004] 00
                    nop                                     ;[3005] 00
                    nop                                     ;[3006] 00
                    nop                                     ;[3007] 00
                    nop                                     ;[3008] 00
                    nop                                     ;[3009] 00
                    nop                                     ;[300a] 00
                    nop                                     ;[300b] 00
                    nop                                     ;[300c] 00
                    nop                                     ;[300d] 00
                    nop                                     ;[300e] 00
                    nop                                     ;[300f] 00
                    nop                                     ;[3010] 00
                    nop                                     ;[3011] 00
                    nop                                     ;[3012] 00
                    nop                                     ;[3013] 00
                    nop                                     ;[3014] 00
                    nop                                     ;[3015] 00
                    nop                                     ;[3016] 00
                    nop                                     ;[3017] 00
                    nop                                     ;[3018] 00
                    nop                                     ;[3019] 00
                    nop                                     ;[301a] 00
                    nop                                     ;[301b] 00
                    nop                                     ;[301c] 00
                    nop                                     ;[301d] 00
                    nop                                     ;[301e] 00
                    nop                                     ;[301f] 00
                    nop                                     ;[3020] 00
                    nop                                     ;[3021] 00
                    nop                                     ;[3022] 00
                    nop                                     ;[3023] 00
                    nop                                     ;[3024] 00
                    nop                                     ;[3025] 00
                    nop                                     ;[3026] 00
                    nop                                     ;[3027] 00
                    nop                                     ;[3028] 00
                    nop                                     ;[3029] 00
                    nop                                     ;[302a] 00
                    nop                                     ;[302b] 00
                    nop                                     ;[302c] 00
                    nop                                     ;[302d] 00
                    nop                                     ;[302e] 00
                    nop                                     ;[302f] 00
                    nop                                     ;[3030] 00
                    nop                                     ;[3031] 00
                    nop                                     ;[3032] 00
                    nop                                     ;[3033] 00
                    nop                                     ;[3034] 00
                    nop                                     ;[3035] 00
                    nop                                     ;[3036] 00
                    nop                                     ;[3037] 00
                    nop                                     ;[3038] 00
                    nop                                     ;[3039] 00
                    nop                                     ;[303a] 00
                    nop                                     ;[303b] 00
                    nop                                     ;[303c] 00
                    nop                                     ;[303d] 00
                    nop                                     ;[303e] 00
                    nop                                     ;[303f] 00
                    nop                                     ;[3040] 00
                    nop                                     ;[3041] 00
                    nop                                     ;[3042] 00
                    nop                                     ;[3043] 00
                    nop                                     ;[3044] 00
                    nop                                     ;[3045] 00
                    nop                                     ;[3046] 00
                    nop                                     ;[3047] 00
                    nop                                     ;[3048] 00
                    nop                                     ;[3049] 00
                    nop                                     ;[304a] 00
                    nop                                     ;[304b] 00
                    nop                                     ;[304c] 00
                    nop                                     ;[304d] 00
                    nop                                     ;[304e] 00
                    nop                                     ;[304f] 00
                    nop                                     ;[3050] 00
                    nop                                     ;[3051] 00
                    nop                                     ;[3052] 00
                    nop                                     ;[3053] 00
                    nop                                     ;[3054] 00
                    nop                                     ;[3055] 00
                    nop                                     ;[3056] 00
                    nop                                     ;[3057] 00
                    nop                                     ;[3058] 00
                    nop                                     ;[3059] 00
                    nop                                     ;[305a] 00
                    nop                                     ;[305b] 00
                    nop                                     ;[305c] 00
                    nop                                     ;[305d] 00
                    nop                                     ;[305e] 00
                    nop                                     ;[305f] 00
                    nop                                     ;[3060] 00
                    nop                                     ;[3061] 00
                    nop                                     ;[3062] 00
                    nop                                     ;[3063] 00
                    nop                                     ;[3064] 00
                    nop                                     ;[3065] 00
                    nop                                     ;[3066] 00
                    nop                                     ;[3067] 00
                    nop                                     ;[3068] 00
                    nop                                     ;[3069] 00
                    nop                                     ;[306a] 00
                    nop                                     ;[306b] 00
                    nop                                     ;[306c] 00
                    nop                                     ;[306d] 00
                    nop                                     ;[306e] 00
                    nop                                     ;[306f] 00
                    nop                                     ;[3070] 00
                    nop                                     ;[3071] 00
                    nop                                     ;[3072] 00
                    nop                                     ;[3073] 00
                    nop                                     ;[3074] 00
                    nop                                     ;[3075] 00
                    nop                                     ;[3076] 00
                    nop                                     ;[3077] 00
                    nop                                     ;[3078] 00
                    nop                                     ;[3079] 00
                    nop                                     ;[307a] 00
                    nop                                     ;[307b] 00
                    nop                                     ;[307c] 00
                    nop                                     ;[307d] 00
                    nop                                     ;[307e] 00
                    nop                                     ;[307f] 00
                    nop                                     ;[3080] 00
                    nop                                     ;[3081] 00
                    nop                                     ;[3082] 00
                    nop                                     ;[3083] 00
                    nop                                     ;[3084] 00
                    nop                                     ;[3085] 00
                    nop                                     ;[3086] 00
                    nop                                     ;[3087] 00
                    nop                                     ;[3088] 00
                    nop                                     ;[3089] 00
                    nop                                     ;[308a] 00
                    nop                                     ;[308b] 00
                    nop                                     ;[308c] 00
                    nop                                     ;[308d] 00
                    nop                                     ;[308e] 00
                    nop                                     ;[308f] 00
                    nop                                     ;[3090] 00
                    nop                                     ;[3091] 00
                    nop                                     ;[3092] 00
                    nop                                     ;[3093] 00
                    nop                                     ;[3094] 00
                    nop                                     ;[3095] 00
                    nop                                     ;[3096] 00
                    nop                                     ;[3097] 00
                    nop                                     ;[3098] 00
                    nop                                     ;[3099] 00
                    nop                                     ;[309a] 00
                    nop                                     ;[309b] 00
                    nop                                     ;[309c] 00
                    nop                                     ;[309d] 00
                    nop                                     ;[309e] 00
                    nop                                     ;[309f] 00
                    nop                                     ;[30a0] 00
                    nop                                     ;[30a1] 00
                    nop                                     ;[30a2] 00
                    nop                                     ;[30a3] 00
                    nop                                     ;[30a4] 00
                    nop                                     ;[30a5] 00
                    nop                                     ;[30a6] 00
                    nop                                     ;[30a7] 00
                    nop                                     ;[30a8] 00
                    nop                                     ;[30a9] 00
                    nop                                     ;[30aa] 00
                    nop                                     ;[30ab] 00
                    nop                                     ;[30ac] 00
                    nop                                     ;[30ad] 00
                    nop                                     ;[30ae] 00
                    nop                                     ;[30af] 00
                    nop                                     ;[30b0] 00
                    nop                                     ;[30b1] 00
                    nop                                     ;[30b2] 00
                    nop                                     ;[30b3] 00
                    nop                                     ;[30b4] 00
                    nop                                     ;[30b5] 00
                    nop                                     ;[30b6] 00
                    nop                                     ;[30b7] 00
                    nop                                     ;[30b8] 00
                    nop                                     ;[30b9] 00
                    nop                                     ;[30ba] 00
                    nop                                     ;[30bb] 00
                    nop                                     ;[30bc] 00
                    nop                                     ;[30bd] 00
                    nop                                     ;[30be] 00
                    nop                                     ;[30bf] 00
                    nop                                     ;[30c0] 00
                    nop                                     ;[30c1] 00
                    nop                                     ;[30c2] 00
                    nop                                     ;[30c3] 00
                    nop                                     ;[30c4] 00
                    nop                                     ;[30c5] 00
                    nop                                     ;[30c6] 00
                    nop                                     ;[30c7] 00
                    nop                                     ;[30c8] 00
                    nop                                     ;[30c9] 00
                    nop                                     ;[30ca] 00
                    nop                                     ;[30cb] 00
                    nop                                     ;[30cc] 00
                    nop                                     ;[30cd] 00
                    nop                                     ;[30ce] 00
                    nop                                     ;[30cf] 00
                    nop                                     ;[30d0] 00
                    nop                                     ;[30d1] 00
                    nop                                     ;[30d2] 00
                    nop                                     ;[30d3] 00
                    nop                                     ;[30d4] 00
                    nop                                     ;[30d5] 00
                    nop                                     ;[30d6] 00
                    nop                                     ;[30d7] 00
                    nop                                     ;[30d8] 00
                    nop                                     ;[30d9] 00
                    nop                                     ;[30da] 00
                    nop                                     ;[30db] 00
                    nop                                     ;[30dc] 00
                    nop                                     ;[30dd] 00
                    nop                                     ;[30de] 00
                    nop                                     ;[30df] 00
                    nop                                     ;[30e0] 00
                    nop                                     ;[30e1] 00
                    nop                                     ;[30e2] 00
                    nop                                     ;[30e3] 00
                    nop                                     ;[30e4] 00
                    nop                                     ;[30e5] 00
                    nop                                     ;[30e6] 00
                    nop                                     ;[30e7] 00
                    nop                                     ;[30e8] 00
                    nop                                     ;[30e9] 00
                    nop                                     ;[30ea] 00
                    nop                                     ;[30eb] 00
                    nop                                     ;[30ec] 00
                    nop                                     ;[30ed] 00
                    nop                                     ;[30ee] 00
                    nop                                     ;[30ef] 00
                    nop                                     ;[30f0] 00
                    nop                                     ;[30f1] 00
                    nop                                     ;[30f2] 00
                    nop                                     ;[30f3] 00
                    nop                                     ;[30f4] 00
                    nop                                     ;[30f5] 00
                    nop                                     ;[30f6] 00
                    nop                                     ;[30f7] 00
                    nop                                     ;[30f8] 00
                    nop                                     ;[30f9] 00
                    nop                                     ;[30fa] 00
                    nop                                     ;[30fb] 00
                    nop                                     ;[30fc] 00
                    nop                                     ;[30fd] 00
                    nop                                     ;[30fe] 00
                    nop                                     ;[30ff] 00
                    nop                                     ;[3100] 00
                    nop                                     ;[3101] 00
                    nop                                     ;[3102] 00
                    nop                                     ;[3103] 00
                    nop                                     ;[3104] 00
                    nop                                     ;[3105] 00
                    nop                                     ;[3106] 00
                    nop                                     ;[3107] 00
                    nop                                     ;[3108] 00
                    nop                                     ;[3109] 00
                    nop                                     ;[310a] 00
                    nop                                     ;[310b] 00
                    nop                                     ;[310c] 00
                    nop                                     ;[310d] 00
                    nop                                     ;[310e] 00
                    nop                                     ;[310f] 00
                    nop                                     ;[3110] 00
                    nop                                     ;[3111] 00
                    nop                                     ;[3112] 00
                    nop                                     ;[3113] 00
                    nop                                     ;[3114] 00
                    nop                                     ;[3115] 00
                    nop                                     ;[3116] 00
                    nop                                     ;[3117] 00
                    nop                                     ;[3118] 00
                    nop                                     ;[3119] 00
                    nop                                     ;[311a] 00
                    nop                                     ;[311b] 00
                    nop                                     ;[311c] 00
                    nop                                     ;[311d] 00
                    nop                                     ;[311e] 00
                    nop                                     ;[311f] 00
                    nop                                     ;[3120] 00
                    nop                                     ;[3121] 00
                    nop                                     ;[3122] 00
                    nop                                     ;[3123] 00
                    nop                                     ;[3124] 00
                    nop                                     ;[3125] 00
                    nop                                     ;[3126] 00
                    nop                                     ;[3127] 00
                    nop                                     ;[3128] 00
                    nop                                     ;[3129] 00
                    nop                                     ;[312a] 00
                    nop                                     ;[312b] 00
                    nop                                     ;[312c] 00
                    nop                                     ;[312d] 00
                    nop                                     ;[312e] 00
                    nop                                     ;[312f] 00
                    nop                                     ;[3130] 00
                    nop                                     ;[3131] 00
                    nop                                     ;[3132] 00
                    nop                                     ;[3133] 00
                    nop                                     ;[3134] 00
                    nop                                     ;[3135] 00
                    nop                                     ;[3136] 00
                    nop                                     ;[3137] 00
                    nop                                     ;[3138] 00
                    nop                                     ;[3139] 00
                    nop                                     ;[313a] 00
                    nop                                     ;[313b] 00
                    nop                                     ;[313c] 00
                    nop                                     ;[313d] 00
                    nop                                     ;[313e] 00
                    nop                                     ;[313f] 00
                    nop                                     ;[3140] 00
                    nop                                     ;[3141] 00
                    nop                                     ;[3142] 00
                    nop                                     ;[3143] 00
                    nop                                     ;[3144] 00
                    nop                                     ;[3145] 00
                    nop                                     ;[3146] 00
                    nop                                     ;[3147] 00
                    nop                                     ;[3148] 00
                    nop                                     ;[3149] 00
                    nop                                     ;[314a] 00
                    nop                                     ;[314b] 00
                    nop                                     ;[314c] 00
                    nop                                     ;[314d] 00
                    nop                                     ;[314e] 00
                    nop                                     ;[314f] 00
                    nop                                     ;[3150] 00
                    nop                                     ;[3151] 00
                    nop                                     ;[3152] 00
                    nop                                     ;[3153] 00
                    nop                                     ;[3154] 00
                    nop                                     ;[3155] 00
                    nop                                     ;[3156] 00
                    nop                                     ;[3157] 00
                    nop                                     ;[3158] 00
                    nop                                     ;[3159] 00
                    nop                                     ;[315a] 00
                    nop                                     ;[315b] 00
                    nop                                     ;[315c] 00
                    nop                                     ;[315d] 00
                    nop                                     ;[315e] 00
                    nop                                     ;[315f] 00
                    nop                                     ;[3160] 00
                    nop                                     ;[3161] 00
                    nop                                     ;[3162] 00
                    nop                                     ;[3163] 00
                    nop                                     ;[3164] 00
                    nop                                     ;[3165] 00
                    nop                                     ;[3166] 00
                    nop                                     ;[3167] 00
                    nop                                     ;[3168] 00
                    nop                                     ;[3169] 00
                    nop                                     ;[316a] 00
                    nop                                     ;[316b] 00
                    nop                                     ;[316c] 00
                    nop                                     ;[316d] 00
                    nop                                     ;[316e] 00
                    nop                                     ;[316f] 00
                    nop                                     ;[3170] 00
                    nop                                     ;[3171] 00
                    nop                                     ;[3172] 00
                    nop                                     ;[3173] 00
                    nop                                     ;[3174] 00
                    nop                                     ;[3175] 00
                    nop                                     ;[3176] 00
                    nop                                     ;[3177] 00
                    nop                                     ;[3178] 00
                    nop                                     ;[3179] 00
                    nop                                     ;[317a] 00
                    nop                                     ;[317b] 00
                    nop                                     ;[317c] 00
                    nop                                     ;[317d] 00
                    nop                                     ;[317e] 00
                    nop                                     ;[317f] 00
                    nop                                     ;[3180] 00
                    nop                                     ;[3181] 00
                    nop                                     ;[3182] 00
                    nop                                     ;[3183] 00
                    nop                                     ;[3184] 00
                    nop                                     ;[3185] 00
                    nop                                     ;[3186] 00
                    nop                                     ;[3187] 00
                    nop                                     ;[3188] 00
                    nop                                     ;[3189] 00
                    nop                                     ;[318a] 00
                    nop                                     ;[318b] 00
                    nop                                     ;[318c] 00
                    nop                                     ;[318d] 00
                    nop                                     ;[318e] 00
                    nop                                     ;[318f] 00
                    nop                                     ;[3190] 00
                    nop                                     ;[3191] 00
                    nop                                     ;[3192] 00
                    nop                                     ;[3193] 00
                    nop                                     ;[3194] 00
                    nop                                     ;[3195] 00
                    nop                                     ;[3196] 00
                    nop                                     ;[3197] 00
                    nop                                     ;[3198] 00
                    nop                                     ;[3199] 00
                    nop                                     ;[319a] 00
                    nop                                     ;[319b] 00
                    nop                                     ;[319c] 00
                    nop                                     ;[319d] 00
                    nop                                     ;[319e] 00
                    nop                                     ;[319f] 00
                    nop                                     ;[31a0] 00
                    nop                                     ;[31a1] 00
                    nop                                     ;[31a2] 00
                    nop                                     ;[31a3] 00
                    nop                                     ;[31a4] 00
                    nop                                     ;[31a5] 00
                    nop                                     ;[31a6] 00
                    nop                                     ;[31a7] 00
                    nop                                     ;[31a8] 00
                    nop                                     ;[31a9] 00
                    nop                                     ;[31aa] 00
                    nop                                     ;[31ab] 00
                    nop                                     ;[31ac] 00
                    nop                                     ;[31ad] 00
                    nop                                     ;[31ae] 00
                    nop                                     ;[31af] 00
                    nop                                     ;[31b0] 00
                    nop                                     ;[31b1] 00
                    nop                                     ;[31b2] 00
                    nop                                     ;[31b3] 00
                    nop                                     ;[31b4] 00
                    nop                                     ;[31b5] 00
                    nop                                     ;[31b6] 00
                    nop                                     ;[31b7] 00
                    nop                                     ;[31b8] 00
                    nop                                     ;[31b9] 00
                    nop                                     ;[31ba] 00
                    nop                                     ;[31bb] 00
                    nop                                     ;[31bc] 00
                    nop                                     ;[31bd] 00
                    nop                                     ;[31be] 00
                    nop                                     ;[31bf] 00
                    nop                                     ;[31c0] 00
                    nop                                     ;[31c1] 00
                    nop                                     ;[31c2] 00
                    nop                                     ;[31c3] 00
                    nop                                     ;[31c4] 00
                    nop                                     ;[31c5] 00
                    nop                                     ;[31c6] 00
                    nop                                     ;[31c7] 00
                    nop                                     ;[31c8] 00
                    nop                                     ;[31c9] 00
                    nop                                     ;[31ca] 00
                    nop                                     ;[31cb] 00
                    nop                                     ;[31cc] 00
                    nop                                     ;[31cd] 00
                    nop                                     ;[31ce] 00
                    nop                                     ;[31cf] 00
                    nop                                     ;[31d0] 00
                    nop                                     ;[31d1] 00
                    nop                                     ;[31d2] 00
                    nop                                     ;[31d3] 00
                    nop                                     ;[31d4] 00
                    nop                                     ;[31d5] 00
                    nop                                     ;[31d6] 00
                    nop                                     ;[31d7] 00
                    nop                                     ;[31d8] 00
                    nop                                     ;[31d9] 00
                    nop                                     ;[31da] 00
                    nop                                     ;[31db] 00
                    nop                                     ;[31dc] 00
                    nop                                     ;[31dd] 00
                    nop                                     ;[31de] 00
                    nop                                     ;[31df] 00
                    nop                                     ;[31e0] 00
                    nop                                     ;[31e1] 00
                    nop                                     ;[31e2] 00
                    nop                                     ;[31e3] 00
                    nop                                     ;[31e4] 00
                    nop                                     ;[31e5] 00
                    nop                                     ;[31e6] 00
                    nop                                     ;[31e7] 00
                    nop                                     ;[31e8] 00
                    nop                                     ;[31e9] 00
                    nop                                     ;[31ea] 00
                    nop                                     ;[31eb] 00
                    nop                                     ;[31ec] 00
                    nop                                     ;[31ed] 00
                    nop                                     ;[31ee] 00
                    nop                                     ;[31ef] 00
                    nop                                     ;[31f0] 00
                    nop                                     ;[31f1] 00
                    nop                                     ;[31f2] 00
                    nop                                     ;[31f3] 00
                    nop                                     ;[31f4] 00
                    nop                                     ;[31f5] 00
                    nop                                     ;[31f6] 00
                    nop                                     ;[31f7] 00
                    nop                                     ;[31f8] 00
                    nop                                     ;[31f9] 00
                    nop                                     ;[31fa] 00
                    nop                                     ;[31fb] 00
                    nop                                     ;[31fc] 00
                    nop                                     ;[31fd] 00
                    nop                                     ;[31fe] 00
                    nop                                     ;[31ff] 00
                    nop                                     ;[3200] 00
                    nop                                     ;[3201] 00
                    nop                                     ;[3202] 00
                    nop                                     ;[3203] 00
                    nop                                     ;[3204] 00
                    nop                                     ;[3205] 00
                    nop                                     ;[3206] 00
                    nop                                     ;[3207] 00
                    nop                                     ;[3208] 00
                    nop                                     ;[3209] 00
                    nop                                     ;[320a] 00
                    nop                                     ;[320b] 00
                    nop                                     ;[320c] 00
                    nop                                     ;[320d] 00
                    nop                                     ;[320e] 00
                    nop                                     ;[320f] 00
                    nop                                     ;[3210] 00
                    nop                                     ;[3211] 00
                    nop                                     ;[3212] 00
                    nop                                     ;[3213] 00
                    nop                                     ;[3214] 00
                    nop                                     ;[3215] 00
                    nop                                     ;[3216] 00
                    nop                                     ;[3217] 00
                    nop                                     ;[3218] 00
                    nop                                     ;[3219] 00
                    nop                                     ;[321a] 00
                    nop                                     ;[321b] 00
                    nop                                     ;[321c] 00
                    nop                                     ;[321d] 00
                    nop                                     ;[321e] 00
                    nop                                     ;[321f] 00
                    nop                                     ;[3220] 00
                    nop                                     ;[3221] 00
                    nop                                     ;[3222] 00
                    nop                                     ;[3223] 00
                    nop                                     ;[3224] 00
                    nop                                     ;[3225] 00
                    nop                                     ;[3226] 00
                    nop                                     ;[3227] 00
                    nop                                     ;[3228] 00
                    nop                                     ;[3229] 00
                    nop                                     ;[322a] 00
                    nop                                     ;[322b] 00
                    nop                                     ;[322c] 00
                    nop                                     ;[322d] 00
                    nop                                     ;[322e] 00
                    nop                                     ;[322f] 00
                    nop                                     ;[3230] 00
                    nop                                     ;[3231] 00
                    nop                                     ;[3232] 00
                    nop                                     ;[3233] 00
                    nop                                     ;[3234] 00
                    nop                                     ;[3235] 00
                    nop                                     ;[3236] 00
                    nop                                     ;[3237] 00
                    nop                                     ;[3238] 00
                    nop                                     ;[3239] 00
                    nop                                     ;[323a] 00
                    nop                                     ;[323b] 00
                    nop                                     ;[323c] 00
                    nop                                     ;[323d] 00
                    nop                                     ;[323e] 00
                    nop                                     ;[323f] 00
                    nop                                     ;[3240] 00
                    nop                                     ;[3241] 00
                    nop                                     ;[3242] 00
                    nop                                     ;[3243] 00
                    nop                                     ;[3244] 00
                    nop                                     ;[3245] 00
                    nop                                     ;[3246] 00
                    nop                                     ;[3247] 00
                    nop                                     ;[3248] 00
                    nop                                     ;[3249] 00
                    nop                                     ;[324a] 00
                    nop                                     ;[324b] 00
                    nop                                     ;[324c] 00
                    nop                                     ;[324d] 00
                    nop                                     ;[324e] 00
                    nop                                     ;[324f] 00
                    nop                                     ;[3250] 00
                    nop                                     ;[3251] 00
                    nop                                     ;[3252] 00
                    nop                                     ;[3253] 00
                    nop                                     ;[3254] 00
                    nop                                     ;[3255] 00
                    nop                                     ;[3256] 00
                    nop                                     ;[3257] 00
                    nop                                     ;[3258] 00
                    nop                                     ;[3259] 00
                    nop                                     ;[325a] 00
                    nop                                     ;[325b] 00
                    nop                                     ;[325c] 00
                    nop                                     ;[325d] 00
                    nop                                     ;[325e] 00
                    nop                                     ;[325f] 00
                    nop                                     ;[3260] 00
                    nop                                     ;[3261] 00
                    nop                                     ;[3262] 00
                    nop                                     ;[3263] 00
                    nop                                     ;[3264] 00
                    nop                                     ;[3265] 00
                    nop                                     ;[3266] 00
                    nop                                     ;[3267] 00
                    nop                                     ;[3268] 00
                    nop                                     ;[3269] 00
                    nop                                     ;[326a] 00
                    nop                                     ;[326b] 00
                    nop                                     ;[326c] 00
                    nop                                     ;[326d] 00
                    nop                                     ;[326e] 00
                    nop                                     ;[326f] 00
                    nop                                     ;[3270] 00
                    nop                                     ;[3271] 00
                    nop                                     ;[3272] 00
                    nop                                     ;[3273] 00
                    nop                                     ;[3274] 00
                    nop                                     ;[3275] 00
                    nop                                     ;[3276] 00
                    nop                                     ;[3277] 00
                    nop                                     ;[3278] 00
                    nop                                     ;[3279] 00
                    nop                                     ;[327a] 00
                    nop                                     ;[327b] 00
                    nop                                     ;[327c] 00
                    nop                                     ;[327d] 00
                    nop                                     ;[327e] 00
                    nop                                     ;[327f] 00
                    nop                                     ;[3280] 00
                    nop                                     ;[3281] 00
                    nop                                     ;[3282] 00
                    nop                                     ;[3283] 00
                    nop                                     ;[3284] 00
                    nop                                     ;[3285] 00
                    nop                                     ;[3286] 00
                    nop                                     ;[3287] 00
                    nop                                     ;[3288] 00
                    nop                                     ;[3289] 00
                    nop                                     ;[328a] 00
                    nop                                     ;[328b] 00
                    nop                                     ;[328c] 00
                    nop                                     ;[328d] 00
                    nop                                     ;[328e] 00
                    nop                                     ;[328f] 00
                    nop                                     ;[3290] 00
                    nop                                     ;[3291] 00
                    nop                                     ;[3292] 00
                    nop                                     ;[3293] 00
                    nop                                     ;[3294] 00
                    nop                                     ;[3295] 00
                    nop                                     ;[3296] 00
                    nop                                     ;[3297] 00
                    nop                                     ;[3298] 00
                    nop                                     ;[3299] 00
                    nop                                     ;[329a] 00
                    nop                                     ;[329b] 00
                    nop                                     ;[329c] 00
                    nop                                     ;[329d] 00
                    nop                                     ;[329e] 00
                    nop                                     ;[329f] 00
                    nop                                     ;[32a0] 00
                    nop                                     ;[32a1] 00
                    nop                                     ;[32a2] 00
                    nop                                     ;[32a3] 00
                    nop                                     ;[32a4] 00
                    nop                                     ;[32a5] 00
                    nop                                     ;[32a6] 00
                    nop                                     ;[32a7] 00
                    nop                                     ;[32a8] 00
                    nop                                     ;[32a9] 00
                    nop                                     ;[32aa] 00
                    nop                                     ;[32ab] 00
                    nop                                     ;[32ac] 00
                    nop                                     ;[32ad] 00
                    nop                                     ;[32ae] 00
                    nop                                     ;[32af] 00
                    nop                                     ;[32b0] 00
                    nop                                     ;[32b1] 00
                    nop                                     ;[32b2] 00
                    nop                                     ;[32b3] 00
                    nop                                     ;[32b4] 00
                    nop                                     ;[32b5] 00
                    nop                                     ;[32b6] 00
                    nop                                     ;[32b7] 00
                    nop                                     ;[32b8] 00
                    nop                                     ;[32b9] 00
                    nop                                     ;[32ba] 00
                    nop                                     ;[32bb] 00
                    nop                                     ;[32bc] 00
                    nop                                     ;[32bd] 00
                    nop                                     ;[32be] 00
                    nop                                     ;[32bf] 00
                    nop                                     ;[32c0] 00
                    nop                                     ;[32c1] 00
                    nop                                     ;[32c2] 00
                    nop                                     ;[32c3] 00
                    nop                                     ;[32c4] 00
                    nop                                     ;[32c5] 00
                    nop                                     ;[32c6] 00
                    nop                                     ;[32c7] 00
                    nop                                     ;[32c8] 00
                    nop                                     ;[32c9] 00
                    nop                                     ;[32ca] 00
                    nop                                     ;[32cb] 00
                    nop                                     ;[32cc] 00
                    nop                                     ;[32cd] 00
                    nop                                     ;[32ce] 00
                    nop                                     ;[32cf] 00
                    nop                                     ;[32d0] 00
                    nop                                     ;[32d1] 00
                    nop                                     ;[32d2] 00
                    nop                                     ;[32d3] 00
                    nop                                     ;[32d4] 00
                    nop                                     ;[32d5] 00
                    nop                                     ;[32d6] 00
                    nop                                     ;[32d7] 00
                    nop                                     ;[32d8] 00
                    nop                                     ;[32d9] 00
                    nop                                     ;[32da] 00
                    nop                                     ;[32db] 00
                    nop                                     ;[32dc] 00
                    nop                                     ;[32dd] 00
                    nop                                     ;[32de] 00
                    nop                                     ;[32df] 00
                    nop                                     ;[32e0] 00
                    nop                                     ;[32e1] 00
                    nop                                     ;[32e2] 00
                    nop                                     ;[32e3] 00
                    nop                                     ;[32e4] 00
                    nop                                     ;[32e5] 00
                    nop                                     ;[32e6] 00
                    nop                                     ;[32e7] 00
                    nop                                     ;[32e8] 00
                    nop                                     ;[32e9] 00
                    nop                                     ;[32ea] 00
                    nop                                     ;[32eb] 00
                    nop                                     ;[32ec] 00
                    nop                                     ;[32ed] 00
                    nop                                     ;[32ee] 00
                    nop                                     ;[32ef] 00
                    nop                                     ;[32f0] 00
                    nop                                     ;[32f1] 00
                    nop                                     ;[32f2] 00
                    nop                                     ;[32f3] 00
                    nop                                     ;[32f4] 00
                    nop                                     ;[32f5] 00
                    nop                                     ;[32f6] 00
                    nop                                     ;[32f7] 00
                    nop                                     ;[32f8] 00
                    nop                                     ;[32f9] 00
                    nop                                     ;[32fa] 00
                    nop                                     ;[32fb] 00
                    nop                                     ;[32fc] 00
                    nop                                     ;[32fd] 00
                    nop                                     ;[32fe] 00
                    nop                                     ;[32ff] 00
                    nop                                     ;[3300] 00
                    nop                                     ;[3301] 00
                    nop                                     ;[3302] 00
                    nop                                     ;[3303] 00
                    nop                                     ;[3304] 00
                    nop                                     ;[3305] 00
                    nop                                     ;[3306] 00
                    nop                                     ;[3307] 00
                    nop                                     ;[3308] 00
                    nop                                     ;[3309] 00
                    nop                                     ;[330a] 00
                    nop                                     ;[330b] 00
                    nop                                     ;[330c] 00
                    nop                                     ;[330d] 00
                    nop                                     ;[330e] 00
                    nop                                     ;[330f] 00
                    nop                                     ;[3310] 00
                    nop                                     ;[3311] 00
                    nop                                     ;[3312] 00
                    nop                                     ;[3313] 00
                    nop                                     ;[3314] 00
                    nop                                     ;[3315] 00
                    nop                                     ;[3316] 00
                    nop                                     ;[3317] 00
                    nop                                     ;[3318] 00
                    nop                                     ;[3319] 00
                    nop                                     ;[331a] 00
                    nop                                     ;[331b] 00
                    nop                                     ;[331c] 00
                    nop                                     ;[331d] 00
                    nop                                     ;[331e] 00
                    nop                                     ;[331f] 00
                    nop                                     ;[3320] 00
                    nop                                     ;[3321] 00
                    nop                                     ;[3322] 00
                    nop                                     ;[3323] 00
                    nop                                     ;[3324] 00
                    nop                                     ;[3325] 00
                    nop                                     ;[3326] 00
                    nop                                     ;[3327] 00
                    nop                                     ;[3328] 00
                    nop                                     ;[3329] 00
                    nop                                     ;[332a] 00
                    nop                                     ;[332b] 00
                    nop                                     ;[332c] 00
                    nop                                     ;[332d] 00
                    nop                                     ;[332e] 00
                    nop                                     ;[332f] 00
                    nop                                     ;[3330] 00
                    nop                                     ;[3331] 00
                    nop                                     ;[3332] 00
                    nop                                     ;[3333] 00
                    nop                                     ;[3334] 00
                    nop                                     ;[3335] 00
                    nop                                     ;[3336] 00
                    nop                                     ;[3337] 00
                    nop                                     ;[3338] 00
                    nop                                     ;[3339] 00
                    nop                                     ;[333a] 00
                    nop                                     ;[333b] 00
                    nop                                     ;[333c] 00
                    nop                                     ;[333d] 00
                    nop                                     ;[333e] 00
                    nop                                     ;[333f] 00
                    nop                                     ;[3340] 00
                    nop                                     ;[3341] 00
                    nop                                     ;[3342] 00
                    nop                                     ;[3343] 00
                    nop                                     ;[3344] 00
                    nop                                     ;[3345] 00
                    nop                                     ;[3346] 00
                    nop                                     ;[3347] 00
                    nop                                     ;[3348] 00
                    nop                                     ;[3349] 00
                    nop                                     ;[334a] 00
                    nop                                     ;[334b] 00
                    nop                                     ;[334c] 00
                    nop                                     ;[334d] 00
                    nop                                     ;[334e] 00
                    nop                                     ;[334f] 00
                    nop                                     ;[3350] 00
                    nop                                     ;[3351] 00
                    nop                                     ;[3352] 00
                    nop                                     ;[3353] 00
                    nop                                     ;[3354] 00
                    nop                                     ;[3355] 00
                    nop                                     ;[3356] 00
                    nop                                     ;[3357] 00
                    nop                                     ;[3358] 00
                    nop                                     ;[3359] 00
                    nop                                     ;[335a] 00
                    nop                                     ;[335b] 00
                    nop                                     ;[335c] 00
                    nop                                     ;[335d] 00
                    nop                                     ;[335e] 00
                    nop                                     ;[335f] 00
                    nop                                     ;[3360] 00
                    nop                                     ;[3361] 00
                    nop                                     ;[3362] 00
                    nop                                     ;[3363] 00
                    nop                                     ;[3364] 00
                    nop                                     ;[3365] 00
                    nop                                     ;[3366] 00
                    nop                                     ;[3367] 00
                    nop                                     ;[3368] 00
                    nop                                     ;[3369] 00
                    nop                                     ;[336a] 00
                    nop                                     ;[336b] 00
                    nop                                     ;[336c] 00
                    nop                                     ;[336d] 00
                    nop                                     ;[336e] 00
                    nop                                     ;[336f] 00
                    nop                                     ;[3370] 00
                    nop                                     ;[3371] 00
                    nop                                     ;[3372] 00
                    nop                                     ;[3373] 00
                    nop                                     ;[3374] 00
                    nop                                     ;[3375] 00
                    nop                                     ;[3376] 00
                    nop                                     ;[3377] 00
                    nop                                     ;[3378] 00
                    nop                                     ;[3379] 00
                    nop                                     ;[337a] 00
                    nop                                     ;[337b] 00
                    nop                                     ;[337c] 00
                    nop                                     ;[337d] 00
                    nop                                     ;[337e] 00
                    nop                                     ;[337f] 00
                    nop                                     ;[3380] 00
                    nop                                     ;[3381] 00
                    nop                                     ;[3382] 00
                    nop                                     ;[3383] 00
                    nop                                     ;[3384] 00
                    nop                                     ;[3385] 00
                    nop                                     ;[3386] 00
                    nop                                     ;[3387] 00
                    nop                                     ;[3388] 00
                    nop                                     ;[3389] 00
                    nop                                     ;[338a] 00
                    nop                                     ;[338b] 00
                    nop                                     ;[338c] 00
                    nop                                     ;[338d] 00
                    nop                                     ;[338e] 00
                    nop                                     ;[338f] 00
                    nop                                     ;[3390] 00
                    nop                                     ;[3391] 00
                    nop                                     ;[3392] 00
                    nop                                     ;[3393] 00
                    nop                                     ;[3394] 00
                    nop                                     ;[3395] 00
                    nop                                     ;[3396] 00
                    nop                                     ;[3397] 00
                    nop                                     ;[3398] 00
                    nop                                     ;[3399] 00
                    nop                                     ;[339a] 00
                    nop                                     ;[339b] 00
                    nop                                     ;[339c] 00
                    nop                                     ;[339d] 00
                    nop                                     ;[339e] 00
                    nop                                     ;[339f] 00
                    nop                                     ;[33a0] 00
                    nop                                     ;[33a1] 00
                    nop                                     ;[33a2] 00
                    nop                                     ;[33a3] 00
                    nop                                     ;[33a4] 00
                    nop                                     ;[33a5] 00
                    nop                                     ;[33a6] 00
                    nop                                     ;[33a7] 00
                    nop                                     ;[33a8] 00
                    nop                                     ;[33a9] 00
                    nop                                     ;[33aa] 00
                    nop                                     ;[33ab] 00
                    nop                                     ;[33ac] 00
                    nop                                     ;[33ad] 00
                    nop                                     ;[33ae] 00
                    nop                                     ;[33af] 00
                    nop                                     ;[33b0] 00
                    nop                                     ;[33b1] 00
                    nop                                     ;[33b2] 00
                    nop                                     ;[33b3] 00
                    nop                                     ;[33b4] 00
                    nop                                     ;[33b5] 00
                    nop                                     ;[33b6] 00
                    nop                                     ;[33b7] 00
                    nop                                     ;[33b8] 00
                    nop                                     ;[33b9] 00
                    nop                                     ;[33ba] 00
                    nop                                     ;[33bb] 00
                    nop                                     ;[33bc] 00
                    nop                                     ;[33bd] 00
                    nop                                     ;[33be] 00
                    nop                                     ;[33bf] 00
                    nop                                     ;[33c0] 00
                    nop                                     ;[33c1] 00
                    nop                                     ;[33c2] 00
                    nop                                     ;[33c3] 00
                    nop                                     ;[33c4] 00
                    nop                                     ;[33c5] 00
                    nop                                     ;[33c6] 00
                    nop                                     ;[33c7] 00
                    nop                                     ;[33c8] 00
                    nop                                     ;[33c9] 00
                    nop                                     ;[33ca] 00
                    nop                                     ;[33cb] 00
                    nop                                     ;[33cc] 00
                    nop                                     ;[33cd] 00
                    nop                                     ;[33ce] 00
                    nop                                     ;[33cf] 00
                    nop                                     ;[33d0] 00
                    nop                                     ;[33d1] 00
                    nop                                     ;[33d2] 00
                    nop                                     ;[33d3] 00
                    nop                                     ;[33d4] 00
                    nop                                     ;[33d5] 00
                    nop                                     ;[33d6] 00
                    nop                                     ;[33d7] 00
                    nop                                     ;[33d8] 00
                    nop                                     ;[33d9] 00
                    nop                                     ;[33da] 00
                    nop                                     ;[33db] 00
                    nop                                     ;[33dc] 00
                    nop                                     ;[33dd] 00
                    nop                                     ;[33de] 00
                    nop                                     ;[33df] 00
                    nop                                     ;[33e0] 00
                    nop                                     ;[33e1] 00
                    nop                                     ;[33e2] 00
                    nop                                     ;[33e3] 00
                    nop                                     ;[33e4] 00
                    nop                                     ;[33e5] 00
                    nop                                     ;[33e6] 00
                    nop                                     ;[33e7] 00
                    nop                                     ;[33e8] 00
                    nop                                     ;[33e9] 00
                    nop                                     ;[33ea] 00
                    nop                                     ;[33eb] 00
                    nop                                     ;[33ec] 00
                    nop                                     ;[33ed] 00
                    nop                                     ;[33ee] 00
                    nop                                     ;[33ef] 00
                    nop                                     ;[33f0] 00
                    nop                                     ;[33f1] 00
                    nop                                     ;[33f2] 00
                    nop                                     ;[33f3] 00
                    nop                                     ;[33f4] 00
                    nop                                     ;[33f5] 00
                    nop                                     ;[33f6] 00
                    nop                                     ;[33f7] 00
                    nop                                     ;[33f8] 00
                    nop                                     ;[33f9] 00
                    nop                                     ;[33fa] 00
                    nop                                     ;[33fb] 00
                    nop                                     ;[33fc] 00
                    nop                                     ;[33fd] 00
                    nop                                     ;[33fe] 00
                    nop                                     ;[33ff] 00
                    nop                                     ;[3400] 00
                    nop                                     ;[3401] 00
                    nop                                     ;[3402] 00
                    nop                                     ;[3403] 00
                    nop                                     ;[3404] 00
                    nop                                     ;[3405] 00
                    nop                                     ;[3406] 00
                    nop                                     ;[3407] 00
                    nop                                     ;[3408] 00
                    nop                                     ;[3409] 00
                    nop                                     ;[340a] 00
                    nop                                     ;[340b] 00
                    nop                                     ;[340c] 00
                    nop                                     ;[340d] 00
                    nop                                     ;[340e] 00
                    nop                                     ;[340f] 00
                    nop                                     ;[3410] 00
                    nop                                     ;[3411] 00
                    nop                                     ;[3412] 00
                    nop                                     ;[3413] 00
                    nop                                     ;[3414] 00
                    nop                                     ;[3415] 00
                    nop                                     ;[3416] 00
                    nop                                     ;[3417] 00
                    nop                                     ;[3418] 00
                    nop                                     ;[3419] 00
                    nop                                     ;[341a] 00
                    nop                                     ;[341b] 00
                    nop                                     ;[341c] 00
                    nop                                     ;[341d] 00
                    nop                                     ;[341e] 00
                    nop                                     ;[341f] 00
                    nop                                     ;[3420] 00
                    nop                                     ;[3421] 00
                    nop                                     ;[3422] 00
                    nop                                     ;[3423] 00
                    nop                                     ;[3424] 00
                    nop                                     ;[3425] 00
                    nop                                     ;[3426] 00
                    nop                                     ;[3427] 00
                    nop                                     ;[3428] 00
                    nop                                     ;[3429] 00
                    nop                                     ;[342a] 00
                    nop                                     ;[342b] 00
                    nop                                     ;[342c] 00
                    nop                                     ;[342d] 00
                    nop                                     ;[342e] 00
                    nop                                     ;[342f] 00
                    nop                                     ;[3430] 00
                    nop                                     ;[3431] 00
                    nop                                     ;[3432] 00
                    nop                                     ;[3433] 00
                    nop                                     ;[3434] 00
                    nop                                     ;[3435] 00
                    nop                                     ;[3436] 00
                    nop                                     ;[3437] 00
                    nop                                     ;[3438] 00
                    nop                                     ;[3439] 00
                    nop                                     ;[343a] 00
                    nop                                     ;[343b] 00
                    nop                                     ;[343c] 00
                    nop                                     ;[343d] 00
                    nop                                     ;[343e] 00
                    nop                                     ;[343f] 00
                    nop                                     ;[3440] 00
                    nop                                     ;[3441] 00
                    nop                                     ;[3442] 00
                    nop                                     ;[3443] 00
                    nop                                     ;[3444] 00
                    nop                                     ;[3445] 00
                    nop                                     ;[3446] 00
                    nop                                     ;[3447] 00
                    nop                                     ;[3448] 00
                    nop                                     ;[3449] 00
                    nop                                     ;[344a] 00
                    nop                                     ;[344b] 00
                    nop                                     ;[344c] 00
                    nop                                     ;[344d] 00
                    nop                                     ;[344e] 00
                    nop                                     ;[344f] 00
                    nop                                     ;[3450] 00
                    nop                                     ;[3451] 00
                    nop                                     ;[3452] 00
                    nop                                     ;[3453] 00
                    nop                                     ;[3454] 00
                    nop                                     ;[3455] 00
                    nop                                     ;[3456] 00
                    nop                                     ;[3457] 00
                    nop                                     ;[3458] 00
                    nop                                     ;[3459] 00
                    nop                                     ;[345a] 00
                    nop                                     ;[345b] 00
                    nop                                     ;[345c] 00
                    nop                                     ;[345d] 00
                    nop                                     ;[345e] 00
                    nop                                     ;[345f] 00
                    nop                                     ;[3460] 00
                    nop                                     ;[3461] 00
                    nop                                     ;[3462] 00
                    nop                                     ;[3463] 00
                    nop                                     ;[3464] 00
                    nop                                     ;[3465] 00
                    nop                                     ;[3466] 00
                    nop                                     ;[3467] 00
                    nop                                     ;[3468] 00
                    nop                                     ;[3469] 00
                    nop                                     ;[346a] 00
                    nop                                     ;[346b] 00
                    nop                                     ;[346c] 00
                    nop                                     ;[346d] 00
                    nop                                     ;[346e] 00
                    nop                                     ;[346f] 00
                    nop                                     ;[3470] 00
                    nop                                     ;[3471] 00
                    nop                                     ;[3472] 00
                    nop                                     ;[3473] 00
                    nop                                     ;[3474] 00
                    nop                                     ;[3475] 00
                    nop                                     ;[3476] 00
                    nop                                     ;[3477] 00
                    nop                                     ;[3478] 00
                    nop                                     ;[3479] 00
                    nop                                     ;[347a] 00
                    nop                                     ;[347b] 00
                    nop                                     ;[347c] 00
                    nop                                     ;[347d] 00
                    nop                                     ;[347e] 00
                    nop                                     ;[347f] 00
                    nop                                     ;[3480] 00
                    nop                                     ;[3481] 00
                    nop                                     ;[3482] 00
                    nop                                     ;[3483] 00
                    nop                                     ;[3484] 00
                    nop                                     ;[3485] 00
                    nop                                     ;[3486] 00
                    nop                                     ;[3487] 00
                    nop                                     ;[3488] 00
                    nop                                     ;[3489] 00
                    nop                                     ;[348a] 00
                    nop                                     ;[348b] 00
                    nop                                     ;[348c] 00
                    nop                                     ;[348d] 00
                    nop                                     ;[348e] 00
                    nop                                     ;[348f] 00
                    nop                                     ;[3490] 00
                    nop                                     ;[3491] 00
                    nop                                     ;[3492] 00
                    nop                                     ;[3493] 00
                    nop                                     ;[3494] 00
                    nop                                     ;[3495] 00
                    nop                                     ;[3496] 00
                    nop                                     ;[3497] 00
                    nop                                     ;[3498] 00
                    nop                                     ;[3499] 00
                    nop                                     ;[349a] 00
                    nop                                     ;[349b] 00
                    nop                                     ;[349c] 00
                    nop                                     ;[349d] 00
                    nop                                     ;[349e] 00
                    nop                                     ;[349f] 00
                    nop                                     ;[34a0] 00
                    nop                                     ;[34a1] 00
                    nop                                     ;[34a2] 00
                    nop                                     ;[34a3] 00
                    nop                                     ;[34a4] 00
                    nop                                     ;[34a5] 00
                    nop                                     ;[34a6] 00
                    nop                                     ;[34a7] 00
                    nop                                     ;[34a8] 00
                    nop                                     ;[34a9] 00
                    nop                                     ;[34aa] 00
                    nop                                     ;[34ab] 00
                    nop                                     ;[34ac] 00
                    nop                                     ;[34ad] 00
                    nop                                     ;[34ae] 00
                    nop                                     ;[34af] 00
                    nop                                     ;[34b0] 00
                    nop                                     ;[34b1] 00
                    nop                                     ;[34b2] 00
                    nop                                     ;[34b3] 00
                    nop                                     ;[34b4] 00
                    nop                                     ;[34b5] 00
                    nop                                     ;[34b6] 00
                    nop                                     ;[34b7] 00
                    nop                                     ;[34b8] 00
                    nop                                     ;[34b9] 00
                    nop                                     ;[34ba] 00
                    nop                                     ;[34bb] 00
                    nop                                     ;[34bc] 00
                    nop                                     ;[34bd] 00
                    nop                                     ;[34be] 00
                    nop                                     ;[34bf] 00
                    nop                                     ;[34c0] 00
                    nop                                     ;[34c1] 00
                    nop                                     ;[34c2] 00
                    nop                                     ;[34c3] 00
                    nop                                     ;[34c4] 00
                    nop                                     ;[34c5] 00
                    nop                                     ;[34c6] 00
                    nop                                     ;[34c7] 00
                    nop                                     ;[34c8] 00
                    nop                                     ;[34c9] 00
                    nop                                     ;[34ca] 00
                    nop                                     ;[34cb] 00
                    nop                                     ;[34cc] 00
                    nop                                     ;[34cd] 00
                    nop                                     ;[34ce] 00
                    nop                                     ;[34cf] 00
                    nop                                     ;[34d0] 00
                    nop                                     ;[34d1] 00
                    nop                                     ;[34d2] 00
                    nop                                     ;[34d3] 00
                    nop                                     ;[34d4] 00
                    nop                                     ;[34d5] 00
                    nop                                     ;[34d6] 00
                    nop                                     ;[34d7] 00
                    nop                                     ;[34d8] 00
                    nop                                     ;[34d9] 00
                    nop                                     ;[34da] 00
                    nop                                     ;[34db] 00
                    nop                                     ;[34dc] 00
                    nop                                     ;[34dd] 00
                    nop                                     ;[34de] 00
                    nop                                     ;[34df] 00
                    nop                                     ;[34e0] 00
                    nop                                     ;[34e1] 00
                    nop                                     ;[34e2] 00
                    nop                                     ;[34e3] 00
                    nop                                     ;[34e4] 00
                    nop                                     ;[34e5] 00
                    nop                                     ;[34e6] 00
                    nop                                     ;[34e7] 00
                    nop                                     ;[34e8] 00
                    nop                                     ;[34e9] 00
                    nop                                     ;[34ea] 00
                    nop                                     ;[34eb] 00
                    nop                                     ;[34ec] 00
                    nop                                     ;[34ed] 00
                    nop                                     ;[34ee] 00
                    nop                                     ;[34ef] 00
                    nop                                     ;[34f0] 00
                    nop                                     ;[34f1] 00
                    nop                                     ;[34f2] 00
                    nop                                     ;[34f3] 00
                    nop                                     ;[34f4] 00
                    nop                                     ;[34f5] 00
                    nop                                     ;[34f6] 00
                    nop                                     ;[34f7] 00
                    nop                                     ;[34f8] 00
                    nop                                     ;[34f9] 00
                    nop                                     ;[34fa] 00
                    nop                                     ;[34fb] 00
                    nop                                     ;[34fc] 00
                    nop                                     ;[34fd] 00
                    nop                                     ;[34fe] 00
                    nop                                     ;[34ff] 00
                    nop                                     ;[3500] 00
                    nop                                     ;[3501] 00
                    nop                                     ;[3502] 00
                    nop                                     ;[3503] 00
                    nop                                     ;[3504] 00
                    nop                                     ;[3505] 00
                    nop                                     ;[3506] 00
                    nop                                     ;[3507] 00
                    nop                                     ;[3508] 00
                    nop                                     ;[3509] 00
                    nop                                     ;[350a] 00
                    nop                                     ;[350b] 00
                    nop                                     ;[350c] 00
                    nop                                     ;[350d] 00
                    nop                                     ;[350e] 00
                    nop                                     ;[350f] 00
                    nop                                     ;[3510] 00
                    nop                                     ;[3511] 00
                    nop                                     ;[3512] 00
                    nop                                     ;[3513] 00
                    nop                                     ;[3514] 00
                    nop                                     ;[3515] 00
                    nop                                     ;[3516] 00
                    nop                                     ;[3517] 00
                    nop                                     ;[3518] 00
                    nop                                     ;[3519] 00
                    nop                                     ;[351a] 00
                    nop                                     ;[351b] 00
                    nop                                     ;[351c] 00
                    nop                                     ;[351d] 00
                    nop                                     ;[351e] 00
                    nop                                     ;[351f] 00
                    nop                                     ;[3520] 00
                    nop                                     ;[3521] 00
                    nop                                     ;[3522] 00
                    nop                                     ;[3523] 00
                    nop                                     ;[3524] 00
                    nop                                     ;[3525] 00
                    nop                                     ;[3526] 00
                    nop                                     ;[3527] 00
                    nop                                     ;[3528] 00
                    nop                                     ;[3529] 00
                    nop                                     ;[352a] 00
                    nop                                     ;[352b] 00
                    nop                                     ;[352c] 00
                    nop                                     ;[352d] 00
                    nop                                     ;[352e] 00
                    nop                                     ;[352f] 00
                    nop                                     ;[3530] 00
                    nop                                     ;[3531] 00
                    nop                                     ;[3532] 00
                    nop                                     ;[3533] 00
                    nop                                     ;[3534] 00
                    nop                                     ;[3535] 00
                    nop                                     ;[3536] 00
                    nop                                     ;[3537] 00
                    nop                                     ;[3538] 00
                    nop                                     ;[3539] 00
                    nop                                     ;[353a] 00
                    nop                                     ;[353b] 00
                    nop                                     ;[353c] 00
                    nop                                     ;[353d] 00
                    nop                                     ;[353e] 00
                    nop                                     ;[353f] 00
                    nop                                     ;[3540] 00
                    nop                                     ;[3541] 00
                    nop                                     ;[3542] 00
                    nop                                     ;[3543] 00
                    nop                                     ;[3544] 00
                    nop                                     ;[3545] 00
                    nop                                     ;[3546] 00
                    nop                                     ;[3547] 00
                    nop                                     ;[3548] 00
                    nop                                     ;[3549] 00
                    nop                                     ;[354a] 00
                    nop                                     ;[354b] 00
                    nop                                     ;[354c] 00
                    nop                                     ;[354d] 00
                    nop                                     ;[354e] 00
                    nop                                     ;[354f] 00
                    nop                                     ;[3550] 00
                    nop                                     ;[3551] 00
                    nop                                     ;[3552] 00
                    nop                                     ;[3553] 00
                    nop                                     ;[3554] 00
                    nop                                     ;[3555] 00
                    nop                                     ;[3556] 00
                    nop                                     ;[3557] 00
                    nop                                     ;[3558] 00
                    nop                                     ;[3559] 00
                    nop                                     ;[355a] 00
                    nop                                     ;[355b] 00
                    nop                                     ;[355c] 00
                    nop                                     ;[355d] 00
                    nop                                     ;[355e] 00
                    nop                                     ;[355f] 00
                    nop                                     ;[3560] 00
                    nop                                     ;[3561] 00
                    nop                                     ;[3562] 00
                    nop                                     ;[3563] 00
                    nop                                     ;[3564] 00
                    nop                                     ;[3565] 00
                    nop                                     ;[3566] 00
                    nop                                     ;[3567] 00
                    nop                                     ;[3568] 00
                    nop                                     ;[3569] 00
                    nop                                     ;[356a] 00
                    nop                                     ;[356b] 00
                    nop                                     ;[356c] 00
                    nop                                     ;[356d] 00
                    nop                                     ;[356e] 00
                    nop                                     ;[356f] 00
                    nop                                     ;[3570] 00
                    nop                                     ;[3571] 00
                    nop                                     ;[3572] 00
                    nop                                     ;[3573] 00
                    nop                                     ;[3574] 00
                    nop                                     ;[3575] 00
                    nop                                     ;[3576] 00
                    nop                                     ;[3577] 00
                    nop                                     ;[3578] 00
                    nop                                     ;[3579] 00
                    nop                                     ;[357a] 00
                    nop                                     ;[357b] 00
                    nop                                     ;[357c] 00
                    nop                                     ;[357d] 00
                    nop                                     ;[357e] 00
                    nop                                     ;[357f] 00
                    nop                                     ;[3580] 00
                    nop                                     ;[3581] 00
                    nop                                     ;[3582] 00
                    nop                                     ;[3583] 00
                    nop                                     ;[3584] 00
                    nop                                     ;[3585] 00
                    nop                                     ;[3586] 00
                    nop                                     ;[3587] 00
                    nop                                     ;[3588] 00
                    nop                                     ;[3589] 00
                    nop                                     ;[358a] 00
                    nop                                     ;[358b] 00
                    nop                                     ;[358c] 00
                    nop                                     ;[358d] 00
                    nop                                     ;[358e] 00
                    nop                                     ;[358f] 00
                    nop                                     ;[3590] 00
                    nop                                     ;[3591] 00
                    nop                                     ;[3592] 00
                    nop                                     ;[3593] 00
                    nop                                     ;[3594] 00
                    nop                                     ;[3595] 00
                    nop                                     ;[3596] 00
                    nop                                     ;[3597] 00
                    nop                                     ;[3598] 00
                    nop                                     ;[3599] 00
                    nop                                     ;[359a] 00
                    nop                                     ;[359b] 00
                    nop                                     ;[359c] 00
                    nop                                     ;[359d] 00
                    nop                                     ;[359e] 00
                    nop                                     ;[359f] 00
                    nop                                     ;[35a0] 00
                    nop                                     ;[35a1] 00
                    nop                                     ;[35a2] 00
                    nop                                     ;[35a3] 00
                    nop                                     ;[35a4] 00
                    nop                                     ;[35a5] 00
                    nop                                     ;[35a6] 00
                    nop                                     ;[35a7] 00
                    nop                                     ;[35a8] 00
                    nop                                     ;[35a9] 00
                    nop                                     ;[35aa] 00
                    nop                                     ;[35ab] 00
                    nop                                     ;[35ac] 00
                    nop                                     ;[35ad] 00
                    nop                                     ;[35ae] 00
                    nop                                     ;[35af] 00
                    nop                                     ;[35b0] 00
                    nop                                     ;[35b1] 00
                    nop                                     ;[35b2] 00
                    nop                                     ;[35b3] 00
                    nop                                     ;[35b4] 00
                    nop                                     ;[35b5] 00
                    nop                                     ;[35b6] 00
                    nop                                     ;[35b7] 00
                    nop                                     ;[35b8] 00
                    nop                                     ;[35b9] 00
                    nop                                     ;[35ba] 00
                    nop                                     ;[35bb] 00
                    nop                                     ;[35bc] 00
                    nop                                     ;[35bd] 00
                    nop                                     ;[35be] 00
                    nop                                     ;[35bf] 00
                    nop                                     ;[35c0] 00
                    nop                                     ;[35c1] 00
                    nop                                     ;[35c2] 00
                    nop                                     ;[35c3] 00
                    nop                                     ;[35c4] 00
                    nop                                     ;[35c5] 00
                    nop                                     ;[35c6] 00
                    nop                                     ;[35c7] 00
                    nop                                     ;[35c8] 00
                    nop                                     ;[35c9] 00
                    nop                                     ;[35ca] 00
                    nop                                     ;[35cb] 00
                    nop                                     ;[35cc] 00
                    nop                                     ;[35cd] 00
                    nop                                     ;[35ce] 00
                    nop                                     ;[35cf] 00
                    nop                                     ;[35d0] 00
                    nop                                     ;[35d1] 00
                    nop                                     ;[35d2] 00
                    nop                                     ;[35d3] 00
                    nop                                     ;[35d4] 00
                    nop                                     ;[35d5] 00
                    nop                                     ;[35d6] 00
                    nop                                     ;[35d7] 00
                    nop                                     ;[35d8] 00
                    nop                                     ;[35d9] 00
                    nop                                     ;[35da] 00
                    nop                                     ;[35db] 00
                    nop                                     ;[35dc] 00
                    nop                                     ;[35dd] 00
                    nop                                     ;[35de] 00
                    nop                                     ;[35df] 00
                    nop                                     ;[35e0] 00
                    nop                                     ;[35e1] 00
                    nop                                     ;[35e2] 00
                    nop                                     ;[35e3] 00
                    nop                                     ;[35e4] 00
                    nop                                     ;[35e5] 00
                    nop                                     ;[35e6] 00
                    nop                                     ;[35e7] 00
                    nop                                     ;[35e8] 00
                    nop                                     ;[35e9] 00
                    nop                                     ;[35ea] 00
                    nop                                     ;[35eb] 00
                    nop                                     ;[35ec] 00
                    nop                                     ;[35ed] 00
                    nop                                     ;[35ee] 00
                    nop                                     ;[35ef] 00
                    nop                                     ;[35f0] 00
                    nop                                     ;[35f1] 00
                    nop                                     ;[35f2] 00
                    nop                                     ;[35f3] 00
                    nop                                     ;[35f4] 00
                    nop                                     ;[35f5] 00
                    nop                                     ;[35f6] 00
                    nop                                     ;[35f7] 00
                    nop                                     ;[35f8] 00
                    nop                                     ;[35f9] 00
                    nop                                     ;[35fa] 00
                    nop                                     ;[35fb] 00
                    nop                                     ;[35fc] 00
                    nop                                     ;[35fd] 00
                    nop                                     ;[35fe] 00
                    nop                                     ;[35ff] 00
                    nop                                     ;[3600] 00
                    nop                                     ;[3601] 00
                    nop                                     ;[3602] 00
                    nop                                     ;[3603] 00
                    nop                                     ;[3604] 00
                    nop                                     ;[3605] 00
                    nop                                     ;[3606] 00
                    nop                                     ;[3607] 00
                    nop                                     ;[3608] 00
                    nop                                     ;[3609] 00
                    nop                                     ;[360a] 00
                    nop                                     ;[360b] 00
                    nop                                     ;[360c] 00
                    nop                                     ;[360d] 00
                    nop                                     ;[360e] 00
                    nop                                     ;[360f] 00
                    nop                                     ;[3610] 00
                    nop                                     ;[3611] 00
                    nop                                     ;[3612] 00
                    nop                                     ;[3613] 00
                    nop                                     ;[3614] 00
                    nop                                     ;[3615] 00
                    nop                                     ;[3616] 00
                    nop                                     ;[3617] 00
                    nop                                     ;[3618] 00
                    nop                                     ;[3619] 00
                    nop                                     ;[361a] 00
                    nop                                     ;[361b] 00
                    nop                                     ;[361c] 00
                    nop                                     ;[361d] 00
                    nop                                     ;[361e] 00
                    nop                                     ;[361f] 00
                    nop                                     ;[3620] 00
                    nop                                     ;[3621] 00
                    nop                                     ;[3622] 00
                    nop                                     ;[3623] 00
                    nop                                     ;[3624] 00
                    nop                                     ;[3625] 00
                    nop                                     ;[3626] 00
                    nop                                     ;[3627] 00
                    nop                                     ;[3628] 00
                    nop                                     ;[3629] 00
                    nop                                     ;[362a] 00
                    nop                                     ;[362b] 00
                    nop                                     ;[362c] 00
                    nop                                     ;[362d] 00
                    nop                                     ;[362e] 00
                    nop                                     ;[362f] 00
                    nop                                     ;[3630] 00
                    nop                                     ;[3631] 00
                    nop                                     ;[3632] 00
                    nop                                     ;[3633] 00
                    nop                                     ;[3634] 00
                    nop                                     ;[3635] 00
                    nop                                     ;[3636] 00
                    nop                                     ;[3637] 00
                    nop                                     ;[3638] 00
                    nop                                     ;[3639] 00
                    nop                                     ;[363a] 00
                    nop                                     ;[363b] 00
                    nop                                     ;[363c] 00
                    nop                                     ;[363d] 00
                    nop                                     ;[363e] 00
                    nop                                     ;[363f] 00
                    nop                                     ;[3640] 00
                    nop                                     ;[3641] 00
                    nop                                     ;[3642] 00
                    nop                                     ;[3643] 00
                    nop                                     ;[3644] 00
                    nop                                     ;[3645] 00
                    nop                                     ;[3646] 00
                    nop                                     ;[3647] 00
                    nop                                     ;[3648] 00
                    nop                                     ;[3649] 00
                    nop                                     ;[364a] 00
                    nop                                     ;[364b] 00
                    nop                                     ;[364c] 00
                    nop                                     ;[364d] 00
                    nop                                     ;[364e] 00
                    nop                                     ;[364f] 00
                    nop                                     ;[3650] 00
                    nop                                     ;[3651] 00
                    nop                                     ;[3652] 00
                    nop                                     ;[3653] 00
                    nop                                     ;[3654] 00
                    nop                                     ;[3655] 00
                    nop                                     ;[3656] 00
                    nop                                     ;[3657] 00
                    nop                                     ;[3658] 00
                    nop                                     ;[3659] 00
                    nop                                     ;[365a] 00
                    nop                                     ;[365b] 00
                    nop                                     ;[365c] 00
                    nop                                     ;[365d] 00
                    nop                                     ;[365e] 00
                    nop                                     ;[365f] 00
                    nop                                     ;[3660] 00
                    nop                                     ;[3661] 00
                    nop                                     ;[3662] 00
                    nop                                     ;[3663] 00
                    nop                                     ;[3664] 00
                    nop                                     ;[3665] 00
                    nop                                     ;[3666] 00
                    nop                                     ;[3667] 00
                    nop                                     ;[3668] 00
                    nop                                     ;[3669] 00
                    nop                                     ;[366a] 00
                    nop                                     ;[366b] 00
                    nop                                     ;[366c] 00
                    nop                                     ;[366d] 00
                    nop                                     ;[366e] 00
                    nop                                     ;[366f] 00
                    nop                                     ;[3670] 00
                    nop                                     ;[3671] 00
                    nop                                     ;[3672] 00
                    nop                                     ;[3673] 00
                    nop                                     ;[3674] 00
                    nop                                     ;[3675] 00
                    nop                                     ;[3676] 00
                    nop                                     ;[3677] 00
                    nop                                     ;[3678] 00
                    nop                                     ;[3679] 00
                    nop                                     ;[367a] 00
                    nop                                     ;[367b] 00
                    nop                                     ;[367c] 00
                    nop                                     ;[367d] 00
                    nop                                     ;[367e] 00
                    nop                                     ;[367f] 00
                    nop                                     ;[3680] 00
                    nop                                     ;[3681] 00
                    nop                                     ;[3682] 00
                    nop                                     ;[3683] 00
                    nop                                     ;[3684] 00
                    nop                                     ;[3685] 00
                    nop                                     ;[3686] 00
                    nop                                     ;[3687] 00
                    nop                                     ;[3688] 00
                    nop                                     ;[3689] 00
                    nop                                     ;[368a] 00
                    nop                                     ;[368b] 00
                    nop                                     ;[368c] 00
                    nop                                     ;[368d] 00
                    nop                                     ;[368e] 00
                    nop                                     ;[368f] 00
                    nop                                     ;[3690] 00
                    nop                                     ;[3691] 00
                    nop                                     ;[3692] 00
                    nop                                     ;[3693] 00
                    nop                                     ;[3694] 00
                    nop                                     ;[3695] 00
                    nop                                     ;[3696] 00
                    nop                                     ;[3697] 00
                    nop                                     ;[3698] 00
                    nop                                     ;[3699] 00
                    nop                                     ;[369a] 00
                    nop                                     ;[369b] 00
                    nop                                     ;[369c] 00
                    nop                                     ;[369d] 00
                    nop                                     ;[369e] 00
                    nop                                     ;[369f] 00
                    nop                                     ;[36a0] 00
                    nop                                     ;[36a1] 00
                    nop                                     ;[36a2] 00
                    nop                                     ;[36a3] 00
                    nop                                     ;[36a4] 00
                    nop                                     ;[36a5] 00
                    nop                                     ;[36a6] 00
                    nop                                     ;[36a7] 00
                    nop                                     ;[36a8] 00
                    nop                                     ;[36a9] 00
                    nop                                     ;[36aa] 00
                    nop                                     ;[36ab] 00
                    nop                                     ;[36ac] 00
                    nop                                     ;[36ad] 00
                    nop                                     ;[36ae] 00
                    nop                                     ;[36af] 00
                    nop                                     ;[36b0] 00
                    nop                                     ;[36b1] 00
                    nop                                     ;[36b2] 00
                    nop                                     ;[36b3] 00
                    nop                                     ;[36b4] 00
                    nop                                     ;[36b5] 00
                    nop                                     ;[36b6] 00
                    nop                                     ;[36b7] 00
                    nop                                     ;[36b8] 00
                    nop                                     ;[36b9] 00
                    nop                                     ;[36ba] 00
                    nop                                     ;[36bb] 00
                    nop                                     ;[36bc] 00
                    nop                                     ;[36bd] 00
                    nop                                     ;[36be] 00
                    nop                                     ;[36bf] 00
                    nop                                     ;[36c0] 00
                    nop                                     ;[36c1] 00
                    nop                                     ;[36c2] 00
                    nop                                     ;[36c3] 00
                    nop                                     ;[36c4] 00
                    nop                                     ;[36c5] 00
                    nop                                     ;[36c6] 00
                    nop                                     ;[36c7] 00
                    nop                                     ;[36c8] 00
                    nop                                     ;[36c9] 00
                    nop                                     ;[36ca] 00
                    nop                                     ;[36cb] 00
                    nop                                     ;[36cc] 00
                    nop                                     ;[36cd] 00
                    nop                                     ;[36ce] 00
                    nop                                     ;[36cf] 00
                    nop                                     ;[36d0] 00
                    nop                                     ;[36d1] 00
                    nop                                     ;[36d2] 00
                    nop                                     ;[36d3] 00
                    nop                                     ;[36d4] 00
                    nop                                     ;[36d5] 00
                    nop                                     ;[36d6] 00
                    nop                                     ;[36d7] 00
                    nop                                     ;[36d8] 00
                    nop                                     ;[36d9] 00
                    nop                                     ;[36da] 00
                    nop                                     ;[36db] 00
                    nop                                     ;[36dc] 00
                    nop                                     ;[36dd] 00
                    nop                                     ;[36de] 00
                    nop                                     ;[36df] 00
                    nop                                     ;[36e0] 00
                    nop                                     ;[36e1] 00
                    nop                                     ;[36e2] 00
                    nop                                     ;[36e3] 00
                    nop                                     ;[36e4] 00
                    nop                                     ;[36e5] 00
                    nop                                     ;[36e6] 00
                    nop                                     ;[36e7] 00
                    nop                                     ;[36e8] 00
                    nop                                     ;[36e9] 00
                    nop                                     ;[36ea] 00
                    nop                                     ;[36eb] 00
                    nop                                     ;[36ec] 00
                    nop                                     ;[36ed] 00
                    nop                                     ;[36ee] 00
                    nop                                     ;[36ef] 00
                    nop                                     ;[36f0] 00
                    nop                                     ;[36f1] 00
                    nop                                     ;[36f2] 00
                    nop                                     ;[36f3] 00
                    nop                                     ;[36f4] 00
                    nop                                     ;[36f5] 00
                    nop                                     ;[36f6] 00
                    nop                                     ;[36f7] 00
                    nop                                     ;[36f8] 00
                    nop                                     ;[36f9] 00
                    nop                                     ;[36fa] 00
                    nop                                     ;[36fb] 00
                    nop                                     ;[36fc] 00
                    nop                                     ;[36fd] 00
                    nop                                     ;[36fe] 00
                    nop                                     ;[36ff] 00
                    nop                                     ;[3700] 00
                    nop                                     ;[3701] 00
                    nop                                     ;[3702] 00
                    nop                                     ;[3703] 00
                    nop                                     ;[3704] 00
                    nop                                     ;[3705] 00
                    nop                                     ;[3706] 00
                    nop                                     ;[3707] 00
                    nop                                     ;[3708] 00
                    nop                                     ;[3709] 00
                    nop                                     ;[370a] 00
                    nop                                     ;[370b] 00
                    nop                                     ;[370c] 00
                    nop                                     ;[370d] 00
                    nop                                     ;[370e] 00
                    nop                                     ;[370f] 00
                    nop                                     ;[3710] 00
                    nop                                     ;[3711] 00
                    nop                                     ;[3712] 00
                    nop                                     ;[3713] 00
                    nop                                     ;[3714] 00
                    nop                                     ;[3715] 00
                    nop                                     ;[3716] 00
                    nop                                     ;[3717] 00
                    nop                                     ;[3718] 00
                    nop                                     ;[3719] 00
                    nop                                     ;[371a] 00
                    nop                                     ;[371b] 00
                    nop                                     ;[371c] 00
                    nop                                     ;[371d] 00
                    nop                                     ;[371e] 00
                    nop                                     ;[371f] 00
                    nop                                     ;[3720] 00
                    nop                                     ;[3721] 00
                    nop                                     ;[3722] 00
                    nop                                     ;[3723] 00
                    nop                                     ;[3724] 00
                    nop                                     ;[3725] 00
                    nop                                     ;[3726] 00
                    nop                                     ;[3727] 00
                    nop                                     ;[3728] 00
                    nop                                     ;[3729] 00
                    nop                                     ;[372a] 00
                    nop                                     ;[372b] 00
                    nop                                     ;[372c] 00
                    nop                                     ;[372d] 00
                    nop                                     ;[372e] 00
                    nop                                     ;[372f] 00
                    nop                                     ;[3730] 00
                    nop                                     ;[3731] 00
                    nop                                     ;[3732] 00
                    nop                                     ;[3733] 00
                    nop                                     ;[3734] 00
                    nop                                     ;[3735] 00
                    nop                                     ;[3736] 00
                    nop                                     ;[3737] 00
                    nop                                     ;[3738] 00
                    nop                                     ;[3739] 00
                    nop                                     ;[373a] 00
                    nop                                     ;[373b] 00
                    nop                                     ;[373c] 00
                    nop                                     ;[373d] 00
                    nop                                     ;[373e] 00
                    nop                                     ;[373f] 00
                    nop                                     ;[3740] 00
                    nop                                     ;[3741] 00
                    nop                                     ;[3742] 00
                    nop                                     ;[3743] 00
                    nop                                     ;[3744] 00
                    nop                                     ;[3745] 00
                    nop                                     ;[3746] 00
                    nop                                     ;[3747] 00
                    nop                                     ;[3748] 00
                    nop                                     ;[3749] 00
                    nop                                     ;[374a] 00
                    nop                                     ;[374b] 00
                    nop                                     ;[374c] 00
                    nop                                     ;[374d] 00
                    nop                                     ;[374e] 00
                    nop                                     ;[374f] 00
                    nop                                     ;[3750] 00
                    nop                                     ;[3751] 00
                    nop                                     ;[3752] 00
                    nop                                     ;[3753] 00
                    nop                                     ;[3754] 00
                    nop                                     ;[3755] 00
                    nop                                     ;[3756] 00
                    nop                                     ;[3757] 00
                    nop                                     ;[3758] 00
                    nop                                     ;[3759] 00
                    nop                                     ;[375a] 00
                    nop                                     ;[375b] 00
                    nop                                     ;[375c] 00
                    nop                                     ;[375d] 00
                    nop                                     ;[375e] 00
                    nop                                     ;[375f] 00
                    nop                                     ;[3760] 00
                    nop                                     ;[3761] 00
                    nop                                     ;[3762] 00
                    nop                                     ;[3763] 00
                    nop                                     ;[3764] 00
                    nop                                     ;[3765] 00
                    nop                                     ;[3766] 00
                    nop                                     ;[3767] 00
                    nop                                     ;[3768] 00
                    nop                                     ;[3769] 00
                    nop                                     ;[376a] 00
                    nop                                     ;[376b] 00
                    nop                                     ;[376c] 00
                    nop                                     ;[376d] 00
                    nop                                     ;[376e] 00
                    nop                                     ;[376f] 00
                    nop                                     ;[3770] 00
                    nop                                     ;[3771] 00
                    nop                                     ;[3772] 00
                    nop                                     ;[3773] 00
                    nop                                     ;[3774] 00
                    nop                                     ;[3775] 00
                    nop                                     ;[3776] 00
                    nop                                     ;[3777] 00
                    nop                                     ;[3778] 00
                    nop                                     ;[3779] 00
                    nop                                     ;[377a] 00
                    nop                                     ;[377b] 00
                    nop                                     ;[377c] 00
                    nop                                     ;[377d] 00
                    nop                                     ;[377e] 00
                    nop                                     ;[377f] 00
                    nop                                     ;[3780] 00
                    nop                                     ;[3781] 00
                    nop                                     ;[3782] 00
                    nop                                     ;[3783] 00
                    nop                                     ;[3784] 00
                    nop                                     ;[3785] 00
                    nop                                     ;[3786] 00
                    nop                                     ;[3787] 00
                    nop                                     ;[3788] 00
                    nop                                     ;[3789] 00
                    nop                                     ;[378a] 00
                    nop                                     ;[378b] 00
                    nop                                     ;[378c] 00
                    nop                                     ;[378d] 00
                    nop                                     ;[378e] 00
                    nop                                     ;[378f] 00
                    nop                                     ;[3790] 00
                    nop                                     ;[3791] 00
                    nop                                     ;[3792] 00
                    nop                                     ;[3793] 00
                    nop                                     ;[3794] 00
                    nop                                     ;[3795] 00
                    nop                                     ;[3796] 00
                    nop                                     ;[3797] 00
                    nop                                     ;[3798] 00
                    nop                                     ;[3799] 00
                    nop                                     ;[379a] 00
                    nop                                     ;[379b] 00
                    nop                                     ;[379c] 00
                    nop                                     ;[379d] 00
                    nop                                     ;[379e] 00
                    nop                                     ;[379f] 00
                    nop                                     ;[37a0] 00
                    nop                                     ;[37a1] 00
                    nop                                     ;[37a2] 00
                    nop                                     ;[37a3] 00
                    nop                                     ;[37a4] 00
                    nop                                     ;[37a5] 00
                    nop                                     ;[37a6] 00
                    nop                                     ;[37a7] 00
                    nop                                     ;[37a8] 00
                    nop                                     ;[37a9] 00
                    nop                                     ;[37aa] 00
                    nop                                     ;[37ab] 00
                    nop                                     ;[37ac] 00
                    nop                                     ;[37ad] 00
                    nop                                     ;[37ae] 00
                    nop                                     ;[37af] 00
                    nop                                     ;[37b0] 00
                    nop                                     ;[37b1] 00
                    nop                                     ;[37b2] 00
                    nop                                     ;[37b3] 00
                    nop                                     ;[37b4] 00
                    nop                                     ;[37b5] 00
                    nop                                     ;[37b6] 00
                    nop                                     ;[37b7] 00
                    nop                                     ;[37b8] 00
                    nop                                     ;[37b9] 00
                    nop                                     ;[37ba] 00
                    nop                                     ;[37bb] 00
                    nop                                     ;[37bc] 00
                    nop                                     ;[37bd] 00
                    nop                                     ;[37be] 00
                    nop                                     ;[37bf] 00
                    nop                                     ;[37c0] 00
                    nop                                     ;[37c1] 00
                    nop                                     ;[37c2] 00
                    nop                                     ;[37c3] 00
                    nop                                     ;[37c4] 00
                    nop                                     ;[37c5] 00
                    nop                                     ;[37c6] 00
                    nop                                     ;[37c7] 00
                    nop                                     ;[37c8] 00
                    nop                                     ;[37c9] 00
                    nop                                     ;[37ca] 00
                    nop                                     ;[37cb] 00
                    nop                                     ;[37cc] 00
                    nop                                     ;[37cd] 00
                    nop                                     ;[37ce] 00
                    nop                                     ;[37cf] 00
                    nop                                     ;[37d0] 00
                    nop                                     ;[37d1] 00
                    nop                                     ;[37d2] 00
                    nop                                     ;[37d3] 00
                    nop                                     ;[37d4] 00
                    nop                                     ;[37d5] 00
                    nop                                     ;[37d6] 00
                    nop                                     ;[37d7] 00
                    nop                                     ;[37d8] 00
                    nop                                     ;[37d9] 00
                    nop                                     ;[37da] 00
                    nop                                     ;[37db] 00
                    nop                                     ;[37dc] 00
                    nop                                     ;[37dd] 00
                    nop                                     ;[37de] 00
                    nop                                     ;[37df] 00
                    nop                                     ;[37e0] 00
                    nop                                     ;[37e1] 00
                    nop                                     ;[37e2] 00
                    nop                                     ;[37e3] 00
                    nop                                     ;[37e4] 00
                    nop                                     ;[37e5] 00
                    nop                                     ;[37e6] 00
                    nop                                     ;[37e7] 00
                    nop                                     ;[37e8] 00
                    nop                                     ;[37e9] 00
                    nop                                     ;[37ea] 00
                    nop                                     ;[37eb] 00
                    nop                                     ;[37ec] 00
                    nop                                     ;[37ed] 00
                    nop                                     ;[37ee] 00
                    nop                                     ;[37ef] 00
                    nop                                     ;[37f0] 00
                    nop                                     ;[37f1] 00
                    nop                                     ;[37f2] 00
                    nop                                     ;[37f3] 00
                    nop                                     ;[37f4] 00
                    nop                                     ;[37f5] 00
                    nop                                     ;[37f6] 00
                    nop                                     ;[37f7] 00
                    nop                                     ;[37f8] 00
                    nop                                     ;[37f9] 00
                    nop                                     ;[37fa] 00
                    nop                                     ;[37fb] 00
                    nop                                     ;[37fc] 00
                    nop                                     ;[37fd] 00
                    nop                                     ;[37fe] 00
                    nop                                     ;[37ff] 00
                    nop                                     ;[3800] 00
                    nop                                     ;[3801] 00
                    nop                                     ;[3802] 00
                    nop                                     ;[3803] 00
                    nop                                     ;[3804] 00
                    nop                                     ;[3805] 00
                    nop                                     ;[3806] 00
                    nop                                     ;[3807] 00
                    nop                                     ;[3808] 00
                    nop                                     ;[3809] 00
                    nop                                     ;[380a] 00
                    nop                                     ;[380b] 00
                    nop                                     ;[380c] 00
                    nop                                     ;[380d] 00
                    nop                                     ;[380e] 00
                    nop                                     ;[380f] 00
                    nop                                     ;[3810] 00
                    nop                                     ;[3811] 00
                    nop                                     ;[3812] 00
                    nop                                     ;[3813] 00
                    nop                                     ;[3814] 00
                    nop                                     ;[3815] 00
                    nop                                     ;[3816] 00
                    nop                                     ;[3817] 00
                    nop                                     ;[3818] 00
                    nop                                     ;[3819] 00
                    nop                                     ;[381a] 00
                    nop                                     ;[381b] 00
                    nop                                     ;[381c] 00
                    nop                                     ;[381d] 00
                    nop                                     ;[381e] 00
                    nop                                     ;[381f] 00
                    nop                                     ;[3820] 00
                    nop                                     ;[3821] 00
                    nop                                     ;[3822] 00
                    nop                                     ;[3823] 00
                    nop                                     ;[3824] 00
                    nop                                     ;[3825] 00
                    nop                                     ;[3826] 00
                    nop                                     ;[3827] 00
                    nop                                     ;[3828] 00
                    nop                                     ;[3829] 00
                    nop                                     ;[382a] 00
                    nop                                     ;[382b] 00
                    nop                                     ;[382c] 00
                    nop                                     ;[382d] 00
                    nop                                     ;[382e] 00
                    nop                                     ;[382f] 00
                    nop                                     ;[3830] 00
                    nop                                     ;[3831] 00
                    nop                                     ;[3832] 00
                    nop                                     ;[3833] 00
                    nop                                     ;[3834] 00
                    nop                                     ;[3835] 00
                    nop                                     ;[3836] 00
                    nop                                     ;[3837] 00
                    nop                                     ;[3838] 00
                    nop                                     ;[3839] 00
                    nop                                     ;[383a] 00
                    nop                                     ;[383b] 00
                    nop                                     ;[383c] 00
                    nop                                     ;[383d] 00
                    nop                                     ;[383e] 00
                    nop                                     ;[383f] 00
                    nop                                     ;[3840] 00
                    nop                                     ;[3841] 00
                    nop                                     ;[3842] 00
                    nop                                     ;[3843] 00
                    nop                                     ;[3844] 00
                    nop                                     ;[3845] 00
                    nop                                     ;[3846] 00
                    nop                                     ;[3847] 00
                    nop                                     ;[3848] 00
                    nop                                     ;[3849] 00
                    nop                                     ;[384a] 00
                    nop                                     ;[384b] 00
                    nop                                     ;[384c] 00
                    nop                                     ;[384d] 00
                    nop                                     ;[384e] 00
                    nop                                     ;[384f] 00
                    nop                                     ;[3850] 00
                    nop                                     ;[3851] 00
                    nop                                     ;[3852] 00
                    nop                                     ;[3853] 00
                    nop                                     ;[3854] 00
                    nop                                     ;[3855] 00
                    nop                                     ;[3856] 00
                    nop                                     ;[3857] 00
                    nop                                     ;[3858] 00
                    nop                                     ;[3859] 00
                    nop                                     ;[385a] 00
                    nop                                     ;[385b] 00
                    nop                                     ;[385c] 00
                    nop                                     ;[385d] 00
                    nop                                     ;[385e] 00
                    nop                                     ;[385f] 00
                    nop                                     ;[3860] 00
                    nop                                     ;[3861] 00
                    nop                                     ;[3862] 00
                    nop                                     ;[3863] 00
                    nop                                     ;[3864] 00
                    nop                                     ;[3865] 00
                    nop                                     ;[3866] 00
                    nop                                     ;[3867] 00
                    nop                                     ;[3868] 00
                    nop                                     ;[3869] 00
                    nop                                     ;[386a] 00
                    nop                                     ;[386b] 00
                    nop                                     ;[386c] 00
                    nop                                     ;[386d] 00
                    nop                                     ;[386e] 00
                    nop                                     ;[386f] 00
                    nop                                     ;[3870] 00
                    nop                                     ;[3871] 00
                    nop                                     ;[3872] 00
                    nop                                     ;[3873] 00
                    nop                                     ;[3874] 00
                    nop                                     ;[3875] 00
                    nop                                     ;[3876] 00
                    nop                                     ;[3877] 00
                    nop                                     ;[3878] 00
                    nop                                     ;[3879] 00
                    nop                                     ;[387a] 00
                    nop                                     ;[387b] 00
                    nop                                     ;[387c] 00
                    nop                                     ;[387d] 00
                    nop                                     ;[387e] 00
                    nop                                     ;[387f] 00
                    nop                                     ;[3880] 00
                    nop                                     ;[3881] 00
                    nop                                     ;[3882] 00
                    nop                                     ;[3883] 00
                    nop                                     ;[3884] 00
                    nop                                     ;[3885] 00
                    nop                                     ;[3886] 00
                    nop                                     ;[3887] 00
                    nop                                     ;[3888] 00
                    nop                                     ;[3889] 00
                    nop                                     ;[388a] 00
                    nop                                     ;[388b] 00
                    nop                                     ;[388c] 00
                    nop                                     ;[388d] 00
                    nop                                     ;[388e] 00
                    nop                                     ;[388f] 00
                    nop                                     ;[3890] 00
                    nop                                     ;[3891] 00
                    nop                                     ;[3892] 00
                    nop                                     ;[3893] 00
                    nop                                     ;[3894] 00
                    nop                                     ;[3895] 00
                    nop                                     ;[3896] 00
                    nop                                     ;[3897] 00
                    nop                                     ;[3898] 00
                    nop                                     ;[3899] 00
                    nop                                     ;[389a] 00
                    nop                                     ;[389b] 00
                    nop                                     ;[389c] 00
                    nop                                     ;[389d] 00
                    nop                                     ;[389e] 00
                    nop                                     ;[389f] 00
                    nop                                     ;[38a0] 00
                    nop                                     ;[38a1] 00
                    nop                                     ;[38a2] 00
                    nop                                     ;[38a3] 00
                    nop                                     ;[38a4] 00
                    nop                                     ;[38a5] 00
                    nop                                     ;[38a6] 00
                    nop                                     ;[38a7] 00
                    nop                                     ;[38a8] 00
                    nop                                     ;[38a9] 00
                    nop                                     ;[38aa] 00
                    nop                                     ;[38ab] 00
                    nop                                     ;[38ac] 00
                    nop                                     ;[38ad] 00
                    nop                                     ;[38ae] 00
                    nop                                     ;[38af] 00
                    nop                                     ;[38b0] 00
                    nop                                     ;[38b1] 00
                    nop                                     ;[38b2] 00
                    nop                                     ;[38b3] 00
                    nop                                     ;[38b4] 00
                    nop                                     ;[38b5] 00
                    nop                                     ;[38b6] 00
                    nop                                     ;[38b7] 00
                    nop                                     ;[38b8] 00
                    nop                                     ;[38b9] 00
                    nop                                     ;[38ba] 00
                    nop                                     ;[38bb] 00
                    nop                                     ;[38bc] 00
                    nop                                     ;[38bd] 00
                    nop                                     ;[38be] 00
                    nop                                     ;[38bf] 00
                    nop                                     ;[38c0] 00
                    nop                                     ;[38c1] 00
                    nop                                     ;[38c2] 00
                    nop                                     ;[38c3] 00
                    nop                                     ;[38c4] 00
                    nop                                     ;[38c5] 00
                    nop                                     ;[38c6] 00
                    nop                                     ;[38c7] 00
                    nop                                     ;[38c8] 00
                    nop                                     ;[38c9] 00
                    nop                                     ;[38ca] 00
                    nop                                     ;[38cb] 00
                    nop                                     ;[38cc] 00
                    nop                                     ;[38cd] 00
                    nop                                     ;[38ce] 00
                    nop                                     ;[38cf] 00
                    nop                                     ;[38d0] 00
                    nop                                     ;[38d1] 00
                    nop                                     ;[38d2] 00
                    nop                                     ;[38d3] 00
                    nop                                     ;[38d4] 00
                    nop                                     ;[38d5] 00
                    nop                                     ;[38d6] 00
                    nop                                     ;[38d7] 00
                    nop                                     ;[38d8] 00
                    nop                                     ;[38d9] 00
                    nop                                     ;[38da] 00
                    nop                                     ;[38db] 00
                    nop                                     ;[38dc] 00
                    nop                                     ;[38dd] 00
                    nop                                     ;[38de] 00
                    nop                                     ;[38df] 00
                    nop                                     ;[38e0] 00
                    nop                                     ;[38e1] 00
                    nop                                     ;[38e2] 00
                    nop                                     ;[38e3] 00
                    nop                                     ;[38e4] 00
                    nop                                     ;[38e5] 00
                    nop                                     ;[38e6] 00
                    nop                                     ;[38e7] 00
                    nop                                     ;[38e8] 00
                    nop                                     ;[38e9] 00
                    nop                                     ;[38ea] 00
                    nop                                     ;[38eb] 00
                    nop                                     ;[38ec] 00
                    nop                                     ;[38ed] 00
                    nop                                     ;[38ee] 00
                    nop                                     ;[38ef] 00
                    nop                                     ;[38f0] 00
                    nop                                     ;[38f1] 00
                    nop                                     ;[38f2] 00
                    nop                                     ;[38f3] 00
                    nop                                     ;[38f4] 00
                    nop                                     ;[38f5] 00
                    nop                                     ;[38f6] 00
                    nop                                     ;[38f7] 00
                    nop                                     ;[38f8] 00
                    nop                                     ;[38f9] 00
                    nop                                     ;[38fa] 00
                    nop                                     ;[38fb] 00
                    nop                                     ;[38fc] 00
                    nop                                     ;[38fd] 00
                    nop                                     ;[38fe] 00
                    nop                                     ;[38ff] 00
                    nop                                     ;[3900] 00
                    nop                                     ;[3901] 00
                    nop                                     ;[3902] 00
                    nop                                     ;[3903] 00
                    nop                                     ;[3904] 00
                    nop                                     ;[3905] 00
                    nop                                     ;[3906] 00
                    nop                                     ;[3907] 00
                    nop                                     ;[3908] 00
                    nop                                     ;[3909] 00
                    nop                                     ;[390a] 00
                    nop                                     ;[390b] 00
                    nop                                     ;[390c] 00
                    nop                                     ;[390d] 00
                    nop                                     ;[390e] 00
                    nop                                     ;[390f] 00
                    nop                                     ;[3910] 00
                    nop                                     ;[3911] 00
                    nop                                     ;[3912] 00
                    nop                                     ;[3913] 00
                    nop                                     ;[3914] 00
                    nop                                     ;[3915] 00
                    nop                                     ;[3916] 00
                    nop                                     ;[3917] 00
                    nop                                     ;[3918] 00
                    nop                                     ;[3919] 00
                    nop                                     ;[391a] 00
                    nop                                     ;[391b] 00
                    nop                                     ;[391c] 00
                    nop                                     ;[391d] 00
                    nop                                     ;[391e] 00
                    nop                                     ;[391f] 00
                    nop                                     ;[3920] 00
                    nop                                     ;[3921] 00
                    nop                                     ;[3922] 00
                    nop                                     ;[3923] 00
                    nop                                     ;[3924] 00
                    nop                                     ;[3925] 00
                    nop                                     ;[3926] 00
                    nop                                     ;[3927] 00
                    nop                                     ;[3928] 00
                    nop                                     ;[3929] 00
                    nop                                     ;[392a] 00
                    nop                                     ;[392b] 00
                    nop                                     ;[392c] 00
                    nop                                     ;[392d] 00
                    nop                                     ;[392e] 00
                    nop                                     ;[392f] 00
                    nop                                     ;[3930] 00
                    nop                                     ;[3931] 00
                    nop                                     ;[3932] 00
                    nop                                     ;[3933] 00
                    nop                                     ;[3934] 00
                    nop                                     ;[3935] 00
                    nop                                     ;[3936] 00
                    nop                                     ;[3937] 00
                    nop                                     ;[3938] 00
                    nop                                     ;[3939] 00
                    nop                                     ;[393a] 00
                    nop                                     ;[393b] 00
                    nop                                     ;[393c] 00
                    nop                                     ;[393d] 00
                    nop                                     ;[393e] 00
                    nop                                     ;[393f] 00
                    nop                                     ;[3940] 00
                    nop                                     ;[3941] 00
                    nop                                     ;[3942] 00
                    nop                                     ;[3943] 00
                    nop                                     ;[3944] 00
                    nop                                     ;[3945] 00
                    nop                                     ;[3946] 00
                    nop                                     ;[3947] 00
                    nop                                     ;[3948] 00
                    nop                                     ;[3949] 00
                    nop                                     ;[394a] 00
                    nop                                     ;[394b] 00
                    nop                                     ;[394c] 00
                    nop                                     ;[394d] 00
                    nop                                     ;[394e] 00
                    nop                                     ;[394f] 00
                    nop                                     ;[3950] 00
                    nop                                     ;[3951] 00
                    nop                                     ;[3952] 00
                    nop                                     ;[3953] 00
                    nop                                     ;[3954] 00
                    nop                                     ;[3955] 00
                    nop                                     ;[3956] 00
                    nop                                     ;[3957] 00
                    nop                                     ;[3958] 00
                    nop                                     ;[3959] 00
                    nop                                     ;[395a] 00
                    nop                                     ;[395b] 00
                    nop                                     ;[395c] 00
                    nop                                     ;[395d] 00
                    nop                                     ;[395e] 00
                    nop                                     ;[395f] 00
                    nop                                     ;[3960] 00
                    nop                                     ;[3961] 00
                    nop                                     ;[3962] 00
                    nop                                     ;[3963] 00
                    nop                                     ;[3964] 00
                    nop                                     ;[3965] 00
                    nop                                     ;[3966] 00
                    nop                                     ;[3967] 00
                    nop                                     ;[3968] 00
                    nop                                     ;[3969] 00
                    nop                                     ;[396a] 00
                    nop                                     ;[396b] 00
                    nop                                     ;[396c] 00
                    nop                                     ;[396d] 00
                    nop                                     ;[396e] 00
                    nop                                     ;[396f] 00
                    nop                                     ;[3970] 00
                    nop                                     ;[3971] 00
                    nop                                     ;[3972] 00
                    nop                                     ;[3973] 00
                    nop                                     ;[3974] 00
                    nop                                     ;[3975] 00
                    nop                                     ;[3976] 00
                    nop                                     ;[3977] 00
                    nop                                     ;[3978] 00
                    nop                                     ;[3979] 00
                    nop                                     ;[397a] 00
                    nop                                     ;[397b] 00
                    nop                                     ;[397c] 00
                    nop                                     ;[397d] 00
                    nop                                     ;[397e] 00
                    nop                                     ;[397f] 00
                    nop                                     ;[3980] 00
                    nop                                     ;[3981] 00
                    nop                                     ;[3982] 00
                    nop                                     ;[3983] 00
                    nop                                     ;[3984] 00
                    nop                                     ;[3985] 00
                    nop                                     ;[3986] 00
                    nop                                     ;[3987] 00
                    nop                                     ;[3988] 00
                    nop                                     ;[3989] 00
                    nop                                     ;[398a] 00
                    nop                                     ;[398b] 00
                    nop                                     ;[398c] 00
                    nop                                     ;[398d] 00
                    nop                                     ;[398e] 00
                    nop                                     ;[398f] 00
                    nop                                     ;[3990] 00
                    nop                                     ;[3991] 00
                    nop                                     ;[3992] 00
                    nop                                     ;[3993] 00
                    nop                                     ;[3994] 00
                    nop                                     ;[3995] 00
                    nop                                     ;[3996] 00
                    nop                                     ;[3997] 00
                    nop                                     ;[3998] 00
                    nop                                     ;[3999] 00
                    nop                                     ;[399a] 00
                    nop                                     ;[399b] 00
                    nop                                     ;[399c] 00
                    nop                                     ;[399d] 00
                    nop                                     ;[399e] 00
                    nop                                     ;[399f] 00
                    nop                                     ;[39a0] 00
                    nop                                     ;[39a1] 00
                    nop                                     ;[39a2] 00
                    nop                                     ;[39a3] 00
                    nop                                     ;[39a4] 00
                    nop                                     ;[39a5] 00
                    nop                                     ;[39a6] 00
                    nop                                     ;[39a7] 00
                    nop                                     ;[39a8] 00
                    nop                                     ;[39a9] 00
                    nop                                     ;[39aa] 00
                    nop                                     ;[39ab] 00
                    nop                                     ;[39ac] 00
                    nop                                     ;[39ad] 00
                    nop                                     ;[39ae] 00
                    nop                                     ;[39af] 00
                    nop                                     ;[39b0] 00
                    nop                                     ;[39b1] 00
                    nop                                     ;[39b2] 00
                    nop                                     ;[39b3] 00
                    nop                                     ;[39b4] 00
                    nop                                     ;[39b5] 00
                    nop                                     ;[39b6] 00
                    nop                                     ;[39b7] 00
                    nop                                     ;[39b8] 00
                    nop                                     ;[39b9] 00
                    nop                                     ;[39ba] 00
                    nop                                     ;[39bb] 00
                    nop                                     ;[39bc] 00
                    nop                                     ;[39bd] 00
                    nop                                     ;[39be] 00
                    nop                                     ;[39bf] 00
                    nop                                     ;[39c0] 00
                    nop                                     ;[39c1] 00
                    nop                                     ;[39c2] 00
                    nop                                     ;[39c3] 00
                    nop                                     ;[39c4] 00
                    nop                                     ;[39c5] 00
                    nop                                     ;[39c6] 00
                    nop                                     ;[39c7] 00
                    nop                                     ;[39c8] 00
                    nop                                     ;[39c9] 00
                    nop                                     ;[39ca] 00
                    nop                                     ;[39cb] 00
                    nop                                     ;[39cc] 00
                    nop                                     ;[39cd] 00
                    nop                                     ;[39ce] 00
                    nop                                     ;[39cf] 00
                    nop                                     ;[39d0] 00
                    nop                                     ;[39d1] 00
                    nop                                     ;[39d2] 00
                    nop                                     ;[39d3] 00
                    nop                                     ;[39d4] 00
                    nop                                     ;[39d5] 00
                    nop                                     ;[39d6] 00
                    nop                                     ;[39d7] 00
                    nop                                     ;[39d8] 00
                    nop                                     ;[39d9] 00
                    nop                                     ;[39da] 00
                    nop                                     ;[39db] 00
                    nop                                     ;[39dc] 00
                    nop                                     ;[39dd] 00
                    nop                                     ;[39de] 00
                    nop                                     ;[39df] 00
                    nop                                     ;[39e0] 00
                    nop                                     ;[39e1] 00
                    nop                                     ;[39e2] 00
                    nop                                     ;[39e3] 00
                    nop                                     ;[39e4] 00
                    nop                                     ;[39e5] 00
                    nop                                     ;[39e6] 00
                    nop                                     ;[39e7] 00
                    nop                                     ;[39e8] 00
                    nop                                     ;[39e9] 00
                    nop                                     ;[39ea] 00
                    nop                                     ;[39eb] 00
                    nop                                     ;[39ec] 00
                    nop                                     ;[39ed] 00
                    nop                                     ;[39ee] 00
                    nop                                     ;[39ef] 00
                    nop                                     ;[39f0] 00
                    nop                                     ;[39f1] 00
                    nop                                     ;[39f2] 00
                    nop                                     ;[39f3] 00
                    nop                                     ;[39f4] 00
                    nop                                     ;[39f5] 00
                    nop                                     ;[39f6] 00
                    nop                                     ;[39f7] 00
                    nop                                     ;[39f8] 00
                    nop                                     ;[39f9] 00
                    nop                                     ;[39fa] 00
                    nop                                     ;[39fb] 00
                    nop                                     ;[39fc] 00
                    nop                                     ;[39fd] 00
                    nop                                     ;[39fe] 00
                    nop                                     ;[39ff] 00
                    nop                                     ;[3a00] 00
                    nop                                     ;[3a01] 00
                    nop                                     ;[3a02] 00
                    nop                                     ;[3a03] 00
                    nop                                     ;[3a04] 00
                    nop                                     ;[3a05] 00
                    nop                                     ;[3a06] 00
                    nop                                     ;[3a07] 00
                    nop                                     ;[3a08] 00
                    nop                                     ;[3a09] 00
                    nop                                     ;[3a0a] 00
                    nop                                     ;[3a0b] 00
                    nop                                     ;[3a0c] 00
                    nop                                     ;[3a0d] 00
                    nop                                     ;[3a0e] 00
                    nop                                     ;[3a0f] 00
                    nop                                     ;[3a10] 00
                    nop                                     ;[3a11] 00
                    nop                                     ;[3a12] 00
                    nop                                     ;[3a13] 00
                    nop                                     ;[3a14] 00
                    nop                                     ;[3a15] 00
                    nop                                     ;[3a16] 00
                    nop                                     ;[3a17] 00
                    nop                                     ;[3a18] 00
                    nop                                     ;[3a19] 00
                    nop                                     ;[3a1a] 00
                    nop                                     ;[3a1b] 00
                    nop                                     ;[3a1c] 00
                    nop                                     ;[3a1d] 00
                    nop                                     ;[3a1e] 00
                    nop                                     ;[3a1f] 00
                    nop                                     ;[3a20] 00
                    nop                                     ;[3a21] 00
                    nop                                     ;[3a22] 00
                    nop                                     ;[3a23] 00
                    nop                                     ;[3a24] 00
                    nop                                     ;[3a25] 00
                    nop                                     ;[3a26] 00
                    nop                                     ;[3a27] 00
                    nop                                     ;[3a28] 00
                    nop                                     ;[3a29] 00
                    nop                                     ;[3a2a] 00
                    nop                                     ;[3a2b] 00
                    nop                                     ;[3a2c] 00
                    nop                                     ;[3a2d] 00
                    nop                                     ;[3a2e] 00
                    nop                                     ;[3a2f] 00
                    nop                                     ;[3a30] 00
                    nop                                     ;[3a31] 00
                    nop                                     ;[3a32] 00
                    nop                                     ;[3a33] 00
                    nop                                     ;[3a34] 00
                    nop                                     ;[3a35] 00
                    nop                                     ;[3a36] 00
                    nop                                     ;[3a37] 00
                    nop                                     ;[3a38] 00
                    nop                                     ;[3a39] 00
                    nop                                     ;[3a3a] 00
                    nop                                     ;[3a3b] 00
                    nop                                     ;[3a3c] 00
                    nop                                     ;[3a3d] 00
                    nop                                     ;[3a3e] 00
                    nop                                     ;[3a3f] 00
                    nop                                     ;[3a40] 00
                    nop                                     ;[3a41] 00
                    nop                                     ;[3a42] 00
                    nop                                     ;[3a43] 00
                    nop                                     ;[3a44] 00
                    nop                                     ;[3a45] 00
                    nop                                     ;[3a46] 00
                    nop                                     ;[3a47] 00
                    nop                                     ;[3a48] 00
                    nop                                     ;[3a49] 00
                    nop                                     ;[3a4a] 00
                    nop                                     ;[3a4b] 00
                    nop                                     ;[3a4c] 00
                    nop                                     ;[3a4d] 00
                    nop                                     ;[3a4e] 00
                    nop                                     ;[3a4f] 00
                    nop                                     ;[3a50] 00
                    nop                                     ;[3a51] 00
                    nop                                     ;[3a52] 00
                    nop                                     ;[3a53] 00
                    nop                                     ;[3a54] 00
                    nop                                     ;[3a55] 00
                    nop                                     ;[3a56] 00
                    nop                                     ;[3a57] 00
                    nop                                     ;[3a58] 00
                    nop                                     ;[3a59] 00
                    nop                                     ;[3a5a] 00
                    nop                                     ;[3a5b] 00
                    nop                                     ;[3a5c] 00
                    nop                                     ;[3a5d] 00
                    nop                                     ;[3a5e] 00
                    nop                                     ;[3a5f] 00
                    nop                                     ;[3a60] 00
                    nop                                     ;[3a61] 00
                    nop                                     ;[3a62] 00
                    nop                                     ;[3a63] 00
                    nop                                     ;[3a64] 00
                    nop                                     ;[3a65] 00
                    nop                                     ;[3a66] 00
                    nop                                     ;[3a67] 00
                    nop                                     ;[3a68] 00
                    nop                                     ;[3a69] 00
                    nop                                     ;[3a6a] 00
                    nop                                     ;[3a6b] 00
                    nop                                     ;[3a6c] 00
                    nop                                     ;[3a6d] 00
                    nop                                     ;[3a6e] 00
                    nop                                     ;[3a6f] 00
                    nop                                     ;[3a70] 00
                    nop                                     ;[3a71] 00
                    nop                                     ;[3a72] 00
                    nop                                     ;[3a73] 00
                    nop                                     ;[3a74] 00
                    nop                                     ;[3a75] 00
                    nop                                     ;[3a76] 00
                    nop                                     ;[3a77] 00
                    nop                                     ;[3a78] 00
                    nop                                     ;[3a79] 00
                    nop                                     ;[3a7a] 00
                    nop                                     ;[3a7b] 00
                    nop                                     ;[3a7c] 00
                    nop                                     ;[3a7d] 00
                    nop                                     ;[3a7e] 00
                    nop                                     ;[3a7f] 00
                    nop                                     ;[3a80] 00
                    nop                                     ;[3a81] 00
                    nop                                     ;[3a82] 00
                    nop                                     ;[3a83] 00
                    nop                                     ;[3a84] 00
                    nop                                     ;[3a85] 00
                    nop                                     ;[3a86] 00
                    nop                                     ;[3a87] 00
                    nop                                     ;[3a88] 00
                    nop                                     ;[3a89] 00
                    nop                                     ;[3a8a] 00
                    nop                                     ;[3a8b] 00
                    nop                                     ;[3a8c] 00
                    nop                                     ;[3a8d] 00
                    nop                                     ;[3a8e] 00
                    nop                                     ;[3a8f] 00
                    nop                                     ;[3a90] 00
                    nop                                     ;[3a91] 00
                    nop                                     ;[3a92] 00
                    nop                                     ;[3a93] 00
                    nop                                     ;[3a94] 00
                    nop                                     ;[3a95] 00
                    nop                                     ;[3a96] 00
                    nop                                     ;[3a97] 00
                    nop                                     ;[3a98] 00
                    nop                                     ;[3a99] 00
                    nop                                     ;[3a9a] 00
                    nop                                     ;[3a9b] 00
                    nop                                     ;[3a9c] 00
                    nop                                     ;[3a9d] 00
                    nop                                     ;[3a9e] 00
                    nop                                     ;[3a9f] 00
                    nop                                     ;[3aa0] 00
                    nop                                     ;[3aa1] 00
                    nop                                     ;[3aa2] 00
                    nop                                     ;[3aa3] 00
                    nop                                     ;[3aa4] 00
                    nop                                     ;[3aa5] 00
                    nop                                     ;[3aa6] 00
                    nop                                     ;[3aa7] 00
                    nop                                     ;[3aa8] 00
                    nop                                     ;[3aa9] 00
                    nop                                     ;[3aaa] 00
                    nop                                     ;[3aab] 00
                    nop                                     ;[3aac] 00
                    nop                                     ;[3aad] 00
                    nop                                     ;[3aae] 00
                    nop                                     ;[3aaf] 00
                    nop                                     ;[3ab0] 00
                    nop                                     ;[3ab1] 00
                    nop                                     ;[3ab2] 00
                    nop                                     ;[3ab3] 00
                    nop                                     ;[3ab4] 00
                    nop                                     ;[3ab5] 00
                    nop                                     ;[3ab6] 00
                    nop                                     ;[3ab7] 00
                    nop                                     ;[3ab8] 00
                    nop                                     ;[3ab9] 00
                    nop                                     ;[3aba] 00
                    nop                                     ;[3abb] 00
                    nop                                     ;[3abc] 00
                    nop                                     ;[3abd] 00
                    nop                                     ;[3abe] 00
                    nop                                     ;[3abf] 00
                    nop                                     ;[3ac0] 00
                    nop                                     ;[3ac1] 00
                    nop                                     ;[3ac2] 00
                    nop                                     ;[3ac3] 00
                    nop                                     ;[3ac4] 00
                    nop                                     ;[3ac5] 00
                    nop                                     ;[3ac6] 00
                    nop                                     ;[3ac7] 00
                    nop                                     ;[3ac8] 00
                    nop                                     ;[3ac9] 00
                    nop                                     ;[3aca] 00
                    nop                                     ;[3acb] 00
                    nop                                     ;[3acc] 00
                    nop                                     ;[3acd] 00
                    nop                                     ;[3ace] 00
                    nop                                     ;[3acf] 00
                    nop                                     ;[3ad0] 00
                    nop                                     ;[3ad1] 00
                    nop                                     ;[3ad2] 00
                    nop                                     ;[3ad3] 00
                    nop                                     ;[3ad4] 00
                    nop                                     ;[3ad5] 00
                    nop                                     ;[3ad6] 00
                    nop                                     ;[3ad7] 00
                    nop                                     ;[3ad8] 00
                    nop                                     ;[3ad9] 00
                    nop                                     ;[3ada] 00
                    nop                                     ;[3adb] 00
                    nop                                     ;[3adc] 00
                    nop                                     ;[3add] 00
                    nop                                     ;[3ade] 00
                    nop                                     ;[3adf] 00
                    nop                                     ;[3ae0] 00
                    nop                                     ;[3ae1] 00
                    nop                                     ;[3ae2] 00
                    nop                                     ;[3ae3] 00
                    nop                                     ;[3ae4] 00
                    nop                                     ;[3ae5] 00
                    nop                                     ;[3ae6] 00
                    nop                                     ;[3ae7] 00
                    nop                                     ;[3ae8] 00
                    nop                                     ;[3ae9] 00
                    nop                                     ;[3aea] 00
                    nop                                     ;[3aeb] 00
                    nop                                     ;[3aec] 00
                    nop                                     ;[3aed] 00
                    nop                                     ;[3aee] 00
                    nop                                     ;[3aef] 00
                    nop                                     ;[3af0] 00
                    nop                                     ;[3af1] 00
                    nop                                     ;[3af2] 00
                    nop                                     ;[3af3] 00
                    nop                                     ;[3af4] 00
                    nop                                     ;[3af5] 00
                    nop                                     ;[3af6] 00
                    nop                                     ;[3af7] 00
                    nop                                     ;[3af8] 00
                    nop                                     ;[3af9] 00
                    nop                                     ;[3afa] 00
                    nop                                     ;[3afb] 00
                    nop                                     ;[3afc] 00
                    nop                                     ;[3afd] 00
                    nop                                     ;[3afe] 00
                    nop                                     ;[3aff] 00
                    nop                                     ;[3b00] 00
                    nop                                     ;[3b01] 00
                    nop                                     ;[3b02] 00
                    nop                                     ;[3b03] 00
                    nop                                     ;[3b04] 00
                    nop                                     ;[3b05] 00
                    nop                                     ;[3b06] 00
                    nop                                     ;[3b07] 00
                    nop                                     ;[3b08] 00
                    nop                                     ;[3b09] 00
                    nop                                     ;[3b0a] 00
                    nop                                     ;[3b0b] 00
                    nop                                     ;[3b0c] 00
                    nop                                     ;[3b0d] 00
                    nop                                     ;[3b0e] 00
                    nop                                     ;[3b0f] 00
                    nop                                     ;[3b10] 00
                    nop                                     ;[3b11] 00
                    nop                                     ;[3b12] 00
                    nop                                     ;[3b13] 00
                    nop                                     ;[3b14] 00
                    nop                                     ;[3b15] 00
                    nop                                     ;[3b16] 00
                    nop                                     ;[3b17] 00
                    nop                                     ;[3b18] 00
                    nop                                     ;[3b19] 00
                    nop                                     ;[3b1a] 00
                    nop                                     ;[3b1b] 00
                    nop                                     ;[3b1c] 00
                    nop                                     ;[3b1d] 00
                    nop                                     ;[3b1e] 00
                    nop                                     ;[3b1f] 00
                    nop                                     ;[3b20] 00
                    nop                                     ;[3b21] 00
                    nop                                     ;[3b22] 00
                    nop                                     ;[3b23] 00
                    nop                                     ;[3b24] 00
                    nop                                     ;[3b25] 00
                    nop                                     ;[3b26] 00
                    nop                                     ;[3b27] 00
                    nop                                     ;[3b28] 00
                    nop                                     ;[3b29] 00
                    nop                                     ;[3b2a] 00
                    nop                                     ;[3b2b] 00
                    nop                                     ;[3b2c] 00
                    nop                                     ;[3b2d] 00
                    nop                                     ;[3b2e] 00
                    nop                                     ;[3b2f] 00
                    nop                                     ;[3b30] 00
                    nop                                     ;[3b31] 00
                    nop                                     ;[3b32] 00
                    nop                                     ;[3b33] 00
                    nop                                     ;[3b34] 00
                    nop                                     ;[3b35] 00
                    nop                                     ;[3b36] 00
                    nop                                     ;[3b37] 00
                    nop                                     ;[3b38] 00
                    nop                                     ;[3b39] 00
                    nop                                     ;[3b3a] 00
                    nop                                     ;[3b3b] 00
                    nop                                     ;[3b3c] 00
                    nop                                     ;[3b3d] 00
                    nop                                     ;[3b3e] 00
                    nop                                     ;[3b3f] 00
                    nop                                     ;[3b40] 00
                    nop                                     ;[3b41] 00
                    nop                                     ;[3b42] 00
                    nop                                     ;[3b43] 00
                    nop                                     ;[3b44] 00
                    nop                                     ;[3b45] 00
                    nop                                     ;[3b46] 00
                    nop                                     ;[3b47] 00
                    nop                                     ;[3b48] 00
                    nop                                     ;[3b49] 00
                    nop                                     ;[3b4a] 00
                    nop                                     ;[3b4b] 00
                    nop                                     ;[3b4c] 00
                    nop                                     ;[3b4d] 00
                    nop                                     ;[3b4e] 00
                    nop                                     ;[3b4f] 00
                    nop                                     ;[3b50] 00
                    nop                                     ;[3b51] 00
                    nop                                     ;[3b52] 00
                    nop                                     ;[3b53] 00
                    nop                                     ;[3b54] 00
                    nop                                     ;[3b55] 00
                    nop                                     ;[3b56] 00
                    nop                                     ;[3b57] 00
                    nop                                     ;[3b58] 00
                    nop                                     ;[3b59] 00
                    nop                                     ;[3b5a] 00
                    nop                                     ;[3b5b] 00
                    nop                                     ;[3b5c] 00
                    nop                                     ;[3b5d] 00
                    nop                                     ;[3b5e] 00
                    nop                                     ;[3b5f] 00
                    nop                                     ;[3b60] 00
                    nop                                     ;[3b61] 00
                    nop                                     ;[3b62] 00
                    nop                                     ;[3b63] 00
                    nop                                     ;[3b64] 00
                    nop                                     ;[3b65] 00
                    nop                                     ;[3b66] 00
                    nop                                     ;[3b67] 00
                    nop                                     ;[3b68] 00
                    nop                                     ;[3b69] 00
                    nop                                     ;[3b6a] 00
                    nop                                     ;[3b6b] 00
                    nop                                     ;[3b6c] 00
                    nop                                     ;[3b6d] 00
                    nop                                     ;[3b6e] 00
                    nop                                     ;[3b6f] 00
                    nop                                     ;[3b70] 00
                    nop                                     ;[3b71] 00
                    nop                                     ;[3b72] 00
                    nop                                     ;[3b73] 00
                    nop                                     ;[3b74] 00
                    nop                                     ;[3b75] 00
                    nop                                     ;[3b76] 00
                    nop                                     ;[3b77] 00
                    nop                                     ;[3b78] 00
                    nop                                     ;[3b79] 00
                    nop                                     ;[3b7a] 00
                    nop                                     ;[3b7b] 00
                    nop                                     ;[3b7c] 00
                    nop                                     ;[3b7d] 00
                    nop                                     ;[3b7e] 00
                    nop                                     ;[3b7f] 00
                    nop                                     ;[3b80] 00
                    nop                                     ;[3b81] 00
                    nop                                     ;[3b82] 00
                    nop                                     ;[3b83] 00
                    nop                                     ;[3b84] 00
                    nop                                     ;[3b85] 00
                    nop                                     ;[3b86] 00
                    nop                                     ;[3b87] 00
                    nop                                     ;[3b88] 00
                    nop                                     ;[3b89] 00
                    nop                                     ;[3b8a] 00
                    nop                                     ;[3b8b] 00
                    nop                                     ;[3b8c] 00
                    nop                                     ;[3b8d] 00
                    nop                                     ;[3b8e] 00
                    nop                                     ;[3b8f] 00
                    nop                                     ;[3b90] 00
                    nop                                     ;[3b91] 00
                    nop                                     ;[3b92] 00
                    nop                                     ;[3b93] 00
                    nop                                     ;[3b94] 00
                    nop                                     ;[3b95] 00
                    nop                                     ;[3b96] 00
                    nop                                     ;[3b97] 00
                    nop                                     ;[3b98] 00
                    nop                                     ;[3b99] 00
                    nop                                     ;[3b9a] 00
                    nop                                     ;[3b9b] 00
                    nop                                     ;[3b9c] 00
                    nop                                     ;[3b9d] 00
                    nop                                     ;[3b9e] 00
                    nop                                     ;[3b9f] 00
                    nop                                     ;[3ba0] 00
                    nop                                     ;[3ba1] 00
                    nop                                     ;[3ba2] 00
                    nop                                     ;[3ba3] 00
                    nop                                     ;[3ba4] 00
                    nop                                     ;[3ba5] 00
                    nop                                     ;[3ba6] 00
                    nop                                     ;[3ba7] 00
                    nop                                     ;[3ba8] 00
                    nop                                     ;[3ba9] 00
                    nop                                     ;[3baa] 00
                    nop                                     ;[3bab] 00
                    nop                                     ;[3bac] 00
                    nop                                     ;[3bad] 00
                    nop                                     ;[3bae] 00
                    nop                                     ;[3baf] 00
                    nop                                     ;[3bb0] 00
                    nop                                     ;[3bb1] 00
                    nop                                     ;[3bb2] 00
                    nop                                     ;[3bb3] 00
                    nop                                     ;[3bb4] 00
                    nop                                     ;[3bb5] 00
                    nop                                     ;[3bb6] 00
                    nop                                     ;[3bb7] 00
                    nop                                     ;[3bb8] 00
                    nop                                     ;[3bb9] 00
                    nop                                     ;[3bba] 00
                    nop                                     ;[3bbb] 00
                    nop                                     ;[3bbc] 00
                    nop                                     ;[3bbd] 00
                    nop                                     ;[3bbe] 00
                    nop                                     ;[3bbf] 00
                    nop                                     ;[3bc0] 00
                    nop                                     ;[3bc1] 00
                    nop                                     ;[3bc2] 00
                    nop                                     ;[3bc3] 00
                    nop                                     ;[3bc4] 00
                    nop                                     ;[3bc5] 00
                    nop                                     ;[3bc6] 00
                    nop                                     ;[3bc7] 00
                    nop                                     ;[3bc8] 00
                    nop                                     ;[3bc9] 00
                    nop                                     ;[3bca] 00
                    nop                                     ;[3bcb] 00
                    nop                                     ;[3bcc] 00
                    nop                                     ;[3bcd] 00
                    nop                                     ;[3bce] 00
                    nop                                     ;[3bcf] 00
                    nop                                     ;[3bd0] 00
                    nop                                     ;[3bd1] 00
                    nop                                     ;[3bd2] 00
                    nop                                     ;[3bd3] 00
                    nop                                     ;[3bd4] 00
                    nop                                     ;[3bd5] 00
                    nop                                     ;[3bd6] 00
                    nop                                     ;[3bd7] 00
                    nop                                     ;[3bd8] 00
                    nop                                     ;[3bd9] 00
                    nop                                     ;[3bda] 00
                    nop                                     ;[3bdb] 00
                    nop                                     ;[3bdc] 00
                    nop                                     ;[3bdd] 00
                    nop                                     ;[3bde] 00
                    nop                                     ;[3bdf] 00
                    nop                                     ;[3be0] 00
                    nop                                     ;[3be1] 00
                    nop                                     ;[3be2] 00
                    nop                                     ;[3be3] 00
                    nop                                     ;[3be4] 00
                    nop                                     ;[3be5] 00
                    nop                                     ;[3be6] 00
                    nop                                     ;[3be7] 00
                    nop                                     ;[3be8] 00
                    nop                                     ;[3be9] 00
                    nop                                     ;[3bea] 00
                    nop                                     ;[3beb] 00
                    nop                                     ;[3bec] 00
                    nop                                     ;[3bed] 00
                    nop                                     ;[3bee] 00
                    nop                                     ;[3bef] 00
                    nop                                     ;[3bf0] 00
                    nop                                     ;[3bf1] 00
                    nop                                     ;[3bf2] 00
                    nop                                     ;[3bf3] 00
                    nop                                     ;[3bf4] 00
                    nop                                     ;[3bf5] 00
                    nop                                     ;[3bf6] 00
                    nop                                     ;[3bf7] 00
                    nop                                     ;[3bf8] 00
                    nop                                     ;[3bf9] 00
                    nop                                     ;[3bfa] 00
                    nop                                     ;[3bfb] 00
                    nop                                     ;[3bfc] 00
                    nop                                     ;[3bfd] 00
                    nop                                     ;[3bfe] 00
                    nop                                     ;[3bff] 00
                    nop                                     ;[3c00] 00
                    nop                                     ;[3c01] 00
                    nop                                     ;[3c02] 00
                    nop                                     ;[3c03] 00
                    nop                                     ;[3c04] 00
                    nop                                     ;[3c05] 00
                    nop                                     ;[3c06] 00
                    nop                                     ;[3c07] 00
                    nop                                     ;[3c08] 00
                    nop                                     ;[3c09] 00
                    nop                                     ;[3c0a] 00
                    nop                                     ;[3c0b] 00
                    nop                                     ;[3c0c] 00
                    nop                                     ;[3c0d] 00
                    nop                                     ;[3c0e] 00
                    nop                                     ;[3c0f] 00
                    nop                                     ;[3c10] 00
                    nop                                     ;[3c11] 00
                    nop                                     ;[3c12] 00
                    nop                                     ;[3c13] 00
                    nop                                     ;[3c14] 00
                    nop                                     ;[3c15] 00
                    nop                                     ;[3c16] 00
                    nop                                     ;[3c17] 00
                    nop                                     ;[3c18] 00
                    nop                                     ;[3c19] 00
                    nop                                     ;[3c1a] 00
                    nop                                     ;[3c1b] 00
                    nop                                     ;[3c1c] 00
                    nop                                     ;[3c1d] 00
                    nop                                     ;[3c1e] 00
                    nop                                     ;[3c1f] 00
                    nop                                     ;[3c20] 00
                    nop                                     ;[3c21] 00
                    nop                                     ;[3c22] 00
                    nop                                     ;[3c23] 00
                    nop                                     ;[3c24] 00
                    nop                                     ;[3c25] 00
                    nop                                     ;[3c26] 00
                    nop                                     ;[3c27] 00
                    nop                                     ;[3c28] 00
                    nop                                     ;[3c29] 00
                    nop                                     ;[3c2a] 00
                    nop                                     ;[3c2b] 00
                    nop                                     ;[3c2c] 00
                    nop                                     ;[3c2d] 00
                    nop                                     ;[3c2e] 00
                    nop                                     ;[3c2f] 00
                    nop                                     ;[3c30] 00
                    nop                                     ;[3c31] 00
                    nop                                     ;[3c32] 00
                    nop                                     ;[3c33] 00
                    nop                                     ;[3c34] 00
                    nop                                     ;[3c35] 00
                    nop                                     ;[3c36] 00
                    nop                                     ;[3c37] 00
                    nop                                     ;[3c38] 00
                    nop                                     ;[3c39] 00
                    nop                                     ;[3c3a] 00
                    nop                                     ;[3c3b] 00
                    nop                                     ;[3c3c] 00
                    nop                                     ;[3c3d] 00
                    nop                                     ;[3c3e] 00
                    nop                                     ;[3c3f] 00
                    nop                                     ;[3c40] 00
                    nop                                     ;[3c41] 00
                    nop                                     ;[3c42] 00
                    nop                                     ;[3c43] 00
                    nop                                     ;[3c44] 00
                    nop                                     ;[3c45] 00
                    nop                                     ;[3c46] 00
                    nop                                     ;[3c47] 00
                    nop                                     ;[3c48] 00
                    nop                                     ;[3c49] 00
                    nop                                     ;[3c4a] 00
                    nop                                     ;[3c4b] 00
                    nop                                     ;[3c4c] 00
                    nop                                     ;[3c4d] 00
                    nop                                     ;[3c4e] 00
                    nop                                     ;[3c4f] 00
                    nop                                     ;[3c50] 00
                    nop                                     ;[3c51] 00
                    nop                                     ;[3c52] 00
                    nop                                     ;[3c53] 00
                    nop                                     ;[3c54] 00
                    nop                                     ;[3c55] 00
                    nop                                     ;[3c56] 00
                    nop                                     ;[3c57] 00
                    nop                                     ;[3c58] 00
                    nop                                     ;[3c59] 00
                    nop                                     ;[3c5a] 00
                    nop                                     ;[3c5b] 00
                    nop                                     ;[3c5c] 00
                    nop                                     ;[3c5d] 00
                    nop                                     ;[3c5e] 00
                    nop                                     ;[3c5f] 00
                    nop                                     ;[3c60] 00
                    nop                                     ;[3c61] 00
                    nop                                     ;[3c62] 00
                    nop                                     ;[3c63] 00
                    nop                                     ;[3c64] 00
                    nop                                     ;[3c65] 00
                    nop                                     ;[3c66] 00
                    nop                                     ;[3c67] 00
                    nop                                     ;[3c68] 00
                    nop                                     ;[3c69] 00
                    nop                                     ;[3c6a] 00
                    nop                                     ;[3c6b] 00
                    nop                                     ;[3c6c] 00
                    nop                                     ;[3c6d] 00
                    nop                                     ;[3c6e] 00
                    nop                                     ;[3c6f] 00
                    nop                                     ;[3c70] 00
                    nop                                     ;[3c71] 00
                    nop                                     ;[3c72] 00
                    nop                                     ;[3c73] 00
                    nop                                     ;[3c74] 00
                    nop                                     ;[3c75] 00
                    nop                                     ;[3c76] 00
                    nop                                     ;[3c77] 00
                    nop                                     ;[3c78] 00
                    nop                                     ;[3c79] 00
                    nop                                     ;[3c7a] 00
                    nop                                     ;[3c7b] 00
                    nop                                     ;[3c7c] 00
                    nop                                     ;[3c7d] 00
                    nop                                     ;[3c7e] 00
                    nop                                     ;[3c7f] 00
                    nop                                     ;[3c80] 00
                    nop                                     ;[3c81] 00
                    nop                                     ;[3c82] 00
                    nop                                     ;[3c83] 00
                    nop                                     ;[3c84] 00
                    nop                                     ;[3c85] 00
                    nop                                     ;[3c86] 00
                    nop                                     ;[3c87] 00
                    nop                                     ;[3c88] 00
                    nop                                     ;[3c89] 00
                    nop                                     ;[3c8a] 00
                    nop                                     ;[3c8b] 00
                    nop                                     ;[3c8c] 00
                    nop                                     ;[3c8d] 00
                    nop                                     ;[3c8e] 00
                    nop                                     ;[3c8f] 00
                    nop                                     ;[3c90] 00
                    nop                                     ;[3c91] 00
                    nop                                     ;[3c92] 00
                    nop                                     ;[3c93] 00
                    nop                                     ;[3c94] 00
                    nop                                     ;[3c95] 00
                    nop                                     ;[3c96] 00
                    nop                                     ;[3c97] 00
                    nop                                     ;[3c98] 00
                    nop                                     ;[3c99] 00
                    nop                                     ;[3c9a] 00
                    nop                                     ;[3c9b] 00
                    nop                                     ;[3c9c] 00
                    nop                                     ;[3c9d] 00
                    nop                                     ;[3c9e] 00
                    nop                                     ;[3c9f] 00
                    nop                                     ;[3ca0] 00
                    nop                                     ;[3ca1] 00
                    nop                                     ;[3ca2] 00
                    nop                                     ;[3ca3] 00
                    nop                                     ;[3ca4] 00
                    nop                                     ;[3ca5] 00
                    nop                                     ;[3ca6] 00
                    nop                                     ;[3ca7] 00
                    nop                                     ;[3ca8] 00
                    nop                                     ;[3ca9] 00
                    nop                                     ;[3caa] 00
                    nop                                     ;[3cab] 00
                    nop                                     ;[3cac] 00
                    nop                                     ;[3cad] 00
                    nop                                     ;[3cae] 00
                    nop                                     ;[3caf] 00
                    nop                                     ;[3cb0] 00
                    nop                                     ;[3cb1] 00
                    nop                                     ;[3cb2] 00
                    nop                                     ;[3cb3] 00
                    nop                                     ;[3cb4] 00
                    nop                                     ;[3cb5] 00
                    nop                                     ;[3cb6] 00
                    nop                                     ;[3cb7] 00
                    nop                                     ;[3cb8] 00
                    nop                                     ;[3cb9] 00
                    nop                                     ;[3cba] 00
                    nop                                     ;[3cbb] 00
                    nop                                     ;[3cbc] 00
                    nop                                     ;[3cbd] 00
                    nop                                     ;[3cbe] 00
                    nop                                     ;[3cbf] 00
                    nop                                     ;[3cc0] 00
                    nop                                     ;[3cc1] 00
                    nop                                     ;[3cc2] 00
                    nop                                     ;[3cc3] 00
                    nop                                     ;[3cc4] 00
                    nop                                     ;[3cc5] 00
                    nop                                     ;[3cc6] 00
                    nop                                     ;[3cc7] 00
                    nop                                     ;[3cc8] 00
                    nop                                     ;[3cc9] 00
                    nop                                     ;[3cca] 00
                    nop                                     ;[3ccb] 00
                    nop                                     ;[3ccc] 00
                    nop                                     ;[3ccd] 00
                    nop                                     ;[3cce] 00
                    nop                                     ;[3ccf] 00
                    nop                                     ;[3cd0] 00
                    nop                                     ;[3cd1] 00
                    nop                                     ;[3cd2] 00
                    nop                                     ;[3cd3] 00
                    nop                                     ;[3cd4] 00
                    nop                                     ;[3cd5] 00
                    nop                                     ;[3cd6] 00
                    nop                                     ;[3cd7] 00
                    nop                                     ;[3cd8] 00
                    nop                                     ;[3cd9] 00
                    nop                                     ;[3cda] 00
                    nop                                     ;[3cdb] 00
                    nop                                     ;[3cdc] 00
                    nop                                     ;[3cdd] 00
                    nop                                     ;[3cde] 00
                    nop                                     ;[3cdf] 00
                    nop                                     ;[3ce0] 00
                    nop                                     ;[3ce1] 00
                    nop                                     ;[3ce2] 00
                    nop                                     ;[3ce3] 00
                    nop                                     ;[3ce4] 00
                    nop                                     ;[3ce5] 00
                    nop                                     ;[3ce6] 00
                    nop                                     ;[3ce7] 00
                    nop                                     ;[3ce8] 00
                    nop                                     ;[3ce9] 00
                    nop                                     ;[3cea] 00
                    nop                                     ;[3ceb] 00
                    nop                                     ;[3cec] 00
                    nop                                     ;[3ced] 00
                    nop                                     ;[3cee] 00
                    nop                                     ;[3cef] 00
                    nop                                     ;[3cf0] 00
                    nop                                     ;[3cf1] 00
                    nop                                     ;[3cf2] 00
                    nop                                     ;[3cf3] 00
                    nop                                     ;[3cf4] 00
                    nop                                     ;[3cf5] 00
                    nop                                     ;[3cf6] 00
                    nop                                     ;[3cf7] 00
                    nop                                     ;[3cf8] 00
                    nop                                     ;[3cf9] 00
                    nop                                     ;[3cfa] 00
                    nop                                     ;[3cfb] 00
                    nop                                     ;[3cfc] 00
                    nop                                     ;[3cfd] 00
                    nop                                     ;[3cfe] 00
                    nop                                     ;[3cff] 00
                    nop                                     ;[3d00] 00
                    nop                                     ;[3d01] 00
                    nop                                     ;[3d02] 00
                    nop                                     ;[3d03] 00
                    nop                                     ;[3d04] 00
                    nop                                     ;[3d05] 00
                    nop                                     ;[3d06] 00
                    nop                                     ;[3d07] 00
                    nop                                     ;[3d08] 00
                    nop                                     ;[3d09] 00
                    nop                                     ;[3d0a] 00
                    nop                                     ;[3d0b] 00
                    nop                                     ;[3d0c] 00
                    nop                                     ;[3d0d] 00
                    nop                                     ;[3d0e] 00
                    nop                                     ;[3d0f] 00
                    nop                                     ;[3d10] 00
                    nop                                     ;[3d11] 00
                    nop                                     ;[3d12] 00
                    nop                                     ;[3d13] 00
                    nop                                     ;[3d14] 00
                    nop                                     ;[3d15] 00
                    nop                                     ;[3d16] 00
                    nop                                     ;[3d17] 00
                    nop                                     ;[3d18] 00
                    nop                                     ;[3d19] 00
                    nop                                     ;[3d1a] 00
                    nop                                     ;[3d1b] 00
                    nop                                     ;[3d1c] 00
                    nop                                     ;[3d1d] 00
                    nop                                     ;[3d1e] 00
                    nop                                     ;[3d1f] 00
                    nop                                     ;[3d20] 00
                    nop                                     ;[3d21] 00
                    nop                                     ;[3d22] 00
                    nop                                     ;[3d23] 00
                    nop                                     ;[3d24] 00
                    nop                                     ;[3d25] 00
                    nop                                     ;[3d26] 00
                    nop                                     ;[3d27] 00
                    nop                                     ;[3d28] 00
                    nop                                     ;[3d29] 00
                    nop                                     ;[3d2a] 00
                    nop                                     ;[3d2b] 00
                    nop                                     ;[3d2c] 00
                    nop                                     ;[3d2d] 00
                    nop                                     ;[3d2e] 00
                    nop                                     ;[3d2f] 00
                    nop                                     ;[3d30] 00
                    nop                                     ;[3d31] 00
                    nop                                     ;[3d32] 00
                    nop                                     ;[3d33] 00
                    nop                                     ;[3d34] 00
                    nop                                     ;[3d35] 00
                    nop                                     ;[3d36] 00
                    nop                                     ;[3d37] 00
                    nop                                     ;[3d38] 00
                    nop                                     ;[3d39] 00
                    nop                                     ;[3d3a] 00
                    nop                                     ;[3d3b] 00
                    nop                                     ;[3d3c] 00
                    nop                                     ;[3d3d] 00
                    nop                                     ;[3d3e] 00
                    nop                                     ;[3d3f] 00
                    nop                                     ;[3d40] 00
                    nop                                     ;[3d41] 00
                    nop                                     ;[3d42] 00
                    nop                                     ;[3d43] 00
                    nop                                     ;[3d44] 00
                    nop                                     ;[3d45] 00
                    nop                                     ;[3d46] 00
                    nop                                     ;[3d47] 00
                    nop                                     ;[3d48] 00
                    nop                                     ;[3d49] 00
                    nop                                     ;[3d4a] 00
                    nop                                     ;[3d4b] 00
                    nop                                     ;[3d4c] 00
                    nop                                     ;[3d4d] 00
                    nop                                     ;[3d4e] 00
                    nop                                     ;[3d4f] 00
                    nop                                     ;[3d50] 00
                    nop                                     ;[3d51] 00
                    nop                                     ;[3d52] 00
                    nop                                     ;[3d53] 00
                    nop                                     ;[3d54] 00
                    nop                                     ;[3d55] 00
                    nop                                     ;[3d56] 00
                    nop                                     ;[3d57] 00
                    nop                                     ;[3d58] 00
                    nop                                     ;[3d59] 00
                    nop                                     ;[3d5a] 00
                    nop                                     ;[3d5b] 00
                    nop                                     ;[3d5c] 00
                    nop                                     ;[3d5d] 00
                    nop                                     ;[3d5e] 00
                    nop                                     ;[3d5f] 00
                    nop                                     ;[3d60] 00
                    nop                                     ;[3d61] 00
                    nop                                     ;[3d62] 00
                    nop                                     ;[3d63] 00
                    nop                                     ;[3d64] 00
                    nop                                     ;[3d65] 00
                    nop                                     ;[3d66] 00
                    nop                                     ;[3d67] 00
                    nop                                     ;[3d68] 00
                    nop                                     ;[3d69] 00
                    nop                                     ;[3d6a] 00
                    nop                                     ;[3d6b] 00
                    nop                                     ;[3d6c] 00
                    nop                                     ;[3d6d] 00
                    nop                                     ;[3d6e] 00
                    nop                                     ;[3d6f] 00
                    nop                                     ;[3d70] 00
                    nop                                     ;[3d71] 00
                    nop                                     ;[3d72] 00
                    nop                                     ;[3d73] 00
                    nop                                     ;[3d74] 00
                    nop                                     ;[3d75] 00
                    nop                                     ;[3d76] 00
                    nop                                     ;[3d77] 00
                    nop                                     ;[3d78] 00
                    nop                                     ;[3d79] 00
                    nop                                     ;[3d7a] 00
                    nop                                     ;[3d7b] 00
                    nop                                     ;[3d7c] 00
                    nop                                     ;[3d7d] 00
                    nop                                     ;[3d7e] 00
                    nop                                     ;[3d7f] 00
                    nop                                     ;[3d80] 00
                    nop                                     ;[3d81] 00
                    nop                                     ;[3d82] 00
                    nop                                     ;[3d83] 00
                    nop                                     ;[3d84] 00
                    nop                                     ;[3d85] 00
                    nop                                     ;[3d86] 00
                    nop                                     ;[3d87] 00
                    nop                                     ;[3d88] 00
                    nop                                     ;[3d89] 00
                    nop                                     ;[3d8a] 00
                    nop                                     ;[3d8b] 00
                    nop                                     ;[3d8c] 00
                    nop                                     ;[3d8d] 00
                    nop                                     ;[3d8e] 00
                    nop                                     ;[3d8f] 00
                    nop                                     ;[3d90] 00
                    nop                                     ;[3d91] 00
                    nop                                     ;[3d92] 00
                    nop                                     ;[3d93] 00
                    nop                                     ;[3d94] 00
                    nop                                     ;[3d95] 00
                    nop                                     ;[3d96] 00
                    nop                                     ;[3d97] 00
                    nop                                     ;[3d98] 00
                    nop                                     ;[3d99] 00
                    nop                                     ;[3d9a] 00
                    nop                                     ;[3d9b] 00
                    nop                                     ;[3d9c] 00
                    nop                                     ;[3d9d] 00
                    nop                                     ;[3d9e] 00
                    nop                                     ;[3d9f] 00
                    nop                                     ;[3da0] 00
                    nop                                     ;[3da1] 00
                    nop                                     ;[3da2] 00
                    nop                                     ;[3da3] 00
                    nop                                     ;[3da4] 00
                    nop                                     ;[3da5] 00
                    nop                                     ;[3da6] 00
                    nop                                     ;[3da7] 00
                    nop                                     ;[3da8] 00
                    nop                                     ;[3da9] 00
                    nop                                     ;[3daa] 00
                    nop                                     ;[3dab] 00
                    nop                                     ;[3dac] 00
                    nop                                     ;[3dad] 00
                    nop                                     ;[3dae] 00
                    nop                                     ;[3daf] 00
                    nop                                     ;[3db0] 00
                    nop                                     ;[3db1] 00
                    nop                                     ;[3db2] 00
                    nop                                     ;[3db3] 00
                    nop                                     ;[3db4] 00
                    nop                                     ;[3db5] 00
                    nop                                     ;[3db6] 00
                    nop                                     ;[3db7] 00
                    nop                                     ;[3db8] 00
                    nop                                     ;[3db9] 00
                    nop                                     ;[3dba] 00
                    nop                                     ;[3dbb] 00
                    nop                                     ;[3dbc] 00
                    nop                                     ;[3dbd] 00
                    nop                                     ;[3dbe] 00
                    nop                                     ;[3dbf] 00
                    nop                                     ;[3dc0] 00
                    nop                                     ;[3dc1] 00
                    nop                                     ;[3dc2] 00
                    nop                                     ;[3dc3] 00
                    nop                                     ;[3dc4] 00
                    nop                                     ;[3dc5] 00
                    nop                                     ;[3dc6] 00
                    nop                                     ;[3dc7] 00
                    nop                                     ;[3dc8] 00
                    nop                                     ;[3dc9] 00
                    nop                                     ;[3dca] 00
                    nop                                     ;[3dcb] 00
                    nop                                     ;[3dcc] 00
                    nop                                     ;[3dcd] 00
                    nop                                     ;[3dce] 00
                    nop                                     ;[3dcf] 00
                    nop                                     ;[3dd0] 00
                    nop                                     ;[3dd1] 00
                    nop                                     ;[3dd2] 00
                    nop                                     ;[3dd3] 00
                    nop                                     ;[3dd4] 00
                    nop                                     ;[3dd5] 00
                    nop                                     ;[3dd6] 00
                    nop                                     ;[3dd7] 00
                    nop                                     ;[3dd8] 00
                    nop                                     ;[3dd9] 00
                    nop                                     ;[3dda] 00
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
                    ld        ($5b52),hl                    ;[3e00] 22 52 5b
                    push      af                            ;[3e03] f5
                    pop       hl                            ;[3e04] e1
                    ld        ($5b56),hl                    ;[3e05] 22 56 5b
                    ex        (sp),hl                       ;[3e08] e3
                    ld        c,(hl)                        ;[3e09] 4e
                    inc       hl                            ;[3e0a] 23
                    ld        b,(hl)                        ;[3e0b] 46
                    inc       hl                            ;[3e0c] 23
                    ex        (sp),hl                       ;[3e0d] e3
                    push      bc                            ;[3e0e] c5
                    pop       hl                            ;[3e0f] e1
                    ld        a,($5b67)                     ;[3e10] 3a 67 5b
                    ld        bc,$1ffd                      ;[3e13] 01 fd 1f
                    res       2,a                           ;[3e16] cb 97
                    di                                      ;[3e18] f3
                    ld        ($5b67),a                     ;[3e19] 32 67 5b
                    out       (c),a                         ;[3e1c] ed 79
                    ei                                      ;[3e1e] fb
                    ld        bc,$3e2d                      ;[3e1f] 01 2d 3e
                    push      bc                            ;[3e22] c5
                    push      hl                            ;[3e23] e5
                    ld        hl,($5b56)                    ;[3e24] 2a 56 5b
                    push      hl                            ;[3e27] e5
                    pop       af                            ;[3e28] f1
                    ld        hl,($5b52)                    ;[3e29] 2a 52 5b
                    ret                                     ;[3e2c] c9

                    push      bc                            ;[3e2d] c5
                    push      af                            ;[3e2e] f5
                    ld        a,($5b67)                     ;[3e2f] 3a 67 5b
                    ld        bc,$1ffd                      ;[3e32] 01 fd 1f
                    set       2,a                           ;[3e35] cb d7
                    di                                      ;[3e37] f3
                    ld        ($5b67),a                     ;[3e38] 32 67 5b
                    out       (c),a                         ;[3e3b] ed 79
                    ei                                      ;[3e3d] fb
                    pop       af                            ;[3e3e] f1
                    pop       bc                            ;[3e3f] c1
                    ret                                     ;[3e40] c9

                    nop                                     ;[3e41] 00
                    nop                                     ;[3e42] 00
                    nop                                     ;[3e43] 00
                    nop                                     ;[3e44] 00
                    nop                                     ;[3e45] 00
                    nop                                     ;[3e46] 00
                    nop                                     ;[3e47] 00
                    nop                                     ;[3e48] 00
                    nop                                     ;[3e49] 00
                    nop                                     ;[3e4a] 00
                    nop                                     ;[3e4b] 00
                    nop                                     ;[3e4c] 00
                    nop                                     ;[3e4d] 00
                    nop                                     ;[3e4e] 00
                    nop                                     ;[3e4f] 00
                    nop                                     ;[3e50] 00
                    nop                                     ;[3e51] 00
                    nop                                     ;[3e52] 00
                    nop                                     ;[3e53] 00
                    nop                                     ;[3e54] 00
                    nop                                     ;[3e55] 00
                    nop                                     ;[3e56] 00
                    nop                                     ;[3e57] 00
                    nop                                     ;[3e58] 00
                    nop                                     ;[3e59] 00
                    nop                                     ;[3e5a] 00
                    nop                                     ;[3e5b] 00
                    nop                                     ;[3e5c] 00
                    nop                                     ;[3e5d] 00
                    nop                                     ;[3e5e] 00
                    nop                                     ;[3e5f] 00
                    nop                                     ;[3e60] 00
                    nop                                     ;[3e61] 00
                    nop                                     ;[3e62] 00
                    nop                                     ;[3e63] 00
                    nop                                     ;[3e64] 00
                    nop                                     ;[3e65] 00
                    nop                                     ;[3e66] 00
                    nop                                     ;[3e67] 00
                    nop                                     ;[3e68] 00
                    nop                                     ;[3e69] 00
                    nop                                     ;[3e6a] 00
                    nop                                     ;[3e6b] 00
                    nop                                     ;[3e6c] 00
                    nop                                     ;[3e6d] 00
                    nop                                     ;[3e6e] 00
                    nop                                     ;[3e6f] 00
                    nop                                     ;[3e70] 00
                    nop                                     ;[3e71] 00
                    nop                                     ;[3e72] 00
                    nop                                     ;[3e73] 00
                    nop                                     ;[3e74] 00
                    nop                                     ;[3e75] 00
                    nop                                     ;[3e76] 00
                    nop                                     ;[3e77] 00
                    nop                                     ;[3e78] 00
                    nop                                     ;[3e79] 00
                    nop                                     ;[3e7a] 00
                    nop                                     ;[3e7b] 00
                    nop                                     ;[3e7c] 00
                    nop                                     ;[3e7d] 00
                    nop                                     ;[3e7e] 00
                    nop                                     ;[3e7f] 00
                    nop                                     ;[3e80] 00
                    nop                                     ;[3e81] 00
                    nop                                     ;[3e82] 00
                    nop                                     ;[3e83] 00
                    nop                                     ;[3e84] 00
                    nop                                     ;[3e85] 00
                    nop                                     ;[3e86] 00
                    nop                                     ;[3e87] 00
                    nop                                     ;[3e88] 00
                    nop                                     ;[3e89] 00
                    nop                                     ;[3e8a] 00
                    nop                                     ;[3e8b] 00
                    nop                                     ;[3e8c] 00
                    nop                                     ;[3e8d] 00
                    nop                                     ;[3e8e] 00
                    nop                                     ;[3e8f] 00
                    nop                                     ;[3e90] 00
                    nop                                     ;[3e91] 00
                    nop                                     ;[3e92] 00
                    nop                                     ;[3e93] 00
                    nop                                     ;[3e94] 00
                    nop                                     ;[3e95] 00
                    nop                                     ;[3e96] 00
                    nop                                     ;[3e97] 00
                    nop                                     ;[3e98] 00
                    nop                                     ;[3e99] 00
                    nop                                     ;[3e9a] 00
                    nop                                     ;[3e9b] 00
                    nop                                     ;[3e9c] 00
                    nop                                     ;[3e9d] 00
                    nop                                     ;[3e9e] 00
                    nop                                     ;[3e9f] 00
                    nop                                     ;[3ea0] 00
                    nop                                     ;[3ea1] 00
                    nop                                     ;[3ea2] 00
                    nop                                     ;[3ea3] 00
                    nop                                     ;[3ea4] 00
                    nop                                     ;[3ea5] 00
                    nop                                     ;[3ea6] 00
                    nop                                     ;[3ea7] 00
                    nop                                     ;[3ea8] 00
                    nop                                     ;[3ea9] 00
                    nop                                     ;[3eaa] 00
                    nop                                     ;[3eab] 00
                    nop                                     ;[3eac] 00
                    nop                                     ;[3ead] 00
                    nop                                     ;[3eae] 00
                    nop                                     ;[3eaf] 00
                    nop                                     ;[3eb0] 00
                    nop                                     ;[3eb1] 00
                    nop                                     ;[3eb2] 00
                    nop                                     ;[3eb3] 00
                    nop                                     ;[3eb4] 00
                    nop                                     ;[3eb5] 00
                    nop                                     ;[3eb6] 00
                    nop                                     ;[3eb7] 00
                    nop                                     ;[3eb8] 00
                    nop                                     ;[3eb9] 00
                    nop                                     ;[3eba] 00
                    nop                                     ;[3ebb] 00
                    nop                                     ;[3ebc] 00
                    nop                                     ;[3ebd] 00
                    nop                                     ;[3ebe] 00
                    nop                                     ;[3ebf] 00
                    nop                                     ;[3ec0] 00
                    nop                                     ;[3ec1] 00
                    nop                                     ;[3ec2] 00
                    nop                                     ;[3ec3] 00
                    nop                                     ;[3ec4] 00
                    nop                                     ;[3ec5] 00
                    nop                                     ;[3ec6] 00
                    nop                                     ;[3ec7] 00
                    nop                                     ;[3ec8] 00
                    nop                                     ;[3ec9] 00
                    nop                                     ;[3eca] 00
                    nop                                     ;[3ecb] 00
                    nop                                     ;[3ecc] 00
                    nop                                     ;[3ecd] 00
                    nop                                     ;[3ece] 00
                    nop                                     ;[3ecf] 00
                    nop                                     ;[3ed0] 00
                    nop                                     ;[3ed1] 00
                    nop                                     ;[3ed2] 00
                    nop                                     ;[3ed3] 00
                    nop                                     ;[3ed4] 00
                    nop                                     ;[3ed5] 00
                    nop                                     ;[3ed6] 00
                    nop                                     ;[3ed7] 00
                    nop                                     ;[3ed8] 00
                    nop                                     ;[3ed9] 00
                    nop                                     ;[3eda] 00
                    nop                                     ;[3edb] 00
                    nop                                     ;[3edc] 00
                    nop                                     ;[3edd] 00
                    nop                                     ;[3ede] 00
                    nop                                     ;[3edf] 00
                    nop                                     ;[3ee0] 00
                    nop                                     ;[3ee1] 00
                    nop                                     ;[3ee2] 00
                    nop                                     ;[3ee3] 00
                    nop                                     ;[3ee4] 00
                    nop                                     ;[3ee5] 00
                    nop                                     ;[3ee6] 00
                    nop                                     ;[3ee7] 00
                    nop                                     ;[3ee8] 00
                    nop                                     ;[3ee9] 00
                    nop                                     ;[3eea] 00
                    nop                                     ;[3eeb] 00
                    nop                                     ;[3eec] 00
                    nop                                     ;[3eed] 00
                    nop                                     ;[3eee] 00
                    nop                                     ;[3eef] 00
                    nop                                     ;[3ef0] 00
                    nop                                     ;[3ef1] 00
                    nop                                     ;[3ef2] 00
                    nop                                     ;[3ef3] 00
                    nop                                     ;[3ef4] 00
                    nop                                     ;[3ef5] 00
                    nop                                     ;[3ef6] 00
                    nop                                     ;[3ef7] 00
                    nop                                     ;[3ef8] 00
                    nop                                     ;[3ef9] 00
                    nop                                     ;[3efa] 00
                    nop                                     ;[3efb] 00
                    nop                                     ;[3efc] 00
                    nop                                     ;[3efd] 00
                    nop                                     ;[3efe] 00
                    nop                                     ;[3eff] 00
                    ld        ($5b52),hl                    ;[3f00] 22 52 5b
                    ld        ($5b54),bc                    ;[3f03] ed 43 54 5b
                    push      af                            ;[3f07] f5
                    pop       hl                            ;[3f08] e1
                    ld        ($5b56),hl                    ;[3f09] 22 56 5b
                    ex        (sp),hl                       ;[3f0c] e3
                    ld        c,(hl)                        ;[3f0d] 4e
                    inc       hl                            ;[3f0e] 23
                    ld        b,(hl)                        ;[3f0f] 46
                    inc       hl                            ;[3f10] 23
                    ex        (sp),hl                       ;[3f11] e3
                    push      bc                            ;[3f12] c5
                    pop       hl                            ;[3f13] e1
                    ld        a,($5b5c)                     ;[3f14] 3a 5c 5b
                    and       $ef                           ;[3f17] e6 ef
                    di                                      ;[3f19] f3
                    ld        ($5b5c),a                     ;[3f1a] 32 5c 5b
                    ld        bc,$7ffd                      ;[3f1d] 01 fd 7f
                    out       (c),a                         ;[3f20] ed 79
                    ld        a,($5b67)                     ;[3f22] 3a 67 5b
                    or        $04                           ;[3f25] f6 04
                    ld        ($5b67),a                     ;[3f27] 32 67 5b
                    ld        bc,$1ffd                      ;[3f2a] 01 fd 1f
                    out       (c),a                         ;[3f2d] ed 79
                    ei                                      ;[3f2f] fb
                    ld        bc,$3f42                      ;[3f30] 01 42 3f
                    push      bc                            ;[3f33] c5
                    push      hl                            ;[3f34] e5
                    ld        hl,($5b56)                    ;[3f35] 2a 56 5b
                    push      hl                            ;[3f38] e5
                    pop       af                            ;[3f39] f1
                    ld        bc,($5b54)                    ;[3f3a] ed 4b 54 5b
                    ld        hl,($5b52)                    ;[3f3e] 2a 52 5b
                    ret                                     ;[3f41] c9

                    push      bc                            ;[3f42] c5
                    push      af                            ;[3f43] f5
                    ld        a,($5b67)                     ;[3f44] 3a 67 5b
                    and       $fb                           ;[3f47] e6 fb
                    di                                      ;[3f49] f3
                    ld        ($5b67),a                     ;[3f4a] 32 67 5b
                    ld        bc,$1ffd                      ;[3f4d] 01 fd 1f
                    out       (c),a                         ;[3f50] ed 79
                    ld        a,($5b5c)                     ;[3f52] 3a 5c 5b
                    or        $10                           ;[3f55] f6 10
                    ld        ($5b5c),a                     ;[3f57] 32 5c 5b
                    ld        bc,$7ffd                      ;[3f5a] 01 fd 7f
                    out       (c),a                         ;[3f5d] ed 79
                    ei                                      ;[3f5f] fb
                    pop       af                            ;[3f60] f1
                    pop       bc                            ;[3f61] c1
                    ret                                     ;[3f62] c9

                    rst       $38                           ;[3f63] ff
                    rst       $38                           ;[3f64] ff
                    rst       $38                           ;[3f65] ff
                    rst       $38                           ;[3f66] ff
                    rst       $38                           ;[3f67] ff
                    rst       $38                           ;[3f68] ff
                    rst       $38                           ;[3f69] ff
                    rst       $38                           ;[3f6a] ff
                    rst       $38                           ;[3f6b] ff
                    rst       $38                           ;[3f6c] ff
                    rst       $38                           ;[3f6d] ff
                    rst       $38                           ;[3f6e] ff
                    rst       $38                           ;[3f6f] ff
                    rst       $38                           ;[3f70] ff
                    rst       $38                           ;[3f71] ff
                    rst       $38                           ;[3f72] ff
                    rst       $38                           ;[3f73] ff
                    rst       $38                           ;[3f74] ff
                    rst       $38                           ;[3f75] ff
                    rst       $38                           ;[3f76] ff
                    rst       $38                           ;[3f77] ff
                    rst       $38                           ;[3f78] ff
                    rst       $38                           ;[3f79] ff
                    rst       $38                           ;[3f7a] ff
                    rst       $38                           ;[3f7b] ff
                    rst       $38                           ;[3f7c] ff
                    rst       $38                           ;[3f7d] ff
                    rst       $38                           ;[3f7e] ff
                    rst       $38                           ;[3f7f] ff
                    rst       $38                           ;[3f80] ff
                    rst       $38                           ;[3f81] ff
                    rst       $38                           ;[3f82] ff
                    rst       $38                           ;[3f83] ff
                    rst       $38                           ;[3f84] ff
                    rst       $38                           ;[3f85] ff
                    rst       $38                           ;[3f86] ff
                    rst       $38                           ;[3f87] ff
                    rst       $38                           ;[3f88] ff
                    rst       $38                           ;[3f89] ff
                    rst       $38                           ;[3f8a] ff
                    rst       $38                           ;[3f8b] ff
                    rst       $38                           ;[3f8c] ff
                    rst       $38                           ;[3f8d] ff
                    rst       $38                           ;[3f8e] ff
                    rst       $38                           ;[3f8f] ff
                    rst       $38                           ;[3f90] ff
                    rst       $38                           ;[3f91] ff
                    rst       $38                           ;[3f92] ff
                    rst       $38                           ;[3f93] ff
                    rst       $38                           ;[3f94] ff
                    rst       $38                           ;[3f95] ff
                    rst       $38                           ;[3f96] ff
                    rst       $38                           ;[3f97] ff
                    rst       $38                           ;[3f98] ff
                    rst       $38                           ;[3f99] ff
                    rst       $38                           ;[3f9a] ff
                    rst       $38                           ;[3f9b] ff
                    rst       $38                           ;[3f9c] ff
                    rst       $38                           ;[3f9d] ff
                    rst       $38                           ;[3f9e] ff
                    rst       $38                           ;[3f9f] ff
                    rst       $38                           ;[3fa0] ff
                    rst       $38                           ;[3fa1] ff
                    rst       $38                           ;[3fa2] ff
                    rst       $38                           ;[3fa3] ff
                    rst       $38                           ;[3fa4] ff
                    rst       $38                           ;[3fa5] ff
                    rst       $38                           ;[3fa6] ff
                    rst       $38                           ;[3fa7] ff
                    rst       $38                           ;[3fa8] ff
                    rst       $38                           ;[3fa9] ff
                    rst       $38                           ;[3faa] ff
                    rst       $38                           ;[3fab] ff
                    rst       $38                           ;[3fac] ff
                    rst       $38                           ;[3fad] ff
                    rst       $38                           ;[3fae] ff
                    rst       $38                           ;[3faf] ff
                    rst       $38                           ;[3fb0] ff
                    rst       $38                           ;[3fb1] ff
                    rst       $38                           ;[3fb2] ff
                    rst       $38                           ;[3fb3] ff
                    rst       $38                           ;[3fb4] ff
                    rst       $38                           ;[3fb5] ff
                    rst       $38                           ;[3fb6] ff
                    rst       $38                           ;[3fb7] ff
                    rst       $38                           ;[3fb8] ff
                    rst       $38                           ;[3fb9] ff
                    rst       $38                           ;[3fba] ff
                    rst       $38                           ;[3fbb] ff
                    rst       $38                           ;[3fbc] ff
                    rst       $38                           ;[3fbd] ff
                    rst       $38                           ;[3fbe] ff
                    rst       $38                           ;[3fbf] ff
                    rst       $38                           ;[3fc0] ff
                    rst       $38                           ;[3fc1] ff
                    rst       $38                           ;[3fc2] ff
                    rst       $38                           ;[3fc3] ff
                    rst       $38                           ;[3fc4] ff
                    rst       $38                           ;[3fc5] ff
                    rst       $38                           ;[3fc6] ff
                    rst       $38                           ;[3fc7] ff
                    rst       $38                           ;[3fc8] ff
                    rst       $38                           ;[3fc9] ff
                    rst       $38                           ;[3fca] ff
                    rst       $38                           ;[3fcb] ff
                    rst       $38                           ;[3fcc] ff
                    rst       $38                           ;[3fcd] ff
                    rst       $38                           ;[3fce] ff
                    rst       $38                           ;[3fcf] ff
                    rst       $38                           ;[3fd0] ff
                    rst       $38                           ;[3fd1] ff
                    rst       $38                           ;[3fd2] ff
                    rst       $38                           ;[3fd3] ff
                    rst       $38                           ;[3fd4] ff
                    rst       $38                           ;[3fd5] ff
                    rst       $38                           ;[3fd6] ff
                    rst       $38                           ;[3fd7] ff
                    rst       $38                           ;[3fd8] ff
                    rst       $38                           ;[3fd9] ff
                    rst       $38                           ;[3fda] ff
                    rst       $38                           ;[3fdb] ff
                    rst       $38                           ;[3fdc] ff
                    rst       $38                           ;[3fdd] ff
                    rst       $38                           ;[3fde] ff
                    rst       $38                           ;[3fdf] ff
                    rst       $38                           ;[3fe0] ff
                    rst       $38                           ;[3fe1] ff
                    rst       $38                           ;[3fe2] ff
                    rst       $38                           ;[3fe3] ff
                    rst       $38                           ;[3fe4] ff
                    rst       $38                           ;[3fe5] ff
                    rst       $38                           ;[3fe6] ff
                    rst       $38                           ;[3fe7] ff
                    rst       $38                           ;[3fe8] ff
                    rst       $38                           ;[3fe9] ff
                    rst       $38                           ;[3fea] ff
                    rst       $38                           ;[3feb] ff
                    rst       $38                           ;[3fec] ff
                    rst       $38                           ;[3fed] ff
                    rst       $38                           ;[3fee] ff
                    rst       $38                           ;[3fef] ff
                    rst       $38                           ;[3ff0] ff
                    rst       $38                           ;[3ff1] ff
                    rst       $38                           ;[3ff2] ff
                    rst       $38                           ;[3ff3] ff
                    rst       $38                           ;[3ff4] ff
                    rst       $38                           ;[3ff5] ff
                    rst       $38                           ;[3ff6] ff
                    rst       $38                           ;[3ff7] ff
                    rst       $38                           ;[3ff8] ff
                    rst       $38                           ;[3ff9] ff
                    rst       $38                           ;[3ffa] ff
                    rst       $38                           ;[3ffb] ff
                    rst       $38                           ;[3ffc] ff
                    rst       $38                           ;[3ffd] ff
                    rst       $38                           ;[3ffe] ff
                    ld        e,h                           ;[3fff] 5c
