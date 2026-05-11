                    push      af                            ;[0000] f5
                    ld        a,$02                         ;[0001] 3e 02
                    call      $0069                         ;[0003] cd 69 00
                    pop       af                            ;[0006] f1
                    ret                                     ;[0007] c9

                    jp        $0008                         ;[0008] c3 08 00
                    ld        l,$62                         ;[000b] 2e 62
                    ld        l,c                           ;[000d] 69
                    ld        l,(hl)                        ;[000e] 6e
                    rst       $38                           ;[000f] ff
                    ld        ($5b54),bc                    ;[0010] ed 43 54 5b
                    ex        (sp),hl                       ;[0014] e3
                    jp        $1561                         ;[0015] c3 61 15
                    ld        ($5b54),bc                    ;[0018] ed 43 54 5b
                    ex        (sp),hl                       ;[001c] e3
                    jp        $157f                         ;[001d] c3 7f 15
                    ld        ($5b54),bc                    ;[0020] ed 43 54 5b
                    ex        (sp),hl                       ;[0024] e3
                    jp        $1598                         ;[0025] c3 98 15
                    ld        ($5b54),bc                    ;[0028] ed 43 54 5b
                    ex        (sp),hl                       ;[002c] e3
                    jp        $15b1                         ;[002d] c3 b1 15
                    ld        ix,($3f30)                    ;[0030] dd 2a 30 3f
                    push      bc                            ;[0034] c5
                    push      hl                            ;[0035] e5
                    jr        $0041                         ;[0036] 18 09
                    push      af                            ;[0038] f5
                    ld        a,$01                         ;[0039] 3e 01
                    call      $0069                         ;[003b] cd 69 00
                    pop       af                            ;[003e] f1
                    ei                                      ;[003f] fb
                    ret                                     ;[0040] c9

                    rst       $10                           ;[0041] d7
                    adc       e                             ;[0042] 8b
                    daa                                     ;[0043] 27
                    pop       hl                            ;[0044] e1
                    pop       bc                            ;[0045] c1
                    ret                                     ;[0046] c9

                    ld        a,h                           ;[0047] 7c
                    rlca                                    ;[0048] 07
                    rlca                                    ;[0049] 07
                    rlca                                    ;[004a] 07
                    and       $07                           ;[004b] e6 07
                    ld        c,a                           ;[004d] 4f
                    ld        b,$00                         ;[004e] 06 00
                    ex        de,hl                         ;[0050] eb
                    ld        hl,$3f6a                      ;[0051] 21 6a 3f
                    add       hl,bc                         ;[0054] 09
                    ex        de,hl                         ;[0055] eb
                    ld        a,h                           ;[0056] 7c
                    and       $1f                           ;[0057] e6 1f
                    ld        h,a                           ;[0059] 67
                    ld        a,(de)                        ;[005a] 1a
                    ret                                     ;[005b] c9

                    inc       hl                            ;[005c] 23
                    ld        a,c                           ;[005d] 79
                    inc       a                             ;[005e] 3c
                    jr        z,$0063                       ;[005f] 28 02
                    res       5,h                           ;[0061] cb ac
                    ld        a,h                           ;[0063] 7c
                    or        l                             ;[0064] b5
                    ret                                     ;[0065] c9

                    jp        $006d                         ;[0066] c3 6d 00
                    ld        ($3f2f),a                     ;[0069] 32 2f 3f
                    ret                                     ;[006c] c9

                    ld        ($3fb1),sp                    ;[006d] ed 73 b1 3f
                    ld        sp,$3fc4                      ;[0071] 31 c4 3f
                    push      ix                            ;[0074] dd e5
                    push      iy                            ;[0076] fd e5
                    ex        af,af'                        ;[0078] 08
                    push      af                            ;[0079] f5
                    exx                                     ;[007a] d9
                    push      hl                            ;[007b] e5
                    push      de                            ;[007c] d5
                    push      bc                            ;[007d] c5
                    exx                                     ;[007e] d9
                    push      de                            ;[007f] d5
                    ld        a,r                           ;[0080] ed 5f
                    push      af                            ;[0082] f5
                    pop       de                            ;[0083] d1
                    ld        d,e                           ;[0084] 53
                    push      de                            ;[0085] d5
                    inc       sp                            ;[0086] 33
                    ld        d,a                           ;[0087] 57
                    ld        a,i                           ;[0088] ed 57
                    ld        e,a                           ;[008a] 5f
                    push      de                            ;[008b] d5
                    dec       sp                            ;[008c] 3b
                    dec       sp                            ;[008d] 3b
                    push      hl                            ;[008e] e5
                    push      hl                            ;[008f] e5
                    push      bc                            ;[0090] c5
                    ex        af,af'                        ;[0091] 08
                    push      af                            ;[0092] f5
                    ld        sp,$3916                      ;[0093] 31 16 39
                    ld        hl,($3fb1)                    ;[0096] 2a b1 3f
                    inc       hl                            ;[0099] 23
                    inc       hl                            ;[009a] 23
                    ld        ($3fb1),hl                    ;[009b] 22 b1 3f
                    ld        a,($3fb5)                     ;[009e] 3a b5 3f
                    rra                                     ;[00a1] 1f
                    rra                                     ;[00a2] 1f
                    and       $01                           ;[00a3] e6 01
                    ld        ($3fc4),a                     ;[00a5] 32 c4 3f
                    ld        a,($3fb4)                     ;[00a8] 3a b4 3f
                    ld        e,a                           ;[00ab] 5f
                    and       $80                           ;[00ac] e6 80
                    ld        d,a                           ;[00ae] 57
                    ld        a,e                           ;[00af] 7b
                    sub       $13                           ;[00b0] d6 13
                    and       $7f                           ;[00b2] e6 7f
                    or        d                             ;[00b4] b2
                    ld        ($3fb4),a                     ;[00b5] 32 b4 3f
                    ld        bc,$243b                      ;[00b8] 01 3b 24
                    in        a,(c)                         ;[00bb] ed 78
                    ld        ($3f4b),a                     ;[00bd] 32 4b 3f
                    call      $17a3                         ;[00c0] cd a3 17
                    xor       a                             ;[00c3] af
                    nextreg $62,a                           ;[00c4] ed 92 62
                    nextreg $80,a                           ;[00c7] ed 92 80
                    ld        a,$03                         ;[00ca] 3e 03
                    call      $179a                         ;[00cc] cd 9a 17
                    ld        (hl),a                        ;[00cf] 77
                    inc       hl                            ;[00d0] 23
                    ld        a,$28                         ;[00d1] 3e 28
                    call      $179a                         ;[00d3] cd 9a 17
                    ld        (hl),a                        ;[00d6] 77
                    inc       hl                            ;[00d7] 23
                    ld        bc,$123b                      ;[00d8] 01 3b 12
                    in        a,(c)                         ;[00db] ed 78
                    ld        (hl),a                        ;[00dd] 77
                    inc       hl                            ;[00de] 23
                    in        a,($e3)                       ;[00df] db e3
                    ld        (hl),a                        ;[00e1] 77
                    ld        bc,$7f3f                      ;[00e2] 01 3f 7f
                    in        a,(c)                         ;[00e5] ed 78
                    ld        ($3fcc),a                     ;[00e7] 32 cc 3f
                    ld        bc,$1f3f                      ;[00ea] 01 3f 1f
                    in        a,(c)                         ;[00ed] ed 78
                    ld        ($3fff),a                     ;[00ef] 32 ff 3f
                    ld        bc,$df3f                      ;[00f2] 01 3f df
                    in        a,(c)                         ;[00f5] ed 78
                    ld        ($3fa6),a                     ;[00f7] 32 a6 3f
                    ld        bc,$dffd                      ;[00fa] 01 fd df
                    out       (c),0                         ;[00fd] ed 71
                    ld        bc,$ef3f                      ;[00ff] 01 3f ef
                    in        a,(c)                         ;[0102] ed 78
                    ld        ($3fa7),a                     ;[0104] 32 a7 3f
                    ld        bc,$eff7                      ;[0107] 01 f7 ef
                    out       (c),0                         ;[010a] ed 71
                    ld        bc,$fe3f                      ;[010c] 01 3f fe
                    in        a,(c)                         ;[010f] ed 78
                    and       $07                           ;[0111] e6 07
                    add       a                             ;[0113] 87
                    ld        ($3fb5),a                     ;[0114] 32 b5 3f
                    ld        a,$02                         ;[0117] 3e 02
                    call      $179a                         ;[0119] cd 9a 17
                    ld        d,a                           ;[011c] 57
                    and       $80                           ;[011d] e6 80
                    out       (c),a                         ;[011f] ed 79
                    ld        a,d                           ;[0121] 7a
                    rrca                                    ;[0122] 0f
                    rrca                                    ;[0123] 0f
                    rrca                                    ;[0124] 0f
                    and       $0f                           ;[0125] e6 0f
                    ld        ($3f2d),a                     ;[0127] 32 2d 3f
                    ld        hl,$3f3a                      ;[012a] 21 3a 3f
                    ld        a,$1c                         ;[012d] 3e 1c
                    call      $179a                         ;[012f] cd 9a 17
                    ld        (hl),a                        ;[0132] 77
                    inc       hl                            ;[0133] 23
                    nextreg $1c,$0f                         ;[0134] ed 91 1c 0f
                    ld        c,$04                         ;[0138] 0e 04
                    ld        d,$18                         ;[013a] 16 18
                    ld        b,$04                         ;[013c] 06 04
                    push      bc                            ;[013e] c5
                    ld        a,d                           ;[013f] 7a
                    call      $179a                         ;[0140] cd 9a 17
                    ld        (hl),a                        ;[0143] 77
                    inc       hl                            ;[0144] 23
                    out       (c),a                         ;[0145] ed 79
                    pop       bc                            ;[0147] c1
                    djnz      $013e                         ;[0148] 10 f4
                    inc       d                             ;[014a] 14
                    dec       c                             ;[014b] 0d
                    jr        nz,$013c                      ;[014c] 20 ee
                    ld        bc,$bff5                      ;[014e] 01 f5 bf
                    in        a,(c)                         ;[0151] ed 78
                    and       $1f                           ;[0153] e6 1f
                    ld        ($3fcf),a                     ;[0155] 32 cf 3f
                    ld        hl,$3fd0                      ;[0158] 21 d0 3f
                    ld        d,$00                         ;[015b] 16 00
                    ld        bc,$fffd                      ;[015d] 01 fd ff
                    out       (c),d                         ;[0160] ed 51
                    in        e,(c)                         ;[0162] ed 58
                    ld        (hl),e                        ;[0164] 73
                    inc       hl                            ;[0165] 23
                    inc       d                             ;[0166] 14
                    bit       4,d                           ;[0167] cb 62
                    jr        z,$015d                       ;[0169] 28 f2
                    ld        de,$07ff                      ;[016b] 11 ff 07
                    out       (c),d                         ;[016e] ed 51
                    ld        b,$bf                         ;[0170] 06 bf
                    out       (c),e                         ;[0172] ed 59
                    inc       d                             ;[0174] 14
                    ld        b,e                           ;[0175] 43
                    out       (c),d                         ;[0176] ed 51
                    ld        b,$bf                         ;[0178] 06 bf
                    out       (c),0                         ;[017a] ed 71
                    bit       1,d                           ;[017c] cb 4a
                    jr        z,$0174                       ;[017e] 28 f4
                    ld        hl,$3e57                      ;[0180] 21 57 3e
                    ld        d,$20                         ;[0183] 16 20
                    xor       a                             ;[0185] af
                    call      $1820                         ;[0186] cd 20 18
                    ld        bc,$123b                      ;[0189] 01 3b 12
                    xor       a                             ;[018c] af
                    out       (c),a                         ;[018d] ed 79
                    out       ($e3),a                       ;[018f] d3 e3
                    nextreg $8f,$00                         ;[0191] ed 91 8f 00
                    ld        hl,$17fb                      ;[0195] 21 fb 17
                    ld        de,$17d5                      ;[0198] 11 d5 17
                    call      $17c0                         ;[019b] cd c0 17
                    ld        d,$06                         ;[019e] 16 06
                    out       (c),d                         ;[01a0] ed 51
                    inc       b                             ;[01a2] 04
                    in        a,(c)                         ;[01a3] ed 78
                    and       $e7                           ;[01a5] e6 e7
                    out       (c),a                         ;[01a7] ed 79
                    dec       b                             ;[01a9] 05
                    ld        d,$05                         ;[01aa] 16 05
                    out       (c),d                         ;[01ac] ed 51
                    inc       b                             ;[01ae] 04
                    in        a,(c)                         ;[01af] ed 78
                    and       $05                           ;[01b1] e6 05
                    or        $5a                           ;[01b3] f6 5a
                    out       (c),a                         ;[01b5] ed 79
                    dec       b                             ;[01b7] 05
                    ld        d,$08                         ;[01b8] 16 08
                    out       (c),d                         ;[01ba] ed 51
                    inc       b                             ;[01bc] 04
                    in        a,(c)                         ;[01bd] ed 78
                    and       $fd                           ;[01bf] e6 fd
                    or        $c4                           ;[01c1] f6 c4
                    out       (c),a                         ;[01c3] ed 79
                    in        a,($ff)                       ;[01c5] db ff
                    ld        ($3fa5),a                     ;[01c7] 32 a5 3f
                    xor       a                             ;[01ca] af
                    out       ($ff),a                       ;[01cb] d3 ff
                    ld        a,($3f2d)                     ;[01cd] 3a 2d 3f
                    and       a                             ;[01d0] a7
                    jp        m,$0276                       ;[01d1] fa 76 02
                    jr        z,$024d                       ;[01d4] 28 77
                    dec       a                             ;[01d6] 3d
                    jr        nz,$024d                      ;[01d7] 20 74
                    ld        hl,($3f9f)                    ;[01d9] 2a 9f 3f
                    ld        de,$0ab8                      ;[01dc] 11 b8 0a
                    sbc       hl,de                         ;[01df] ed 52
                    jr        z,$01e8                       ;[01e1] 28 05
                    dec       hl                            ;[01e3] 2b
                    ld        a,h                           ;[01e4] 7c
                    or        l                             ;[01e5] b5
                    jr        nz,$024d                      ;[01e6] 20 65
                    ld        a,($3f89)                     ;[01e8] 3a 89 3f
                    and       $c0                           ;[01eb] e6 c0
                    cp        $80                           ;[01ed] fe 80
                    jr        z,$024d                       ;[01ef] 28 5c
                    call      $15f9                         ;[01f1] cd f9 15
                    cp        $04                           ;[01f4] fe 04
                    jr        nz,$024d                      ;[01f6] 20 55
                    ld        hl,$3f6b                      ;[01f8] 21 6b 3f
                    ld        a,(hl)                        ;[01fb] 7e
                    dec       hl                            ;[01fc] 2b
                    and       (hl)                          ;[01fd] a6
                    inc       a                             ;[01fe] 3c
                    jr        nz,$024d                      ;[01ff] 20 4c
                    ex        de,hl                         ;[0201] eb
                    ld        hl,$1806                      ;[0202] 21 06 18
                    ld        bc,$0006                      ;[0205] 01 06 00
                    ldir                                    ;[0208] ed b0
                    ld        hl,$5d46                      ;[020a] 21 46 5d
                    ld        a,(hl)                        ;[020d] 7e
                    and       $07                           ;[020e] e6 07
                    add       a                             ;[0210] 87
                    ld        (de),a                        ;[0211] 12
                    inc       de                            ;[0212] 13
                    inc       a                             ;[0213] 3c
                    ld        (de),a                        ;[0214] 12
                    bit       5,(hl)                        ;[0215] cb 6e
                    ld        hl,$3f51                      ;[0217] 21 51 3f
                    set       7,(hl)                        ;[021a] cb fe
                    jr        z,$0220                       ;[021c] 28 02
                    res       7,(hl)                        ;[021e] cb be
                    ld        hl,($3fb6)                    ;[0220] 2a b6 3f
                    ld        a,l                           ;[0223] 7d
                    ld        ($3f80),a                     ;[0224] 32 80 3f
                    ld        a,h                           ;[0227] 7c
                    ld        ($3f89),a                     ;[0228] 32 89 3f
                    ld        hl,$5d23                      ;[022b] 21 23 5d
                    ld        de,$3fa9                      ;[022e] 11 a9 3f
                    ld        bc,$0057                      ;[0231] 01 57 00
                    ldir                                    ;[0234] ed b0
                    ld        hl,($3fc9)                    ;[0236] 2a c9 3f
                    ld        ($3f9f),hl                    ;[0239] 22 9f 3f
                    ld        a,($3fb5)                     ;[023c] 3a b5 3f
                    rrca                                    ;[023f] 0f
                    call      c,$06ac                       ;[0240] dc ac 06
                    call      $0371                         ;[0243] cd 71 03
                    ld        sp,$5e00                      ;[0246] 31 00 5e
                    scf                                     ;[0249] 37
                    jp        $040f                         ;[024a] c3 0f 04
                    ld        hl,$5b00                      ;[024d] 21 00 5b
                    ld        de,$3b54                      ;[0250] 11 54 3b
                    ld        bc,$0300                      ;[0253] 01 00 03
                    push      hl                            ;[0256] e5
                    ldir                                    ;[0257] ed b0
                    pop       hl                            ;[0259] e1
                    ld        (hl),l                        ;[025a] 75
                    ld        de,$5b01                      ;[025b] 11 01 5b
                    ld        b,e                           ;[025e] 43
                    ld        a,d                           ;[025f] 7a
                    ld        i,a                           ;[0260] ed 47
                    ldir                                    ;[0262] ed b0
                    xor       a                             ;[0264] af
                    ld        ($3f2f),a                     ;[0265] 32 2f 3f
                    ei                                      ;[0268] fb
                    halt                                    ;[0269] 76
                    ld        a,($3f2f)                     ;[026a] 3a 2f 3f
                    ld        ($3fc6),a                     ;[026d] 32 c6 3f
                    im        1                             ;[0270] ed 56
                    ld        iy,$5c3a                      ;[0272] fd 21 3a 5c
                    xor       a                             ;[0276] af
                    ld        ($3e97),a                     ;[0277] 32 97 3e
                    nextreg $07,$03                         ;[027a] ed 91 07 03
                    call      $0371                         ;[027e] cd 71 03
                    ld        sp,$5e00                      ;[0281] 31 00 5e
                    call      $5b8a                         ;[0284] cd 8a 5b
                    jp        nc,$0455                      ;[0287] d2 55 04
                    ld        hl,$1ab9                      ;[028a] 21 b9 1a
                    ld        de,$d5b9                      ;[028d] 11 b9 d5
                    ld        bc,$0005                      ;[0290] 01 05 00
                    ldir                                    ;[0293] ed b0
                    ld        hl,$3b54                      ;[0295] 21 54 3b
                    ld        de,$3b00                      ;[0298] 11 00 3b
                    ld        b,$03                         ;[029b] 06 03
                    push      bc                            ;[029d] c5
                    push      de                            ;[029e] d5
                    ld        de,$5e00                      ;[029f] 11 00 5e
                    ld        bc,$0100                      ;[02a2] 01 00 01
                    ldir                                    ;[02a5] ed b0
                    pop       de                            ;[02a7] d1
                    push      hl                            ;[02a8] e5
                    ld        hl,$5e00                      ;[02a9] 21 00 5e
                    ld        bc,$0100                      ;[02ac] 01 00 01
                    ld        a,$86                         ;[02af] 3e 86
                    call      $5ca0                         ;[02b1] cd a0 5c
                    pop       hl                            ;[02b4] e1
                    pop       bc                            ;[02b5] c1
                    djnz      $029d                         ;[02b6] 10 e5
                    ld        hl,$6000                      ;[02b8] 21 00 60
                    ld        de,$2000                      ;[02bb] 11 00 20
                    ld        bc,$1800                      ;[02be] 01 00 18
                    ldir                                    ;[02c1] ed b0
                    call      $16ce                         ;[02c3] cd ce 16
                    ld        a,($3f50)                     ;[02c6] 3a 50 3f
                    and       $03                           ;[02c9] e6 03
                    ld        ($d5bb),a                     ;[02cb] 32 bb d5
                    rst       $10                           ;[02ce] d7
                    sub       a                             ;[02cf] 97
                    ld        a,(bc)                        ;[02d0] 0a
                    rst       $10                           ;[02d1] d7
                    ld        h,b                           ;[02d2] 60
                    inc       bc                            ;[02d3] 03
                    call      $170b                         ;[02d4] cd 0b 17
                    ld        bc,$1000                      ;[02d7] 01 00 10
                    ld        hl,$387f                      ;[02da] 21 7f 38
                    push      bc                            ;[02dd] c5
                    push      hl                            ;[02de] e5
                    rst       $20                           ;[02df] e7
                    jp        nz,$e105                      ;[02e0] c2 05 e1
                    pop       bc                            ;[02e3] c1
                    bit       3,a                           ;[02e4] cb 5f
                    jr        z,$02f7                       ;[02e6] 28 0f
                    push      hl                            ;[02e8] e5
                    ld        a,c                           ;[02e9] 79
                    rst       $20                           ;[02ea] e7
                    call      nz,$e103                      ;[02eb] c4 03 e1
                    rra                                     ;[02ee] 1f
                    cp        $4d                           ;[02ef] fe 4d
                    jr        nz,$02f7                      ;[02f1] 20 04
                    set       7,c                           ;[02f3] cb f9
                    jr        $02fe                         ;[02f5] 18 07
                    add       hl,$0016                      ;[02f7] ed 34 16 00
                    inc       c                             ;[02fb] 0c
                    djnz      $02dd                         ;[02fc] 10 df
                    ld        a,c                           ;[02fe] 79
                    ld        ($3a53),a                     ;[02ff] 32 53 3a
                    push      af                            ;[0302] f5
                    ld        b,$80                         ;[0303] 06 80
                    call      $0598                         ;[0305] cd 98 05
                    pop       af                            ;[0308] f1
                    add       $c1                           ;[0309] c6 c1
                    ld        hl,$3a52                      ;[030b] 21 52 3a
                    cp        (hl)                          ;[030e] be
                    jr        nz,$0316                      ;[030f] 20 05
                    ld        a,$43                         ;[0311] 3e 43
                    rst       $20                           ;[0313] e7
                    dec       l                             ;[0314] 2d
                    ld        bc,$2d21                      ;[0315] 01 21 2d
                    ccf                                     ;[0318] 3f
                    ld        a,(hl)                        ;[0319] 7e
                    ld        (hl),$00                      ;[031a] 36 00
                    bit       0,a                           ;[031c] cb 47
                    push      af                            ;[031e] f5
                    call      nz,$0bc6                      ;[031f] c4 c6 0b
                    pop       af                            ;[0322] f1
                    cp        $10                           ;[0323] fe 10
                    jr        nc,$032c                      ;[0325] 30 05
                    cp        $02                           ;[0327] fe 02
                    call      nc,$0349                      ;[0329] d4 49 03
                    rst       $28                           ;[032c] ef
                    ld        l,e                           ;[032d] 6b
                    dec       c                             ;[032e] 0d
                    call      $10ce                         ;[032f] cd ce 10
                    ld        a,$18                         ;[0332] 3e 18
                    rst       $30                           ;[0334] f7
                    ld        a,($5c8d)                     ;[0335] 3a 8d 5c
                    rst       $30                           ;[0338] f7
                    call      $13ef                         ;[0339] cd ef 13
                    push      af                            ;[033c] f5
                    rst       $28                           ;[033d] ef
                    ld        l,e                           ;[033e] 6b
                    dec       c                             ;[033f] 0d
                    pop       af                            ;[0340] f1
                    jp        c,$03f1                       ;[0341] da f1 03
                    call      $037d                         ;[0344] cd 7d 03
                    jr        $032c                         ;[0347] 18 e3
                    rst       $28                           ;[0349] ef
                    ld        l,e                           ;[034a] 6b
                    dec       c                             ;[034b] 0d
                    ld        hl,$1f57                      ;[034c] 21 57 1f
                    call      $11e9                         ;[034f] cd e9 11
                    rst       $10                           ;[0352] d7
                    jr        $0393                         ;[0353] 18 3e
                    ld        a,$dd                         ;[0355] 3e dd
                    in        a,($fe)                       ;[0357] db fe
                    and       $04                           ;[0359] e6 04
                    jr        z,$0355                       ;[035b] 28 f8
                    rst       $10                           ;[035d] d7
                    jp        po,$e60c                      ;[035e] e2 0c e6
                    rst       $18                           ;[0361] df
                    cp        $44                           ;[0362] fe 44
                    jp        z,$0bc6                       ;[0364] ca c6 0b
                    sub       $49                           ;[0367] d6 49
                    jr        nz,$035d                      ;[0369] 20 f2
                    ld        ($3f9d),a                     ;[036b] 32 9d 3f
                    jp        $03f1                         ;[036e] c3 f1 03
                    ld        hl,$185f                      ;[0371] 21 5f 18
                    ld        bc,$025a                      ;[0374] 01 5a 02
                    ld        de,$5b00                      ;[0377] 11 00 5b
                    ldir                                    ;[037a] ed b0
                    ret                                     ;[037c] c9

                    and       a                             ;[037d] a7
                    jp        z,$06bf                       ;[037e] ca bf 06
                    dec       a                             ;[0381] 3d
                    ld        d,$02                         ;[0382] 16 02
                    jr        z,$03a0                       ;[0384] 28 1a
                    dec       a                             ;[0386] 3d
                    ld        d,$03                         ;[0387] 16 03
                    jr        z,$03a0                       ;[0389] 28 15
                    dec       a                             ;[038b] 3d
                    ld        d,$04                         ;[038c] 16 04
                    jr        z,$03a0                       ;[038e] 28 10
                    dec       a                             ;[0390] 3d
                    jp        z,$0758                       ;[0391] ca 58 07
                    dec       a                             ;[0394] 3d
                    ld        d,$05                         ;[0395] 16 05
                    jr        z,$03a0                       ;[0397] 28 07
                    dec       a                             ;[0399] 3d
                    ld        d,$06                         ;[039a] 16 06
                    jr        z,$03a0                       ;[039c] 28 02
                    ld        d,$01                         ;[039e] 16 01
                    call      $12b6                         ;[03a0] cd b6 12
                    jp        nc,$f722                      ;[03a3] d2 22 f7
                    pop       af                            ;[03a6] f1
                    jr        $032f                         ;[03a7] 18 86
                    ld        a,$ff                         ;[03a9] 3e ff
                    ld        ($3f2d),a                     ;[03ab] 32 2d 3f
                    ld        hl,$3f4f                      ;[03ae] 21 4f 3f
                    ld        a,(hl)                        ;[03b1] 7e
                    and       $e7                           ;[03b2] e6 e7
                    ld        (hl),a                        ;[03b4] 77
                    jr        $03f1                         ;[03b5] 18 3a
                    ld        bc,$7ffe                      ;[03b7] 01 fe 7f
                    in        a,(c)                         ;[03ba] ed 78
                    and       $01                           ;[03bc] e6 01
                    jr        nz,$03b7                      ;[03be] 20 f7
                    ld        bc,$7ffe                      ;[03c0] 01 fe 7f
                    in        a,(c)                         ;[03c3] ed 78
                    and       $01                           ;[03c5] e6 01
                    jr        z,$03c0                       ;[03c7] 28 f7
                    ld        hl,$3f4f                      ;[03c9] 21 4f 3f
                    ld        a,(hl)                        ;[03cc] 7e
                    or        $08                           ;[03cd] f6 08
                    ld        (hl),a                        ;[03cf] 77
                    jp        $0189                         ;[03d0] c3 89 01
                    ld        a,$fe                         ;[03d3] 3e fe
                    jr        $03ab                         ;[03d5] 18 d4
                    ld        hl,$185f                      ;[03d7] 21 5f 18
                    ld        de,$5b00                      ;[03da] 11 00 5b
                    ld        bc,$025a                      ;[03dd] 01 5a 02
                    ldir                                    ;[03e0] ed b0
                    ld        sp,$5e00                      ;[03e2] 31 00 5e
                    ld        bc,$f650                      ;[03e5] 01 50 f6
                    call      $1572                         ;[03e8] cd 72 15
                    sub       d                             ;[03eb] 92
                    jr        $03c9                         ;[03ec] 18 db
                    call      $174d                         ;[03ee] cd 4d 17
                    call      $16a5                         ;[03f1] cd a5 16
                    ld        hl,$2000                      ;[03f4] 21 00 20
                    ld        de,$6000                      ;[03f7] 11 00 60
                    ld        bc,$1800                      ;[03fa] 01 00 18
                    ldir                                    ;[03fd] ed b0
                    ld        a,($d5bb)                     ;[03ff] 3a bb d5
                    ld        ($3f50),a                     ;[0402] 32 50 3f
                    ld        a,c                           ;[0405] 79
                    ld        a,($3a53)                     ;[0406] 3a 53 3a
                    ld        b,$4d                         ;[0409] 06 4d
                    call      $0598                         ;[040b] cd 98 05
                    and       a                             ;[040e] a7
                    push      af                            ;[040f] f5
                    ld        hl,$3b00                      ;[0410] 21 00 3b
                    ld        de,$3b54                      ;[0413] 11 54 3b
                    ld        b,$03                         ;[0416] 06 03
                    push      bc                            ;[0418] c5
                    push      de                            ;[0419] d5
                    ld        de,$5e00                      ;[041a] 11 00 5e
                    ld        bc,$0100                      ;[041d] 01 00 01
                    ld        a,$86                         ;[0420] 3e 86
                    call      $5ca0                         ;[0422] cd a0 5c
                    pop       de                            ;[0425] d1
                    push      hl                            ;[0426] e5
                    ld        hl,$5e00                      ;[0427] 21 00 5e
                    ld        bc,$0100                      ;[042a] 01 00 01
                    ldir                                    ;[042d] ed b0
                    pop       hl                            ;[042f] e1
                    pop       bc                            ;[0430] c1
                    djnz      $0418                         ;[0431] 10 e5
                    di                                      ;[0433] f3
                    pop       af                            ;[0434] f1
                    ld        hl,$13ae                      ;[0435] 21 ae 13
                    jr        c,$044c                       ;[0438] 38 12
                    call      $5b43                         ;[043a] cd 43 5b
                    ld        hl,$1363                      ;[043d] 21 63 13
                    ld        bc,$0031                      ;[0440] 01 31 00
                    call      $0377                         ;[0443] cd 77 03
                    call      $5b00                         ;[0446] cd 00 5b
                    ld        hl,$1394                      ;[0449] 21 94 13
                    ld        bc,$005b                      ;[044c] 01 5b 00
                    call      $0377                         ;[044f] cd 77 03
                    call      $5b00                         ;[0452] cd 00 5b
                    ld        sp,$3916                      ;[0455] 31 16 39
                    ld        hl,$3b54                      ;[0458] 21 54 3b
                    ld        de,$5b00                      ;[045b] 11 00 5b
                    ld        bc,$0300                      ;[045e] 01 00 03
                    ldir                                    ;[0461] ed b0
                    ei                                      ;[0463] fb
                    halt                                    ;[0464] 76
                    di                                      ;[0465] f3
                    ld        hl,$3e57                      ;[0466] 21 57 3e
                    ld        d,$20                         ;[0469] 16 20
                    xor       a                             ;[046b] af
                    scf                                     ;[046c] 37
                    call      $1845                         ;[046d] cd 45 18
                    ld        hl,$3f3a                      ;[0470] 21 3a 3f
                    ld        a,(hl)                        ;[0473] 7e
                    inc       hl                            ;[0474] 23
                    nextreg $1c,$0f                         ;[0475] ed 91 1c 0f
                    ld        b,$04                         ;[0479] 06 04
                    ld        d,$18                         ;[047b] 16 18
                    push      bc                            ;[047d] c5
                    push      hl                            ;[047e] e5
                    push      af                            ;[047f] f5
                    ld        e,$04                         ;[0480] 1e 04
                    call      $05a5                         ;[0482] cd a5 05
                    pop       af                            ;[0485] f1
                    ex        (sp),hl                       ;[0486] e3
                    push      af                            ;[0487] f5
                    and       $03                           ;[0488] e6 03
                    ld        e,a                           ;[048a] 5f
                    call      nz,$05a5                      ;[048b] c4 a5 05
                    pop       af                            ;[048e] f1
                    rrca                                    ;[048f] 0f
                    rrca                                    ;[0490] 0f
                    pop       hl                            ;[0491] e1
                    pop       bc                            ;[0492] c1
                    inc       d                             ;[0493] 14
                    djnz      $047d                         ;[0494] 10 e7
                    ld        a,($3fb5)                     ;[0496] 3a b5 3f
                    srl       a                             ;[0499] cb 3f
                    out       ($fe),a                       ;[049b] d3 fe
                    ld        a,($3f8a)                     ;[049d] 3a 8a 3f
                    nextreg $8f,a                           ;[04a0] ed 92 8f
                    ld        bc,$dffd                      ;[04a3] 01 fd df
                    ld        a,($3fa6)                     ;[04a6] 3a a6 3f
                    out       (c),a                         ;[04a9] ed 79
                    ld        bc,$eff7                      ;[04ab] 01 f7 ef
                    ld        a,($3fa7)                     ;[04ae] 3a a7 3f
                    out       (c),a                         ;[04b1] ed 79
                    ld        bc,$1ffd                      ;[04b3] 01 fd 1f
                    ld        a,($3fff)                     ;[04b6] 3a ff 3f
                    out       (c),a                         ;[04b9] ed 79
                    ld        bc,$7ffd                      ;[04bb] 01 fd 7f
                    ld        a,($3fcc)                     ;[04be] 3a cc 3f
                    out       (c),a                         ;[04c1] ed 79
                    ld        a,($3fa5)                     ;[04c3] 3a a5 3f
                    out       ($ff),a                       ;[04c6] d3 ff
                    ld        a,($3f73)                     ;[04c8] 3a 73 3f
                    push      af                            ;[04cb] f5
                    ld        h,a                           ;[04cc] 67
                    and       $c0                           ;[04cd] e6 c0
                    ld        l,a                           ;[04cf] 6f
                    ld        a,h                           ;[04d0] 7c
                    jr        z,$04dd                       ;[04d1] 28 0a
                    ld        a,l                           ;[04d3] 7d
                    cp        $c0                           ;[04d4] fe c0
                    ld        a,h                           ;[04d6] 7c
                    jr        z,$04dd                       ;[04d7] 28 04
                    and       $3f                           ;[04d9] e6 3f
                    or        $80                           ;[04db] f6 80
                    ld        ($3f73),a                     ;[04dd] 32 73 3f
                    ld        hl,$3f50                      ;[04e0] 21 50 3f
                    ld        a,(hl)                        ;[04e3] 7e
                    push      af                            ;[04e4] f5
                    and       $03                           ;[04e5] e6 03
                    ld        (hl),a                        ;[04e7] 77
                    call      $17ba                         ;[04e8] cd ba 17
                    pop       af                            ;[04eb] f1
                    ld        ($3f50),a                     ;[04ec] 32 50 3f
                    pop       af                            ;[04ef] f1
                    ld        ($3f73),a                     ;[04f0] 32 73 3f
                    ld        a,(hl)                        ;[04f3] 7e
                    inc       hl                            ;[04f4] 23
                    add       a                             ;[04f5] 87
                    ld        a,(hl)                        ;[04f6] 7e
                    inc       hl                            ;[04f7] 23
                    jr        nc,$04fd                      ;[04f8] 30 03
                    nextreg $44,a                           ;[04fa] ed 92 44
                    ld        bc,$123b                      ;[04fd] 01 3b 12
                    ld        a,(hl)                        ;[0500] 7e
                    and       $fa                           ;[0501] e6 fa
                    out       (c),a                         ;[0503] ed 79
                    ld        a,($3f4b)                     ;[0505] 3a 4b 3f
                    ld        bc,$243b                      ;[0508] 01 3b 24
                    out       (c),a                         ;[050b] ed 79
                    ld        a,($3f2d)                     ;[050d] 3a 2d 3f
                    inc       a                             ;[0510] 3c
                    jp        z,$03b7                       ;[0511] ca b7 03
                    jp        m,$03d7                       ;[0514] fa d7 03
                    ld        hl,$3fd0                      ;[0517] 21 d0 3f
                    ld        d,$00                         ;[051a] 16 00
                    ld        bc,$fffd                      ;[051c] 01 fd ff
                    out       (c),d                         ;[051f] ed 51
                    ld        b,$bf                         ;[0521] 06 bf
                    ld        e,(hl)                        ;[0523] 5e
                    inc       hl                            ;[0524] 23
                    out       (c),e                         ;[0525] ed 59
                    inc       d                             ;[0527] 14
                    bit       4,d                           ;[0528] cb 62
                    jr        z,$051c                       ;[052a] 28 f0
                    ld        a,($3fcf)                     ;[052c] 3a cf 3f
                    ld        bc,$fffd                      ;[052f] 01 fd ff
                    out       (c),a                         ;[0532] ed 79
                    out       ($3f),a                       ;[0534] d3 3f
                    ld        bc,$123b                      ;[0536] 01 3b 12
                    ld        a,($3fa3)                     ;[0539] 3a a3 3f
                    out       (c),a                         ;[053c] ed 79
                    ld        a,($3fa4)                     ;[053e] 3a a4 3f
                    out       ($e3),a                       ;[0541] d3 e3
                    ld        a,($3fb4)                     ;[0543] 3a b4 3f
                    ld        l,a                           ;[0546] 6f
                    and       $80                           ;[0547] e6 80
                    ld        h,a                           ;[0549] 67
                    ld        a,l                           ;[054a] 7d
                    sub       $11                           ;[054b] d6 11
                    and       $7f                           ;[054d] e6 7f
                    or        h                             ;[054f] b4
                    ld        ($3fb4),a                     ;[0550] 32 b4 3f
                    ld        hl,($3fb1)                    ;[0553] 2a b1 3f
                    dec       hl                            ;[0556] 2b
                    dec       hl                            ;[0557] 2b
                    ld        ($3fb1),hl                    ;[0558] 22 b1 3f
                    ei                                      ;[055b] fb
                    halt                                    ;[055c] 76
                    di                                      ;[055d] f3
                    ld        a,($3fc6)                     ;[055e] 3a c6 3f
                    and       $03                           ;[0561] e6 03
                    im        0                             ;[0563] ed 46
                    jr        z,$056e                       ;[0565] 28 07
                    im        1                             ;[0567] ed 56
                    dec       a                             ;[0569] 3d
                    jr        z,$056e                       ;[056a] 28 02
                    im        2                             ;[056c] ed 5e
                    ld        a,($3fc4)                     ;[056e] 3a c4 3f
                    and       a                             ;[0571] a7
                    jr        z,$0575                       ;[0572] 28 01
                    ei                                      ;[0574] fb
                    ld        sp,$3fa9                      ;[0575] 31 a9 3f
                    pop       af                            ;[0578] f1
                    ex        af,af'                        ;[0579] 08
                    pop       bc                            ;[057a] c1
                    pop       hl                            ;[057b] e1
                    pop       de                            ;[057c] d1
                    inc       sp                            ;[057d] 33
                    pop       af                            ;[057e] f1
                    ld        i,a                           ;[057f] ed 47
                    dec       sp                            ;[0581] 3b
                    pop       af                            ;[0582] f1
                    ld        r,a                           ;[0583] ed 4f
                    inc       sp                            ;[0585] 33
                    pop       de                            ;[0586] d1
                    exx                                     ;[0587] d9
                    pop       bc                            ;[0588] c1
                    pop       de                            ;[0589] d1
                    pop       hl                            ;[058a] e1
                    exx                                     ;[058b] d9
                    pop       af                            ;[058c] f1
                    ex        af,af'                        ;[058d] 08
                    pop       iy                            ;[058e] fd e1
                    pop       ix                            ;[0590] dd e1
                    ld        sp,($3fb1)                    ;[0592] ed 7b b1 3f
                    retn                                    ;[0596] ed 45

                    sub       $80                           ;[0598] d6 80
                    ret       c                             ;[059a] d8
                    ld        hl,$2310                      ;[059b] 21 10 23
                    add       hl,a                          ;[059e] ed 31
                    ld        a,b                           ;[05a0] 78
                    rst       $20                           ;[05a1] e7
                    rst       $00                           ;[05a2] c7
                    dec       b                             ;[05a3] 05
                    ret                                     ;[05a4] c9

                    ld        bc,$243b                      ;[05a5] 01 3b 24
                    out       (c),d                         ;[05a8] ed 51
                    inc       b                             ;[05aa] 04
                    ld        a,(hl)                        ;[05ab] 7e
                    inc       hl                            ;[05ac] 23
                    out       (c),a                         ;[05ad] ed 79
                    dec       e                             ;[05af] 1d
                    jr        nz,$05a5                      ;[05b0] 20 f3
                    ret                                     ;[05b2] c9

                    ld        a,($3fcc)                     ;[05b3] 3a cc 3f
                    and       $20                           ;[05b6] e6 20
                    cp        $20                           ;[05b8] fe 20
                    ret       z                             ;[05ba] c8
                    ld        a,($3f89)                     ;[05bb] 3a 89 3f
                    and       $f0                           ;[05be] e6 f0
                    cp        $a0                           ;[05c0] fe a0
                    scf                                     ;[05c2] 37
                    ccf                                     ;[05c3] 3f
                    ret                                     ;[05c4] c9

                    ld        hl,$3f89                      ;[05c5] 21 89 3f
                    ld        e,(hl)                        ;[05c8] 5e
                    ld        (hl),$a0                      ;[05c9] 36 a0
                    ld        hl,$3f6a                      ;[05cb] 21 6a 3f
                    ld        d,(hl)                        ;[05ce] 56
                    ld        (hl),$ff                      ;[05cf] 36 ff
                    push      de                            ;[05d1] d5
                    ld        hl,$0066                      ;[05d2] 21 66 00
                    call      $15ca                         ;[05d5] cd ca 15
                    push      hl                            ;[05d8] e5
                    ld        hl,$09a2                      ;[05d9] 21 a2 09
                    call      $15ca                         ;[05dc] cd ca 15
                    ld        c,l                           ;[05df] 4d
                    pop       de                            ;[05e0] d1
                    pop       hl                            ;[05e1] e1
                    ld        a,l                           ;[05e2] 7d
                    ld        ($3f89),a                     ;[05e3] 32 89 3f
                    ld        a,h                           ;[05e6] 7c
                    ld        ($3f6a),a                     ;[05e7] 32 6a 3f
                    ld        hl,$e5f5                      ;[05ea] 21 f5 e5
                    and       a                             ;[05ed] a7
                    sbc       hl,de                         ;[05ee] ed 52
                    jr        z,$0607                       ;[05f0] 28 15
                    ld        b,$00                         ;[05f2] 06 00
                    ld        hl,$cbfd                      ;[05f4] 21 fd cb
                    and       a                             ;[05f7] a7
                    sbc       hl,de                         ;[05f8] ed 52
                    scf                                     ;[05fa] 37
                    ret       z                             ;[05fb] c8
                    inc       b                             ;[05fc] 04
                    ld        hl,$3c08                      ;[05fd] 21 08 3c
                    and       a                             ;[0600] a7
                    sbc       hl,de                         ;[0601] ed 52
                    scf                                     ;[0603] 37
                    ret       z                             ;[0604] c8
                    inc       b                             ;[0605] 04
                    ret                                     ;[0606] c9

                    call      $05b3                         ;[0607] cd b3 05
                    ld        b,$03                         ;[060a] 06 03
                    ret       z                             ;[060c] c8
                    inc       b                             ;[060d] 04
                    ld        a,c                           ;[060e] 79
                    cp        $53                           ;[060f] fe 53
                    ret       z                             ;[0611] c8
                    inc       b                             ;[0612] 04
                    and       a                             ;[0613] a7
                    ret                                     ;[0614] c9

                    inc       bc                            ;[0615] 03
                    nop                                     ;[0616] 00
                    nop                                     ;[0617] 00
                    nop                                     ;[0618] 00
                    nop                                     ;[0619] 00
                    nop                                     ;[061a] 00
                    rst       $38                           ;[061b] ff
                    rst       $38                           ;[061c] ff
                    inc       bc                            ;[061d] 03
                    ld        bc,$0203                      ;[061e] 01 03 02
                    inc       bc                            ;[0621] 03
                    inc       b                             ;[0622] 04
                    inc       bc                            ;[0623] 03
                    ex        af,af'                        ;[0624] 08
                    inc       bc                            ;[0625] 03
                    djnz      $0659                         ;[0626] 10 31
                    nop                                     ;[0628] 00
                    ld        ($3300),a                     ;[0629] 32 00 33
                    nop                                     ;[062c] 00
                    inc       (hl)                          ;[062d] 34
                    nop                                     ;[062e] 00
                    dec       (hl)                          ;[062f] 35
                    nop                                     ;[0630] 00
                    nop                                     ;[0631] 00
                    nop                                     ;[0632] 00
                    nop                                     ;[0633] 00
                    ld        hl,$3fb5                      ;[0634] 21 b5 3f
                    rr        (hl)                          ;[0637] cb 1e
                    ld        a,($3fb4)                     ;[0639] 3a b4 3f
                    add       a                             ;[063c] 87
                    rl        (hl)                          ;[063d] cb 16
                    ld        a,$08                         ;[063f] 3e 08
                    call      $179a                         ;[0641] cd 9a 17
                    and       $01                           ;[0644] e6 01
                    add       a                             ;[0646] 87
                    add       a                             ;[0647] 87
                    ld        b,a                           ;[0648] 47
                    ld        hl,$3fc6                      ;[0649] 21 c6 3f
                    ld        a,(hl)                        ;[064c] 7e
                    and       $03                           ;[064d] e6 03
                    cp        $03                           ;[064f] fe 03
                    jr        nz,$0655                      ;[0651] 20 02
                    ld        a,$02                         ;[0653] 3e 02
                    or        b                             ;[0655] b0
                    ld        (hl),a                        ;[0656] 77
                    ld        hl,$0037                      ;[0657] 21 37 00
                    ld        ($3fc7),hl                    ;[065a] 22 c7 3f
                    ld        l,$00                         ;[065d] 2e 00
                    ld        ($3faf),hl                    ;[065f] 22 af 3f
                    ld        hl,($3f9f)                    ;[0662] 2a 9f 3f
                    ld        ($3fc9),hl                    ;[0665] 22 c9 3f
                    xor       a                             ;[0668] af
                    ld        ($3fcd),a                     ;[0669] 32 cd 3f
                    ld        a,($3f82)                     ;[066c] 3a 82 3f
                    and       $01                           ;[066f] e6 01
                    add       a                             ;[0671] 87
                    add       a                             ;[0672] 87
                    or        $03                           ;[0673] f6 03
                    ld        ($3fce),a                     ;[0675] 32 ce 3f
                    ld        a,($3fc4)                     ;[0678] 3a c4 3f
                    ld        ($3fc5),a                     ;[067b] 32 c5 3f
                    ld        hl,$0615                      ;[067e] 21 15 06
                    ld        de,$3fe0                      ;[0681] 11 e0 3f
                    ld        bc,$001f                      ;[0684] 01 1f 00
                    ldir                                    ;[0687] ed b0
                    call      $05c5                         ;[0689] cd c5 05
                    ld        a,$51                         ;[068c] 3e 51
                    dec       b                             ;[068e] 05
                    jr        z,$06a0                       ;[068f] 28 0f
                    dec       a                             ;[0691] 3d
                    inc       b                             ;[0692] 04
                    jr        z,$06a0                       ;[0693] 28 0b
                    ld        a,b                           ;[0695] 78
                    cp        $04                           ;[0696] fe 04
                    jr        z,$06a0                       ;[0698] 28 06
                    sub       $03                           ;[069a] d6 03
                    jr        z,$06a0                       ;[069c] 28 02
                    ld        a,$0c                         ;[069e] 3e 0c
                    ld        ($3fcb),a                     ;[06a0] 32 cb 3f
                    jr        $06ac                         ;[06a3] 18 07
                    ld        hl,$3fc6                      ;[06a5] 21 c6 3f
                    ld        a,(hl)                        ;[06a8] 7e
                    and       $03                           ;[06a9] e6 03
                    ld        (hl),a                        ;[06ab] 77
                    ld        hl,$3fa9                      ;[06ac] 21 a9 3f
                    ld        d,(hl)                        ;[06af] 56
                    inc       hl                            ;[06b0] 23
                    ld        e,(hl)                        ;[06b1] 5e
                    ld        (hl),d                        ;[06b2] 72
                    dec       hl                            ;[06b3] 2b
                    ld        (hl),e                        ;[06b4] 73
                    ld        hl,$3fbe                      ;[06b5] 21 be 3f
                    ld        d,(hl)                        ;[06b8] 56
                    inc       hl                            ;[06b9] 23
                    ld        e,(hl)                        ;[06ba] 5e
                    ld        (hl),d                        ;[06bb] 72
                    dec       hl                            ;[06bc] 2b
                    ld        (hl),e                        ;[06bd] 73
                    ret                                     ;[06be] c9

                    call      $05c5                         ;[06bf] cd c5 05
                    jr        nc,$06d6                      ;[06c2] 30 12
                    ld        a,b                           ;[06c4] 78
                    cp        $02                           ;[06c5] fe 02
                    jr        c,$06d6                       ;[06c7] 38 0d
                    ld        hl,$1bbe                      ;[06c9] 21 be 1b
                    call      $11c3                         ;[06cc] cd c3 11
                    rst       $10                           ;[06cf] d7
                    jr        $0710                         ;[06d0] 18 3e
                    rst       $10                           ;[06d2] d7
                    jp        po,$c90c                      ;[06d3] e2 0c c9
                    call      $0634                         ;[06d6] cd 34 06
                    ld        hl,$1abe                      ;[06d9] 21 be 1a
                    ld        bc,$1ad8                      ;[06dc] 01 d8 1a
                    call      $110f                         ;[06df] cd 0f 11
                    jr        nc,$0748                      ;[06e2] 30 64
                    ld        d,$52                         ;[06e4] 16 52
                    ld        bc,$1c07                      ;[06e6] 01 07 1c
                    call      $1163                         ;[06e9] cd 63 11
                    push      hl                            ;[06ec] e5
                    push      af                            ;[06ed] f5
                    ld        hl,$1b92                      ;[06ee] 21 92 1b
                    call      $11c3                         ;[06f1] cd c3 11
                    pop       af                            ;[06f4] f1
                    pop       hl                            ;[06f5] e1
                    jp        nc,$074e                      ;[06f6] d2 4e 07
                    ld        de,$1acb                      ;[06f9] 11 cb 1a
                    call      $1179                         ;[06fc] cd 79 11
                    call      $1230                         ;[06ff] cd 30 12
                    jp        nc,$0750                      ;[0702] d2 50 07
                    ld        hl,$3fa9                      ;[0705] 21 a9 3f
                    ld        bc,$0057                      ;[0708] 01 57 00
                    call      $125c                         ;[070b] cd 5c 12
                    jr        nc,$0750                      ;[070e] 30 40
                    ld        a,($3fcb)                     ;[0710] 3a cb 3f
                    and       a                             ;[0713] a7
                    jr        nz,$0735                      ;[0714] 20 1f
                    ld        bc,$0402                      ;[0716] 01 02 04
                    call      $1271                         ;[0719] cd 71 12
                    jr        nc,$0750                      ;[071c] 30 32
                    ld        b,$05                         ;[071e] 06 05
                    ld        a,($3f70)                     ;[0720] 3a 70 3f
                    srl       a                             ;[0723] cb 3f
                    ld        c,a                           ;[0725] 4f
                    call      $1271                         ;[0726] cd 71 12
                    jr        nc,$0750                      ;[0729] 30 25
                    ld        bc,$0805                      ;[072b] 01 05 08
                    call      $1271                         ;[072e] cd 71 12
                    jr        nc,$0750                      ;[0731] 30 1d
                    jr        $0745                         ;[0733] 18 10
                    ld        bc,$0300                      ;[0735] 01 00 03
                    push      bc                            ;[0738] c5
                    call      $1271                         ;[0739] cd 71 12
                    pop       bc                            ;[073c] c1
                    jr        nc,$0750                      ;[073d] 30 11
                    inc       b                             ;[073f] 04
                    inc       c                             ;[0740] 0c
                    bit       3,c                           ;[0741] cb 59
                    jr        z,$0738                       ;[0743] 28 f3
                    call      $1254                         ;[0745] cd 54 12
                    push      af                            ;[0748] f5
                    call      $06a5                         ;[0749] cd a5 06
                    pop       af                            ;[074c] f1
                    ret                                     ;[074d] c9

                    ld        a,$14                         ;[074e] 3e 14
                    rst       $10                           ;[0750] d7
                    dec       bc                            ;[0751] 0b
                    dec       sp                            ;[0752] 3b
                    jr        $0745                         ;[0753] 18 f0
                    call      $10c1                         ;[0755] cd c1 10
                    rst       $28                           ;[0758] ef
                    ld        l,e                           ;[0759] 6b
                    dec       c                             ;[075a] 0d
                    ld        hl,$10d5                      ;[075b] 21 d5 10
                    ld        bc,$003a                      ;[075e] 01 3a 00
                    call      $13f5                         ;[0761] cd f5 13
                    ret       c                             ;[0764] d8
                    and       a                             ;[0765] a7
                    jp        z,$0bfb                       ;[0766] ca fb 0b
                    dec       a                             ;[0769] 3d
                    jr        z,$07ea                       ;[076a] 28 7e
                    dec       a                             ;[076c] 3d
                    jp        z,$0ed6                       ;[076d] ca d6 0e
                    dec       a                             ;[0770] 3d
                    jr        z,$0774                       ;[0771] 28 01
                    ret                                     ;[0773] c9

                    ld        hl,$1100                      ;[0774] 21 00 11
                    call      $1093                         ;[0777] cd 93 10
                    ld        de,$1e85                      ;[077a] 11 85 1e
                    call      $0b0f                         ;[077d] cd 0f 0b
                    cp        $e0                           ;[0780] fe e0
                    jr        nc,$07e5                      ;[0782] 30 61
                    push      de                            ;[0784] d5
                    ld        de,$1e8f                      ;[0785] 11 8f 1e
                    call      $0b0f                         ;[0788] cd 0f 0b
                    pop       de                            ;[078b] d1
                    ld        d,a                           ;[078c] 57
                    cp        $e0                           ;[078d] fe e0
                    jr        nc,$07e5                      ;[078f] 30 54
                    cp        e                             ;[0791] bb
                    jr        c,$07e5                       ;[0792] 38 51
                    push      de                            ;[0794] d5
                    call      $10c1                         ;[0795] cd c1 10
                    ld        hl,$1ac3                      ;[0798] 21 c3 1a
                    ld        bc,$1b35                      ;[079b] 01 35 1b
                    call      $110f                         ;[079e] cd 0f 11
                    pop       de                            ;[07a1] d1
                    jr        nc,$0758                      ;[07a2] 30 b4
                    push      de                            ;[07a4] d5
                    ld        d,$50                         ;[07a5] 16 50
                    ld        bc,$1c19                      ;[07a7] 01 19 1c
                    call      $1163                         ;[07aa] cd 63 11
                    push      hl                            ;[07ad] e5
                    push      af                            ;[07ae] f5
                    ld        hl,$1b92                      ;[07af] 21 92 1b
                    call      $11c3                         ;[07b2] cd c3 11
                    pop       af                            ;[07b5] f1
                    pop       hl                            ;[07b6] e1
                    jr        nc,$07dc                      ;[07b7] 30 23
                    ld        de,$000e                      ;[07b9] 11 0e 00
                    call      $1179                         ;[07bc] cd 79 11
                    call      $1230                         ;[07bf] cd 30 12
                    jr        nc,$07de                      ;[07c2] 30 1a
                    pop       de                            ;[07c4] d1
                    push      de                            ;[07c5] d5
                    ld        a,e                           ;[07c6] 7b
                    call      $1292                         ;[07c7] cd 92 12
                    jr        nc,$07de                      ;[07ca] 30 12
                    pop       de                            ;[07cc] d1
                    inc       e                             ;[07cd] 1c
                    push      de                            ;[07ce] d5
                    ld        a,d                           ;[07cf] 7a
                    cp        e                             ;[07d0] bb
                    jr        nc,$07c4                      ;[07d1] 30 f1
                    call      $1254                         ;[07d3] cd 54 12
                    jr        nc,$07de                      ;[07d6] 30 06
                    pop       de                            ;[07d8] d1
                    jp        $0758                         ;[07d9] c3 58 07
                    ld        a,$14                         ;[07dc] 3e 14
                    rst       $10                           ;[07de] d7
                    dec       bc                            ;[07df] 0b
                    dec       sp                            ;[07e0] 3b
                    pop       de                            ;[07e1] d1
                    jp        $1254                         ;[07e2] c3 54 12
                    rst       $10                           ;[07e5] d7
                    jr        $0826                         ;[07e6] 18 3e
                    jr        $07d9                         ;[07e8] 18 ef
                    ld        hl,$0000                      ;[07ea] 21 00 00
                    ld        c,$ff                         ;[07ed] 0e ff
                    push      bc                            ;[07ef] c5
                    push      hl                            ;[07f0] e5
                    ld        hl,$10e3                      ;[07f1] 21 e3 10
                    call      $1093                         ;[07f4] cd 93 10
                    ld        hl,$1c39                      ;[07f7] 21 39 1c
                    call      $11cb                         ;[07fa] cd cb 11
                    ld        bc,$1000                      ;[07fd] 01 00 10
                    ld        de,$0208                      ;[0800] 11 08 02
                    call      $1020                         ;[0803] cd 20 10
                    pop       hl                            ;[0806] e1
                    pop       bc                            ;[0807] c1
                    call      $0811                         ;[0808] cd 11 08
                    ld        de,$14aa                      ;[080b] 11 aa 14
                    jp        $1422                         ;[080e] c3 22 14
                    call      $0a9d                         ;[0811] cd 9d 0a
                    ld        de,$0300                      ;[0814] 11 00 03
                    call      $107e                         ;[0817] cd 7e 10
                    push      bc                            ;[081a] c5
                    push      hl                            ;[081b] e5
                    ld        l,$00                         ;[081c] 2e 00
                    ld        b,$10                         ;[081e] 06 10
                    push      bc                            ;[0820] c5
                    push      hl                            ;[0821] e5
                    ld        de,$1ad0                      ;[0822] 11 d0 1a
                    call      $108a                         ;[0825] cd 8a 10
                    ld        a,$20                         ;[0828] 3e 20
                    rst       $30                           ;[082a] f7
                    call      $1044                         ;[082b] cd 44 10
                    ld        a,c                           ;[082e] 79
                    inc       a                             ;[082f] 3c
                    ld        a,c                           ;[0830] 79
                    call      z,$0047                       ;[0831] cc 47 00
                    call      $1609                         ;[0834] cd 09 16
                    ld        a,$20                         ;[0837] 3e 20
                    rst       $30                           ;[0839] f7
                    ld        de,$1ace                      ;[083a] 11 ce 1a
                    call      $108a                         ;[083d] cd 8a 10
                    ld        a,$20                         ;[0840] 3e 20
                    rst       $30                           ;[0842] f7
                    ld        b,$10                         ;[0843] 06 10
                    push      hl                            ;[0845] e5
                    ld        a,$20                         ;[0846] 3e 20
                    rst       $30                           ;[0848] f7
                    call      $105b                         ;[0849] cd 5b 10
                    inc       hl                            ;[084c] 23
                    djnz      $0846                         ;[084d] 10 f7
                    pop       hl                            ;[084f] e1
                    ld        a,$20                         ;[0850] 3e 20
                    rst       $30                           ;[0852] f7
                    ld        a,$20                         ;[0853] 3e 20
                    rst       $30                           ;[0855] f7
                    ld        b,$10                         ;[0856] 06 10
                    call      $0ae7                         ;[0858] cd e7 0a
                    inc       hl                            ;[085b] 23
                    djnz      $0858                         ;[085c] 10 fa
                    ld        a,$0d                         ;[085e] 3e 0d
                    rst       $30                           ;[0860] f7
                    pop       hl                            ;[0861] e1
                    add       hl,$0010                      ;[0862] ed 34 10 00
                    pop       bc                            ;[0866] c1
                    djnz      $0820                         ;[0867] 10 b7
                    pop       hl                            ;[0869] e1
                    pop       bc                            ;[086a] c1
                    ret                                     ;[086b] c9

                    inc       h                             ;[086c] 24
                    ld        a,c                           ;[086d] 79
                    inc       a                             ;[086e] 3c
                    jr        z,$0877                       ;[086f] 28 06
                    bit       5,h                           ;[0871] cb 6c
                    jr        z,$0877                       ;[0873] 28 02
                    ld        h,$00                         ;[0875] 26 00
                    call      $0811                         ;[0877] cd 11 08
                    xor       a                             ;[087a] af
                    ret                                     ;[087b] c9

                    dec       h                             ;[087c] 25
                    ld        a,c                           ;[087d] 79
                    inc       a                             ;[087e] 3c
                    jr        z,$0877                       ;[087f] 28 f6
                    ld        a,h                           ;[0881] 7c
                    inc       a                             ;[0882] 3c
                    jr        nz,$0877                      ;[0883] 20 f2
                    ld        h,$1f                         ;[0885] 26 1f
                    jr        $0877                         ;[0887] 18 ee
                    ld        a,c                           ;[0889] 79
                    inc       a                             ;[088a] 3c
                    jr        z,$0896                       ;[088b] 28 09
                    dec       c                             ;[088d] 0d
                    ld        a,c                           ;[088e] 79
                    inc       a                             ;[088f] 3c
                    jr        nz,$0877                      ;[0890] 20 e5
                    ld        c,$df                         ;[0892] 0e df
                    jr        $0877                         ;[0894] 18 e1
                    ld        a,h                           ;[0896] 7c
                    sub       $20                           ;[0897] d6 20
                    ld        h,a                           ;[0899] 67
                    jr        $0877                         ;[089a] 18 db
                    ld        a,c                           ;[089c] 79
                    inc       a                             ;[089d] 3c
                    jr        z,$08aa                       ;[089e] 28 0a
                    inc       c                             ;[08a0] 0c
                    ld        a,$df                         ;[08a1] 3e df
                    cp        c                             ;[08a3] b9
                    jr        nc,$0877                      ;[08a4] 30 d1
                    ld        c,$00                         ;[08a6] 0e 00
                    jr        $0877                         ;[08a8] 18 cd
                    ld        a,h                           ;[08aa] 7c
                    add       $20                           ;[08ab] c6 20
                    ld        h,a                           ;[08ad] 67
                    jr        $0877                         ;[08ae] 18 c7
                    ld        de,$1ce9                      ;[08b0] 11 e9 1c
                    call      $0b0f                         ;[08b3] cd 0f 0b
                    cp        $e0                           ;[08b6] fe e0
                    ccf                                     ;[08b8] 3f
                    ret       c                             ;[08b9] d8
                    ld        c,a                           ;[08ba] 4f
                    ld        a,h                           ;[08bb] 7c
                    and       $1f                           ;[08bc] e6 1f
                    ld        h,a                           ;[08be] 67
                    jr        $0877                         ;[08bf] 18 b6
                    ld        a,c                           ;[08c1] 79
                    inc       a                             ;[08c2] 3c
                    scf                                     ;[08c3] 37
                    ret       z                             ;[08c4] c8
                    ld        de,$1cef                      ;[08c5] 11 ef 1c
                    call      $0b16                         ;[08c8] cd 16 0b
                    ld        a,d                           ;[08cb] 7a
                    and       $e0                           ;[08cc] e6 e0
                    scf                                     ;[08ce] 37
                    ret       nz                            ;[08cf] c0
                    ex        de,hl                         ;[08d0] eb
                    jr        $0877                         ;[08d1] 18 a4
                    ld        de,$1cf7                      ;[08d3] 11 f7 1c
                    call      $0b16                         ;[08d6] cd 16 0b
                    ex        de,hl                         ;[08d9] eb
                    ld        c,$ff                         ;[08da] 0e ff
                    jr        $0877                         ;[08dc] 18 99
                    push      bc                            ;[08de] c5
                    push      hl                            ;[08df] e5
                    ld        bc,$0282                      ;[08e0] 01 82 02
                    call      $0b58                         ;[08e3] cd 58 0b
                    pop       hl                            ;[08e6] e1
                    pop       bc                            ;[08e7] c1
                    jr        z,$087a                       ;[08e8] 28 90
                    push      bc                            ;[08ea] c5
                    push      hl                            ;[08eb] e5
                    push      af                            ;[08ec] f5
                    call      $0a7a                         ;[08ed] cd 7a 0a
                    call      $0a89                         ;[08f0] cd 89 0a
                    call      $0ae7                         ;[08f3] cd e7 0a
                    pop       af                            ;[08f6] f1
                    pop       hl                            ;[08f7] e1
                    pop       bc                            ;[08f8] c1
                    ret       nc                            ;[08f9] d0
                    inc       l                             ;[08fa] 2c
                    call      $0a9d                         ;[08fb] cd 9d 0a
                    ld        de,$1cdf                      ;[08fe] 11 df 1c
                    call      $0af4                         ;[0901] cd f4 0a
                    jr        $08de                         ;[0904] 18 d8
                    call      $0a89                         ;[0906] cd 89 0a
                    call      $0ac9                         ;[0909] cd c9 0a
                    ret       z                             ;[090c] c8
                    push      bc                            ;[090d] c5
                    push      hl                            ;[090e] e5
                    ld        e,a                           ;[090f] 5f
                    call      $0a7a                         ;[0910] cd 7a 0a
                    ld        de,$1cdf                      ;[0913] 11 df 1c
                    call      $0af4                         ;[0916] cd f4 0a
                    call      $105b                         ;[0919] cd 5b 10
                    pop       hl                            ;[091c] e1
                    pop       bc                            ;[091d] c1
                    inc       l                             ;[091e] 2c
                    call      $0a9d                         ;[091f] cd 9d 0a
                    ld        de,$1cdf                      ;[0922] 11 df 1c
                    call      $0a89                         ;[0925] cd 89 0a
                    jr        $0906                         ;[0928] 18 dc
                    push      bc                            ;[092a] c5
                    push      hl                            ;[092b] e5
                    call      $0b2c                         ;[092c] cd 2c 0b
                    ld        de,$1d2d                      ;[092f] 11 2d 1d
                    call      $108a                         ;[0932] cd 8a 10
                    rst       $10                           ;[0935] d7
                    jp        po,$e60c                      ;[0936] e2 0c e6
                    rst       $18                           ;[0939] df
                    push      af                            ;[093a] f5
                    call      $0b2c                         ;[093b] cd 2c 0b
                    pop       af                            ;[093e] f1
                    ld        b,$08                         ;[093f] 06 08
                    ld        hl,$3e99                      ;[0941] 21 99 3e
                    cp        $48                           ;[0944] fe 48
                    jr        z,$095f                       ;[0946] 28 17
                    cp        $54                           ;[0948] fe 54
                    jr        z,$0950                       ;[094a] 28 04
                    pop       hl                            ;[094c] e1
                    pop       bc                            ;[094d] c1
                    scf                                     ;[094e] 37
                    ret                                     ;[094f] c9

                    rst       $30                           ;[0950] f7
                    ld        a,$20                         ;[0951] 3e 20
                    rst       $30                           ;[0953] f7
                    call      $0ac9                         ;[0954] cd c9 0a
                    jr        z,$0971                       ;[0957] 28 18
                    ld        (hl),a                        ;[0959] 77
                    inc       hl                            ;[095a] 23
                    djnz      $0954                         ;[095b] 10 f7
                    jr        $0971                         ;[095d] 18 12
                    rst       $30                           ;[095f] f7
                    ld        a,$20                         ;[0960] 3e 20
                    rst       $30                           ;[0962] f7
                    push      bc                            ;[0963] c5
                    ld        bc,$0202                      ;[0964] 01 02 02
                    call      $0b58                         ;[0967] cd 58 0b
                    pop       bc                            ;[096a] c1
                    jr        z,$0971                       ;[096b] 28 04
                    ld        (hl),e                        ;[096d] 73
                    inc       hl                            ;[096e] 23
                    djnz      $0960                         ;[096f] 10 ef
                    ld        a,$08                         ;[0971] 3e 08
                    sub       b                             ;[0973] 90
                    ld        ($3e98),a                     ;[0974] 32 98 3e
                    pop       hl                            ;[0977] e1
                    pop       bc                            ;[0978] c1
                    ld        a,($3e98)                     ;[0979] 3a 98 3e
                    and       a                             ;[097c] a7
                    scf                                     ;[097d] 37
                    ret       z                             ;[097e] c8
                    ld        b,a                           ;[097f] 47
                    push      bc                            ;[0980] c5
                    push      hl                            ;[0981] e5
                    ld        de,$3e99                      ;[0982] 11 99 3e
                    push      bc                            ;[0985] c5
                    push      hl                            ;[0986] e5
                    push      de                            ;[0987] d5
                    ld        a,c                           ;[0988] 79
                    inc       a                             ;[0989] 3c
                    ld        a,c                           ;[098a] 79
                    call      z,$0047                       ;[098b] cc 47 00
                    call      $1609                         ;[098e] cd 09 16
                    pop       de                            ;[0991] d1
                    ld        a,(de)                        ;[0992] 1a
                    cp        (hl)                          ;[0993] be
                    pop       hl                            ;[0994] e1
                    pop       bc                            ;[0995] c1
                    jr        nz,$09a5                      ;[0996] 20 0d
                    inc       de                            ;[0998] 13
                    call      $005c                         ;[0999] cd 5c 00
                    djnz      $0985                         ;[099c] 10 e7
                    pop       hl                            ;[099e] e1
                    pop       bc                            ;[099f] c1
                    call      $0811                         ;[09a0] cd 11 08
                    xor       a                             ;[09a3] af
                    ret                                     ;[09a4] c9

                    pop       hl                            ;[09a5] e1
                    pop       bc                            ;[09a6] c1
                    call      $005c                         ;[09a7] cd 5c 00
                    jr        nz,$0980                      ;[09aa] 20 d4
                    call      $0811                         ;[09ac] cd 11 08
                    scf                                     ;[09af] 37
                    ret                                     ;[09b0] c9

                    ld        a,($3e98)                     ;[09b1] 3a 98 3e
                    and       a                             ;[09b4] a7
                    scf                                     ;[09b5] 37
                    ret       z                             ;[09b6] c8
                    call      $005c                         ;[09b7] cd 5c 00
                    jr        $0979                         ;[09ba] 18 bd
                    push      hl                            ;[09bc] e5
                    push      bc                            ;[09bd] c5
                    ld        de,$1d00                      ;[09be] 11 00 1d
                    inc       c                             ;[09c1] 0c
                    jr        nz,$09c7                      ;[09c2] 20 03
                    ld        de,$1d0c                      ;[09c4] 11 0c 1d
                    call      $0b49                         ;[09c7] cd 49 0b
                    push      de                            ;[09ca] d5
                    ld        de,$1d19                      ;[09cb] 11 19 1d
                    call      $0b4c                         ;[09ce] cd 4c 0b
                    push      de                            ;[09d1] d5
                    ld        de,$1d20                      ;[09d2] 11 20 1d
                    call      $0b4c                         ;[09d5] cd 4c 0b
                    push      de                            ;[09d8] d5
                    ld        de,$1d27                      ;[09d9] 11 27 1d
                    call      $108a                         ;[09dc] cd 8a 10
                    rst       $10                           ;[09df] d7
                    jp        po,$e60c                      ;[09e0] e2 0c e6
                    rst       $18                           ;[09e3] df
                    cp        $59                           ;[09e4] fe 59
                    scf                                     ;[09e6] 37
                    pop       ix                            ;[09e7] dd e1
                    pop       hl                            ;[09e9] e1
                    pop       de                            ;[09ea] d1
                    jr        nz,$0a46                      ;[09eb] 20 59
                    pop       bc                            ;[09ed] c1
                    push      bc                            ;[09ee] c5
                    ld        a,c                           ;[09ef] 79
                    push      hl                            ;[09f0] e5
                    and       a                             ;[09f1] a7
                    sbc       hl,de                         ;[09f2] ed 52
                    ld        b,h                           ;[09f4] 44
                    ld        c,l                           ;[09f5] 4d
                    pop       hl                            ;[09f6] e1
                    jr        c,$0a46                       ;[09f7] 38 4d
                    push      ix                            ;[09f9] dd e5
                    add       ix,bc                         ;[09fb] dd 09
                    push      ix                            ;[09fd] dd e5
                    ex        (sp),hl                       ;[09ff] e3
                    bit       5,h                           ;[0a00] cb 6c
                    pop       hl                            ;[0a02] e1
                    pop       ix                            ;[0a03] dd e1
                    jr        c,$0a46                       ;[0a05] 38 3f
                    jr        z,$0a0d                       ;[0a07] 28 04
                    cp        $ff                           ;[0a09] fe ff
                    jr        c,$0a46                       ;[0a0b] 38 39
                    cp        $ff                           ;[0a0d] fe ff
                    jr        z,$0a20                       ;[0a0f] 28 0f
                    ex        af,af'                        ;[0a11] 08
                    ld        a,h                           ;[0a12] 7c
                    and       $e0                           ;[0a13] e6 e0
                    scf                                     ;[0a15] 37
                    jr        nz,$0a46                      ;[0a16] 20 2e
                    ld        a,ixh                         ;[0a18] dd 7c
                    and       $e0                           ;[0a1a] e6 e0
                    scf                                     ;[0a1c] 37
                    jr        nz,$0a46                      ;[0a1d] 20 27
                    ex        af,af'                        ;[0a1f] 08
                    push      ix                            ;[0a20] dd e5
                    ex        (sp),hl                       ;[0a22] e3
                    and       a                             ;[0a23] a7
                    sbc       hl,de                         ;[0a24] ed 52
                    pop       hl                            ;[0a26] e1
                    jr        c,$0a39                       ;[0a27] 38 10
                    add       ix,bc                         ;[0a29] dd 09
                    push      ix                            ;[0a2b] dd e5
                    pop       de                            ;[0a2d] d1
                    inc       bc                            ;[0a2e] 03
                    ex        af,af'                        ;[0a2f] 08
                    call      $0a51                         ;[0a30] cd 51 0a
                    dec       hl                            ;[0a33] 2b
                    dec       de                            ;[0a34] 1b
                    jr        nz,$0a30                      ;[0a35] 20 f9
                    jr        $0a46                         ;[0a37] 18 0d
                    ex        de,hl                         ;[0a39] eb
                    push      ix                            ;[0a3a] dd e5
                    pop       de                            ;[0a3c] d1
                    inc       bc                            ;[0a3d] 03
                    ex        af,af'                        ;[0a3e] 08
                    call      $0a51                         ;[0a3f] cd 51 0a
                    inc       hl                            ;[0a42] 23
                    inc       de                            ;[0a43] 13
                    jr        nz,$0a3f                      ;[0a44] 20 f9
                    pop       bc                            ;[0a46] c1
                    pop       hl                            ;[0a47] e1
                    push      af                            ;[0a48] f5
                    call      $0b2c                         ;[0a49] cd 2c 0b
                    call      $0811                         ;[0a4c] cd 11 08
                    pop       af                            ;[0a4f] f1
                    ret                                     ;[0a50] c9

                    ex        af,af'                        ;[0a51] 08
                    push      bc                            ;[0a52] c5
                    push      hl                            ;[0a53] e5
                    push      de                            ;[0a54] d5
                    push      af                            ;[0a55] f5
                    cp        $ff                           ;[0a56] fe ff
                    call      z,$0047                       ;[0a58] cc 47 00
                    call      $1609                         ;[0a5b] cd 09 16
                    ld        c,(hl)                        ;[0a5e] 4e
                    pop       af                            ;[0a5f] f1
                    pop       hl                            ;[0a60] e1
                    push      hl                            ;[0a61] e5
                    push      af                            ;[0a62] f5
                    push      bc                            ;[0a63] c5
                    cp        $ff                           ;[0a64] fe ff
                    call      z,$0047                       ;[0a66] cc 47 00
                    call      $1609                         ;[0a69] cd 09 16
                    pop       bc                            ;[0a6c] c1
                    ld        (hl),c                        ;[0a6d] 71
                    call      $16c8                         ;[0a6e] cd c8 16
                    pop       af                            ;[0a71] f1
                    pop       de                            ;[0a72] d1
                    pop       hl                            ;[0a73] e1
                    pop       bc                            ;[0a74] c1
                    ex        af,af'                        ;[0a75] 08
                    dec       bc                            ;[0a76] 0b
                    ld        a,b                           ;[0a77] 78
                    or        c                             ;[0a78] b1
                    ret                                     ;[0a79] c9

                    push      de                            ;[0a7a] d5
                    ld        a,c                           ;[0a7b] 79
                    inc       a                             ;[0a7c] 3c
                    ld        a,c                           ;[0a7d] 79
                    call      z,$0047                       ;[0a7e] cc 47 00
                    call      $1609                         ;[0a81] cd 09 16
                    pop       de                            ;[0a84] d1
                    ld        (hl),e                        ;[0a85] 73
                    jp        $16c8                         ;[0a86] c3 c8 16
                    ld        a,$16                         ;[0a89] 3e 16
                    rst       $30                           ;[0a8b] f7
                    ld        a,l                           ;[0a8c] 7d
                    rrca                                    ;[0a8d] 0f
                    rrca                                    ;[0a8e] 0f
                    rrca                                    ;[0a8f] 0f
                    rrca                                    ;[0a90] 0f
                    and       $0f                           ;[0a91] e6 0f
                    add       $03                           ;[0a93] c6 03
                    rst       $30                           ;[0a95] f7
                    ld        a,l                           ;[0a96] 7d
                    and       $0f                           ;[0a97] e6 0f
                    add       $39                           ;[0a99] c6 39
                    rst       $30                           ;[0a9b] f7
                    ret                                     ;[0a9c] c9

                    ld        de,$0000                      ;[0a9d] 11 00 00
                    call      $107e                         ;[0aa0] cd 7e 10
                    ld        a,c                           ;[0aa3] 79
                    inc       a                             ;[0aa4] 3c
                    jr        z,$0aba                       ;[0aa5] 28 13
                    ld        de,$1cc5                      ;[0aa7] 11 c5 1c
                    call      $108a                         ;[0aaa] cd 8a 10
                    ld        a,c                           ;[0aad] 79
                    call      $105c                         ;[0aae] cd 5c 10
                    ld        de,$1ccc                      ;[0ab1] 11 cc 1c
                    call      $108a                         ;[0ab4] cd 8a 10
                    jp        $1044                         ;[0ab7] c3 44 10
                    ld        de,$1cd6                      ;[0aba] 11 d6 1c
                    call      $108a                         ;[0abd] cd 8a 10
                    call      $1044                         ;[0ac0] cd 44 10
                    push      bc                            ;[0ac3] c5
                    call      $0b3f                         ;[0ac4] cd 3f 0b
                    pop       bc                            ;[0ac7] c1
                    ret                                     ;[0ac8] c9

                    push      bc                            ;[0ac9] c5
                    push      hl                            ;[0aca] e5
                    ld        de,$1d35                      ;[0acb] 11 35 1d
                    call      $108a                         ;[0ace] cd 8a 10
                    rst       $10                           ;[0ad1] d7
                    jp        po,$470c                      ;[0ad2] e2 0c 47
                    ld        de,$1d35                      ;[0ad5] 11 35 1d
                    call      $108a                         ;[0ad8] cd 8a 10
                    ld        a,b                           ;[0adb] 78
                    pop       hl                            ;[0adc] e1
                    pop       bc                            ;[0add] c1
                    cp        $0d                           ;[0ade] fe 0d
                    ret       z                             ;[0ae0] c8
                    push      af                            ;[0ae1] f5
                    call      $0ae8                         ;[0ae2] cd e8 0a
                    pop       af                            ;[0ae5] f1
                    ret                                     ;[0ae6] c9

                    ld        a,(hl)                        ;[0ae7] 7e
                    cp        $20                           ;[0ae8] fe 20
                    jr        c,$0af0                       ;[0aea] 38 04
                    cp        $80                           ;[0aec] fe 80
                    jr        c,$0af2                       ;[0aee] 38 02
                    ld        a,$2e                         ;[0af0] 3e 2e
                    rst       $30                           ;[0af2] f7
                    ret                                     ;[0af3] c9

                    push      de                            ;[0af4] d5
                    ld        a,l                           ;[0af5] 7d
                    rrca                                    ;[0af6] 0f
                    rrca                                    ;[0af7] 0f
                    rrca                                    ;[0af8] 0f
                    rrca                                    ;[0af9] 0f
                    and       $0f                           ;[0afa] e6 0f
                    add       $03                           ;[0afc] c6 03
                    ld        d,a                           ;[0afe] 57
                    ld        a,l                           ;[0aff] 7d
                    and       $0f                           ;[0b00] e6 0f
                    ld        e,a                           ;[0b02] 5f
                    add       a                             ;[0b03] 87
                    add       e                             ;[0b04] 83
                    add       $07                           ;[0b05] c6 07
                    ld        e,a                           ;[0b07] 5f
                    call      $107e                         ;[0b08] cd 7e 10
                    pop       de                            ;[0b0b] d1
                    jp        $108a                         ;[0b0c] c3 8a 10
                    ld        b,$02                         ;[0b0f] 06 02
                    call      $0b18                         ;[0b11] cd 18 0b
                    ld        a,e                           ;[0b14] 7b
                    ret                                     ;[0b15] c9

                    ld        b,$04                         ;[0b16] 06 04
                    push      de                            ;[0b18] d5
                    call      $0b2c                         ;[0b19] cd 2c 0b
                    ld        de,$1ce0                      ;[0b1c] 11 e0 1c
                    call      $108a                         ;[0b1f] cd 8a 10
                    pop       de                            ;[0b22] d1
                    call      $108a                         ;[0b23] cd 8a 10
                    push      bc                            ;[0b26] c5
                    ld        c,b                           ;[0b27] 48
                    call      $0b58                         ;[0b28] cd 58 0b
                    pop       bc                            ;[0b2b] c1
                    call      $0b36                         ;[0b2c] cd 36 0b
                    push      bc                            ;[0b2f] c5
                    ld        b,$34                         ;[0b30] 06 34
                    call      $0b41                         ;[0b32] cd 41 0b
                    pop       bc                            ;[0b35] c1
                    push      de                            ;[0b36] d5
                    ld        de,$0020                      ;[0b37] 11 20 00
                    call      $107e                         ;[0b3a] cd 7e 10
                    pop       de                            ;[0b3d] d1
                    ret                                     ;[0b3e] c9

                    ld        b,$09                         ;[0b3f] 06 09
                    push      de                            ;[0b41] d5
                    ld        a,$20                         ;[0b42] 3e 20
                    rst       $30                           ;[0b44] f7
                    djnz      $0b42                         ;[0b45] 10 fb
                    pop       de                            ;[0b47] d1
                    ret                                     ;[0b48] c9

                    call      $0b36                         ;[0b49] cd 36 0b
                    call      $108a                         ;[0b4c] cd 8a 10
                    push      bc                            ;[0b4f] c5
                    ld        bc,$0484                      ;[0b50] 01 84 04
                    call      $0b58                         ;[0b53] cd 58 0b
                    pop       bc                            ;[0b56] c1
                    ret                                     ;[0b57] c9

                    ld        de,$0000                      ;[0b58] 11 00 00
                    push      hl                            ;[0b5b] e5
                    push      bc                            ;[0b5c] c5
                    push      de                            ;[0b5d] d5
                    ld        de,$1d35                      ;[0b5e] 11 35 1d
                    call      $108a                         ;[0b61] cd 8a 10
                    pop       de                            ;[0b64] d1
                    push      de                            ;[0b65] d5
                    rst       $10                           ;[0b66] d7
                    jp        po,$470c                      ;[0b67] e2 0c 47
                    pop       de                            ;[0b6a] d1
                    cp        $0d                           ;[0b6b] fe 0d
                    jr        nz,$0b91                      ;[0b6d] 20 22
                    push      de                            ;[0b6f] d5
                    ld        de,$1d35                      ;[0b70] 11 35 1d
                    call      $108a                         ;[0b73] cd 8a 10
                    pop       de                            ;[0b76] d1
                    pop       bc                            ;[0b77] c1
                    ld        a,c                           ;[0b78] 79
                    and       $7f                           ;[0b79] e6 7f
                    cp        b                             ;[0b7b] b8
                    jr        z,$0b8f                       ;[0b7c] 28 11
                    bit       7,c                           ;[0b7e] cb 79
                    jr        z,$0b8f                       ;[0b80] 28 0d
                    ex        de,hl                         ;[0b82] eb
                    add       hl,hl                         ;[0b83] 29
                    add       hl,hl                         ;[0b84] 29
                    add       hl,hl                         ;[0b85] 29
                    add       hl,hl                         ;[0b86] 29
                    ld        a,$30                         ;[0b87] 3e 30
                    rst       $30                           ;[0b89] f7
                    djnz      $0b83                         ;[0b8a] 10 f7
                    ex        de,hl                         ;[0b8c] eb
                    and       a                             ;[0b8d] a7
                    inc       a                             ;[0b8e] 3c
                    pop       hl                            ;[0b8f] e1
                    ret                                     ;[0b90] c9

                    cp        $3a                           ;[0b91] fe 3a
                    jr        nc,$0ba0                      ;[0b93] 30 0b
                    sub       $30                           ;[0b95] d6 30
                    jr        nc,$0bac                      ;[0b97] 30 13
                    push      de                            ;[0b99] d5
                    rst       $10                           ;[0b9a] d7
                    jr        $0bdb                         ;[0b9b] 18 3e
                    pop       de                            ;[0b9d] d1
                    jr        $0b65                         ;[0b9e] 18 c5
                    and       $df                           ;[0ba0] e6 df
                    cp        $47                           ;[0ba2] fe 47
                    jr        nc,$0b99                      ;[0ba4] 30 f3
                    sub       $41                           ;[0ba6] d6 41
                    jr        c,$0b99                       ;[0ba8] 38 ef
                    add       $0a                           ;[0baa] c6 0a
                    push      af                            ;[0bac] f5
                    push      de                            ;[0bad] d5
                    ld        a,b                           ;[0bae] 78
                    rst       $30                           ;[0baf] f7
                    pop       de                            ;[0bb0] d1
                    pop       af                            ;[0bb1] f1
                    add       a                             ;[0bb2] 87
                    add       a                             ;[0bb3] 87
                    add       a                             ;[0bb4] 87
                    add       a                             ;[0bb5] 87
                    ld        b,$04                         ;[0bb6] 06 04
                    ex        de,hl                         ;[0bb8] eb
                    add       a                             ;[0bb9] 87
                    adc       hl,hl                         ;[0bba] ed 6a
                    djnz      $0bb9                         ;[0bbc] 10 fb
                    ex        de,hl                         ;[0bbe] eb
                    pop       bc                            ;[0bbf] c1
                    pop       hl                            ;[0bc0] e1
                    djnz      $0b5b                         ;[0bc1] 10 98
                    scf                                     ;[0bc3] 37
                    sbc       a                             ;[0bc4] 9f
                    ret                                     ;[0bc5] c9

                    inc       a                             ;[0bc6] 3c
                    jr        z,$0bfb                       ;[0bc7] 28 32
                    ld        b,$14                         ;[0bc9] 06 14
                    push      bc                            ;[0bcb] c5
                    call      $16f0                         ;[0bcc] cd f0 16
                    jr        z,$0bf4                       ;[0bcf] 28 23
                    ld        hl,$0004                      ;[0bd1] 21 04 00
                    add       hl,de                         ;[0bd4] 19
                    push      af                            ;[0bd5] f5
                    push      hl                            ;[0bd6] e5
                    ld        hl,($3f9f)                    ;[0bd7] 2a 9f 3f
                    call      $0047                         ;[0bda] cd 47 00
                    pop       de                            ;[0bdd] d1
                    pop       bc                            ;[0bde] c1
                    and       a                             ;[0bdf] a7
                    sbc       hl,de                         ;[0be0] ed 52
                    jr        nz,$0bf4                      ;[0be2] 20 10
                    cp        b                             ;[0be4] b8
                    jr        nz,$0bf4                      ;[0be5] 20 0d
                    ld        hl,($3f9f)                    ;[0be7] 2a 9f 3f
                    add       hl,$fffc                      ;[0bea] ed 34 fc ff
                    ld        ($3f9f),hl                    ;[0bee] 22 9f 3f
                    pop       bc                            ;[0bf1] c1
                    jr        $0bfb                         ;[0bf2] 18 07
                    pop       bc                            ;[0bf4] c1
                    djnz      $0bcb                         ;[0bf5] 10 d4
                    jr        $0bfb                         ;[0bf7] 18 02
                    pop       de                            ;[0bf9] d1
                    pop       de                            ;[0bfa] d1
                    ld        hl,$10db                      ;[0bfb] 21 db 10
                    call      $1093                         ;[0bfe] cd 93 10
                    ld        hl,$1d3c                      ;[0c01] 21 3c 1d
                    call      $11cb                         ;[0c04] cd cb 11
                    ld        hl,$1e02                      ;[0c07] 21 02 1e
                    call      $11cb                         ;[0c0a] cd cb 11
                    ld        a,$01                         ;[0c0d] 3e 01
                    call      $179a                         ;[0c0f] cd 9a 17
                    ld        c,a                           ;[0c12] 4f
                    swapnib                                 ;[0c13] ed 23
                    call      $1065                         ;[0c15] cd 65 10
                    ld        a,$2e                         ;[0c18] 3e 2e
                    rst       $30                           ;[0c1a] f7
                    ld        a,c                           ;[0c1b] 79
                    and       $0f                           ;[0c1c] e6 0f
                    call      $106e                         ;[0c1e] cd 6e 10
                    ld        a,$2e                         ;[0c21] 3e 2e
                    rst       $30                           ;[0c23] f7
                    ld        a,$0e                         ;[0c24] 3e 0e
                    call      $179a                         ;[0c26] cd 9a 17
                    call      $106e                         ;[0c29] cd 6e 10
                    ld        hl,$0e76                      ;[0c2c] 21 76 0e
                    ld        bc,$1c80                      ;[0c2f] 01 80 1c
                    ld        de,$0002                      ;[0c32] 11 02 00
                    call      $1020                         ;[0c35] cd 20 10
                    ld        bc,$1c80                      ;[0c38] 01 80 1c
                    ld        de,$0202                      ;[0c3b] 11 02 02
                    call      $1020                         ;[0c3e] cd 20 10
                    ld        bc,$1980                      ;[0c41] 01 80 19
                    ld        de,$0402                      ;[0c44] 11 02 04
                    call      $1020                         ;[0c47] cd 20 10
                    ld        hl,$0eca                      ;[0c4a] 21 ca 0e
                    ld        d,(hl)                        ;[0c4d] 56
                    res       7,d                           ;[0c4e] cb ba
                    ld        e,$0a                         ;[0c50] 1e 0a
                    call      $107e                         ;[0c52] cd 7e 10
                    ld        de,$1e82                      ;[0c55] 11 82 1e
                    call      $108a                         ;[0c58] cd 8a 10
                    bit       7,(hl)                        ;[0c5b] cb 7e
                    inc       hl                            ;[0c5d] 23
                    jr        z,$0c4d                       ;[0c5e] 28 ed
                    ld        hl,$0d86                      ;[0c60] 21 86 0d
                    ld        c,$00                         ;[0c63] 0e 00
                    ld        a,(hl)                        ;[0c65] 7e
                    and       $3f                           ;[0c66] e6 3f
                    ld        b,a                           ;[0c68] 47
                    push      bc                            ;[0c69] c5
                    call      $0d03                         ;[0c6a] cd 03 0d
                    pop       bc                            ;[0c6d] c1
                    inc       c                             ;[0c6e] 0c
                    djnz      $0c69                         ;[0c6f] 10 f8
                    call      $0cf6                         ;[0c71] cd f6 0c
                    jr        nz,$0c63                      ;[0c74] 20 ed
                    ld        hl,$0d86                      ;[0c76] 21 86 0d
                    ld        c,$00                         ;[0c79] 0e 00
                    ld        de,$1480                      ;[0c7b] 11 80 14
                    jp        $1422                         ;[0c7e] c3 22 14
                    inc       c                             ;[0c81] 0c
                    dec       c                             ;[0c82] 0d
                    scf                                     ;[0c83] 37
                    ret       z                             ;[0c84] c8
                    dec       c                             ;[0c85] 0d
                    xor       a                             ;[0c86] af
                    ret                                     ;[0c87] c9

                    ld        a,(hl)                        ;[0c88] 7e
                    and       $3f                           ;[0c89] e6 3f
                    dec       a                             ;[0c8b] 3d
                    cp        c                             ;[0c8c] b9
                    scf                                     ;[0c8d] 37
                    ret       z                             ;[0c8e] c8
                    inc       c                             ;[0c8f] 0c
                    xor       a                             ;[0c90] af
                    ret                                     ;[0c91] c9

                    push      bc                            ;[0c92] c5
                    call      $0cf6                         ;[0c93] cd f6 0c
                    pop       bc                            ;[0c96] c1
                    scf                                     ;[0c97] 37
                    ret       z                             ;[0c98] c8
                    and       a                             ;[0c99] a7
                    inc       c                             ;[0c9a] 0c
                    dec       c                             ;[0c9b] 0d
                    ret       z                             ;[0c9c] c8
                    ld        a,(hl)                        ;[0c9d] 7e
                    and       $3f                           ;[0c9e] e6 3f
                    dec       a                             ;[0ca0] 3d
                    ld        c,a                           ;[0ca1] 4f
                    xor       a                             ;[0ca2] af
                    ret                                     ;[0ca3] c9

                    bit       7,(hl)                        ;[0ca4] cb 7e
                    scf                                     ;[0ca6] 37
                    ret       z                             ;[0ca7] c8
                    push      bc                            ;[0ca8] c5
                    ex        de,hl                         ;[0ca9] eb
                    ld        hl,$0d86                      ;[0caa] 21 86 0d
                    push      hl                            ;[0cad] e5
                    call      $0cf6                         ;[0cae] cd f6 0c
                    and       a                             ;[0cb1] a7
                    sbc       hl,de                         ;[0cb2] ed 52
                    pop       hl                            ;[0cb4] e1
                    jr        z,$0cbc                       ;[0cb5] 28 05
                    call      $0cf6                         ;[0cb7] cd f6 0c
                    jr        $0cad                         ;[0cba] 18 f1
                    pop       bc                            ;[0cbc] c1
                    jr        $0c99                         ;[0cbd] 18 da
                    call      $0d56                         ;[0cbf] cd 56 0d
                    pop       hl                            ;[0cc2] e1
                    pop       hl                            ;[0cc3] e1
                    bit       5,d                           ;[0cc4] cb 6a
                    jp        z,$07ea                       ;[0cc6] ca ea 07
                    ld        h,$3f                         ;[0cc9] 26 3f
                    ld        l,e                           ;[0ccb] 6b
                    ld        e,(hl)                        ;[0ccc] 5e
                    inc       hl                            ;[0ccd] 23
                    ld        d,(hl)                        ;[0cce] 56
                    ex        de,hl                         ;[0ccf] eb
                    jp        $07ed                         ;[0cd0] c3 ed 07
                    call      $0d56                         ;[0cd3] cd 56 0d
                    push      bc                            ;[0cd6] c5
                    push      de                            ;[0cd7] d5
                    ld        a,d                           ;[0cd8] 7a
                    and       $0f                           ;[0cd9] e6 0f
                    ld        b,a                           ;[0cdb] 47
                    or        $80                           ;[0cdc] f6 80
                    ld        c,a                           ;[0cde] 4f
                    call      $0b58                         ;[0cdf] cd 58 0b
                    ex        (sp),hl                       ;[0ce2] e3
                    jr        z,$0cef                       ;[0ce3] 28 0a
                    ld        a,h                           ;[0ce5] 7c
                    ld        h,$3f                         ;[0ce6] 26 3f
                    ld        (hl),e                        ;[0ce8] 73
                    inc       hl                            ;[0ce9] 23
                    bit       5,a                           ;[0cea] cb 6f
                    jr        z,$0cef                       ;[0cec] 28 01
                    ld        (hl),d                        ;[0cee] 72
                    pop       hl                            ;[0cef] e1
                    pop       bc                            ;[0cf0] c1
                    call      $0d03                         ;[0cf1] cd 03 0d
                    xor       a                             ;[0cf4] af
                    ret                                     ;[0cf5] c9

                    bit       6,(hl)                        ;[0cf6] cb 76
                    ret       z                             ;[0cf8] c8
                    ld        a,(hl)                        ;[0cf9] 7e
                    and       $3f                           ;[0cfa] e6 3f
                    inc       a                             ;[0cfc] 3c
                    ld        c,a                           ;[0cfd] 4f
                    ld        b,$00                         ;[0cfe] 06 00
                    add       hl,bc                         ;[0d00] 09
                    add       hl,bc                         ;[0d01] 09
                    ret                                     ;[0d02] c9

                    call      $0d56                         ;[0d03] cd 56 0d
                    push      hl                            ;[0d06] e5
                    ld        l,e                           ;[0d07] 6b
                    ld        h,$3f                         ;[0d08] 26 3f
                    bit       5,d                           ;[0d0a] cb 6a
                    jr        z,$0d2f                       ;[0d0c] 28 21
                    call      $1040                         ;[0d0e] cd 40 10
                    ld        b,$04                         ;[0d11] 06 04
                    ld        a,$09                         ;[0d13] 3e 09
                    rst       $30                           ;[0d15] f7
                    djnz      $0d13                         ;[0d16] 10 fb
                    ld        b,$10                         ;[0d18] 06 10
                    ld        a,$20                         ;[0d1a] 3e 20
                    rst       $30                           ;[0d1c] f7
                    push      bc                            ;[0d1d] c5
                    push      hl                            ;[0d1e] e5
                    call      $0047                         ;[0d1f] cd 47 00
                    call      $1609                         ;[0d22] cd 09 16
                    call      $105b                         ;[0d25] cd 5b 10
                    pop       hl                            ;[0d28] e1
                    pop       bc                            ;[0d29] c1
                    inc       hl                            ;[0d2a] 23
                    djnz      $0d1a                         ;[0d2b] 10 ed
                    pop       hl                            ;[0d2d] e1
                    ret                                     ;[0d2e] c9

                    push      de                            ;[0d2f] d5
                    call      $105b                         ;[0d30] cd 5b 10
                    pop       de                            ;[0d33] d1
                    ld        a,(hl)                        ;[0d34] 7e
                    pop       hl                            ;[0d35] e1
                    bit       4,d                           ;[0d36] cb 62
                    ret       z                             ;[0d38] c8
                    push      af                            ;[0d39] f5
                    ld        a,$20                         ;[0d3a] 3e 20
                    rst       $30                           ;[0d3c] f7
                    pop       af                            ;[0d3d] f1
                    ld        b,$08                         ;[0d3e] 06 08
                    add       a                             ;[0d40] 87
                    push      af                            ;[0d41] f5
                    ld        a,$30                         ;[0d42] 3e 30
                    adc       $00                           ;[0d44] ce 00
                    rst       $30                           ;[0d46] f7
                    pop       af                            ;[0d47] f1
                    djnz      $0d40                         ;[0d48] 10 f6
                    ret                                     ;[0d4a] c9

                    push      de                            ;[0d4b] d5
                    call      $0d56                         ;[0d4c] cd 56 0d
                    ld        a,$08                         ;[0d4f] 3e 08
                    rst       $30                           ;[0d51] f7
                    pop       de                            ;[0d52] d1
                    jp        $108a                         ;[0d53] c3 8a 10
                    push      bc                            ;[0d56] c5
                    push      hl                            ;[0d57] e5
                    inc       hl                            ;[0d58] 23
                    ld        d,(hl)                        ;[0d59] 56
                    inc       hl                            ;[0d5a] 23
                    ld        b,$00                         ;[0d5b] 06 00
                    add       hl,bc                         ;[0d5d] 09
                    add       hl,bc                         ;[0d5e] 09
                    ld        e,(hl)                        ;[0d5f] 5e
                    inc       hl                            ;[0d60] 23
                    ld        b,(hl)                        ;[0d61] 46
                    push      de                            ;[0d62] d5
                    res       7,e                           ;[0d63] cb bb
                    call      $107e                         ;[0d65] cd 7e 10
                    pop       de                            ;[0d68] d1
                    ld        d,e                           ;[0d69] 53
                    ld        e,b                           ;[0d6a] 58
                    pop       hl                            ;[0d6b] e1
                    pop       bc                            ;[0d6c] c1
                    ld        a,c                           ;[0d6d] 79
                    and       a                             ;[0d6e] a7
                    ld        a,d                           ;[0d6f] 7a
                    ld        d,$c2                         ;[0d70] 16 c2
                    jr        nz,$0d7f                      ;[0d72] 20 0b
                    add       a                             ;[0d74] 87
                    jr        nc,$0d79                      ;[0d75] 30 02
                    ld        d,$e4                         ;[0d77] 16 e4
                    inc       c                             ;[0d79] 0c
                    dec       c                             ;[0d7a] 0d
                    ret       nz                            ;[0d7b] c0
                    res       7,d                           ;[0d7c] cb ba
                    ret                                     ;[0d7e] c9

                    res       6,d                           ;[0d7f] cb b2
                    add       a                             ;[0d81] 87
                    ret       nc                            ;[0d82] d0
                    set       4,d                           ;[0d83] cb e2
                    ret                                     ;[0d85] c9

                    ld        e,h                           ;[0d86] 5c
                    ld        bc,$4e02                      ;[0d87] 01 02 4e
                    dec       b                             ;[0d8a] 05
                    ld        c,a                           ;[0d8b] 4f
                    ex        af,af'                        ;[0d8c] 08
                    ld        d,b                           ;[0d8d] 50
                    dec       bc                            ;[0d8e] 0b
                    ld        d,c                           ;[0d8f] 51
                    ld        c,$52                         ;[0d90] 0e 52
                    ld        de,$1453                      ;[0d92] 11 53 14
                    ld        d,h                           ;[0d95] 54
                    rla                                     ;[0d96] 17
                    ld        d,l                           ;[0d97] 55
                    ld        a,(de)                        ;[0d98] 1a
                    ld        d,(hl)                        ;[0d99] 56
                    dec       e                             ;[0d9a] 1d
                    ld        d,a                           ;[0d9b] 57
                    jr        nz,$0df6                      ;[0d9c] 20 58
                    inc       hl                            ;[0d9e] 23
                    ld        e,c                           ;[0d9f] 59
                    ld        h,$5a                         ;[0da0] 26 5a
                    add       hl,hl                         ;[0da2] 29
                    ld        e,e                           ;[0da3] 5b
                    inc       l                             ;[0da4] 2c
                    ld        e,h                           ;[0da5] 5c
                    cpl                                     ;[0da6] 2f
                    ld        e,l                           ;[0da7] 5d
                    ld        ($355e),a                     ;[0da8] 32 5e 35
                    ld        e,a                           ;[0dab] 5f
                    jr        c,$0e0e                       ;[0dac] 38 60
                    dec       sp                            ;[0dae] 3b
                    ld        h,c                           ;[0daf] 61
                    ld        a,$62                         ;[0db0] 3e 62
                    ld        b,c                           ;[0db2] 41
                    ld        h,e                           ;[0db3] 63
                    ld        b,h                           ;[0db4] 44
                    ld        h,h                           ;[0db5] 64
                    ld        b,a                           ;[0db6] 47
                    ld        h,l                           ;[0db7] 65
                    ld        c,d                           ;[0db8] 4a
                    ld        h,(hl)                        ;[0db9] 66
                    ld        c,l                           ;[0dba] 4d
                    ld        h,a                           ;[0dbb] 67
                    ld        d,b                           ;[0dbc] 50
                    ld        l,b                           ;[0dbd] 68
                    ld        d,e                           ;[0dbe] 53
                    ld        l,c                           ;[0dbf] 69
                    call      c,$0203                       ;[0dc0] dc 03 02
                    ld        l,d                           ;[0dc3] 6a
                    dec       b                             ;[0dc4] 05
                    ld        l,e                           ;[0dc5] 6b
                    ex        af,af'                        ;[0dc6] 08
                    ld        l,h                           ;[0dc7] 6c
                    dec       bc                            ;[0dc8] 0b
                    ld        l,l                           ;[0dc9] 6d
                    ld        c,$6e                         ;[0dca] 0e 6e
                    ld        de,$146f                      ;[0dcc] 11 6f 14
                    ld        (hl),b                        ;[0dcf] 70
                    rla                                     ;[0dd0] 17
                    ld        (hl),c                        ;[0dd1] 71
                    ld        a,(de)                        ;[0dd2] 1a
                    ld        (hl),d                        ;[0dd3] 72
                    dec       e                             ;[0dd4] 1d
                    ld        (hl),e                        ;[0dd5] 73
                    jr        nz,$0e4c                      ;[0dd6] 20 74
                    inc       hl                            ;[0dd8] 23
                    ld        (hl),l                        ;[0dd9] 75
                    ld        h,$76                         ;[0dda] 26 76
                    add       hl,hl                         ;[0ddc] 29
                    ld        (hl),a                        ;[0ddd] 77
                    inc       l                             ;[0dde] 2c
                    ld        a,b                           ;[0ddf] 78
                    cpl                                     ;[0de0] 2f
                    ld        a,c                           ;[0de1] 79
                    ld        ($357a),a                     ;[0de2] 32 7a 35
                    ld        a,e                           ;[0de5] 7b
                    jr        c,$0e64                       ;[0de6] 38 7c
                    dec       sp                            ;[0de8] 3b
                    ld        a,l                           ;[0de9] 7d
                    ld        a,$7e                         ;[0dea] 3e 7e
                    ld        b,c                           ;[0dec] 41
                    ld        a,a                           ;[0ded] 7f
                    ld        b,h                           ;[0dee] 44
                    add       b                             ;[0def] 80
                    ld        b,a                           ;[0df0] 47
                    add       c                             ;[0df1] 81
                    ld        c,d                           ;[0df2] 4a
                    add       d                             ;[0df3] 82
                    ld        c,l                           ;[0df4] 4d
                    add       e                             ;[0df5] 83
                    ld        d,b                           ;[0df6] 50
                    add       h                             ;[0df7] 84
                    ld        d,e                           ;[0df8] 53
                    add       l                             ;[0df9] 85
                    exx                                     ;[0dfa] d9
                    dec       b                             ;[0dfb] 05
                    ld        (bc),a                        ;[0dfc] 02
                    add       (hl)                          ;[0dfd] 86
                    dec       b                             ;[0dfe] 05
                    add       a                             ;[0dff] 87
                    ex        af,af'                        ;[0e00] 08
                    adc       b                             ;[0e01] 88
                    dec       bc                            ;[0e02] 0b
                    adc       c                             ;[0e03] 89
                    ld        c,$8a                         ;[0e04] 0e 8a
                    ld        de,$148b                      ;[0e06] 11 8b 14
                    adc       h                             ;[0e09] 8c
                    rla                                     ;[0e0a] 17
                    adc       l                             ;[0e0b] 8d
                    ld        a,(de)                        ;[0e0c] 1a
                    adc       (hl)                          ;[0e0d] 8e
                    dec       e                             ;[0e0e] 1d
                    adc       a                             ;[0e0f] 8f
                    jr        nz,$0da2                      ;[0e10] 20 90
                    inc       hl                            ;[0e12] 23
                    sub       c                             ;[0e13] 91
                    ld        h,$92                         ;[0e14] 26 92
                    add       hl,hl                         ;[0e16] 29
                    sub       e                             ;[0e17] 93
                    inc       l                             ;[0e18] 2c
                    sub       h                             ;[0e19] 94
                    cpl                                     ;[0e1a] 2f
                    sub       l                             ;[0e1b] 95
                    ld        ($3596),a                     ;[0e1c] 32 96 35
                    sub       a                             ;[0e1f] 97
                    jr        c,$0dba                       ;[0e20] 38 98
                    dec       sp                            ;[0e22] 3b
                    sbc       c                             ;[0e23] 99
                    ld        a,$9a                         ;[0e24] 3e 9a
                    ld        b,c                           ;[0e26] 41
                    sbc       e                             ;[0e27] 9b
                    ld        b,h                           ;[0e28] 44
                    sbc       h                             ;[0e29] 9c
                    ld        b,a                           ;[0e2a] 47
                    sbc       l                             ;[0e2b] 9d
                    ld        c,d                           ;[0e2c] 4a
                    sbc       (hl)                          ;[0e2d] 9e
                    jp        $8507                         ;[0e2e] c3 07 85
                    xor       e                             ;[0e31] ab
                    ld        c,c                           ;[0e32] 49
                    call      z,$a653                       ;[0e33] cc 53 a6
                    jp        $8508                         ;[0e36] c3 08 85
                    or        (hl)                          ;[0e39] b6
                    ld        c,c                           ;[0e3a] 49
                    rst       $38                           ;[0e3b] ff
                    ld        d,e                           ;[0e3c] 53
                    and       a                             ;[0e3d] a7
                    jp        $8509                         ;[0e3e] c3 09 85
                    xor       l                             ;[0e41] ad
                    ld        c,c                           ;[0e42] 49
                    and       l                             ;[0e43] a5
                    ld        d,e                           ;[0e44] 53
                    and       h                             ;[0e45] a4
                    jp        $850b                         ;[0e46] c3 0b 85
                    cp        b                             ;[0e49] b8
                    ld        c,c                           ;[0e4a] 49
                    ld        c,e                           ;[0e4b] 4b
                    ld        d,e                           ;[0e4c] 53
                    and       e                             ;[0e4d] a3
                    pop       bc                            ;[0e4e] c1
                    inc       c                             ;[0e4f] 0c
                    add       l                             ;[0e50] 85
                    cp        d                             ;[0e51] ba
                    pop       bc                            ;[0e52] c1
                    dec       c                             ;[0e53] 0d
                    add       l                             ;[0e54] 85
                    cp        h                             ;[0e55] bc
                    jp        $850f                         ;[0e56] c3 0f 85
                    jp        nz,$b349                      ;[0e59] c2 49 b3
                    ld        d,e                           ;[0e5c] 53
                    or        h                             ;[0e5d] b4
                    jp        $8510                         ;[0e5e] c3 10 85
                    ret       nz                            ;[0e61] c0
                    ld        c,c                           ;[0e62] 49
                    add       $53                           ;[0e63] c6 53
                    call      nz,$12c3                      ;[0e65] c4 c3 12
                    add       l                             ;[0e68] 85
                    or        c                             ;[0e69] b1
                    ld        b,a                           ;[0e6a] 47
                    xor       d                             ;[0e6b] aa
                    jp        z,$83a9                       ;[0e6c] ca a9 83
                    inc       d                             ;[0e6f] 14
                    add       l                             ;[0e70] 85
                    sbc       a                             ;[0e71] 9f
                    ld        b,a                           ;[0e72] 47
                    cp        a                             ;[0e73] bf
                    jp        z,$05be                       ;[0e74] ca be 05
                    ld        b,$07                         ;[0e77] 06 07
                    ex        af,af'                        ;[0e79] 08
                    add       hl,bc                         ;[0e7a] 09
                    ld        a,(bc)                        ;[0e7b] 0a
                    ld        (de),a                        ;[0e7c] 12
                    inc       de                            ;[0e7d] 13
                    inc       d                             ;[0e7e] 14
                    dec       d                             ;[0e7f] 15
                    ld        d,$17                         ;[0e80] 16 17
                    ld        ($2623),hl                    ;[0e82] 22 23 26
                    daa                                     ;[0e85] 27
                    cpl                                     ;[0e86] 2f
                    jr        nc,$0eba                      ;[0e87] 30 31
                    ld        ($3433),a                     ;[0e89] 32 33 34
                    ld        b,b                           ;[0e8c] 40
                    ld        b,d                           ;[0e8d] 42
                    ld        b,e                           ;[0e8e] 43
                    ld        c,d                           ;[0e8f] 4a
                    ld        c,e                           ;[0e90] 4b
                    ld        c,h                           ;[0e91] 4c
                    ld        d,b                           ;[0e92] 50
                    ld        d,c                           ;[0e93] 51
                    ld        d,d                           ;[0e94] 52
                    ld        d,e                           ;[0e95] 53
                    ld        d,h                           ;[0e96] 54
                    ld        d,l                           ;[0e97] 55
                    ld        d,(hl)                        ;[0e98] 56
                    ld        d,a                           ;[0e99] 57
                    ld        h,c                           ;[0e9a] 61
                    ld        h,d                           ;[0e9b] 62
                    ld        h,h                           ;[0e9c] 64
                    ld        l,b                           ;[0e9d] 68
                    ld        l,d                           ;[0e9e] 6a
                    ld        l,e                           ;[0e9f] 6b
                    ld        l,h                           ;[0ea0] 6c
                    ld        l,(hl)                        ;[0ea1] 6e
                    ld        l,a                           ;[0ea2] 6f
                    ld        (hl),b                        ;[0ea3] 70
                    ld        (hl),c                        ;[0ea4] 71
                    ld        a,a                           ;[0ea5] 7f
                    add       b                             ;[0ea6] 80
                    add       c                             ;[0ea7] 81
                    add       d                             ;[0ea8] 82
                    add       e                             ;[0ea9] 83
                    add       h                             ;[0eaa] 84
                    add       l                             ;[0eab] 85
                    add       (hl)                          ;[0eac] 86
                    add       a                             ;[0ead] 87
                    adc       b                             ;[0eae] 88
                    adc       c                             ;[0eaf] 89
                    adc       d                             ;[0eb0] 8a
                    adc       h                             ;[0eb1] 8c
                    adc       a                             ;[0eb2] 8f
                    sub       b                             ;[0eb3] 90
                    sub       c                             ;[0eb4] 91
                    sub       d                             ;[0eb5] 92
                    sub       e                             ;[0eb6] 93
                    and       b                             ;[0eb7] a0
                    and       d                             ;[0eb8] a2
                    xor       b                             ;[0eb9] a8
                    cp        b                             ;[0eba] b8
                    cp        c                             ;[0ebb] b9
                    cp        d                             ;[0ebc] ba
                    cp        e                             ;[0ebd] bb
                    ret       nz                            ;[0ebe] c0
                    call      nz,$c6c5                      ;[0ebf] c4 c5 c6
                    call      z,$cecd                       ;[0ec2] cc cd ce
                    ret       c                             ;[0ec5] d8
                    exx                                     ;[0ec6] d9
                    jp        nz,$00c3                      ;[0ec7] c2 c3 00
                    rlca                                    ;[0eca] 07
                    ex        af,af'                        ;[0ecb] 08
                    add       hl,bc                         ;[0ecc] 09
                    dec       bc                            ;[0ecd] 0b
                    inc       c                             ;[0ece] 0c
                    dec       c                             ;[0ecf] 0d
                    rrca                                    ;[0ed0] 0f
                    djnz      $0ee5                         ;[0ed1] 10 12
                    sub       h                             ;[0ed3] 94
                    pop       de                            ;[0ed4] d1
                    pop       de                            ;[0ed5] d1
                    ld        hl,$10f3                      ;[0ed6] 21 f3 10
                    call      $1093                         ;[0ed9] cd 93 10
                    ld        hl,$1e9a                      ;[0edc] 21 9a 1e
                    call      $11cb                         ;[0edf] cd cb 11
                    ld        b,$14                         ;[0ee2] 06 14
                    ld        de,$1ad0                      ;[0ee4] 11 d0 1a
                    call      $108a                         ;[0ee7] cd 8a 10
                    ld        a,$14                         ;[0eea] 3e 14
                    sub       b                             ;[0eec] 90
                    call      $105c                         ;[0eed] cd 5c 10
                    ld        de,$1ace                      ;[0ef0] 11 ce 1a
                    call      $108a                         ;[0ef3] cd 8a 10
                    call      $0fec                         ;[0ef6] cd ec 0f
                    ld        a,$0d                         ;[0ef9] 3e 0d
                    rst       $30                           ;[0efb] f7
                    add       hl,$0007                      ;[0efc] ed 34 07 00
                    djnz      $0ee4                         ;[0f00] 10 e2
                    ld        b,$14                         ;[0f02] 06 14
                    call      $0f14                         ;[0f04] cd 14 0f
                    ld        de,$14e0                      ;[0f07] 11 e0 14
                    jp        $1422                         ;[0f0a] c3 22 14
                    ld        hl,$3f2e                      ;[0f0d] 21 2e 3f
                    ld        a,(hl)                        ;[0f10] 7e
                    xor       $40                           ;[0f11] ee 40
                    ld        (hl),a                        ;[0f13] 77
                    ld        de,$0047                      ;[0f14] 11 47 00
                    call      $107e                         ;[0f17] cd 7e 10
                    ld        a,($3f2e)                     ;[0f1a] 3a 2e 3f
                    bit       6,a                           ;[0f1d] cb 77
                    ld        a,$64                         ;[0f1f] 3e 64
                    jr        nz,$0f24                      ;[0f21] 20 01
                    inc       a                             ;[0f23] 3c
                    rst       $30                           ;[0f24] f7
                    xor       a                             ;[0f25] af
                    ret                                     ;[0f26] c9

                    ld        a,b                           ;[0f27] 78
                    cp        $14                           ;[0f28] fe 14
                    ccf                                     ;[0f2a] 3f
                    ret       c                             ;[0f2b] d8
                    inc       b                             ;[0f2c] 04
                    ret                                     ;[0f2d] c9

                    call      $16f0                         ;[0f2e] cd f0 16
                    bit       6,c                           ;[0f31] cb 71
                    scf                                     ;[0f33] 37
                    ret       z                             ;[0f34] c8
                    pop       hl                            ;[0f35] e1
                    pop       hl                            ;[0f36] e1
                    ld        c,a                           ;[0f37] 4f
                    ex        de,hl                         ;[0f38] eb
                    jp        $07ef                         ;[0f39] c3 ef 07
                    call      $16f0                         ;[0f3c] cd f0 16
                    dec       hl                            ;[0f3f] 2b
                    dec       hl                            ;[0f40] 2b
                    res       7,(hl)                        ;[0f41] cb be
                    xor       a                             ;[0f43] af
                    ret                                     ;[0f44] c9

                    and       a                             ;[0f45] a7
                    dec       b                             ;[0f46] 05
                    ret       nz                            ;[0f47] c0
                    inc       b                             ;[0f48] 04
                    scf                                     ;[0f49] 37
                    ret                                     ;[0f4a] c9

                    call      $16f0                         ;[0f4b] cd f0 16
                    dec       hl                            ;[0f4e] 2b
                    dec       hl                            ;[0f4f] 2b
                    bit       6,(hl)                        ;[0f50] cb 76
                    scf                                     ;[0f52] 37
                    ret       z                             ;[0f53] c8
                    set       7,(hl)                        ;[0f54] cb fe
                    xor       a                             ;[0f56] af
                    ret                                     ;[0f57] c9

                    call      $0fe5                         ;[0f58] cd e5 0f
                    push      bc                            ;[0f5b] c5
                    ld        bc,$0282                      ;[0f5c] 01 82 02
                    call      $0b58                         ;[0f5f] cd 58 0b
                    pop       bc                            ;[0f62] c1
                    jr        z,$0fcc                       ;[0f63] 28 67
                    ld        a,e                           ;[0f65] 7b
                    cp        $e0                           ;[0f66] fe e0
                    ccf                                     ;[0f68] 3f
                    ret       c                             ;[0f69] d8
                    push      af                            ;[0f6a] f5
                    ld        a,$20                         ;[0f6b] 3e 20
                    rst       $30                           ;[0f6d] f7
                    call      $0b4f                         ;[0f6e] cd 4f 0b
                    ex        de,hl                         ;[0f71] eb
                    pop       de                            ;[0f72] d1
                    jr        z,$0fcc                       ;[0f73] 28 57
                    ld        a,d                           ;[0f75] 7a
                    jr        $0f89                         ;[0f76] 18 11
                    call      $0fe5                         ;[0f78] cd e5 0f
                    call      $0b4f                         ;[0f7b] cd 4f 0b
                    ld        a,d                           ;[0f7e] 7a
                    and       $c0                           ;[0f7f] e6 c0
                    scf                                     ;[0f81] 37
                    ret       z                             ;[0f82] c8
                    ex        de,hl                         ;[0f83] eb
                    push      bc                            ;[0f84] c5
                    call      $0047                         ;[0f85] cd 47 00
                    pop       bc                            ;[0f88] c1
                    ld        c,a                           ;[0f89] 4f
                    ld        a,h                           ;[0f8a] 7c
                    cp        $1f                           ;[0f8b] fe 1f
                    jr        c,$0f96                       ;[0f8d] 38 07
                    scf                                     ;[0f8f] 37
                    ret       nz                            ;[0f90] c0
                    ld        a,l                           ;[0f91] 7d
                    cp        $fa                           ;[0f92] fe fa
                    ccf                                     ;[0f94] 3f
                    ret       c                             ;[0f95] d8
                    push      bc                            ;[0f96] c5
                    ld        b,$14                         ;[0f97] 06 14
                    push      hl                            ;[0f99] e5
                    push      bc                            ;[0f9a] c5
                    call      $16f0                         ;[0f9b] cd f0 16
                    bit       6,c                           ;[0f9e] cb 71
                    pop       bc                            ;[0fa0] c1
                    pop       hl                            ;[0fa1] e1
                    jr        z,$0fb6                       ;[0fa2] 28 12
                    cp        c                             ;[0fa4] b9
                    jr        nz,$0fb6                      ;[0fa5] 20 0f
                    call      $0fce                         ;[0fa7] cd ce 0f
                    jr        c,$0fb6                       ;[0faa] 38 0a
                    ex        de,hl                         ;[0fac] eb
                    call      $0fce                         ;[0fad] cd ce 0f
                    ex        de,hl                         ;[0fb0] eb
                    jr        c,$0fb6                       ;[0fb1] 38 03
                    pop       bc                            ;[0fb3] c1
                    scf                                     ;[0fb4] 37
                    ret                                     ;[0fb5] c9

                    djnz      $0f99                         ;[0fb6] 10 e1
                    pop       bc                            ;[0fb8] c1
                    push      hl                            ;[0fb9] e5
                    push      bc                            ;[0fba] c5
                    push      de                            ;[0fbb] d5
                    call      $16f0                         ;[0fbc] cd f0 16
                    pop       de                            ;[0fbf] d1
                    pop       bc                            ;[0fc0] c1
                    dec       hl                            ;[0fc1] 2b
                    ld        (hl),c                        ;[0fc2] 71
                    dec       hl                            ;[0fc3] 2b
                    pop       de                            ;[0fc4] d1
                    set       7,d                           ;[0fc5] cb fa
                    set       6,d                           ;[0fc7] cb f2
                    ld        (hl),d                        ;[0fc9] 72
                    dec       hl                            ;[0fca] 2b
                    ld        (hl),e                        ;[0fcb] 73
                    xor       a                             ;[0fcc] af
                    ret                                     ;[0fcd] c9

                    push      hl                            ;[0fce] e5
                    add       hl,$0006                      ;[0fcf] ed 34 06 00
                    and       a                             ;[0fd3] a7
                    sbc       hl,de                         ;[0fd4] ed 52
                    pop       hl                            ;[0fd6] e1
                    ret                                     ;[0fd7] c9

                    push      de                            ;[0fd8] d5
                    call      $0fec                         ;[0fd9] cd ec 0f
                    ld        e,$03                         ;[0fdc] 1e 03
                    call      $1018                         ;[0fde] cd 18 10
                    pop       de                            ;[0fe1] d1
                    jp        $108a                         ;[0fe2] c3 8a 10
                    call      $16f0                         ;[0fe5] cd f0 16
                    dec       hl                            ;[0fe8] 2b
                    dec       hl                            ;[0fe9] 2b
                    ld        (hl),$00                      ;[0fea] 36 00
                    call      $1016                         ;[0fec] cd 16 10
                    push      bc                            ;[0fef] c5
                    call      $0b3f                         ;[0ff0] cd 3f 0b
                    pop       bc                            ;[0ff3] c1
                    call      $1016                         ;[0ff4] cd 16 10
                    call      $16f0                         ;[0ff7] cd f0 16
                    bit       6,c                           ;[0ffa] cb 71
                    ret       z                             ;[0ffc] c8
                    ex        de,hl                         ;[0ffd] eb
                    push      af                            ;[0ffe] f5
                    ld        a,$64                         ;[0fff] 3e 64
                    bit       7,c                           ;[1001] cb 79
                    jr        z,$1006                       ;[1003] 28 01
                    inc       a                             ;[1005] 3c
                    rst       $30                           ;[1006] f7
                    ld        a,$20                         ;[1007] 3e 20
                    rst       $30                           ;[1009] f7
                    pop       af                            ;[100a] f1
                    call      $105c                         ;[100b] cd 5c 10
                    ld        a,$20                         ;[100e] 3e 20
                    rst       $30                           ;[1010] f7
                    call      $1044                         ;[1011] cd 44 10
                    xor       a                             ;[1014] af
                    ret                                     ;[1015] c9

                    ld        e,$05                         ;[1016] 1e 05
                    ld        a,$14                         ;[1018] 3e 14
                    sub       b                             ;[101a] 90
                    ld        d,a                           ;[101b] 57
                    call      $107e                         ;[101c] cd 7e 10
                    ret                                     ;[101f] c9

                    call      $107e                         ;[1020] cd 7e 10
                    ld        de,$1ad0                      ;[1023] 11 d0 1a
                    call      $108a                         ;[1026] cd 8a 10
                    ld        a,(hl)                        ;[1029] 7e
                    bit       7,c                           ;[102a] cb 79
                    jr        nz,$102f                      ;[102c] 20 01
                    ld        a,c                           ;[102e] 79
                    call      $105c                         ;[102f] cd 5c 10
                    ld        de,$1ace                      ;[1032] 11 ce 1a
                    call      $108a                         ;[1035] cd 8a 10
                    inc       hl                            ;[1038] 23
                    inc       c                             ;[1039] 0c
                    ld        a,$20                         ;[103a] 3e 20
                    rst       $30                           ;[103c] f7
                    djnz      $1023                         ;[103d] 10 e4
                    ret                                     ;[103f] c9

                    ld        e,(hl)                        ;[1040] 5e
                    inc       hl                            ;[1041] 23
                    ld        d,(hl)                        ;[1042] 56
                    ex        de,hl                         ;[1043] eb
                    ld        a,h                           ;[1044] 7c
                    rrca                                    ;[1045] 0f
                    rrca                                    ;[1046] 0f
                    rrca                                    ;[1047] 0f
                    rrca                                    ;[1048] 0f
                    call      $1065                         ;[1049] cd 65 10
                    ld        a,h                           ;[104c] 7c
                    call      $1065                         ;[104d] cd 65 10
                    ld        a,l                           ;[1050] 7d
                    rrca                                    ;[1051] 0f
                    rrca                                    ;[1052] 0f
                    rrca                                    ;[1053] 0f
                    rrca                                    ;[1054] 0f
                    call      $1065                         ;[1055] cd 65 10
                    ld        a,l                           ;[1058] 7d
                    jr        $1065                         ;[1059] 18 0a
                    ld        a,(hl)                        ;[105b] 7e
                    push      af                            ;[105c] f5
                    rrca                                    ;[105d] 0f
                    rrca                                    ;[105e] 0f
                    rrca                                    ;[105f] 0f
                    rrca                                    ;[1060] 0f
                    call      $1065                         ;[1061] cd 65 10
                    pop       af                            ;[1064] f1
                    and       $0f                           ;[1065] e6 0f
                    cp        $0a                           ;[1067] fe 0a
                    sbc       $69                           ;[1069] de 69
                    daa                                     ;[106b] 27
                    rst       $30                           ;[106c] f7
                    ret                                     ;[106d] c9

                    ld        hl,$2f0a                      ;[106e] 21 0a 2f
                    sub       l                             ;[1071] 95
                    inc       h                             ;[1072] 24
                    jr        nc,$1071                      ;[1073] 30 fc
                    add       l                             ;[1075] 85
                    ld        l,a                           ;[1076] 6f
                    ld        a,h                           ;[1077] 7c
                    rst       $30                           ;[1078] f7
                    ld        a,l                           ;[1079] 7d
                    add       $30                           ;[107a] c6 30
                    rst       $30                           ;[107c] f7
                    ret                                     ;[107d] c9

                    push      de                            ;[107e] d5
                    ld        a,$16                         ;[107f] 3e 16
                    rst       $30                           ;[1081] f7
                    pop       de                            ;[1082] d1
                    push      de                            ;[1083] d5
                    ld        a,d                           ;[1084] 7a
                    rst       $30                           ;[1085] f7
                    pop       de                            ;[1086] d1
                    ld        a,e                           ;[1087] 7b
                    rst       $30                           ;[1088] f7
                    ret                                     ;[1089] c9

                    push      bc                            ;[108a] c5
                    push      hl                            ;[108b] e5
                    ex        de,hl                         ;[108c] eb
                    call      $11cb                         ;[108d] cd cb 11
                    pop       hl                            ;[1090] e1
                    pop       bc                            ;[1091] c1
                    ret                                     ;[1092] c9

                    push      hl                            ;[1093] e5
                    ld        a,($5c8d)                     ;[1094] 3a 8d 5c
                    cpl                                     ;[1097] 2f
                    and       $38                           ;[1098] e6 38
                    add       $06                           ;[109a] c6 06
                    out       ($ff),a                       ;[109c] d3 ff
                    ld        (iy+$45),$09                  ;[109e] fd 36 45 09
                    ld        bc,$5b4d                      ;[10a2] 01 4d 5b
                    call      $10ca                         ;[10a5] cd ca 10
                    ld        hl,$fb00                      ;[10a8] 21 00 fb
                    ld        ($3f30),hl                    ;[10ab] 22 30 3f
                    pop       hl                            ;[10ae] e1
                    ld        a,$0e                         ;[10af] 3e 0e
                    rst       $30                           ;[10b1] f7
                    call      $11aa                         ;[10b2] cd aa 11
                    ld        a,$1a                         ;[10b5] 3e 1a
                    rst       $30                           ;[10b7] f7
                    xor       a                             ;[10b8] af
                    rst       $30                           ;[10b9] f7
                    ld        a,$1e                         ;[10ba] 3e 1e
                    rst       $30                           ;[10bc] f7
                    ld        a,$06                         ;[10bd] 3e 06
                    rst       $30                           ;[10bf] f7
                    ret                                     ;[10c0] c9

                    xor       a                             ;[10c1] af
                    out       ($ff),a                       ;[10c2] d3 ff
                    ld        (iy+$45),a                    ;[10c4] fd 77 45
                    ld        bc,$09f4                      ;[10c7] 01 f4 09
                    scf                                     ;[10ca] 37
                    rst       $18                           ;[10cb] df
                    sub       (hl)                          ;[10cc] 96
                    inc       d                             ;[10cd] 14
                    ld        hl,$f700                      ;[10ce] 21 00 f7
                    ld        ($3f30),hl                    ;[10d1] 22 30 3f
                    ret                                     ;[10d4] c9

                    ld        b,h                           ;[10d5] 44
                    ld        h,l                           ;[10d6] 65
                    ld        h,d                           ;[10d7] 62
                    ld        (hl),l                        ;[10d8] 75
                    ld        h,a                           ;[10d9] 67
                    dec       c                             ;[10da] 0d
                    ld        d,e                           ;[10db] 53
                    ld        e,a                           ;[10dc] 5f
                    ld        (hl),h                        ;[10dd] 74
                    ld        h,c                           ;[10de] 61
                    ld        (hl),h                        ;[10df] 74
                    ld        (hl),l                        ;[10e0] 75
                    ld        (hl),e                        ;[10e1] 73
                    dec       c                             ;[10e2] 0d
                    ld        c,l                           ;[10e3] 4d
                    ld        e,a                           ;[10e4] 5f
                    ld        h,l                           ;[10e5] 65
                    ld        l,l                           ;[10e6] 6d
                    ld        l,a                           ;[10e7] 6f
                    ld        (hl),d                        ;[10e8] 72
                    ld        a,c                           ;[10e9] 79
                    jr        nz,$112e                      ;[10ea] 20 42
                    ld        (hl),d                        ;[10ec] 72
                    ld        l,a                           ;[10ed] 6f
                    ld        (hl),a                        ;[10ee] 77
                    ld        (hl),e                        ;[10ef] 73
                    ld        h,l                           ;[10f0] 65
                    ld        (hl),d                        ;[10f1] 72
                    dec       c                             ;[10f2] 0d
                    ld        b,d                           ;[10f3] 42
                    ld        e,a                           ;[10f4] 5f
                    ld        (hl),d                        ;[10f5] 72
                    ld        h,l                           ;[10f6] 65
                    ld        h,c                           ;[10f7] 61
                    ld        l,e                           ;[10f8] 6b
                    ld        (hl),b                        ;[10f9] 70
                    ld        l,a                           ;[10fa] 6f
                    ld        l,c                           ;[10fb] 69
                    ld        l,(hl)                        ;[10fc] 6e
                    ld        (hl),h                        ;[10fd] 74
                    ld        (hl),e                        ;[10fe] 73
                    dec       c                             ;[10ff] 0d
                    ld        d,e                           ;[1100] 53
                    ld        h,c                           ;[1101] 61
                    halt                                    ;[1102] 76
                    ld        h,l                           ;[1103] 65
                    jr        nz,$113e                      ;[1104] 20 38
                    ld        c,e                           ;[1106] 4b
                    ld        e,a                           ;[1107] 5f
                    jr        nz,$114c                      ;[1108] 20 42
                    ld        h,c                           ;[110a] 61
                    ld        l,(hl)                        ;[110b] 6e
                    ld        l,e                           ;[110c] 6b
                    ld        (hl),e                        ;[110d] 73
                    dec       c                             ;[110e] 0d
                    xor       a                             ;[110f] af
                    ld        ($3e97),a                     ;[1110] 32 97 3e
                    call      $11f5                         ;[1113] cd f5 11
                    push      de                            ;[1116] d5
                    ld        h,b                           ;[1117] 60
                    ld        l,c                           ;[1118] 69
                    ld        de,$5f80                      ;[1119] 11 80 5f
                    push      de                            ;[111c] d5
                    call      $11f8                         ;[111d] cd f8 11
                    call      $114c                         ;[1120] cd 4c 11
                    call      $05c5                         ;[1123] cd c5 05
                    ld        a,b                           ;[1126] 78
                    cp        $02                           ;[1127] fe 02
                    ld        a,$00                         ;[1129] 3e 00
                    jr        z,$1131                       ;[112b] 28 04
                    ld        a,$40                         ;[112d] 3e 40
                    ld        b,$08                         ;[112f] 06 08
                    pop       de                            ;[1131] d1
                    pop       hl                            ;[1132] e1
                    rst       $10                           ;[1133] d7
                    ld        d,d                           ;[1134] 52
                    inc       l                             ;[1135] 2c
                    ex        de,hl                         ;[1136] eb
                    scf                                     ;[1137] 37
                    ret       z                             ;[1138] c8
                    call      $1140                         ;[1139] cd 40 11
                    ld        a,$00                         ;[113c] 3e 00
                    inc       a                             ;[113e] 3c
                    ret                                     ;[113f] c9

                    ld        a,$7f                         ;[1140] 3e 7f
                    in        a,($fe)                       ;[1142] db fe
                    rra                                     ;[1144] 1f
                    ret       c                             ;[1145] d8
                    ld        a,$fe                         ;[1146] 3e fe
                    in        a,($fe)                       ;[1148] db fe
                    rra                                     ;[114a] 1f
                    ret                                     ;[114b] c9

                    xor       a                             ;[114c] af
                    call      $1152                         ;[114d] cd 52 11
                    ld        a,$01                         ;[1150] 3e 01
                    ld        de,$0000                      ;[1152] 11 00 00
                    push      de                            ;[1155] d5
                    pop       ix                            ;[1156] dd e1
                    push      af                            ;[1158] f5
                    call      $1572                         ;[1159] cd 72 15
                    add       (hl)                          ;[115c] 86
                    pop       af                            ;[115d] f1
                    call      $1572                         ;[115e] cd 72 15
                    add       a                             ;[1161] 87
                    ret                                     ;[1162] c9

                    push      bc                            ;[1163] c5
                    ld        e,$00                         ;[1164] 1e 00
                    jr        nz,$116e                      ;[1166] 20 06
                    push      de                            ;[1168] d5
                    rst       $10                           ;[1169] d7
                    ld        b,l                           ;[116a] 45
                    dec       sp                            ;[116b] 3b
                    pop       af                            ;[116c] f1
                    ld        d,a                           ;[116d] 57
                    pop       hl                            ;[116e] e1
                    push      de                            ;[116f] d5
                    call      $11f5                         ;[1170] cd f5 11
                    ex        de,hl                         ;[1173] eb
                    pop       de                            ;[1174] d1
                    rst       $10                           ;[1175] d7
                    ld        c,l                           ;[1176] 4d
                    add       hl,sp                         ;[1177] 39
                    ret                                     ;[1178] c9

                    push      hl                            ;[1179] e5
                    ld        a,(hl)                        ;[117a] 7e
                    inc       hl                            ;[117b] 23
                    inc       a                             ;[117c] 3c
                    jr        nz,$117a                      ;[117d] 20 fb
                    dec       hl                            ;[117f] 2b
                    push      hl                            ;[1180] e5
                    push      de                            ;[1181] d5
                    ld        b,$04                         ;[1182] 06 04
                    dec       hl                            ;[1184] 2b
                    ld        c,(hl)                        ;[1185] 4e
                    set       5,c                           ;[1186] cb e9
                    ld        a,(de)                        ;[1188] 1a
                    dec       de                            ;[1189] 1b
                    cp        c                             ;[118a] b9
                    jr        nz,$1193                      ;[118b] 20 06
                    djnz      $1184                         ;[118d] 10 f5
                    pop       de                            ;[118f] d1
                    pop       hl                            ;[1190] e1
                    pop       hl                            ;[1191] e1
                    ret                                     ;[1192] c9

                    pop       hl                            ;[1193] e1
                    add       hl,$fffd                      ;[1194] ed 34 fd ff
                    pop       de                            ;[1198] d1
                    ld        bc,$0005                      ;[1199] 01 05 00
                    ldir                                    ;[119c] ed b0
                    pop       hl                            ;[119e] e1
                    ret                                     ;[119f] c9

                    push      hl                            ;[11a0] e5
                    ld        a,(hl)                        ;[11a1] 7e
                    inc       hl                            ;[11a2] 23
                    inc       a                             ;[11a3] 3c
                    jr        nz,$11a1                      ;[11a4] 20 fb
                    dec       hl                            ;[11a6] 2b
                    ld        (hl),a                        ;[11a7] 77
                    pop       hl                            ;[11a8] e1
                    ret                                     ;[11a9] c9

                    ld        de,$5eff                      ;[11aa] 11 ff 5e
                    push      de                            ;[11ad] d5
                    inc       de                            ;[11ae] 13
                    ld        a,(hl)                        ;[11af] 7e
                    inc       hl                            ;[11b0] 23
                    cp        $5f                           ;[11b1] fe 5f
                    jr        z,$11af                       ;[11b3] 28 fa
                    ld        (de),a                        ;[11b5] 12
                    cp        $20                           ;[11b6] fe 20
                    jr        nc,$11ae                      ;[11b8] 30 f4
                    set       7,a                           ;[11ba] cb ff
                    ld        (de),a                        ;[11bc] 12
                    pop       hl                            ;[11bd] e1
                    inc       hl                            ;[11be] 23
                    rst       $10                           ;[11bf] d7
                    ld        h,c                           ;[11c0] 61
                    ccf                                     ;[11c1] 3f
                    ret                                     ;[11c2] c9

                    call      $11f5                         ;[11c3] cd f5 11
                    ex        de,hl                         ;[11c6] eb
                    rst       $10                           ;[11c7] d7
                    inc       sp                            ;[11c8] 33
                    inc       sp                            ;[11c9] 33
                    ret                                     ;[11ca] c9

                    ld        ix,($3f30)                    ;[11cb] dd 2a 30 3f
                    call      $11f5                         ;[11cf] cd f5 11
                    ex        de,hl                         ;[11d2] eb
                    rst       $10                           ;[11d3] d7
                    ld        c,a                           ;[11d4] 4f
                    daa                                     ;[11d5] 27
                    ret                                     ;[11d6] c9

                    ld        a,(hl)                        ;[11d7] 7e
                    inc       hl                            ;[11d8] 23
                    cp        $20                           ;[11d9] fe 20
                    jr        c,$11e1                       ;[11db] 38 04
                    cp        $80                           ;[11dd] fe 80
                    jr        c,$11e3                       ;[11df] 38 02
                    ld        a,$3f                         ;[11e1] 3e 3f
                    rst       $28                           ;[11e3] ef
                    djnz      $11e6                         ;[11e4] 10 00
                    djnz      $11d7                         ;[11e6] 10 ef
                    ret                                     ;[11e8] c9

                    ld        a,(hl)                        ;[11e9] 7e
                    and       $7f                           ;[11ea] e6 7f
                    rst       $28                           ;[11ec] ef
                    djnz      $11ef                         ;[11ed] 10 00
                    bit       7,(hl)                        ;[11ef] cb 7e
                    ret       nz                            ;[11f1] c0
                    inc       hl                            ;[11f2] 23
                    jr        $11e9                         ;[11f3] 18 f4
                    ld        de,$5f00                      ;[11f5] 11 00 5f
                    push      de                            ;[11f8] d5
                    ld        a,(hl)                        ;[11f9] 7e
                    inc       hl                            ;[11fa] 23
                    bit       7,a                           ;[11fb] cb 7f
                    jr        nz,$1203                      ;[11fd] 20 04
                    ld        (de),a                        ;[11ff] 12
                    inc       de                            ;[1200] 13
                    jr        $11f9                         ;[1201] 18 f6
                    and       $7f                           ;[1203] e6 7f
                    cp        $40                           ;[1205] fe 40
                    jr        c,$120d                       ;[1207] 38 04
                    cp        $60                           ;[1209] fe 60
                    jr        c,$1214                       ;[120b] 38 07
                    ld        (de),a                        ;[120d] 12
                    inc       de                            ;[120e] 13
                    ex        de,hl                         ;[120f] eb
                    ld        (hl),$ff                      ;[1210] 36 ff
                    pop       de                            ;[1212] d1
                    ret                                     ;[1213] c9

                    push      hl                            ;[1214] e5
                    ld        hl,$1acd                      ;[1215] 21 cd 1a
                    sub       $3f                           ;[1218] d6 3f
                    bit       7,(hl)                        ;[121a] cb 7e
                    inc       hl                            ;[121c] 23
                    jr        z,$121a                       ;[121d] 28 fb
                    dec       a                             ;[121f] 3d
                    jr        nz,$121a                      ;[1220] 20 f8
                    call      $11f8                         ;[1222] cd f8 11
                    ex        de,hl                         ;[1225] eb
                    pop       hl                            ;[1226] e1
                    jr        $11f9                         ;[1227] 18 d0
                    ld        de,$0002                      ;[1229] 11 02 00
                    ld        c,$05                         ;[122c] 0e 05
                    jr        $1235                         ;[122e] 18 05
                    ld        de,$0203                      ;[1230] 11 03 02
                    ld        c,$02                         ;[1233] 0e 02
                    ld        a,$ff                         ;[1235] 3e ff
                    ld        ($3f35),a                     ;[1237] 32 35 3f
                    ld        b,$0f                         ;[123a] 06 0f
                    push      bc                            ;[123c] c5
                    push      de                            ;[123d] d5
                    push      hl                            ;[123e] e5
                    rst       $20                           ;[123f] e7
                    ld        b,$01                         ;[1240] 06 01
                    pop       hl                            ;[1242] e1
                    pop       de                            ;[1243] d1
                    pop       bc                            ;[1244] c1
                    jr        c,$124f                       ;[1245] 38 08
                    cp        $1d                           ;[1247] fe 1d
                    scf                                     ;[1249] 37
                    ccf                                     ;[124a] 3f
                    ret       nz                            ;[124b] c0
                    djnz      $123c                         ;[124c] 10 ee
                    ret                                     ;[124e] c9

                    ld        a,b                           ;[124f] 78
                    ld        ($3f35),a                     ;[1250] 32 35 3f
                    ret                                     ;[1253] c9

                    ld        a,($3f35)                     ;[1254] 3a 35 3f
                    ld        b,a                           ;[1257] 47
                    rst       $20                           ;[1258] e7
                    add       hl,bc                         ;[1259] 09
                    ld        bc,$11c9                      ;[125a] 01 c9 11
                    nop                                     ;[125d] 00
                    ld        e,a                           ;[125e] 5f
                    push      bc                            ;[125f] c5
                    push      de                            ;[1260] d5
                    ldir                                    ;[1261] ed b0
                    pop       hl                            ;[1263] e1
                    pop       bc                            ;[1264] c1
                    ld        d,b                           ;[1265] 50
                    ld        e,c                           ;[1266] 59
                    ld        a,($3f35)                     ;[1267] 3a 35 3f
                    ld        b,a                           ;[126a] 47
                    ld        c,$07                         ;[126b] 0e 07
                    rst       $20                           ;[126d] e7
                    dec       d                             ;[126e] 15
                    ld        bc,$c5c9                      ;[126f] 01 c9 c5
                    ld        hl,$ffff                      ;[1272] 21 ff ff
                    ld        ($3e54),hl                    ;[1275] 22 54 3e
                    ld        a,b                           ;[1278] 78
                    ld        ($3e56),a                     ;[1279] 32 56 3e
                    ld        hl,$3e54                      ;[127c] 21 54 3e
                    ld        bc,$0003                      ;[127f] 01 03 00
                    call      $125c                         ;[1282] cd 5c 12
                    pop       bc                            ;[1285] c1
                    ret       nc                            ;[1286] d0
                    push      bc                            ;[1287] c5
                    ld        a,c                           ;[1288] 79
                    add       a                             ;[1289] 87
                    call      $1292                         ;[128a] cd 92 12
                    pop       bc                            ;[128d] c1
                    ret       nc                            ;[128e] d0
                    ld        a,c                           ;[128f] 79
                    scf                                     ;[1290] 37
                    adc       a                             ;[1291] 8f
                    ld        bc,$2000                      ;[1292] 01 00 20
                    ld        hl,$0000                      ;[1295] 21 00 00
                    push      hl                            ;[1298] e5
                    push      af                            ;[1299] f5
                    push      bc                            ;[129a] c5
                    call      $1609                         ;[129b] cd 09 16
                    bit       7,h                           ;[129e] cb 7c
                    jr        nz,$12b1                      ;[12a0] 20 0f
                    ld        bc,$0100                      ;[12a2] 01 00 01
                    call      $1265                         ;[12a5] cd 65 12
                    pop       bc                            ;[12a8] c1
                    pop       de                            ;[12a9] d1
                    pop       hl                            ;[12aa] e1
                    ret       nc                            ;[12ab] d0
                    inc       h                             ;[12ac] 24
                    ld        a,d                           ;[12ad] 7a
                    djnz      $1298                         ;[12ae] 10 e8
                    ret                                     ;[12b0] c9

                    pop       bc                            ;[12b1] c1
                    pop       af                            ;[12b2] f1
                    pop       af                            ;[12b3] f1
                    jr        $1265                         ;[12b4] 18 af
                    ld        a,($3e97)                     ;[12b6] 3a 97 3e
                    cp        d                             ;[12b9] ba
                    ret       z                             ;[12ba] c8
                    push      de                            ;[12bb] d5
                    call      $114c                         ;[12bc] cd 4c 11
                    xor       a                             ;[12bf] af
                    ld        ($3e97),a                     ;[12c0] 32 97 3e
                    ld        hl,$f720                      ;[12c3] 21 20 f7
                    ld        de,$f721                      ;[12c6] 11 21 f7
                    ld        bc,$07df                      ;[12c9] 01 df 07
                    ld        (hl),$ff                      ;[12cc] 36 ff
                    ldir                                    ;[12ce] ed b0
                    ld        hl,$1f42                      ;[12d0] 21 42 1f
                    call      $11f5                         ;[12d3] cd f5 11
                    pop       af                            ;[12d6] f1
                    push      af                            ;[12d7] f5
                    add       $30                           ;[12d8] c6 30
                    ld        ($5f10),a                     ;[12da] 32 10 5f
                    ex        de,hl                         ;[12dd] eb
                    call      $1229                         ;[12de] cd 29 12
                    jr        nc,$1348                      ;[12e1] 30 65
                    ld        hl,$f720                      ;[12e3] 21 20 f7
                    ld        de,$07e0                      ;[12e6] 11 e0 07
                    ld        a,($3f35)                     ;[12e9] 3a 35 3f
                    ld        b,a                           ;[12ec] 47
                    ld        c,$07                         ;[12ed] 0e 07
                    rst       $20                           ;[12ef] e7
                    ld        (de),a                        ;[12f0] 12
                    ld        bc,$54cd                      ;[12f1] 01 cd 54
                    ld        (de),a                        ;[12f4] 12
                    ld        hl,($f720)                    ;[12f5] 2a 20 f7
                    add       hl,$f71e                      ;[12f8] ed 34 1e f7
                    ld        c,(hl)                        ;[12fc] 4e
                    inc       hl                            ;[12fd] 23
                    ld        b,(hl)                        ;[12fe] 46
                    ld        hl,$0209                      ;[12ff] 21 09 02
                    and       a                             ;[1302] a7
                    sbc       hl,bc                         ;[1303] ed 42
                    jr        z,$1342                       ;[1305] 28 3b
                    push      bc                            ;[1307] c5
                    ld        hl,$1b9c                      ;[1308] 21 9c 1b
                    ld        b,$11                         ;[130b] 06 11
                    call      $11d7                         ;[130d] cd d7 11
                    ld        a,$0d                         ;[1310] 3e 0d
                    rst       $28                           ;[1312] ef
                    djnz      $1315                         ;[1313] 10 00
                    ld        hl,$1bad                      ;[1315] 21 ad 1b
                    ld        b,$11                         ;[1318] 06 11
                    call      $11d7                         ;[131a] cd d7 11
                    ld        a,$0d                         ;[131d] 3e 0d
                    rst       $28                           ;[131f] ef
                    djnz      $1322                         ;[1320] 10 00
                    pop       hl                            ;[1322] e1
                    ld        b,$04                         ;[1323] 06 04
                    ld        a,h                           ;[1325] 7c
                    swapnib                                 ;[1326] ed 23
                    and       $0f                           ;[1328] e6 0f
                    add       $30                           ;[132a] c6 30
                    cp        $3a                           ;[132c] fe 3a
                    jr        c,$1332                       ;[132e] 38 02
                    add       $07                           ;[1330] c6 07
                    rst       $28                           ;[1332] ef
                    djnz      $1335                         ;[1333] 10 00
                    add       hl,hl                         ;[1335] 29
                    add       hl,hl                         ;[1336] 29
                    add       hl,hl                         ;[1337] 29
                    add       hl,hl                         ;[1338] 29
                    djnz      $1325                         ;[1339] 10 ea
                    ld        hl,$1bfe                      ;[133b] 21 fe 1b
                    ld        b,$05                         ;[133e] 06 05
                    jr        $134d                         ;[1340] 18 0b
                    pop       af                            ;[1342] f1
                    ld        ($3e97),a                     ;[1343] 32 97 3e
                    and       a                             ;[1346] a7
                    ret                                     ;[1347] c9

                    ld        hl,$1bf1                      ;[1348] 21 f1 1b
                    ld        b,$12                         ;[134b] 06 12
                    call      $11d7                         ;[134d] cd d7 11
                    pop       af                            ;[1350] f1
                    add       $30                           ;[1351] c6 30
                    rst       $28                           ;[1353] ef
                    djnz      $1356                         ;[1354] 10 00
                    ld        hl,$1c03                      ;[1356] 21 03 1c
                    ld        b,$04                         ;[1359] 06 04
                    call      $11d7                         ;[135b] cd d7 11
                    rst       $10                           ;[135e] d7
                    jr        $139f                         ;[135f] 18 3e
                    scf                                     ;[1361] 37
                    ret                                     ;[1362] c9

                    ld        hl,$3a54                      ;[1363] 21 54 3a
                    ld        de,$5e00                      ;[1366] 11 00 5e
                    ld        bc,$0100                      ;[1369] 01 00 01
                    push      de                            ;[136c] d5
                    ldir                                    ;[136d] ed b0
                    ld        a,($3a52)                     ;[136f] 3a 52 3a
                    push      af                            ;[1372] f5
                    in        a,($bf)                       ;[1373] db bf
                    pop       af                            ;[1375] f1
                    call      $012d                         ;[1376] cd 2d 01
                    pop       hl                            ;[1379] e1
                    ld        a,$00                         ;[137a] 3e 00
                    call      $01b1                         ;[137c] cd b1 01
                    nextreg $55,$10                         ;[137f] ed 91 55 10
                    ld        a,$8b                         ;[1383] 3e 8b
                    out       ($e3),a                       ;[1385] d3 e3
                    ld        hl,$2000                      ;[1387] 21 00 20
                    ld        de,$a000                      ;[138a] 11 00 a0
                    ld        b,h                           ;[138d] 44
                    ld        c,l                           ;[138e] 4d
                    ldir                                    ;[138f] ed b0
                    in        a,($3f)                       ;[1391] db 3f
                    ret                                     ;[1393] c9

                    ld        hl,$3916                      ;[1394] 21 16 39
                    ld        de,$5e00                      ;[1397] 11 00 5e
                    ld        bc,$013c                      ;[139a] 01 3c 01
                    push      bc                            ;[139d] c5
                    push      de                            ;[139e] d5
                    ldir                                    ;[139f] ed b0
                    in        a,($bf)                       ;[13a1] db bf
                    pop       hl                            ;[13a3] e1
                    pop       bc                            ;[13a4] c1
                    ld        de,$34de                      ;[13a5] 11 de 34
                    ld        a,$8c                         ;[13a8] 3e 8c
                    out       ($e3),a                       ;[13aa] d3 e3
                    ldir                                    ;[13ac] ed b0
                    in        a,($bf)                       ;[13ae] db bf
                    ld        bc,$7ffd                      ;[13b0] 01 fd 7f
                    ld        a,$07                         ;[13b3] 3e 07
                    out       (c),a                         ;[13b5] ed 79
                    ld        a,$86                         ;[13b7] 3e 86
                    out       ($e3),a                       ;[13b9] d3 e3
                    ld        hl,$2000                      ;[13bb] 21 00 20
                    ld        de,$4000                      ;[13be] 11 00 40
                    ld        bc,$1b00                      ;[13c1] 01 00 1b
                    ldir                                    ;[13c4] ed b0
                    ld        hl,$3e00                      ;[13c6] 21 00 3e
                    ld        de,$5e00                      ;[13c9] 11 00 5e
                    ld        bc,$0200                      ;[13cc] 01 00 02
                    ldir                                    ;[13cf] ed b0
                    ld        a,$84                         ;[13d1] 3e 84
                    out       ($e3),a                       ;[13d3] d3 e3
                    ld        hl,$2000                      ;[13d5] 21 00 20
                    push      hl                            ;[13d8] e5
                    ld        de,$c000                      ;[13d9] 11 00 c0
                    ld        b,h                           ;[13dc] 44
                    ld        c,l                           ;[13dd] 4d
                    ldir                                    ;[13de] ed b0
                    ld        a,$85                         ;[13e0] 3e 85
                    out       ($e3),a                       ;[13e2] d3 e3
                    pop       hl                            ;[13e4] e1
                    ld        b,h                           ;[13e5] 44
                    ld        c,l                           ;[13e6] 4d
                    ldir                                    ;[13e7] ed b0
                    xor       a                             ;[13e9] af
                    out       ($e3),a                       ;[13ea] d3 e3
                    in        a,($3f)                       ;[13ec] db 3f
                    ret                                     ;[13ee] c9

                    ld        hl,$150a                      ;[13ef] 21 0a 15
                    ld        bc,$0057                      ;[13f2] 01 57 00
                    ld        de,($5c61)                    ;[13f5] ed 5b 61 5c
                    ldir                                    ;[13f9] ed b0
                    ld        a,$ff                         ;[13fb] 3e ff
                    ld        (de),a                        ;[13fd] 12
                    xor       a                             ;[13fe] af
                    ld        ($f700),a                     ;[13ff] 32 00 f7
                    ld        hl,($5c61)                    ;[1402] 2a 61 5c
                    ld        ($d742),hl                    ;[1405] 22 42 d7
                    ld        ($5b6a),sp                    ;[1408] ed 73 6a 5b
                    ld        sp,$5bff                      ;[140c] 31 ff 5b
                    rst       $10                           ;[140f] d7
                    ld        b,l                           ;[1410] 45
                    ld        b,$ed                         ;[1411] 06 ed
                    ld        a,e                           ;[1413] 7b
                    ld        l,d                           ;[1414] 6a
                    ld        e,e                           ;[1415] 5b
                    ld        hl,$5bff                      ;[1416] 21 ff 5b
                    ld        ($5b6a),hl                    ;[1419] 22 6a 5b
                    ret                                     ;[141c] c9

                    ld        a,($f700)                     ;[141d] 3a 00 f7
                    jr        $13ff                         ;[1420] 18 dd
                    push      de                            ;[1422] d5
                    push      bc                            ;[1423] c5
                    push      hl                            ;[1424] e5
                    ld        a,(de)                        ;[1425] 1a
                    ld        ixl,a                         ;[1426] dd 6f
                    inc       de                            ;[1428] 13
                    ld        a,(de)                        ;[1429] 1a
                    ld        ixh,a                         ;[142a] dd 67
                    push      ix                            ;[142c] dd e5
                    ld        de,$1c2d                      ;[142e] 11 2d 1c
                    call      $147c                         ;[1431] cd 7c 14
                    rst       $10                           ;[1434] d7
                    jp        po,$dd0c                      ;[1435] e2 0c dd
                    pop       hl                            ;[1438] e1
                    pop       hl                            ;[1439] e1
                    pop       bc                            ;[143a] c1
                    push      bc                            ;[143b] c5
                    push      hl                            ;[143c] e5
                    push      af                            ;[143d] f5
                    ld        de,$1cdf                      ;[143e] 11 df 1c
                    call      $147c                         ;[1441] cd 7c 14
                    pop       af                            ;[1444] f1
                    pop       de                            ;[1445] d1
                    pop       bc                            ;[1446] c1
                    pop       hl                            ;[1447] e1
                    cp        $20                           ;[1448] fe 20
                    jp        z,$0755                       ;[144a] ca 55 07
                    push      hl                            ;[144d] e5
                    cp        $61                           ;[144e] fe 61
                    jr        c,$1458                       ;[1450] 38 06
                    cp        $7b                           ;[1452] fe 7b
                    jr        nc,$1458                      ;[1454] 30 02
                    and       $df                           ;[1456] e6 df
                    inc       hl                            ;[1458] 23
                    inc       hl                            ;[1459] 23
                    bit       7,(hl)                        ;[145a] cb 7e
                    jr        nz,$146f                      ;[145c] 20 11
                    cp        (hl)                          ;[145e] be
                    inc       hl                            ;[145f] 23
                    jr        nz,$1458                      ;[1460] 20 f6
                    ld        a,(hl)                        ;[1462] 7e
                    inc       hl                            ;[1463] 23
                    ld        h,(hl)                        ;[1464] 66
                    ld        l,a                           ;[1465] 6f
                    ex        de,hl                         ;[1466] eb
                    call      $147e                         ;[1467] cd 7e 14
                    pop       de                            ;[146a] d1
                    jr        c,$1471                       ;[146b] 38 04
                    jr        $1422                         ;[146d] 18 b3
                    ex        de,hl                         ;[146f] eb
                    pop       de                            ;[1470] d1
                    push      de                            ;[1471] d5
                    push      bc                            ;[1472] c5
                    push      hl                            ;[1473] e5
                    rst       $10                           ;[1474] d7
                    jr        $14b5                         ;[1475] 18 3e
                    pop       hl                            ;[1477] e1
                    pop       bc                            ;[1478] c1
                    pop       de                            ;[1479] d1
                    jr        $1422                         ;[147a] 18 a6
                    jp        (ix)                          ;[147c] dd e9
                    push      de                            ;[147e] d5
                    ret                                     ;[147f] c9

                    ld        c,e                           ;[1480] 4b
                    dec       c                             ;[1481] 0d
                    ld        (hl),$92                      ;[1482] 36 92
                    inc       c                             ;[1484] 0c
                    ld        a,(bc)                        ;[1485] 0a
                    sub       d                             ;[1486] 92
                    inc       c                             ;[1487] 0c
                    scf                                     ;[1488] 37
                    and       h                             ;[1489] a4
                    inc       c                             ;[148a] 0c
                    dec       bc                            ;[148b] 0b
                    and       h                             ;[148c] a4
                    inc       c                             ;[148d] 0c
                    dec       (hl)                          ;[148e] 35
                    add       c                             ;[148f] 81
                    inc       c                             ;[1490] 0c
                    ex        af,af'                        ;[1491] 08
                    add       c                             ;[1492] 81
                    inc       c                             ;[1493] 0c
                    jr        c,$141e                       ;[1494] 38 88
                    inc       c                             ;[1496] 0c
                    add       hl,bc                         ;[1497] 09
                    adc       b                             ;[1498] 88
                    inc       c                             ;[1499] 0c
                    rlca                                    ;[149a] 07
                    out       ($0c),a                       ;[149b] d3 0c
                    ld        b,d                           ;[149d] 42
                    call      nc,$430e                      ;[149e] d4 0e 43
                    xor       $03                           ;[14a1] ee 03
                    ld        c,l                           ;[14a3] 4d
                    cp        a                             ;[14a4] bf
                    inc       c                             ;[14a5] 0c
                    ld        d,(hl)                        ;[14a6] 56
                    xor       c                             ;[14a7] a9
                    inc       bc                            ;[14a8] 03
                    rst       $38                           ;[14a9] ff
                    call      p,$360a                       ;[14aa] f4 0a 36
                    ld        l,h                           ;[14ad] 6c
                    ex        af,af'                        ;[14ae] 08
                    ld        a,(bc)                        ;[14af] 0a
                    ld        l,h                           ;[14b0] 6c
                    ex        af,af'                        ;[14b1] 08
                    scf                                     ;[14b2] 37
                    ld        a,h                           ;[14b3] 7c
                    ex        af,af'                        ;[14b4] 08
                    dec       bc                            ;[14b5] 0b
                    ld        a,h                           ;[14b6] 7c
                    ex        af,af'                        ;[14b7] 08
                    dec       (hl)                          ;[14b8] 35
                    adc       c                             ;[14b9] 89
                    ex        af,af'                        ;[14ba] 08
                    ex        af,af'                        ;[14bb] 08
                    adc       c                             ;[14bc] 89
                    ex        af,af'                        ;[14bd] 08
                    jr        c,$145c                       ;[14be] 38 9c
                    ex        af,af'                        ;[14c0] 08
                    add       hl,bc                         ;[14c1] 09
                    sbc       h                             ;[14c2] 9c
                    ex        af,af'                        ;[14c3] 08
                    rlca                                    ;[14c4] 07
                    sbc       $08                           ;[14c5] de 08
                    ld        b,d                           ;[14c7] 42
                    or        b                             ;[14c8] b0
                    ex        af,af'                        ;[14c9] 08
                    ld        c,a                           ;[14ca] 4f
                    pop       bc                            ;[14cb] c1
                    ex        af,af'                        ;[14cc] 08
                    ld        b,c                           ;[14cd] 41
                    out       ($08),a                       ;[14ce] d3 08
                    ld        d,e                           ;[14d0] 53
                    ld        sp,hl                         ;[14d1] f9
                    dec       bc                            ;[14d2] 0b
                    ld        d,h                           ;[14d3] 54
                    ld        b,$09                         ;[14d4] 06 09
                    ld        b,(hl)                        ;[14d6] 46
                    ld        hl,($4e09)                    ;[14d7] 2a 09 4e
                    or        c                             ;[14da] b1
                    add       hl,bc                         ;[14db] 09
                    ld        b,e                           ;[14dc] 43
                    cp        h                             ;[14dd] bc
                    add       hl,bc                         ;[14de] 09
                    rst       $38                           ;[14df] ff
                    ret       c                             ;[14e0] d8
                    rrca                                    ;[14e1] 0f
                    ld        (hl),$45                      ;[14e2] 36 45
                    rrca                                    ;[14e4] 0f
                    ld        a,(bc)                        ;[14e5] 0a
                    ld        b,l                           ;[14e6] 45
                    rrca                                    ;[14e7] 0f
                    scf                                     ;[14e8] 37
                    daa                                     ;[14e9] 27
                    rrca                                    ;[14ea] 0f
                    dec       bc                            ;[14eb] 0b
                    daa                                     ;[14ec] 27
                    rrca                                    ;[14ed] 0f
                    ld        d,e                           ;[14ee] 53
                    ld        sp,hl                         ;[14ef] f9
                    dec       bc                            ;[14f0] 0b
                    ld        b,e                           ;[14f1] 43
                    xor       $03                           ;[14f2] ee 03
                    ld        c,e                           ;[14f4] 4b
                    push      hl                            ;[14f5] e5
                    rrca                                    ;[14f6] 0f
                    ld        b,h                           ;[14f7] 44
                    inc       a                             ;[14f8] 3c
                    rrca                                    ;[14f9] 0f
                    ld        b,l                           ;[14fa] 45
                    ld        c,e                           ;[14fb] 4b
                    rrca                                    ;[14fc] 0f
                    ld        b,a                           ;[14fd] 47
                    dec       c                             ;[14fe] 0d
                    rrca                                    ;[14ff] 0f
                    ld        c,l                           ;[1500] 4d
                    ld        l,$0f                         ;[1501] 2e 0f
                    ld        b,d                           ;[1503] 42
                    ld        e,b                           ;[1504] 58
                    rrca                                    ;[1505] 0f
                    ld        b,c                           ;[1506] 41
                    ld        a,b                           ;[1507] 78
                    rrca                                    ;[1508] 0f
                    rst       $38                           ;[1509] ff
                    ld        c,(hl)                        ;[150a] 4e
                    ld        c,l                           ;[150b] 4d
                    ld        c,c                           ;[150c] 49
                    jr        nz,$155c                      ;[150d] 20 4d
                    ld        h,l                           ;[150f] 65
                    ld        l,(hl)                        ;[1510] 6e
                    ld        (hl),l                        ;[1511] 75
                    dec       c                             ;[1512] 0d
                    ld        d,e                           ;[1513] 53
                    ld        l,(hl)                        ;[1514] 6e
                    ld        e,a                           ;[1515] 5f
                    ld        h,c                           ;[1516] 61
                    ld        (hl),b                        ;[1517] 70
                    ld        (hl),e                        ;[1518] 73
                    ld        l,b                           ;[1519] 68
                    ld        l,a                           ;[151a] 6f
                    ld        (hl),h                        ;[151b] 74
                    dec       c                             ;[151c] 0d
                    ld        d,e                           ;[151d] 53
                    ld        e,a                           ;[151e] 5f
                    ld        h,e                           ;[151f] 63
                    ld        (hl),d                        ;[1520] 72
                    ld        h,l                           ;[1521] 65
                    ld        h,l                           ;[1522] 65
                    ld        l,(hl)                        ;[1523] 6e
                    ld        (hl),e                        ;[1524] 73
                    ld        l,b                           ;[1525] 68
                    ld        l,a                           ;[1526] 6f
                    ld        (hl),h                        ;[1527] 74
                    dec       c                             ;[1528] 0d
                    ld        d,h                           ;[1529] 54
                    ld        e,a                           ;[152a] 5f
                    ld        b,c                           ;[152b] 41
                    ld        d,b                           ;[152c] 50
                    jr        nz,$1575                      ;[152d] 20 46
                    ld        l,c                           ;[152f] 69
                    ld        l,h                           ;[1530] 6c
                    ld        h,l                           ;[1531] 65
                    ld        (hl),e                        ;[1532] 73
                    dec       c                             ;[1533] 0d
                    ld        d,b                           ;[1534] 50
                    ld        e,a                           ;[1535] 5f
                    ld        c,a                           ;[1536] 4f
                    ld        c,e                           ;[1537] 4b
                    ld        b,l                           ;[1538] 45
                    ld        (hl),e                        ;[1539] 73
                    dec       c                             ;[153a] 0d
                    ld        b,h                           ;[153b] 44
                    ld        e,a                           ;[153c] 5f
                    ld        h,l                           ;[153d] 65
                    ld        h,d                           ;[153e] 62
                    ld        (hl),l                        ;[153f] 75
                    ld        h,a                           ;[1540] 67
                    jr        nz,$1597                      ;[1541] 20 54
                    ld        l,a                           ;[1543] 6f
                    ld        l,a                           ;[1544] 6f
                    ld        l,h                           ;[1545] 6c
                    ld        (hl),e                        ;[1546] 73
                    dec       c                             ;[1547] 0d
                    ld        d,e                           ;[1548] 53
                    ld        h,l                           ;[1549] 65
                    ld        (hl),h                        ;[154a] 74
                    ld        (hl),h                        ;[154b] 74
                    ld        l,c                           ;[154c] 69
                    ld        e,a                           ;[154d] 5f
                    ld        l,(hl)                        ;[154e] 6e
                    ld        h,a                           ;[154f] 67
                    ld        (hl),e                        ;[1550] 73
                    dec       c                             ;[1551] 0d
                    ld        c,e                           ;[1552] 4b
                    ld        e,a                           ;[1553] 5f
                    ld        h,l                           ;[1554] 65
                    ld        a,c                           ;[1555] 79
                    ld        l,l                           ;[1556] 6d
                    ld        h,c                           ;[1557] 61
                    ld        (hl),b                        ;[1558] 70
                    dec       c                             ;[1559] 0d
                    ld        b,c                           ;[155a] 41
                    ld        e,a                           ;[155b] 5f
                    ld        h,d                           ;[155c] 62
                    ld        l,a                           ;[155d] 6f
                    ld        (hl),l                        ;[155e] 75
                    ld        (hl),h                        ;[155f] 74
                    dec       c                             ;[1560] 0d
                    ld        c,(hl)                        ;[1561] 4e
                    inc       hl                            ;[1562] 23
                    ld        b,(hl)                        ;[1563] 46
                    inc       hl                            ;[1564] 23
                    ex        (sp),hl                       ;[1565] e3
                    push    $5c23                           ;[1566] ed 8a 5c 23
                    push      bc                            ;[156a] c5
                    ld        bc,($5b54)                    ;[156b] ed 4b 54 5b
                    jp        $5c1c                         ;[156f] c3 1c 5c
                    ex        (sp),hl                       ;[1572] e3
                    push      af                            ;[1573] f5
                    ld        a,(hl)                        ;[1574] 7e
                    inc       hl                            ;[1575] 23
                    ld        ($5b5e),a                     ;[1576] 32 5e 5b
                    pop       af                            ;[1579] f1
                    ex        (sp),hl                       ;[157a] e3
                    rst       $28                           ;[157b] ef
                    ld        e,l                           ;[157c] 5d
                    ld        e,e                           ;[157d] 5b
                    ret                                     ;[157e] c9

                    ld        c,(hl)                        ;[157f] 4e
                    inc       hl                            ;[1580] 23
                    ld        b,(hl)                        ;[1581] 46
                    inc       hl                            ;[1582] 23
                    ex        (sp),hl                       ;[1583] e3
                    push    $5c23                           ;[1584] ed 8a 5c 23
                    push    $5b4d                           ;[1588] ed 8a 5b 4d
                    push      bc                            ;[158c] c5
                    ld        bc,($5b54)                    ;[158d] ed 4b 54 5b
                    push    $5b3e                           ;[1591] ed 8a 5b 3e
                    jp        $5c1c                         ;[1595] c3 1c 5c
                    ld        c,(hl)                        ;[1598] 4e
                    inc       hl                            ;[1599] 23
                    ld        b,(hl)                        ;[159a] 46
                    inc       hl                            ;[159b] 23
                    ex        (sp),hl                       ;[159c] e3
                    push    $5c23                           ;[159d] ed 8a 5c 23
                    push    $5b4d                           ;[15a1] ed 8a 5b 4d
                    push      bc                            ;[15a5] c5
                    ld        bc,($5b54)                    ;[15a6] ed 4b 54 5b
                    push    $5b43                           ;[15aa] ed 8a 5b 43
                    jp        $5c1c                         ;[15ae] c3 1c 5c
                    ld        c,(hl)                        ;[15b1] 4e
                    inc       hl                            ;[15b2] 23
                    ld        b,(hl)                        ;[15b3] 46
                    inc       hl                            ;[15b4] 23
                    ex        (sp),hl                       ;[15b5] e3
                    push    $5c23                           ;[15b6] ed 8a 5c 23
                    push    $5b4d                           ;[15ba] ed 8a 5b 4d
                    push      bc                            ;[15be] c5
                    ld        bc,($5b54)                    ;[15bf] ed 4b 54 5b
                    push    $5b48                           ;[15c3] ed 8a 5b 48
                    jp        $5c1c                         ;[15c7] c3 1c 5c
                    push      hl                            ;[15ca] e5
                    call      $0047                         ;[15cb] cd 47 00
                    call      $1609                         ;[15ce] cd 09 16
                    ld        l,(hl)                        ;[15d1] 6e
                    ex        (sp),hl                       ;[15d2] e3
                    inc       hl                            ;[15d3] 23
                    call      $0047                         ;[15d4] cd 47 00
                    call      $1609                         ;[15d7] cd 09 16
                    ld        a,(hl)                        ;[15da] 7e
                    pop       hl                            ;[15db] e1
                    ld        h,a                           ;[15dc] 67
                    ret                                     ;[15dd] c9

                    push      hl                            ;[15de] e5
                    push      de                            ;[15df] d5
                    call      $0047                         ;[15e0] cd 47 00
                    call      $1609                         ;[15e3] cd 09 16
                    pop       de                            ;[15e6] d1
                    ld        (hl),e                        ;[15e7] 73
                    call      $16c8                         ;[15e8] cd c8 16
                    pop       hl                            ;[15eb] e1
                    push      de                            ;[15ec] d5
                    inc       hl                            ;[15ed] 23
                    call      $0047                         ;[15ee] cd 47 00
                    call      $1609                         ;[15f1] cd 09 16
                    pop       de                            ;[15f4] d1
                    ld        (hl),d                        ;[15f5] 72
                    jp        $16c8                         ;[15f6] c3 c8 16
                    ld        a,($3fff)                     ;[15f9] 3a ff 3f
                    ld        b,a                           ;[15fc] 47
                    ld        a,($3fcc)                     ;[15fd] 3a cc 3f
                    rrca                                    ;[1600] 0f
                    rrca                                    ;[1601] 0f
                    rrca                                    ;[1602] 0f
                    res       1,b                           ;[1603] cb 88
                    or        b                             ;[1605] b0
                    and       $06                           ;[1606] e6 06
                    ret                                     ;[1608] c9

                    cp        $ff                           ;[1609] fe ff
                    jr        z,$162b                       ;[160b] 28 1e
                    cp        $0a                           ;[160d] fe 0a
                    jr        c,$1625                       ;[160f] 38 14
                    jr        z,$1638                       ;[1611] 28 25
                    cp        $0b                           ;[1613] fe 0b
                    jr        z,$163c                       ;[1615] 28 25
                    cp        $10                           ;[1617] fe 10
                    jr        z,$1648                       ;[1619] 28 2d
                    jr        nc,$1625                      ;[161b] 30 08
                    cp        $0e                           ;[161d] fe 0e
                    jr        z,$164c                       ;[161f] 28 2b
                    cp        $0f                           ;[1621] fe 0f
                    jr        z,$1650                       ;[1623] 28 2b
                    nextreg $54,a                           ;[1625] ed 92 54
                    set       7,h                           ;[1628] cb fc
                    ret                                     ;[162a] c9

                    ld        a,e                           ;[162b] 7b
                    cp        $6b                           ;[162c] fe 6b
                    jr        nz,$1632                      ;[162e] 20 02
                    set       5,h                           ;[1630] cb ec
                    call      $15f9                         ;[1632] cd f9 15
                    inc       a                             ;[1635] 3c
                    jr        $1652                         ;[1636] 18 1a
                    ld        a,$86                         ;[1638] 3e 86
                    jr        $1652                         ;[163a] 18 16
                    ld        a,h                           ;[163c] 7c
                    cp        $18                           ;[163d] fe 18
                    jr        c,$1645                       ;[163f] 38 04
                    ld        a,$0b                         ;[1641] 3e 0b
                    jr        $1625                         ;[1643] 18 e0
                    xor       a                             ;[1645] af
                    jr        $1652                         ;[1646] 18 0a
                    ld        a,$8b                         ;[1648] 3e 8b
                    jr        $1652                         ;[164a] 18 06
                    ld        a,$84                         ;[164c] 3e 84
                    jr        $1652                         ;[164e] 18 02
                    ld        a,$85                         ;[1650] 3e 85
                    ld        de,($3f32)                    ;[1652] ed 5b 32 3f
                    cp        e                             ;[1656] bb
                    jr        nz,$1662                      ;[1657] 20 09
                    ld        c,a                           ;[1659] 4f
                    ld        a,h                           ;[165a] 7c
                    cp        d                             ;[165b] ba
                    ld        a,c                           ;[165c] 79
                    jr        nz,$1662                      ;[165d] 20 03
                    ld        h,$5e                         ;[165f] 26 5e
                    ret                                     ;[1661] c9

                    push      af                            ;[1662] f5
                    push      hl                            ;[1663] e5
                    call      $16a5                         ;[1664] cd a5 16
                    pop       hl                            ;[1667] e1
                    pop       af                            ;[1668] f1
                    push      hl                            ;[1669] e5
                    ld        l,a                           ;[166a] 6f
                    ld        ($3f32),hl                    ;[166b] 22 32 3f
                    pop       hl                            ;[166e] e1
                    push      hl                            ;[166f] e5
                    ld        l,$00                         ;[1670] 2e 00
                    ld        de,$5e00                      ;[1672] 11 00 5e
                    ld        bc,$0100                      ;[1675] 01 00 01
                    and       a                             ;[1678] a7
                    jp        p,$1685                       ;[1679] f2 85 16
                    set       5,h                           ;[167c] cb ec
                    call      $5ca0                         ;[167e] cd a0 5c
                    pop       hl                            ;[1681] e1
                    ld        h,$5e                         ;[1682] 26 5e
                    ret                                     ;[1684] c9

                    jr        nz,$168d                      ;[1685] 20 06
                    set       5,h                           ;[1687] cb ec
                    ldir                                    ;[1689] ed b0
                    jr        $1681                         ;[168b] 18 f4
                    and       $06                           ;[168d] e6 06
                    add       a                             ;[168f] 87
                    add       a                             ;[1690] 87
                    add       $d7                           ;[1691] c6 d7
                    ld        ($5b73),a                     ;[1693] 32 73 5b
                    ld        a,($3f89)                     ;[1696] 3a 89 3f
                    nextreg $8c,a                           ;[1699] ed 92 8c
                    call      $5b73                         ;[169c] cd 73 5b
                    nextreg $8c,$00                         ;[169f] ed 91 8c 00
                    jr        $1681                         ;[16a3] 18 dc
                    ld        a,($3f34)                     ;[16a5] 3a 34 3f
                    and       a                             ;[16a8] a7
                    ret       z                             ;[16a9] c8
                    xor       a                             ;[16aa] af
                    ld        ($3f34),a                     ;[16ab] 32 34 3f
                    ld        de,($3f32)                    ;[16ae] ed 5b 32 3f
                    ld        a,e                           ;[16b2] 7b
                    cp        $ff                           ;[16b3] fe ff
                    ret       z                             ;[16b5] c8
                    set       5,d                           ;[16b6] cb ea
                    ld        e,$00                         ;[16b8] 1e 00
                    ld        hl,$5e00                      ;[16ba] 21 00 5e
                    ld        bc,$0100                      ;[16bd] 01 00 01
                    and       a                             ;[16c0] a7
                    jp        m,$5ca0                       ;[16c1] fa a0 5c
                    ret       nz                            ;[16c4] c0
                    ldir                                    ;[16c5] ed b0
                    ret                                     ;[16c7] c9

                    ld        a,$01                         ;[16c8] 3e 01
                    ld        ($3f34),a                     ;[16ca] 32 34 3f
                    ret                                     ;[16cd] c9

                    xor       a                             ;[16ce] af
                    ld        ($3f34),a                     ;[16cf] 32 34 3f
                    dec       a                             ;[16d2] 3d
                    ld        ($3f32),a                     ;[16d3] 32 32 3f
                    ret                                     ;[16d6] c9

                    ld        b,$04                         ;[16d7] 06 04
                    push      bc                            ;[16d9] c5
                    push      hl                            ;[16da] e5
                    push      de                            ;[16db] d5
                    push      af                            ;[16dc] f5
                    call      $1609                         ;[16dd] cd 09 16
                    pop       af                            ;[16e0] f1
                    pop       de                            ;[16e1] d1
                    push      de                            ;[16e2] d5
                    jr        c,$16e6                       ;[16e3] 38 01
                    ex        de,hl                         ;[16e5] eb
                    ldi                                     ;[16e6] ed a0
                    pop       de                            ;[16e8] d1
                    pop       hl                            ;[16e9] e1
                    pop       bc                            ;[16ea] c1
                    inc       de                            ;[16eb] 13
                    inc       hl                            ;[16ec] 23
                    djnz      $16d9                         ;[16ed] 10 ea
                    ret                                     ;[16ef] c9

                    ld        a,$14                         ;[16f0] 3e 14
                    sub       b                             ;[16f2] 90
                    ld        d,a                           ;[16f3] 57
                    ld        e,$07                         ;[16f4] 1e 07
                    mul       d,e                           ;[16f6] ed 30
                    ex        de,hl                         ;[16f8] eb
                    add       hl,$3ea1                      ;[16f9] ed 34 a1 3e
                    ld        e,(hl)                        ;[16fd] 5e
                    inc       hl                            ;[16fe] 23
                    ld        d,(hl)                        ;[16ff] 56
                    inc       hl                            ;[1700] 23
                    ld        a,(hl)                        ;[1701] 7e
                    inc       hl                            ;[1702] 23
                    ld        c,d                           ;[1703] 4a
                    res       7,d                           ;[1704] cb ba
                    res       6,d                           ;[1706] cb b2
                    bit       7,c                           ;[1708] cb 79
                    ret                                     ;[170a] c9

                    ld        hl,$3f2e                      ;[170b] 21 2e 3f
                    bit       7,(hl)                        ;[170e] cb 7e
                    ret       z                             ;[1710] c8
                    res       7,(hl)                        ;[1711] cb be
                    ld        b,$14                         ;[1713] 06 14
                    push      bc                            ;[1715] c5
                    call      $16f0                         ;[1716] cd f0 16
                    jr        z,$1749                       ;[1719] 28 2e
                    ex        de,hl                         ;[171b] eb
                    push      de                            ;[171c] d5
                    push      hl                            ;[171d] e5
                    push      af                            ;[171e] f5
                    scf                                     ;[171f] 37
                    ld        de,$5f00                      ;[1720] 11 00 5f
                    push      de                            ;[1723] d5
                    call      $16d7                         ;[1724] cd d7 16
                    pop       de                            ;[1727] d1
                    ld        hl,$1796                      ;[1728] 21 96 17
                    ld        b,$04                         ;[172b] 06 04
                    ld        a,(de)                        ;[172d] 1a
                    cp        (hl)                          ;[172e] be
                    jr        nz,$1735                      ;[172f] 20 04
                    inc       de                            ;[1731] 13
                    inc       hl                            ;[1732] 23
                    djnz      $172d                         ;[1733] 10 f8
                    pop       hl                            ;[1735] e1
                    ld        a,h                           ;[1736] 7c
                    pop       hl                            ;[1737] e1
                    jr        z,$1741                       ;[1738] 28 07
                    pop       hl                            ;[173a] e1
                    dec       hl                            ;[173b] 2b
                    dec       hl                            ;[173c] 2b
                    res       7,(hl)                        ;[173d] cb be
                    jr        $1749                         ;[173f] 18 08
                    pop       de                            ;[1741] d1
                    and       a                             ;[1742] a7
                    call      $16d7                         ;[1743] cd d7 16
                    call      $16c8                         ;[1746] cd c8 16
                    pop       bc                            ;[1749] c1
                    djnz      $1715                         ;[174a] 10 c9
                    ret                                     ;[174c] c9

                    ld        hl,$3f2e                      ;[174d] 21 2e 3f
                    bit       6,(hl)                        ;[1750] cb 76
                    ret       nz                            ;[1752] c0
                    set       7,(hl)                        ;[1753] cb fe
                    ld        b,$14                         ;[1755] 06 14
                    push      bc                            ;[1757] c5
                    call      $16f0                         ;[1758] cd f0 16
                    jr        z,$1792                       ;[175b] 28 35
                    push      hl                            ;[175d] e5
                    push      de                            ;[175e] d5
                    push      af                            ;[175f] f5
                    ld        hl,($3f9f)                    ;[1760] 2a 9f 3f
                    call      $0047                         ;[1763] cd 47 00
                    pop       bc                            ;[1766] c1
                    pop       de                            ;[1767] d1
                    cp        b                             ;[1768] b8
                    ld        a,b                           ;[1769] 78
                    jr        nz,$177e                      ;[176a] 20 12
                    ex        de,hl                         ;[176c] eb
                    call      $0fce                         ;[176d] cd ce 0f
                    ex        de,hl                         ;[1770] eb
                    jr        c,$177e                       ;[1771] 38 0b
                    sbc       hl,de                         ;[1773] ed 52
                    jr        c,$177e                       ;[1775] 38 07
                    pop       hl                            ;[1777] e1
                    dec       hl                            ;[1778] 2b
                    dec       hl                            ;[1779] 2b
                    res       7,(hl)                        ;[177a] cb be
                    jr        $1792                         ;[177c] 18 14
                    pop       hl                            ;[177e] e1
                    ex        de,hl                         ;[177f] eb
                    scf                                     ;[1780] 37
                    push      af                            ;[1781] f5
                    push      hl                            ;[1782] e5
                    call      $16d7                         ;[1783] cd d7 16
                    pop       hl                            ;[1786] e1
                    pop       af                            ;[1787] f1
                    ld        de,$1796                      ;[1788] 11 96 17
                    and       a                             ;[178b] a7
                    call      $16d7                         ;[178c] cd d7 16
                    call      $16c8                         ;[178f] cd c8 16
                    pop       bc                            ;[1792] c1
                    djnz      $1757                         ;[1793] 10 c2
                    ret                                     ;[1795] c9

                    nextreg $02,$08                         ;[1796] ed 91 02 08
                    ld        bc,$243b                      ;[179a] 01 3b 24
                    out       (c),a                         ;[179d] ed 79
                    inc       b                             ;[179f] 04
                    in        a,(c)                         ;[17a0] ed 78
                    ret                                     ;[17a2] c9

                    ld        hl,$3f4e                      ;[17a3] 21 4e 3f
                    ld        de,$0e76                      ;[17a6] 11 76 0e
                    ld        bc,$243b                      ;[17a9] 01 3b 24
                    ld        a,(de)                        ;[17ac] 1a
                    inc       de                            ;[17ad] 13
                    and       a                             ;[17ae] a7
                    ret       z                             ;[17af] c8
                    out       (c),a                         ;[17b0] ed 79
                    inc       b                             ;[17b2] 04
                    in        a,(c)                         ;[17b3] ed 78
                    dec       b                             ;[17b5] 05
                    ld        (hl),a                        ;[17b6] 77
                    inc       hl                            ;[17b7] 23
                    jr        $17ac                         ;[17b8] 18 f2
                    ld        hl,$3f4e                      ;[17ba] 21 4e 3f
                    ld        de,$0e76                      ;[17bd] 11 76 0e
                    ld        bc,$243b                      ;[17c0] 01 3b 24
                    ld        a,(de)                        ;[17c3] 1a
                    inc       de                            ;[17c4] 13
                    and       a                             ;[17c5] a7
                    ret       z                             ;[17c6] c8
                    out       (c),a                         ;[17c7] ed 79
                    inc       b                             ;[17c9] 04
                    cp        $8f                           ;[17ca] fe 8f
                    ld        a,(hl)                        ;[17cc] 7e
                    inc       hl                            ;[17cd] 23
                    jr        z,$17d2                       ;[17ce] 28 02
                    out       (c),a                         ;[17d0] ed 79
                    dec       b                             ;[17d2] 05
                    jr        $17c3                         ;[17d3] 18 ee
                    ret       nz                            ;[17d5] c0
                    call      nz,$c6c5                      ;[17d6] c4 c5 c6
                    inc       d                             ;[17d9] 14
                    dec       d                             ;[17da] 15
                    ld        ($2726),hl                    ;[17db] 22 26 27
                    ld        b,d                           ;[17de] 42
                    ld        b,e                           ;[17df] 43
                    ld        d,b                           ;[17e0] 50
                    ld        d,c                           ;[17e1] 51
                    ld        d,d                           ;[17e2] 52
                    ld        d,e                           ;[17e3] 53
                    ld        d,h                           ;[17e4] 54
                    ld        d,l                           ;[17e5] 55
                    ld        d,(hl)                        ;[17e6] 56
                    ld        d,a                           ;[17e7] 57
                    ld        l,b                           ;[17e8] 68
                    ld        l,e                           ;[17e9] 6b
                    add       c                             ;[17ea] 81
                    add       d                             ;[17eb] 82
                    add       e                             ;[17ec] 83
                    add       h                             ;[17ed] 84
                    add       l                             ;[17ee] 85
                    adc       h                             ;[17ef] 8c
                    inc       e                             ;[17f0] 1c
                    ld        a,(de)                        ;[17f1] 1a
                    ld        a,(de)                        ;[17f2] 1a
                    ld        a,(de)                        ;[17f3] 1a
                    ld        a,(de)                        ;[17f4] 1a
                    cp        b                             ;[17f5] b8
                    cp        c                             ;[17f6] b9
                    cp        d                             ;[17f7] ba
                    cp        e                             ;[17f8] bb
                    ld        a,(bc)                        ;[17f9] 0a
                    nop                                     ;[17fa] 00
                    ex        af,af'                        ;[17fb] 08
                    add       e                             ;[17fc] 83
                    nop                                     ;[17fd] 00
                    nop                                     ;[17fe] 00
                    ex        (sp),hl                       ;[17ff] e3
                    djnz      $1802                         ;[1800] 10 00
                    nop                                     ;[1802] 00
                    nop                                     ;[1803] 00
                    nop                                     ;[1804] 00
                    ld        c,(hl)                        ;[1805] 4e
                    rst       $38                           ;[1806] ff
                    rst       $38                           ;[1807] ff
                    ld        a,(bc)                        ;[1808] 0a
                    dec       bc                            ;[1809] 0b
                    inc       b                             ;[180a] 04
                    dec       b                             ;[180b] 05
                    nop                                     ;[180c] 00
                    ld        bc,$0000                      ;[180d] 01 00 00
                    nop                                     ;[1810] 00
                    rst       $38                           ;[1811] ff
                    rst       $38                           ;[1812] ff
                    rst       $38                           ;[1813] ff
                    rst       $38                           ;[1814] ff
                    nop                                     ;[1815] 00
                    inc       b                             ;[1816] 04
                    nop                                     ;[1817] 00
                    rst       $38                           ;[1818] ff
                    nop                                     ;[1819] 00
                    cp        a                             ;[181a] bf
                    add       d                             ;[181b] 82
                    ld        bc,$f200                      ;[181c] 01 00 f2
                    ld        de,$3b01                      ;[181f] 11 01 3b
                    inc       h                             ;[1822] 24
                    nextreg $43,a                           ;[1823] ed 92 43
                    ld        e,$00                         ;[1826] 1e 00
                    ld        a,e                           ;[1828] 7b
                    nextreg $40,a                           ;[1829] ed 92 40
                    ld        a,$41                         ;[182c] 3e 41
                    out       (c),a                         ;[182e] ed 79
                    inc       b                             ;[1830] 04
                    in        a,(c)                         ;[1831] ed 78
                    dec       b                             ;[1833] 05
                    ld        (hl),a                        ;[1834] 77
                    inc       hl                            ;[1835] 23
                    ld        a,$44                         ;[1836] 3e 44
                    out       (c),a                         ;[1838] ed 79
                    inc       b                             ;[183a] 04
                    in        a,(c)                         ;[183b] ed 78
                    dec       b                             ;[183d] 05
                    ld        (hl),a                        ;[183e] 77
                    inc       hl                            ;[183f] 23
                    inc       e                             ;[1840] 1c
                    dec       d                             ;[1841] 15
                    jr        nz,$1828                      ;[1842] 20 e4
                    ret                                     ;[1844] c9

                    ld        e,$00                         ;[1845] 1e 00
                    nextreg $43,a                           ;[1847] ed 92 43
                    ld        a,e                           ;[184a] 7b
                    nextreg $40,a                           ;[184b] ed 92 40
                    ld        a,(hl)                        ;[184e] 7e
                    inc       hl                            ;[184f] 23
                    nextreg $44,a                           ;[1850] ed 92 44
                    jr        nc,$185a                      ;[1853] 30 05
                    ld        a,(hl)                        ;[1855] 7e
                    inc       hl                            ;[1856] 23
                    nextreg $44,a                           ;[1857] ed 92 44
                    inc       e                             ;[185a] 1c
                    dec       d                             ;[185b] 15
                    jr        nz,$184a                      ;[185c] 20 ec
                    ret                                     ;[185e] c9

                    push      af                            ;[185f] f5
                    push      bc                            ;[1860] c5
                    ld        bc,$7ffd                      ;[1861] 01 fd 7f
                    ld        a,($5b5c)                     ;[1864] 3a 5c 5b
                    xor       $10                           ;[1867] ee 10
                    di                                      ;[1869] f3
                    ld        ($5b5c),a                     ;[186a] 32 5c 5b
                    out       (c),a                         ;[186d] ed 79
                    ld        bc,$1ffd                      ;[186f] 01 fd 1f
                    ld        a,($5b67)                     ;[1872] 3a 67 5b
                    xor       $04                           ;[1875] ee 04
                    ld        ($5b67),a                     ;[1877] 32 67 5b
                    out       (c),a                         ;[187a] ed 79
                    ei                                      ;[187c] fb
                    pop       bc                            ;[187d] c1
                    pop       af                            ;[187e] f1
                    ret                                     ;[187f] c9

                    call      $5b00                         ;[1880] cd 00 5b
                    push      hl                            ;[1883] e5
                    ld        hl,($5b5a)                    ;[1884] 2a 5a 5b
                    ex        (sp),hl                       ;[1887] e3
                    ret                                     ;[1888] c9

                    push      hl                            ;[1889] e5
                    ld        hl,$5b34                      ;[188a] 21 34 5b
                    ex        (sp),hl                       ;[188d] e3
                    push      af                            ;[188e] f5
                    push      bc                            ;[188f] c5
                    jr        $186f                         ;[1890] 18 dd
                    nop                                     ;[1892] 00
                    push      hl                            ;[1893] e5
                    ld        hl,($5b5a)                    ;[1894] 2a 5a 5b
                    ex        (sp),hl                       ;[1897] e3
                    ret                                     ;[1898] c9

                    push    $0a9e                           ;[1899] ed 8a 0a 9e
                    nextreg $8e,$01                         ;[189d] ed 91 8e 01
                    ret                                     ;[18a1] c9

                    nextreg $8e,$02                         ;[18a2] ed 91 8e 02
                    ret                                     ;[18a6] c9

                    nextreg $8e,$03                         ;[18a7] ed 91 8e 03
                    ret                                     ;[18ab] c9

                    nextreg $8e,$00                         ;[18ac] ed 91 8e 00
                    ret                                     ;[18b0] c9

                    nop                                     ;[18b1] 00
                    nop                                     ;[18b2] 00
                    nop                                     ;[18b3] 00
                    nop                                     ;[18b4] 00
                    nop                                     ;[18b5] 00
                    nop                                     ;[18b6] 00
                    nop                                     ;[18b7] 00
                    nop                                     ;[18b8] 00
                    nop                                     ;[18b9] 00
                    nop                                     ;[18ba] 00
                    rlca                                    ;[18bb] 07
                    rst       $08                           ;[18bc] cf
                    nop                                     ;[18bd] 00
                    ret                                     ;[18be] c9

                    nop                                     ;[18bf] 00
                    jr        c,$18c2                       ;[18c0] 38 00
                    jr        c,$18c4                       ;[18c2] 38 00
                    nop                                     ;[18c4] 00
                    inc       b                             ;[18c5] 04
                    nop                                     ;[18c6] 00
                    nop                                     ;[18c7] 00
                    nop                                     ;[18c8] 00
                    rst       $38                           ;[18c9] ff
                    ld        e,e                           ;[18ca] 5b
                    nop                                     ;[18cb] 00
                    nop                                     ;[18cc] 00
                    nop                                     ;[18cd] 00
                    nop                                     ;[18ce] 00
                    nop                                     ;[18cf] 00
                    nop                                     ;[18d0] 00
                    nop                                     ;[18d1] 00
                    nop                                     ;[18d2] 00
                    add       h                             ;[18d3] 84
                    ld        e,e                           ;[18d4] 5b
                    ret                                     ;[18d5] c9

                    nop                                     ;[18d6] 00
                    nop                                     ;[18d7] 00
                    ld        b,e                           ;[18d8] 43
                    ld        b,e                           ;[18d9] 43
                    nop                                     ;[18da] 00
                    nop                                     ;[18db] 00
                    nop                                     ;[18dc] 00
                    nop                                     ;[18dd] 00
                    nop                                     ;[18de] 00
                    nop                                     ;[18df] 00
                    nop                                     ;[18e0] 00
                    nop                                     ;[18e1] 00
                    nop                                     ;[18e2] 00
                    ldir                                    ;[18e3] ed b0
                    ret                                     ;[18e5] c9

                    nop                                     ;[18e6] 00
                    rst       $38                           ;[18e7] ff
                    rst       $38                           ;[18e8] ff
                    di                                      ;[18e9] f3
                    in        a,($bf)                       ;[18ea] db bf
                    xor       a                             ;[18ec] af
                    ld        bc,$1ffd                      ;[18ed] 01 fd 1f
                    out       (c),a                         ;[18f0] ed 79
                    ld        bc,$7ffd                      ;[18f2] 01 fd 7f
                    ld        a,$07                         ;[18f5] 3e 07
                    out       (c),a                         ;[18f7] ed 79
                    ld        hl,($0006)                    ;[18f9] 2a 06 00
                    ld        bc,$0209                      ;[18fc] 01 09 02
                    and       a                             ;[18ff] a7
                    sbc       hl,bc                         ;[1900] ed 42
                    jr        z,$1908                       ;[1902] 28 04
                    in        a,($3f)                       ;[1904] db 3f
                    and       a                             ;[1906] a7
                    ret                                     ;[1907] c9

                    nextreg $55,$10                         ;[1908] ed 91 55 10
                    ld        a,$86                         ;[190c] 3e 86
                    out       ($e3),a                       ;[190e] d3 e3
                    ld        hl,$4000                      ;[1910] 21 00 40
                    ld        de,$2000                      ;[1913] 11 00 20
                    push      de                            ;[1916] d5
                    ld        b,d                           ;[1917] 42
                    ld        c,e                           ;[1918] 4b
                    ldir                                    ;[1919] ed b0
                    ld        a,$8b                         ;[191b] 3e 8b
                    out       ($e3),a                       ;[191d] d3 e3
                    ld        hl,$a000                      ;[191f] 21 00 a0
                    pop       de                            ;[1922] d1
                    push      de                            ;[1923] d5
                    ld        b,d                           ;[1924] 42
                    ld        c,e                           ;[1925] 4b
                    ldir                                    ;[1926] ed b0
                    ld        a,$84                         ;[1928] 3e 84
                    out       ($e3),a                       ;[192a] d3 e3
                    ld        hl,$c000                      ;[192c] 21 00 c0
                    pop       de                            ;[192f] d1
                    push      de                            ;[1930] d5
                    ld        b,d                           ;[1931] 42
                    ld        c,e                           ;[1932] 4b
                    ldir                                    ;[1933] ed b0
                    ld        a,$85                         ;[1935] 3e 85
                    out       ($e3),a                       ;[1937] d3 e3
                    pop       de                            ;[1939] d1
                    ld        b,d                           ;[193a] 42
                    ld        c,e                           ;[193b] 4b
                    ldir                                    ;[193c] ed b0
                    jp        $5cce                         ;[193e] c3 ce 5c
                    nop                                     ;[1941] 00
                    nop                                     ;[1942] 00
                    nop                                     ;[1943] 00
                    nop                                     ;[1944] 00
                    nop                                     ;[1945] 00
                    nop                                     ;[1946] 00
                    nop                                     ;[1947] 00
                    nop                                     ;[1948] 00
                    nop                                     ;[1949] 00
                    nop                                     ;[194a] 00
                    nop                                     ;[194b] 00
                    nop                                     ;[194c] 00
                    nop                                     ;[194d] 00
                    nop                                     ;[194e] 00
                    nop                                     ;[194f] 00
                    nop                                     ;[1950] 00
                    nop                                     ;[1951] 00
                    nop                                     ;[1952] 00
                    nop                                     ;[1953] 00
                    nop                                     ;[1954] 00
                    nop                                     ;[1955] 00
                    nop                                     ;[1956] 00
                    nop                                     ;[1957] 00
                    nop                                     ;[1958] 00
                    nop                                     ;[1959] 00
                    nop                                     ;[195a] 00
                    nop                                     ;[195b] 00
                    nop                                     ;[195c] 00
                    nop                                     ;[195d] 00
                    nop                                     ;[195e] 00
                    rst       $38                           ;[195f] ff
                    nop                                     ;[1960] 00
                    nop                                     ;[1961] 00
                    nop                                     ;[1962] 00
                    rst       $38                           ;[1963] ff
                    nop                                     ;[1964] 00
                    nop                                     ;[1965] 00
                    nop                                     ;[1966] 00
                    nop                                     ;[1967] 00
                    inc       hl                            ;[1968] 23
                    ld        (bc),a                        ;[1969] 02
                    nop                                     ;[196a] 00
                    nop                                     ;[196b] 00
                    nop                                     ;[196c] 00
                    nop                                     ;[196d] 00
                    nop                                     ;[196e] 00
                    ld        bc,$0600                      ;[196f] 01 00 06
                    nop                                     ;[1972] 00
                    dec       bc                            ;[1973] 0b
                    nop                                     ;[1974] 00
                    ld        bc,$0100                      ;[1975] 01 00 01
                    nop                                     ;[1978] 00
                    ld        b,$00                         ;[1979] 06 00
                    di                                      ;[197b] f3
                    push      af                            ;[197c] f5
                    in        a,($bf)                       ;[197d] db bf
                    pop       af                            ;[197f] f1
                    ei                                      ;[1980] fb
                    ret                                     ;[1981] c9

                    di                                      ;[1982] f3
                    push      af                            ;[1983] f5
                    in        a,($3f)                       ;[1984] db 3f
                    pop       af                            ;[1986] f1
                    ei                                      ;[1987] fb
                    ret                                     ;[1988] c9

                    nop                                     ;[1989] 00
                    nop                                     ;[198a] 00
                    nop                                     ;[198b] 00
                    nop                                     ;[198c] 00
                    nop                                     ;[198d] 00
                    nop                                     ;[198e] 00
                    nop                                     ;[198f] 00
                    nop                                     ;[1990] 00
                    nop                                     ;[1991] 00
                    nop                                     ;[1992] 00
                    nop                                     ;[1993] 00
                    nop                                     ;[1994] 00
                    nop                                     ;[1995] 00
                    inc       a                             ;[1996] 3c
                    ld        b,b                           ;[1997] 40
                    nop                                     ;[1998] 00
                    rst       $38                           ;[1999] ff
                    jr        $199c                         ;[199a] 18 00
                    nop                                     ;[199c] 00
                    nop                                     ;[199d] 00
                    nop                                     ;[199e] 00
                    nop                                     ;[199f] 00
                    nop                                     ;[19a0] 00
                    nop                                     ;[19a1] 00
                    nop                                     ;[19a2] 00
                    nop                                     ;[19a3] 00
                    nop                                     ;[19a4] 00
                    nop                                     ;[19a5] 00
                    nop                                     ;[19a6] 00
                    jr        c,$19a9                       ;[19a7] 38 00
                    nop                                     ;[19a9] 00
                    bit       3,h                           ;[19aa] cb 5c
                    nop                                     ;[19ac] 00
                    nop                                     ;[19ad] 00
                    or        (hl)                          ;[19ae] b6
                    ld        e,h                           ;[19af] 5c
                    cp        e                             ;[19b0] bb
                    ld        e,h                           ;[19b1] 5c
                    bit       3,h                           ;[19b2] cb 5c
                    nop                                     ;[19b4] 00
                    nop                                     ;[19b5] 00
                    jp        z,$cc5c                       ;[19b6] ca 5c cc
                    ld        e,h                           ;[19b9] 5c
                    nop                                     ;[19ba] 00
                    nop                                     ;[19bb] 00
                    nop                                     ;[19bc] 00
                    nop                                     ;[19bd] 00
                    nop                                     ;[19be] 00
                    nop                                     ;[19bf] 00
                    adc       $5c                           ;[19c0] ce 5c
                    adc       $5c                           ;[19c2] ce 5c
                    adc       $5c                           ;[19c4] ce 5c
                    nop                                     ;[19c6] 00
                    sub       d                             ;[19c7] 92
                    ld        e,h                           ;[19c8] 5c
                    nop                                     ;[19c9] 00
                    ld        (bc),a                        ;[19ca] 02
                    nop                                     ;[19cb] 00
                    nop                                     ;[19cc] 00
                    nop                                     ;[19cd] 00
                    nop                                     ;[19ce] 00
                    nop                                     ;[19cf] 00
                    nop                                     ;[19d0] 00
                    nop                                     ;[19d1] 00
                    nop                                     ;[19d2] 00
                    nop                                     ;[19d3] 00
                    nop                                     ;[19d4] 00
                    nop                                     ;[19d5] 00
                    nop                                     ;[19d6] 00
                    nop                                     ;[19d7] 00
                    nop                                     ;[19d8] 00
                    nop                                     ;[19d9] 00
                    ld        e,b                           ;[19da] 58
                    rst       $38                           ;[19db] ff
                    nop                                     ;[19dc] 00
                    nop                                     ;[19dd] 00
                    nop                                     ;[19de] 00
                    nop                                     ;[19df] 00
                    inc       bc                            ;[19e0] 03
                    nop                                     ;[19e1] 00
                    nop                                     ;[19e2] 00
                    nop                                     ;[19e3] 00
                    ld        b,b                           ;[19e4] 40
                    ret       po                            ;[19e5] e0
                    ld        d,b                           ;[19e6] 50
                    nop                                     ;[19e7] 00
                    nop                                     ;[19e8] 00
                    nop                                     ;[19e9] 00
                    nop                                     ;[19ea] 00
                    ld        bc,$0038                      ;[19eb] 01 38 00
                    jr        c,$19f0                       ;[19ee] 38 00
                    nop                                     ;[19f0] 00
                    nop                                     ;[19f1] 00
                    nop                                     ;[19f2] 00
                    nop                                     ;[19f3] 00
                    nop                                     ;[19f4] 00
                    nop                                     ;[19f5] 00
                    nop                                     ;[19f6] 00
                    nop                                     ;[19f7] 00
                    nop                                     ;[19f8] 00
                    nop                                     ;[19f9] 00
                    nop                                     ;[19fa] 00
                    nop                                     ;[19fb] 00
                    nop                                     ;[19fc] 00
                    nop                                     ;[19fd] 00
                    nop                                     ;[19fe] 00
                    call      $5c1c                         ;[19ff] cd 1c 5c
                    out       ($e3),a                       ;[1a02] d3 e3
                    ldir                                    ;[1a04] ed b0
                    xor       a                             ;[1a06] af
                    out       ($e3),a                       ;[1a07] d3 e3
                    jp        $5c23                         ;[1a09] c3 23 5c
                    nop                                     ;[1a0c] 00
                    nop                                     ;[1a0d] 00
                    nop                                     ;[1a0e] 00
                    nop                                     ;[1a0f] 00
                    nop                                     ;[1a10] 00
                    ld        d,a                           ;[1a11] 57
                    rst       $38                           ;[1a12] ff
                    rst       $38                           ;[1a13] ff
                    rst       $38                           ;[1a14] ff
                    call      p,$a809                       ;[1a15] f4 09 a8
                    djnz      $1a65                         ;[1a18] 10 4b
                    call      p,$c409                       ;[1a1a] f4 09 c4
                    dec       d                             ;[1a1d] 15
                    ld        d,e                           ;[1a1e] 53
                    add       c                             ;[1a1f] 81
                    rrca                                    ;[1a20] 0f
                    call      nz,$5815                      ;[1a21] c4 15 58
                    nop                                     ;[1a24] 00
                    ld        e,e                           ;[1a25] 5b
                    nop                                     ;[1a26] 00
                    ld        e,e                           ;[1a27] 5b
                    ld        d,b                           ;[1a28] 50
                    add       b                             ;[1a29] 80
                    add       b                             ;[1a2a] 80
                    dec       c                             ;[1a2b] 0d
                    add       b                             ;[1a2c] 80
                    ld        hl,$c000                      ;[1a2d] 21 00 c0
                    ld        de,$c001                      ;[1a30] 11 01 c0
                    ld        bc,$3fff                      ;[1a33] 01 ff 3f
                    ld        (hl),l                        ;[1a36] 75
                    ldir                                    ;[1a37] ed b0
                    ld        hl,$0020                      ;[1a39] 21 20 00
                    ld        ($d73d),hl                    ;[1a3c] 22 3d d7
                    ld        a,$87                         ;[1a3f] 3e 87
                    out       ($e3),a                       ;[1a41] d3 e3
                    ld        hl,$2252                      ;[1a43] 21 52 22
                    ld        a,(hl)                        ;[1a46] 7e
                    ld        ($5c81),a                     ;[1a47] 32 81 5c
                    inc       hl                            ;[1a4a] 23
                    ld        a,(hl)                        ;[1a4b] 7e
                    push      af                            ;[1a4c] f5
                    ld        (hl),$00                      ;[1a4d] 36 00
                    xor       a                             ;[1a4f] af
                    out       ($e3),a                       ;[1a50] d3 e3
                    in        a,($3f)                       ;[1a52] db 3f
                    ld        a,$ff                         ;[1a54] 3e ff
                    rst       $20                           ;[1a56] e7
                    dec       l                             ;[1a57] 2d
                    ld        bc,$21f5                      ;[1a58] 01 f5 21
                    dec       hl                            ;[1a5b] 2b
                    ret       c                             ;[1a5c] d8
                    push      hl                            ;[1a5d] e5
                    ld        (hl),a                        ;[1a5e] 77
                    inc       hl                            ;[1a5f] 23
                    ld        (hl),$3a                      ;[1a60] 36 3a
                    inc       hl                            ;[1a62] 23
                    ld        (hl),$ff                      ;[1a63] 36 ff
                    pop       hl                            ;[1a65] e1
                    push      hl                            ;[1a66] e5
                    ld        a,$01                         ;[1a67] 3e 01
                    rst       $20                           ;[1a69] e7
                    or        c                             ;[1a6a] b1
                    ld        bc,$0106                      ;[1a6b] 01 06 01
                    rst       $10                           ;[1a6e] d7
                    ld        b,c                           ;[1a6f] 41
                    inc       hl                            ;[1a70] 23
                    nextreg $55,$05                         ;[1a71] ed 91 55 05
                    di                                      ;[1a75] f3
                    pop       hl                            ;[1a76] e1
                    ld        de,$3a54                      ;[1a77] 11 54 3a
                    ld        bc,$0100                      ;[1a7a] 01 00 01
                    ldir                                    ;[1a7d] ed b0
                    pop       af                            ;[1a7f] f1
                    ld        ($3a52),a                     ;[1a80] 32 52 3a
                    in        a,($bf)                       ;[1a83] db bf
                    ld        a,$8c                         ;[1a85] 3e 8c
                    out       ($e3),a                       ;[1a87] d3 e3
                    ld        hl,$34de                      ;[1a89] 21 de 34
                    ld        de,$5e00                      ;[1a8c] 11 00 5e
                    ld        bc,$013c                      ;[1a8f] 01 3c 01
                    push      bc                            ;[1a92] c5
                    push      de                            ;[1a93] d5
                    ldir                                    ;[1a94] ed b0
                    in        a,($3f)                       ;[1a96] db 3f
                    pop       hl                            ;[1a98] e1
                    pop       bc                            ;[1a99] c1
                    ld        de,$3916                      ;[1a9a] 11 16 39
                    ldir                                    ;[1a9d] ed b0
                    pop       af                            ;[1a9f] f1
                    and       a                             ;[1aa0] a7
                    jr        z,$1ab6                       ;[1aa1] 28 13
                    xor       a                             ;[1aa3] af
                    ld        ($3f2e),a                     ;[1aa4] 32 2e 3f
                    ld        ($3e98),a                     ;[1aa7] 32 98 3e
                    ld        hl,$3ea1                      ;[1aaa] 21 a1 3e
                    ld        de,$3ea2                      ;[1aad] 11 a2 3e
                    ld        bc,$008b                      ;[1ab0] 01 8b 00
                    ld        (hl),a                        ;[1ab3] 77
                    ldir                                    ;[1ab4] ed b0
                    ei                                      ;[1ab6] fb
                    scf                                     ;[1ab7] 37
                    ret                                     ;[1ab8] c9

                    nop                                     ;[1ab9] 00
                    nop                                     ;[1aba] 00
                    inc       bc                            ;[1abb] 03
                    nop                                     ;[1abc] 00
                    inc       a                             ;[1abd] 3c
                    inc       b                             ;[1abe] 04
                    ld        e,d                           ;[1abf] 5a
                    jr        c,$1af2                       ;[1ac0] 38 30
                    cp        d                             ;[1ac2] ba
                    inc       b                             ;[1ac3] 04
                    ld        b,d                           ;[1ac4] 42
                    ld        c,c                           ;[1ac5] 49
                    ld        c,(hl)                        ;[1ac6] 4e
                    cp        d                             ;[1ac7] ba
                    ld        l,$7a                         ;[1ac8] 2e 7a
                    jr        c,$1afc                       ;[1aca] 38 30
                    rst       $38                           ;[1acc] ff
                    add       b                             ;[1acd] 80
                    inc       d                             ;[1ace] 14
                    add       b                             ;[1acf] 80
                    inc       d                             ;[1ad0] 14
                    add       c                             ;[1ad1] 81
                    jr        nz,$1ae8                      ;[1ad2] 20 14
                    add       b                             ;[1ad4] 80
                    inc       d                             ;[1ad5] 14
                    ld        bc,$4ea0                      ;[1ad6] 01 a0 4e
                    ld        h,c                           ;[1ad9] 61
                    halt                                    ;[1ada] 76
                    ld        l,c                           ;[1adb] 69
                    ld        h,a                           ;[1adc] 67
                    ld        h,c                           ;[1add] 61
                    ld        (hl),h                        ;[1ade] 74
                    ld        h,l                           ;[1adf] 65
                    jr        nz,$1b59                      ;[1ae0] 20 77
                    ld        l,c                           ;[1ae2] 69
                    ld        (hl),h                        ;[1ae3] 74
                    ld        l,b                           ;[1ae4] 68
                    jr        nz,$1b4a                      ;[1ae5] 20 63
                    ld        (hl),l                        ;[1ae7] 75
                    ld        (hl),d                        ;[1ae8] 72
                    ld        (hl),e                        ;[1ae9] 73
                    ld        l,a                           ;[1aea] 6f
                    ld        (hl),d                        ;[1aeb] 72
                    jr        nz,$1b59                      ;[1aec] 20 6b
                    ld        h,l                           ;[1aee] 65
                    ld        a,c                           ;[1aef] 79
                    ld        (hl),e                        ;[1af0] 73
                    inc       l                             ;[1af1] 2c
                    jr        nz,$1b39                      ;[1af2] 20 45
                    ld        c,(hl)                        ;[1af4] 4e
                    ld        d,h                           ;[1af5] 54
                    ld        b,l                           ;[1af6] 45
                    ld        d,d                           ;[1af7] 52
                    inc       l                             ;[1af8] 2c
                    jr        nz,$1b40                      ;[1af9] 20 45
                    ld        b,h                           ;[1afb] 44
                    ld        c,c                           ;[1afc] 49
                    ld        d,h                           ;[1afd] 54
                    jr        nz,$1b61                      ;[1afe] 20 61
                    ld        l,(hl)                        ;[1b00] 6e
                    ld        h,h                           ;[1b01] 64
                    jr        nz,$1b48                      ;[1b02] 20 44
                    dec       c                             ;[1b04] 0d
                    ld        d,b                           ;[1b05] 50
                    ld        (hl),d                        ;[1b06] 72
                    ld        h,l                           ;[1b07] 65
                    ld        (hl),e                        ;[1b08] 73
                    ld        (hl),e                        ;[1b09] 73
                    jr        nz,$1b51                      ;[1b0a] 20 45
                    ld        c,(hl)                        ;[1b0c] 4e
                    ld        d,h                           ;[1b0d] 54
                    ld        b,l                           ;[1b0e] 45
                    ld        d,d                           ;[1b0f] 52
                    jr        nz,$1b81                      ;[1b10] 20 6f
                    ld        l,(hl)                        ;[1b12] 6e
                    jr        nz,$1b43                      ;[1b13] 20 2e
                    ld        e,d                           ;[1b15] 5a
                    jr        c,$1b48                       ;[1b16] 38 30
                    jr        nz,$1b8e                      ;[1b18] 20 74
                    ld        l,a                           ;[1b1a] 6f
                    jr        nz,$1b8f                      ;[1b1b] 20 72
                    ld        h,l                           ;[1b1d] 65
                    ld        (hl),b                        ;[1b1e] 70
                    ld        l,h                           ;[1b1f] 6c
                    ld        h,c                           ;[1b20] 61
                    ld        h,e                           ;[1b21] 63
                    ld        h,l                           ;[1b22] 65
                    inc       l                             ;[1b23] 2c
                    jr        nz,$1b95                      ;[1b24] 20 6f
                    ld        (hl),d                        ;[1b26] 72
                    jr        nz,$1b7c                      ;[1b27] 20 53
                    ld        d,b                           ;[1b29] 50
                    ld        b,c                           ;[1b2a] 41
                    ld        b,e                           ;[1b2b] 43
                    ld        b,l                           ;[1b2c] 45
                    jr        nz,$1b95                      ;[1b2d] 20 66
                    ld        l,a                           ;[1b2f] 6f
                    ld        (hl),d                        ;[1b30] 72
                    jr        nz,$1ba1                      ;[1b31] 20 6e
                    ld        h,l                           ;[1b33] 65
                    rst       $30                           ;[1b34] f7
                    ld        c,(hl)                        ;[1b35] 4e
                    ld        h,c                           ;[1b36] 61
                    halt                                    ;[1b37] 76
                    ld        l,c                           ;[1b38] 69
                    ld        h,a                           ;[1b39] 67
                    ld        h,c                           ;[1b3a] 61
                    ld        (hl),h                        ;[1b3b] 74
                    ld        h,l                           ;[1b3c] 65
                    jr        nz,$1bb6                      ;[1b3d] 20 77
                    ld        l,c                           ;[1b3f] 69
                    ld        (hl),h                        ;[1b40] 74
                    ld        l,b                           ;[1b41] 68
                    jr        nz,$1ba7                      ;[1b42] 20 63
                    ld        (hl),l                        ;[1b44] 75
                    ld        (hl),d                        ;[1b45] 72
                    ld        (hl),e                        ;[1b46] 73
                    ld        l,a                           ;[1b47] 6f
                    ld        (hl),d                        ;[1b48] 72
                    jr        nz,$1bb6                      ;[1b49] 20 6b
                    ld        h,l                           ;[1b4b] 65
                    ld        a,c                           ;[1b4c] 79
                    ld        (hl),e                        ;[1b4d] 73
                    inc       l                             ;[1b4e] 2c
                    jr        nz,$1b96                      ;[1b4f] 20 45
                    ld        c,(hl)                        ;[1b51] 4e
                    ld        d,h                           ;[1b52] 54
                    ld        b,l                           ;[1b53] 45
                    ld        d,d                           ;[1b54] 52
                    inc       l                             ;[1b55] 2c
                    jr        nz,$1b9d                      ;[1b56] 20 45
                    ld        b,h                           ;[1b58] 44
                    ld        c,c                           ;[1b59] 49
                    ld        d,h                           ;[1b5a] 54
                    jr        nz,$1bbe                      ;[1b5b] 20 61
                    ld        l,(hl)                        ;[1b5d] 6e
                    ld        h,h                           ;[1b5e] 64
                    jr        nz,$1ba5                      ;[1b5f] 20 44
                    dec       c                             ;[1b61] 0d
                    ld        d,b                           ;[1b62] 50
                    ld        (hl),d                        ;[1b63] 72
                    ld        h,l                           ;[1b64] 65
                    ld        (hl),e                        ;[1b65] 73
                    ld        (hl),e                        ;[1b66] 73
                    jr        nz,$1bae                      ;[1b67] 20 45
                    ld        c,(hl)                        ;[1b69] 4e
                    ld        d,h                           ;[1b6a] 54
                    ld        b,l                           ;[1b6b] 45
                    ld        d,d                           ;[1b6c] 52
                    jr        nz,$1bde                      ;[1b6d] 20 6f
                    ld        l,(hl)                        ;[1b6f] 6e
                    jr        nz,$1ba0                      ;[1b70] 20 2e
                    ld        b,d                           ;[1b72] 42
                    ld        c,c                           ;[1b73] 49
                    ld        c,(hl)                        ;[1b74] 4e
                    jr        nz,$1beb                      ;[1b75] 20 74
                    ld        l,a                           ;[1b77] 6f
                    jr        nz,$1bec                      ;[1b78] 20 72
                    ld        h,l                           ;[1b7a] 65
                    ld        (hl),b                        ;[1b7b] 70
                    ld        l,h                           ;[1b7c] 6c
                    ld        h,c                           ;[1b7d] 61
                    ld        h,e                           ;[1b7e] 63
                    ld        h,l                           ;[1b7f] 65
                    inc       l                             ;[1b80] 2c
                    jr        nz,$1bf2                      ;[1b81] 20 6f
                    ld        (hl),d                        ;[1b83] 72
                    jr        nz,$1bd9                      ;[1b84] 20 53
                    ld        d,b                           ;[1b86] 50
                    ld        b,c                           ;[1b87] 41
                    ld        b,e                           ;[1b88] 43
                    ld        b,l                           ;[1b89] 45
                    jr        nz,$1bf2                      ;[1b8a] 20 66
                    ld        l,a                           ;[1b8c] 6f
                    ld        (hl),d                        ;[1b8d] 72
                    jr        nz,$1bfe                      ;[1b8e] 20 6e
                    ld        h,l                           ;[1b90] 65
                    rst       $30                           ;[1b91] f7
                    ld        d,e                           ;[1b92] 53
                    ld        h,c                           ;[1b93] 61
                    halt                                    ;[1b94] 76
                    ld        l,c                           ;[1b95] 69
                    ld        l,(hl)                        ;[1b96] 6e
                    ld        h,a                           ;[1b97] 67
                    ld        l,$2e                         ;[1b98] 2e 2e
                    ld        l,$8d                         ;[1b9a] 2e 8d
                    ld        d,(hl)                        ;[1b9c] 56
                    ld        h,l                           ;[1b9d] 65
                    ld        (hl),d                        ;[1b9e] 72
                    ld        (hl),e                        ;[1b9f] 73
                    ld        l,c                           ;[1ba0] 69
                    ld        l,a                           ;[1ba1] 6f
                    ld        l,(hl)                        ;[1ba2] 6e
                    jr        nz,$1c12                      ;[1ba3] 20 6d
                    ld        l,c                           ;[1ba5] 69
                    ld        (hl),e                        ;[1ba6] 73
                    ld        l,l                           ;[1ba7] 6d
                    ld        h,c                           ;[1ba8] 61
                    ld        (hl),h                        ;[1ba9] 74
                    ld        h,e                           ;[1baa] 63
                    ld        l,b                           ;[1bab] 68
                    ld        a,($3230)                     ;[1bac] 3a 30 32
                    jr        nc,$1bea                      ;[1baf] 30 39
                    jr        nz,$1c18                      ;[1bb1] 20 65
                    ld        l,(hl)                        ;[1bb3] 6e
                    ld        c,(hl)                        ;[1bb4] 4e
                    ld        h,l                           ;[1bb5] 65
                    ld        a,b                           ;[1bb6] 78
                    ld        (hl),h                        ;[1bb7] 74
                    ld        c,l                           ;[1bb8] 4d
                    ld        h,(hl)                        ;[1bb9] 66
                    ld        l,$72                         ;[1bba] 2e 72
                    ld        l,a                           ;[1bbc] 6f
                    ld        l,l                           ;[1bbd] 6d
                    ld        b,e                           ;[1bbe] 43
                    ld        h,c                           ;[1bbf] 61
                    ld        l,(hl)                        ;[1bc0] 6e
                    jr        nz,$1c32                      ;[1bc1] 20 6f
                    ld        l,(hl)                        ;[1bc3] 6e
                    ld        l,h                           ;[1bc4] 6c
                    ld        a,c                           ;[1bc5] 79
                    jr        nz,$1c3b                      ;[1bc6] 20 73
                    ld        l,(hl)                        ;[1bc8] 6e
                    ld        h,c                           ;[1bc9] 61
                    ld        (hl),b                        ;[1bca] 70
                    ld        (hl),e                        ;[1bcb] 73
                    ld        l,b                           ;[1bcc] 68
                    ld        l,a                           ;[1bcd] 6f
                    ld        (hl),h                        ;[1bce] 74
                    jr        nz,$1c34                      ;[1bcf] 20 63
                    ld        l,h                           ;[1bd1] 6c
                    ld        h,c                           ;[1bd2] 61
                    ld        (hl),e                        ;[1bd3] 73
                    ld        (hl),e                        ;[1bd4] 73
                    ld        l,c                           ;[1bd5] 69
                    ld        h,e                           ;[1bd6] 63
                    jr        nz,$1c4c                      ;[1bd7] 20 73
                    ld        l,a                           ;[1bd9] 6f
                    ld        h,(hl)                        ;[1bda] 66
                    ld        (hl),h                        ;[1bdb] 74
                    ld        (hl),a                        ;[1bdc] 77
                    ld        h,c                           ;[1bdd] 61
                    ld        (hl),d                        ;[1bde] 72
                    ld        h,l                           ;[1bdf] 65
                    jr        nz,$1c0a                      ;[1be0] 20 28
                    ld        l,(hl)                        ;[1be2] 6e
                    ld        l,a                           ;[1be3] 6f
                    ld        (hl),h                        ;[1be4] 74
                    jr        nz,$1c35                      ;[1be5] 20 4e
                    ld        h,l                           ;[1be7] 65
                    ld        a,b                           ;[1be8] 78
                    ld        (hl),h                        ;[1be9] 74
                    jr        nz,$1c5b                      ;[1bea] 20 6f
                    ld        (hl),d                        ;[1bec] 72
                    jr        nz,$1c1a                      ;[1bed] 20 2b
                    inc       sp                            ;[1bef] 33
                    xor       c                             ;[1bf0] a9
                    ld        b,l                           ;[1bf1] 45
                    ld        (hl),d                        ;[1bf2] 72
                    ld        (hl),d                        ;[1bf3] 72
                    ld        l,a                           ;[1bf4] 6f
                    ld        (hl),d                        ;[1bf5] 72
                    jr        nz,$1c64                      ;[1bf6] 20 6c
                    ld        l,a                           ;[1bf8] 6f
                    ld        h,c                           ;[1bf9] 61
                    ld        h,h                           ;[1bfa] 64
                    ld        l,c                           ;[1bfb] 69
                    ld        l,(hl)                        ;[1bfc] 6e
                    ld        h,a                           ;[1bfd] 67
                    jr        nz,$1c65                      ;[1bfe] 20 65
                    ld        l,(hl)                        ;[1c00] 6e
                    ld        c,l                           ;[1c01] 4d
                    ld        h,(hl)                        ;[1c02] 66
                    ld        l,$73                         ;[1c03] 2e 73
                    ld        a,c                           ;[1c05] 79
                    ld        (hl),e                        ;[1c06] 73
                    ld        d,e                           ;[1c07] 53
                    ld        h,c                           ;[1c08] 61
                    halt                                    ;[1c09] 76
                    ld        h,l                           ;[1c0a] 65
                    jr        nz,$1c80                      ;[1c0b] 20 73
                    ld        l,(hl)                        ;[1c0d] 6e
                    ld        h,c                           ;[1c0e] 61
                    ld        (hl),b                        ;[1c0f] 70
                    ld        (hl),e                        ;[1c10] 73
                    ld        l,b                           ;[1c11] 68
                    ld        l,a                           ;[1c12] 6f
                    ld        (hl),h                        ;[1c13] 74
                    jr        nz,$1c77                      ;[1c14] 20 61
                    ld        (hl),e                        ;[1c16] 73
                    ld        a,($53a0)                     ;[1c17] 3a a0 53
                    ld        h,c                           ;[1c1a] 61
                    halt                                    ;[1c1b] 76
                    ld        h,l                           ;[1c1c] 65
                    jr        nz,$1c81                      ;[1c1d] 20 62
                    ld        h,c                           ;[1c1f] 61
                    ld        l,(hl)                        ;[1c20] 6e
                    ld        l,e                           ;[1c21] 6b
                    ld        (hl),e                        ;[1c22] 73
                    jr        nz,$1c89                      ;[1c23] 20 64
                    ld        (hl),l                        ;[1c25] 75
                    ld        l,l                           ;[1c26] 6d
                    ld        (hl),b                        ;[1c27] 70
                    jr        nz,$1c8b                      ;[1c28] 20 61
                    ld        (hl),e                        ;[1c2a] 73
                    ld        a,($c1a0)                     ;[1c2b] 3a a0 c1
                    ld        a,$14                         ;[1c2e] 3e 14
                    add       b                             ;[1c30] 80
                    ld        e,$05                         ;[1c31] 1e 05
                    ld        c,$1a                         ;[1c33] 0e 1a
                    nop                                     ;[1c35] 00
                    ld        d,$17                         ;[1c36] 16 17
                    add       b                             ;[1c38] 80
                    ld        d,$16                         ;[1c39] 16 16
                    nop                                     ;[1c3b] 00
                    ld        b,e                           ;[1c3c] 43
                    ld        (hl),l                        ;[1c3d] 75
                    ld        (hl),d                        ;[1c3e] 72
                    ld        (hl),e                        ;[1c3f] 73
                    ld        l,a                           ;[1c40] 6f
                    ld        (hl),d                        ;[1c41] 72
                    jr        nz,$1ca8                      ;[1c42] 20 64
                    ld        l,a                           ;[1c44] 6f
                    ld        (hl),a                        ;[1c45] 77
                    ld        l,(hl)                        ;[1c46] 6e
                    cpl                                     ;[1c47] 2f
                    ld        (hl),l                        ;[1c48] 75
                    ld        (hl),b                        ;[1c49] 70
                    jr        nz,$1c77                      ;[1c4a] 20 2b
                    cpl                                     ;[1c4c] 2f
                    dec       l                             ;[1c4d] 2d
                    jr        nz,$1c82                      ;[1c4e] 20 32
                    dec       (hl)                          ;[1c50] 35
                    ld        (hl),$20                      ;[1c51] 36 20
                    ld        h,d                           ;[1c53] 62
                    ld        a,c                           ;[1c54] 79
                    ld        (hl),h                        ;[1c55] 74
                    ld        h,l                           ;[1c56] 65
                    ld        (hl),e                        ;[1c57] 73
                    inc       l                             ;[1c58] 2c
                    jr        nz,$1ccd                      ;[1c59] 20 72
                    ld        l,c                           ;[1c5b] 69
                    ld        h,a                           ;[1c5c] 67
                    ld        l,b                           ;[1c5d] 68
                    ld        (hl),h                        ;[1c5e] 74
                    cpl                                     ;[1c5f] 2f
                    ld        l,h                           ;[1c60] 6c
                    ld        h,l                           ;[1c61] 65
                    ld        h,(hl)                        ;[1c62] 66
                    ld        (hl),h                        ;[1c63] 74
                    jr        nz,$1c91                      ;[1c64] 20 2b
                    cpl                                     ;[1c66] 2f
                    dec       l                             ;[1c67] 2d
                    jr        nz,$1ca2                      ;[1c68] 20 38
                    ld        c,e                           ;[1c6a] 4b
                    inc       l                             ;[1c6b] 2c
                    jr        nz,$1cb0                      ;[1c6c] 20 42
                    ld        d,d                           ;[1c6e] 52
                    ld        b,l                           ;[1c6f] 45
                    ld        b,c                           ;[1c70] 41
                    ld        c,e                           ;[1c71] 4b
                    jr        nz,$1ce8                      ;[1c72] 20 74
                    ld        l,a                           ;[1c74] 6f
                    jr        nz,$1cdc                      ;[1c75] 20 65
                    ld        a,b                           ;[1c77] 78
                    ld        l,c                           ;[1c78] 69
                    ld        (hl),h                        ;[1c79] 74
                    dec       c                             ;[1c7a] 0d
                    jp        $c242                         ;[1c7b] c3 42 c2
                    ld        h,c                           ;[1c7e] 61
                    ld        l,(hl)                        ;[1c7f] 6e
                    ld        l,e                           ;[1c80] 6b
                    jr        nz,$1c46                      ;[1c81] 20 c3
                    ld        c,a                           ;[1c83] 4f
                    jp        nz,$6666                      ;[1c84] c2 66 66
                    ld        (hl),e                        ;[1c87] 73
                    ld        h,l                           ;[1c88] 65
                    ld        (hl),h                        ;[1c89] 74
                    jr        nz,$1c4f                      ;[1c8a] 20 c3
                    ld        b,c                           ;[1c8c] 41
                    jp        nz,$6464                      ;[1c8d] c2 64 64
                    ld        (hl),d                        ;[1c90] 72
                    ld        h,l                           ;[1c91] 65
                    ld        (hl),e                        ;[1c92] 73
                    ld        (hl),e                        ;[1c93] 73
                    jr        nz,$1c59                      ;[1c94] 20 c3
                    ld        b,(hl)                        ;[1c96] 46
                    jp        nz,$6e69                      ;[1c97] c2 69 6e
                    ld        h,h                           ;[1c9a] 64
                    jr        nz,$1c60                      ;[1c9b] 20 c3
                    ld        c,(hl)                        ;[1c9d] 4e
                    jp        nz,$7865                      ;[1c9e] c2 65 78
                    ld        (hl),h                        ;[1ca1] 74
                    jr        nz,$1c67                      ;[1ca2] 20 c3
                    ld        b,e                           ;[1ca4] 43
                    jp        nz,$706f                      ;[1ca5] c2 6f 70
                    ld        a,c                           ;[1ca8] 79
                    jr        nz,$1c6e                      ;[1ca9] 20 c3
                    ld        d,e                           ;[1cab] 53
                    jp        nz,$6174                      ;[1cac] c2 74 61
                    ld        (hl),h                        ;[1caf] 74
                    ld        (hl),l                        ;[1cb0] 75
                    ld        (hl),e                        ;[1cb1] 73
                    jr        nz,$1c77                      ;[1cb2] 20 c3
                    ld        d,h                           ;[1cb4] 54
                    jp        nz,$7865                      ;[1cb5] c2 65 78
                    ld        (hl),h                        ;[1cb8] 74
                    jr        nz,$1c7e                      ;[1cb9] 20 c3
                    ld        b,l                           ;[1cbb] 45
                    ld        b,h                           ;[1cbc] 44
                    ld        c,c                           ;[1cbd] 49
                    ld        d,h                           ;[1cbe] 54
                    jp        nz,$6820                      ;[1cbf] c2 20 68
                    ld        h,l                           ;[1cc2] 65
                    ld        a,b                           ;[1cc3] 78
                    and       b                             ;[1cc4] a0
                    jp        $6142                         ;[1cc5] c3 42 61
                    ld        l,(hl)                        ;[1cc8] 6e
                    ld        l,e                           ;[1cc9] 6b
                    jp        nz,$20a0                      ;[1cca] c2 a0 20
                    jp        $664f                         ;[1ccd] c3 4f 66
                    ld        h,(hl)                        ;[1cd0] 66
                    ld        (hl),e                        ;[1cd1] 73
                    ld        h,l                           ;[1cd2] 65
                    ld        (hl),h                        ;[1cd3] 74
                    jp        nz,$c3a0                      ;[1cd4] c2 a0 c3
                    ld        b,c                           ;[1cd7] 41
                    ld        h,h                           ;[1cd8] 64
                    ld        h,h                           ;[1cd9] 64
                    ld        (hl),d                        ;[1cda] 72
                    ld        h,l                           ;[1cdb] 65
                    ld        (hl),e                        ;[1cdc] 73
                    ld        (hl),e                        ;[1cdd] 73
                    jp        nz,$16a0                      ;[1cde] c2 a0 16
                    nop                                     ;[1ce1] 00
                    jr        nz,$1d29                      ;[1ce2] 20 45
                    ld        l,(hl)                        ;[1ce4] 6e
                    ld        (hl),h                        ;[1ce5] 74
                    ld        h,l                           ;[1ce6] 65
                    ld        (hl),d                        ;[1ce7] 72
                    and       b                             ;[1ce8] a0
                    ld        h,d                           ;[1ce9] 62
                    ld        h,c                           ;[1cea] 61
                    ld        l,(hl)                        ;[1ceb] 6e
                    ld        l,e                           ;[1cec] 6b
                    ld        a,($6fa0)                     ;[1ced] 3a a0 6f
                    ld        h,(hl)                        ;[1cf0] 66
                    ld        h,(hl)                        ;[1cf1] 66
                    ld        (hl),e                        ;[1cf2] 73
                    ld        h,l                           ;[1cf3] 65
                    ld        (hl),h                        ;[1cf4] 74
                    ld        a,($61a0)                     ;[1cf5] 3a a0 61
                    ld        h,h                           ;[1cf8] 64
                    ld        h,h                           ;[1cf9] 64
                    ld        (hl),d                        ;[1cfa] 72
                    ld        h,l                           ;[1cfb] 65
                    ld        (hl),e                        ;[1cfc] 73
                    ld        (hl),e                        ;[1cfd] 73
                    ld        a,($31a0)                     ;[1cfe] 3a a0 31
                    ld        (hl),e                        ;[1d01] 73
                    ld        (hl),h                        ;[1d02] 74
                    jr        nz,$1d74                      ;[1d03] 20 6f
                    ld        h,(hl)                        ;[1d05] 66
                    ld        h,(hl)                        ;[1d06] 66
                    ld        (hl),e                        ;[1d07] 73
                    ld        h,l                           ;[1d08] 65
                    ld        (hl),h                        ;[1d09] 74
                    ld        a,($31a0)                     ;[1d0a] 3a a0 31
                    ld        (hl),e                        ;[1d0d] 73
                    ld        (hl),h                        ;[1d0e] 74
                    jr        nz,$1d72                      ;[1d0f] 20 61
                    ld        h,h                           ;[1d11] 64
                    ld        h,h                           ;[1d12] 64
                    ld        (hl),d                        ;[1d13] 72
                    ld        h,l                           ;[1d14] 65
                    ld        (hl),e                        ;[1d15] 73
                    ld        (hl),e                        ;[1d16] 73
                    ld        a,($20a0)                     ;[1d17] 3a a0 20
                    ld        l,h                           ;[1d1a] 6c
                    ld        h,c                           ;[1d1b] 61
                    ld        (hl),e                        ;[1d1c] 73
                    ld        (hl),h                        ;[1d1d] 74
                    ld        a,($20a0)                     ;[1d1e] 3a a0 20
                    ld        h,h                           ;[1d21] 64
                    ld        h,l                           ;[1d22] 65
                    ld        (hl),e                        ;[1d23] 73
                    ld        (hl),h                        ;[1d24] 74
                    ld        a,($20a0)                     ;[1d25] 3a a0 20
                    ld        d,e                           ;[1d28] 53
                    ld        (hl),l                        ;[1d29] 75
                    ld        (hl),d                        ;[1d2a] 72
                    ld        h,l                           ;[1d2b] 65
                    cp        a                             ;[1d2c] bf
                    ld        d,$00                         ;[1d2d] 16 00
                    jr        nz,$1d79                      ;[1d2f] 20 48
                    cpl                                     ;[1d31] 2f
                    ld        d,h                           ;[1d32] 54
                    ccf                                     ;[1d33] 3f
                    and       b                             ;[1d34] a0
                    dec       d                             ;[1d35] 15
                    ld        bc,$15c3                      ;[1d36] 01 c3 15
                    nop                                     ;[1d39] 00
                    ret       nz                            ;[1d3a] c0
                    adc       b                             ;[1d3b] 88
                    pop       bc                            ;[1d3c] c1
                    inc       b                             ;[1d3d] 04
                    ld        d,$07                         ;[1d3e] 16 07
                    nop                                     ;[1d40] 00
                    jr        nz,$1d85                      ;[1d41] 20 42
                    ld        b,e                           ;[1d43] 43
                    jr        nz,$1d5c                      ;[1d44] 20 16
                    rlca                                    ;[1d46] 07
                    ld        b,d                           ;[1d47] 42
                    jr        nz,$1d81                      ;[1d48] 20 37
                    ld        h,(hl)                        ;[1d4a] 66
                    ld        h,(hl)                        ;[1d4b] 66
                    ld        h,h                           ;[1d4c] 64
                    jr        nz,$1d65                      ;[1d4d] 20 16
                    rlca                                    ;[1d4f] 07
                    ld        c,h                           ;[1d50] 4c
                    jr        nz,$1db7                      ;[1d51] 20 64
                    ld        h,(hl)                        ;[1d53] 66
                    ld        h,(hl)                        ;[1d54] 66
                    ld        h,h                           ;[1d55] 64
                    jr        nz,$1d65                      ;[1d56] 20 0d
                    jr        nz,$1d9e                      ;[1d58] 20 44
                    ld        b,l                           ;[1d5a] 45
                    jr        nz,$1d73                      ;[1d5b] 20 16
                    ex        af,af'                        ;[1d5d] 08
                    ld        b,d                           ;[1d5e] 42
                    jr        nz,$1d92                      ;[1d5f] 20 31
                    ld        h,(hl)                        ;[1d61] 66
                    ld        h,(hl)                        ;[1d62] 66
                    ld        h,h                           ;[1d63] 64
                    jr        nz,$1d7c                      ;[1d64] 20 16
                    ex        af,af'                        ;[1d66] 08
                    ld        c,h                           ;[1d67] 4c
                    jr        nz,$1dcf                      ;[1d68] 20 65
                    ld        h,(hl)                        ;[1d6a] 66
                    ld        h,(hl)                        ;[1d6b] 66
                    scf                                     ;[1d6c] 37
                    jr        nz,$1d7c                      ;[1d6d] 20 0d
                    jr        nz,$1db9                      ;[1d6f] 20 48
                    ld        c,h                           ;[1d71] 4c
                    jr        nz,$1d8a                      ;[1d72] 20 16
                    add       hl,bc                         ;[1d74] 09
                    ld        b,d                           ;[1d75] 42
                    jr        nz,$1dcc                      ;[1d76] 20 54
                    ld        l,c                           ;[1d78] 69
                    ld        l,l                           ;[1d79] 6d
                    ld        h,l                           ;[1d7a] 65
                    ld        a,b                           ;[1d7b] 78
                    ld        d,$09                         ;[1d7c] 16 09
                    ld        c,h                           ;[1d7e] 4c
                    ld        b,h                           ;[1d7f] 44
                    ld        l,c                           ;[1d80] 69
                    halt                                    ;[1d81] 76
                    ld        c,l                           ;[1d82] 4d
                    ld        c,l                           ;[1d83] 4d
                    ld        b,e                           ;[1d84] 43
                    dec       c                             ;[1d85] 0d
                    dec       c                             ;[1d86] 0d
                    jr        nz,$1dcb                      ;[1d87] 20 42
                    ld        b,e                           ;[1d89] 43
                    daa                                     ;[1d8a] 27
                    ld        d,$0b                         ;[1d8b] 16 0b
                    ld        b,d                           ;[1d8d] 42
                    ld        c,(hl)                        ;[1d8e] 4e
                    ld        a,b                           ;[1d8f] 78
                    ld        (hl),h                        ;[1d90] 74
                    ld        d,d                           ;[1d91] 52
                    ld        h,l                           ;[1d92] 65
                    ld        h,a                           ;[1d93] 67
                    ld        d,$0b                         ;[1d94] 16 0b
                    ld        c,h                           ;[1d96] 4c
                    ld        c,h                           ;[1d97] 4c
                    ld        ($6f70),a                     ;[1d98] 32 70 6f
                    ld        (hl),d                        ;[1d9b] 72
                    ld        (hl),h                        ;[1d9c] 74
                    dec       c                             ;[1d9d] 0d
                    jr        nz,$1de4                      ;[1d9e] 20 44
                    ld        b,l                           ;[1da0] 45
                    daa                                     ;[1da1] 27
                    dec       c                             ;[1da2] 0d
                    jr        nz,$1ded                      ;[1da3] 20 48
                    ld        c,h                           ;[1da5] 4c
                    daa                                     ;[1da6] 27
                    dec       c                             ;[1da7] 0d
                    dec       c                             ;[1da8] 0d
                    jr        nz,$1df4                      ;[1da9] 20 49
                    ld        e,b                           ;[1dab] 58
                    jr        nz,$1dc4                      ;[1dac] 20 16
                    rrca                                    ;[1dae] 0f
                    ld        b,d                           ;[1daf] 42
                    jr        nz,$1dd2                      ;[1db0] 20 20
                    jr        nz,$1dfd                      ;[1db2] 20 49
                    jr        nz,$1dd6                      ;[1db4] 20 20
                    ld        d,$0f                         ;[1db6] 16 0f
                    ld        c,h                           ;[1db8] 4c
                    jr        nz,$1ddb                      ;[1db9] 20 20
                    jr        nz,$1e0f                      ;[1dbb] 20 52
                    jr        nz,$1ddf                      ;[1dbd] 20 20
                    dec       c                             ;[1dbf] 0d
                    jr        nz,$1e0b                      ;[1dc0] 20 49
                    ld        e,c                           ;[1dc2] 59
                    jr        nz,$1ddb                      ;[1dc3] 20 16
                    djnz      $1e09                         ;[1dc5] 10 42
                    jr        nz,$1de9                      ;[1dc7] 20 20
                    ld        c,c                           ;[1dc9] 49
                    ld        c,l                           ;[1dca] 4d
                    jr        nz,$1ded                      ;[1dcb] 20 20
                    ld        d,$10                         ;[1dcd] 16 10
                    ld        c,h                           ;[1dcf] 4c
                    jr        nz,$1e1b                      ;[1dd0] 20 49
                    ld        b,(hl)                        ;[1dd2] 46
                    ld        b,(hl)                        ;[1dd3] 46
                    ld        sp,$0d20                      ;[1dd4] 31 20 0d
                    dec       c                             ;[1dd7] 0d
                    jr        nz,$1e2d                      ;[1dd8] 20 53
                    ld        d,b                           ;[1dda] 50
                    jr        nz,$1df3                      ;[1ddb] 20 16
                    ld        (de),a                        ;[1ddd] 12
                    ld        b,d                           ;[1dde] 42
                    jr        nz,$1e22                      ;[1ddf] 20 41
                    ld        b,(hl)                        ;[1de1] 46
                    jr        nz,$1dfa                      ;[1de2] 20 16
                    inc       de                            ;[1de4] 13
                    ld        b,a                           ;[1de5] 47
                    ld        b,c                           ;[1de6] 41
                    jr        nz,$1e09                      ;[1de7] 20 20
                    ld        b,(hl)                        ;[1de9] 46
                    jr        nz,$1e0c                      ;[1dea] 20 20
                    ld        (hl),e                        ;[1dec] 73
                    ld        a,d                           ;[1ded] 7a
                    jr        nz,$1e58                      ;[1dee] 20 68
                    jr        nz,$1e62                      ;[1df0] 20 70
                    ld        l,(hl)                        ;[1df2] 6e
                    ld        h,e                           ;[1df3] 63
                    dec       c                             ;[1df4] 0d
                    jr        nz,$1e47                      ;[1df5] 20 50
                    ld        b,e                           ;[1df7] 43
                    jr        nz,$1e10                      ;[1df8] 20 16
                    inc       d                             ;[1dfa] 14
                    ld        b,d                           ;[1dfb] 42
                    jr        nz,$1e3f                      ;[1dfc] 20 41
                    ld        b,(hl)                        ;[1dfe] 46
                    daa                                     ;[1dff] 27
                    inc       d                             ;[1e00] 14
                    add       b                             ;[1e01] 80
                    ld        d,$16                         ;[1e02] 16 16
                    nop                                     ;[1e04] 00
                    ld        c,(hl)                        ;[1e05] 4e
                    ld        h,c                           ;[1e06] 61
                    halt                                    ;[1e07] 76
                    ld        l,c                           ;[1e08] 69
                    ld        h,a                           ;[1e09] 67
                    ld        h,c                           ;[1e0a] 61
                    ld        (hl),h                        ;[1e0b] 74
                    ld        h,l                           ;[1e0c] 65
                    jr        nz,$1e83                      ;[1e0d] 20 74
                    ld        l,a                           ;[1e0f] 6f
                    jr        nz,$1e84                      ;[1e10] 20 72
                    ld        h,l                           ;[1e12] 65
                    ld        h,a                           ;[1e13] 67
                    ld        l,c                           ;[1e14] 69
                    ld        (hl),e                        ;[1e15] 73
                    ld        (hl),h                        ;[1e16] 74
                    ld        h,l                           ;[1e17] 65
                    ld        (hl),d                        ;[1e18] 72
                    jr        nz,$1e92                      ;[1e19] 20 77
                    ld        l,c                           ;[1e1b] 69
                    ld        (hl),h                        ;[1e1c] 74
                    ld        l,b                           ;[1e1d] 68
                    jr        nz,$1e83                      ;[1e1e] 20 63
                    ld        (hl),l                        ;[1e20] 75
                    ld        (hl),d                        ;[1e21] 72
                    ld        (hl),e                        ;[1e22] 73
                    ld        l,a                           ;[1e23] 6f
                    ld        (hl),d                        ;[1e24] 72
                    jr        nz,$1e92                      ;[1e25] 20 6b
                    ld        h,l                           ;[1e27] 65
                    ld        a,c                           ;[1e28] 79
                    ld        (hl),e                        ;[1e29] 73
                    inc       l                             ;[1e2a] 2c
                    jr        nz,$1e6f                      ;[1e2b] 20 42
                    ld        d,d                           ;[1e2d] 52
                    ld        b,l                           ;[1e2e] 45
                    ld        b,c                           ;[1e2f] 41
                    ld        c,e                           ;[1e30] 4b
                    jr        nz,$1ea7                      ;[1e31] 20 74
                    ld        l,a                           ;[1e33] 6f
                    jr        nz,$1e9b                      ;[1e34] 20 65
                    ld        a,b                           ;[1e36] 78
                    ld        l,c                           ;[1e37] 69
                    ld        (hl),h                        ;[1e38] 74
                    dec       c                             ;[1e39] 0d
                    jp        $4445                         ;[1e3a] c3 45 44
                    ld        c,c                           ;[1e3d] 49
                    ld        d,h                           ;[1e3e] 54
                    ret       nz                            ;[1e3f] c0
                    jr        nz,$1eb8                      ;[1e40] 20 76
                    ld        h,c                           ;[1e42] 61
                    ld        l,h                           ;[1e43] 6c
                    ld        (hl),l                        ;[1e44] 75
                    ld        h,l                           ;[1e45] 65
                    jr        nz,$1e0b                      ;[1e46] 20 c3
                    ld        c,l                           ;[1e48] 4d
                    jp        nz,$6d65                      ;[1e49] c2 65 6d
                    ld        l,a                           ;[1e4c] 6f
                    ld        (hl),d                        ;[1e4d] 72
                    ld        a,c                           ;[1e4e] 79
                    jr        nz,$1e14                      ;[1e4f] 20 c3
                    ld        b,d                           ;[1e51] 42
                    jp        nz,$6572                      ;[1e52] c2 72 65
                    ld        h,c                           ;[1e55] 61
                    ld        l,e                           ;[1e56] 6b
                    ld        (hl),b                        ;[1e57] 70
                    ld        l,a                           ;[1e58] 6f
                    ld        l,c                           ;[1e59] 69
                    ld        l,(hl)                        ;[1e5a] 6e
                    ld        (hl),h                        ;[1e5b] 74
                    ld        (hl),e                        ;[1e5c] 73
                    jr        nz,$1e22                      ;[1e5d] 20 c3
                    ld        b,e                           ;[1e5f] 43
                    jp        nz,$6e6f                      ;[1e60] c2 6f 6e
                    ld        (hl),h                        ;[1e63] 74
                    ld        l,c                           ;[1e64] 69
                    ld        l,(hl)                        ;[1e65] 6e
                    ld        (hl),l                        ;[1e66] 75
                    ld        h,l                           ;[1e67] 65
                    jr        nz,$1e2d                      ;[1e68] 20 c3
                    ld        d,(hl)                        ;[1e6a] 56
                    jp        nz,$6569                      ;[1e6b] c2 69 65
                    ld        (hl),a                        ;[1e6e] 77
                    ld        d,$17                         ;[1e6f] 16 17
                    ld        c,e                           ;[1e71] 4b
                    ld        c,a                           ;[1e72] 4f
                    ld        d,e                           ;[1e73] 53
                    jr        nz,$1ea8                      ;[1e74] 20 32
                    ld        l,$30                         ;[1e76] 2e 30
                    add       hl,sp                         ;[1e78] 39
                    jr        nz,$1e91                      ;[1e79] 20 16
                    ld        d,$49                         ;[1e7b] 16 49
                    ld        b,e                           ;[1e7d] 43
                    ld        l,a                           ;[1e7e] 6f
                    ld        (hl),d                        ;[1e7f] 72
                    ld        h,l                           ;[1e80] 65
                    and       b                             ;[1e81] a0
                    dec       l                             ;[1e82] 2d
                    dec       l                             ;[1e83] 2d
                    cp        (hl)                          ;[1e84] be
                    ld        sp,$7473                      ;[1e85] 31 73 74
                    jr        nz,$1eec                      ;[1e88] 20 62
                    ld        h,c                           ;[1e8a] 61
                    ld        l,(hl)                        ;[1e8b] 6e
                    ld        l,e                           ;[1e8c] 6b
                    ld        a,($6ca0)                     ;[1e8d] 3a a0 6c
                    ld        h,c                           ;[1e90] 61
                    ld        (hl),e                        ;[1e91] 73
                    ld        (hl),h                        ;[1e92] 74
                    jr        nz,$1ef7                      ;[1e93] 20 62
                    ld        h,c                           ;[1e95] 61
                    ld        l,(hl)                        ;[1e96] 6e
                    ld        l,e                           ;[1e97] 6b
                    ld        a,($16a0)                     ;[1e98] 3a a0 16
                    ld        d,$00                         ;[1e9b] 16 00
                    ld        c,(hl)                        ;[1e9d] 4e
                    ld        h,c                           ;[1e9e] 61
                    halt                                    ;[1e9f] 76
                    ld        l,c                           ;[1ea0] 69
                    ld        h,a                           ;[1ea1] 67
                    ld        h,c                           ;[1ea2] 61
                    ld        (hl),h                        ;[1ea3] 74
                    ld        h,l                           ;[1ea4] 65
                    jr        nz,$1f1b                      ;[1ea5] 20 74
                    ld        l,a                           ;[1ea7] 6f
                    jr        nz,$1f0c                      ;[1ea8] 20 62
                    ld        (hl),d                        ;[1eaa] 72
                    ld        h,l                           ;[1eab] 65
                    ld        h,c                           ;[1eac] 61
                    ld        l,e                           ;[1ead] 6b
                    ld        (hl),b                        ;[1eae] 70
                    ld        l,a                           ;[1eaf] 6f
                    ld        l,c                           ;[1eb0] 69
                    ld        l,(hl)                        ;[1eb1] 6e
                    ld        (hl),h                        ;[1eb2] 74
                    jr        nz,$1f2c                      ;[1eb3] 20 77
                    ld        l,c                           ;[1eb5] 69
                    ld        (hl),h                        ;[1eb6] 74
                    ld        l,b                           ;[1eb7] 68
                    jr        nz,$1f1d                      ;[1eb8] 20 63
                    ld        (hl),l                        ;[1eba] 75
                    ld        (hl),d                        ;[1ebb] 72
                    ld        (hl),e                        ;[1ebc] 73
                    ld        l,a                           ;[1ebd] 6f
                    ld        (hl),d                        ;[1ebe] 72
                    jr        nz,$1f2c                      ;[1ebf] 20 6b
                    ld        h,l                           ;[1ec1] 65
                    ld        a,c                           ;[1ec2] 79
                    ld        (hl),e                        ;[1ec3] 73
                    inc       l                             ;[1ec4] 2c
                    jr        nz,$1f09                      ;[1ec5] 20 42
                    ld        d,d                           ;[1ec7] 52
                    ld        b,l                           ;[1ec8] 45
                    ld        b,c                           ;[1ec9] 41
                    ld        c,e                           ;[1eca] 4b
                    jr        nz,$1f41                      ;[1ecb] 20 74
                    ld        l,a                           ;[1ecd] 6f
                    jr        nz,$1f35                      ;[1ece] 20 65
                    ld        a,b                           ;[1ed0] 78
                    ld        l,c                           ;[1ed1] 69
                    ld        (hl),h                        ;[1ed2] 74
                    dec       c                             ;[1ed3] 0d
                    pop       bc                            ;[1ed4] c1
                    ld        b,c                           ;[1ed5] 41
                    ret       nz                            ;[1ed6] c0
                    ld        h,h                           ;[1ed7] 64
                    ld        h,h                           ;[1ed8] 64
                    ld        (hl),d                        ;[1ed9] 72
                    ld        h,l                           ;[1eda] 65
                    ld        (hl),e                        ;[1edb] 73
                    ld        (hl),e                        ;[1edc] 73
                    jr        nz,$1ea0                      ;[1edd] 20 c1
                    ld        b,d                           ;[1edf] 42
                    ret       nz                            ;[1ee0] c0
                    ld        h,c                           ;[1ee1] 61
                    ld        l,(hl)                        ;[1ee2] 6e
                    ld        l,e                           ;[1ee3] 6b
                    cpl                                     ;[1ee4] 2f
                    ld        l,a                           ;[1ee5] 6f
                    ld        h,(hl)                        ;[1ee6] 66
                    ld        h,(hl)                        ;[1ee7] 66
                    ld        (hl),e                        ;[1ee8] 73
                    ld        h,l                           ;[1ee9] 65
                    ld        (hl),h                        ;[1eea] 74
                    jr        nz,$1eae                      ;[1eeb] 20 c1
                    ld        c,e                           ;[1eed] 4b
                    ret       nz                            ;[1eee] c0
                    ld        l,c                           ;[1eef] 69
                    ld        l,h                           ;[1ef0] 6c
                    ld        l,h                           ;[1ef1] 6c
                    jr        nz,$1eb5                      ;[1ef2] 20 c1
                    ld        b,h                           ;[1ef4] 44
                    ret       nz                            ;[1ef5] c0
                    ld        l,c                           ;[1ef6] 69
                    ld        (hl),e                        ;[1ef7] 73
                    ld        h,c                           ;[1ef8] 61
                    ld        h,d                           ;[1ef9] 62
                    ld        l,h                           ;[1efa] 6c
                    ld        h,l                           ;[1efb] 65
                    jr        nz,$1ebf                      ;[1efc] 20 c1
                    ld        b,l                           ;[1efe] 45
                    ret       nz                            ;[1eff] c0
                    ld        l,(hl)                        ;[1f00] 6e
                    ld        h,c                           ;[1f01] 61
                    ld        h,d                           ;[1f02] 62
                    ld        l,h                           ;[1f03] 6c
                    ld        h,l                           ;[1f04] 65
                    jr        nz,$1ec8                      ;[1f05] 20 c1
                    ld        b,a                           ;[1f07] 47
                    ret       nz                            ;[1f08] c0
                    ld        l,h                           ;[1f09] 6c
                    ld        l,a                           ;[1f0a] 6f
                    ld        h,d                           ;[1f0b] 62
                    ld        h,c                           ;[1f0c] 61
                    ld        l,h                           ;[1f0d] 6c
                    jr        nz,$1ed1                      ;[1f0e] 20 c1
                    ld        d,e                           ;[1f10] 53
                    ret       nz                            ;[1f11] c0
                    ld        (hl),h                        ;[1f12] 74
                    ld        h,c                           ;[1f13] 61
                    ld        (hl),h                        ;[1f14] 74
                    ld        (hl),l                        ;[1f15] 75
                    ld        (hl),e                        ;[1f16] 73
                    jr        nz,$1eda                      ;[1f17] 20 c1
                    ld        c,l                           ;[1f19] 4d
                    ret       nz                            ;[1f1a] c0
                    ld        h,l                           ;[1f1b] 65
                    ld        l,l                           ;[1f1c] 6d
                    ld        l,a                           ;[1f1d] 6f
                    ld        (hl),d                        ;[1f1e] 72
                    ld        a,c                           ;[1f1f] 79
                    jr        nz,$1f83                      ;[1f20] 20 61
                    ld        (hl),h                        ;[1f22] 74
                    jr        nz,$1f86                      ;[1f23] 20 61
                    ld        h,h                           ;[1f25] 64
                    ld        h,h                           ;[1f26] 64
                    ld        (hl),d                        ;[1f27] 72
                    jr        nz,$1eeb                      ;[1f28] 20 c1
                    ld        b,e                           ;[1f2a] 43
                    ret       nz                            ;[1f2b] c0
                    ld        l,a                           ;[1f2c] 6f
                    ld        l,(hl)                        ;[1f2d] 6e
                    ld        (hl),h                        ;[1f2e] 74
                    ld        l,c                           ;[1f2f] 69
                    ld        l,(hl)                        ;[1f30] 6e
                    ld        (hl),l                        ;[1f31] 75
                    ld        h,l                           ;[1f32] 65
                    jr        nz,$1f4b                      ;[1f33] 20 16
                    nop                                     ;[1f35] 00
                    ld        b,b                           ;[1f36] 40
                    pop       bc                            ;[1f37] c1
                    ld        b,a                           ;[1f38] 47
                    ld        l,h                           ;[1f39] 6c
                    ld        l,a                           ;[1f3a] 6f
                    ld        h,d                           ;[1f3b] 62
                    ld        h,c                           ;[1f3c] 61
                    ld        l,h                           ;[1f3d] 6c
                    ret       nz                            ;[1f3e] c0
                    dec       c                             ;[1f3f] 0d
                    dec       c                             ;[1f40] 0d
                    add       h                             ;[1f41] 84
                    ld        h,e                           ;[1f42] 63
                    ld        a,($6e2f)                     ;[1f43] 3a 2f 6e
                    ld        h,l                           ;[1f46] 65
                    ld        a,b                           ;[1f47] 78
                    ld        (hl),h                        ;[1f48] 74
                    ld        a,d                           ;[1f49] 7a
                    ld        a,b                           ;[1f4a] 78
                    ld        l,a                           ;[1f4b] 6f
                    ld        (hl),e                        ;[1f4c] 73
                    cpl                                     ;[1f4d] 2f
                    ld        h,l                           ;[1f4e] 65
                    ld        l,(hl)                        ;[1f4f] 6e
                    ld        c,l                           ;[1f50] 4d
                    ld        h,(hl)                        ;[1f51] 66
                    jr        nc,$1f82                      ;[1f52] 30 2e
                    ld        (hl),e                        ;[1f54] 73
                    ld        a,c                           ;[1f55] 79
                    di                                      ;[1f56] f3
                    ld        (de),a                        ;[1f57] 12
                    ld        bc,$5245                      ;[1f58] 01 45 52
                    ld        d,d                           ;[1f5b] 52
                    ld        c,a                           ;[1f5c] 4f
                    ld        d,d                           ;[1f5d] 52
                    ld        (de),a                        ;[1f5e] 12
                    nop                                     ;[1f5f] 00
                    dec       c                             ;[1f60] 0d
                    dec       c                             ;[1f61] 0d
                    ld        d,h                           ;[1f62] 54
                    ld        l,b                           ;[1f63] 68
                    ld        l,c                           ;[1f64] 69
                    ld        (hl),e                        ;[1f65] 73
                    jr        nz,$1fd8                      ;[1f66] 20 70
                    ld        (hl),d                        ;[1f68] 72
                    ld        l,a                           ;[1f69] 6f
                    ld        h,a                           ;[1f6a] 67
                    ld        (hl),d                        ;[1f6b] 72
                    ld        h,c                           ;[1f6c] 61
                    ld        l,l                           ;[1f6d] 6d
                    jr        nz,$1fd8                      ;[1f6e] 20 68
                    ld        h,c                           ;[1f70] 61
                    ld        (hl),e                        ;[1f71] 73
                    jr        nz,$1fd5                      ;[1f72] 20 61
                    ld        (hl),h                        ;[1f74] 74
                    ld        (hl),h                        ;[1f75] 74
                    ld        h,l                           ;[1f76] 65
                    ld        l,l                           ;[1f77] 6d
                    ld        (hl),b                        ;[1f78] 70
                    ld        (hl),h                        ;[1f79] 74
                    ld        h,l                           ;[1f7a] 65
                    ld        h,h                           ;[1f7b] 64
                    jr        nz,$1ff2                      ;[1f7c] 20 74
                    ld        l,a                           ;[1f7e] 6f
                    dec       c                             ;[1f7f] 0d
                    ld        h,c                           ;[1f80] 61
                    ld        h,e                           ;[1f81] 63
                    ld        h,e                           ;[1f82] 63
                    ld        h,l                           ;[1f83] 65
                    ld        (hl),e                        ;[1f84] 73
                    ld        (hl),e                        ;[1f85] 73
                    jr        nz,$1ffc                      ;[1f86] 20 74
                    ld        l,b                           ;[1f88] 68
                    ld        h,l                           ;[1f89] 65
                    jr        nz,$1fb7                      ;[1f8a] 20 2b
                    inc       sp                            ;[1f8c] 33
                    jr        nz,$1fd5                      ;[1f8d] 20 46
                    ld        b,h                           ;[1f8f] 44
                    ld        b,e                           ;[1f90] 43
                    jr        nz,$1ffb                      ;[1f91] 20 68
                    ld        h,c                           ;[1f93] 61
                    ld        (hl),d                        ;[1f94] 72
                    ld        h,h                           ;[1f95] 64
                    ld        (hl),a                        ;[1f96] 77
                    ld        h,c                           ;[1f97] 61
                    ld        (hl),d                        ;[1f98] 72
                    ld        h,l                           ;[1f99] 65
                    ld        l,$0d                         ;[1f9a] 2e 0d
                    dec       c                             ;[1f9c] 0d
                    ld        d,b                           ;[1f9d] 50
                    ld        (hl),d                        ;[1f9e] 72
                    ld        h,l                           ;[1f9f] 65
                    ld        (hl),e                        ;[1fa0] 73
                    ld        (hl),e                        ;[1fa1] 73
                    jr        nz,$2016                      ;[1fa2] 20 72
                    ld        h,l                           ;[1fa4] 65
                    ld        (hl),e                        ;[1fa5] 73
                    ld        h,l                           ;[1fa6] 65
                    ld        (hl),h                        ;[1fa7] 74
                    jr        nz,$2018                      ;[1fa8] 20 6e
                    ld        l,a                           ;[1faa] 6f
                    ld        (hl),a                        ;[1fab] 77
                    inc       l                             ;[1fac] 2c
                    jr        nz,$1ff8                      ;[1fad] 20 49
                    jr        nz,$2025                      ;[1faf] 20 74
                    ld        l,a                           ;[1fb1] 6f
                    jr        nz,$201d                      ;[1fb2] 20 69
                    ld        h,a                           ;[1fb4] 67
                    ld        l,(hl)                        ;[1fb5] 6e
                    ld        l,a                           ;[1fb6] 6f
                    ld        (hl),d                        ;[1fb7] 72
                    ld        h,l                           ;[1fb8] 65
                    dec       c                             ;[1fb9] 0d
                    ld        l,a                           ;[1fba] 6f
                    ld        (hl),d                        ;[1fbb] 72
                    jr        nz,$2002                      ;[1fbc] 20 44
                    jr        nz,$2034                      ;[1fbe] 20 74
                    ld        l,a                           ;[1fc0] 6f
                    jr        nz,$2028                      ;[1fc1] 20 65
                    ld        l,(hl)                        ;[1fc3] 6e
                    ld        (hl),h                        ;[1fc4] 74
                    ld        h,l                           ;[1fc5] 65
                    ld        (hl),d                        ;[1fc6] 72
                    jr        nz,$203d                      ;[1fc7] 20 74
                    ld        l,b                           ;[1fc9] 68
                    ld        h,l                           ;[1fca] 65
                    jr        nz,$2031                      ;[1fcb] 20 64
                    ld        h,l                           ;[1fcd] 65
                    ld        h,d                           ;[1fce] 62
                    ld        (hl),l                        ;[1fcf] 75
                    ld        h,a                           ;[1fd0] 67
                    ld        h,a                           ;[1fd1] 67
                    ld        h,l                           ;[1fd2] 65
                    ld        (hl),d                        ;[1fd3] 72
                    xor       (hl)                          ;[1fd4] ae
                    nop                                     ;[1fd5] 00
                    nop                                     ;[1fd6] 00
                    nop                                     ;[1fd7] 00
                    nop                                     ;[1fd8] 00
                    nop                                     ;[1fd9] 00
                    nop                                     ;[1fda] 00
                    nop                                     ;[1fdb] 00
                    nop                                     ;[1fdc] 00
                    nop                                     ;[1fdd] 00
                    nop                                     ;[1fde] 00
                    nop                                     ;[1fdf] 00
                    nop                                     ;[1fe0] 00
                    nop                                     ;[1fe1] 00
                    nop                                     ;[1fe2] 00
                    nop                                     ;[1fe3] 00
                    nop                                     ;[1fe4] 00
                    nop                                     ;[1fe5] 00
                    nop                                     ;[1fe6] 00
                    nop                                     ;[1fe7] 00
                    nop                                     ;[1fe8] 00
                    nop                                     ;[1fe9] 00
                    nop                                     ;[1fea] 00
                    nop                                     ;[1feb] 00
                    nop                                     ;[1fec] 00
                    nop                                     ;[1fed] 00
                    nop                                     ;[1fee] 00
                    nop                                     ;[1fef] 00
                    nop                                     ;[1ff0] 00
                    nop                                     ;[1ff1] 00
                    nop                                     ;[1ff2] 00
                    nop                                     ;[1ff3] 00
                    nop                                     ;[1ff4] 00
                    nop                                     ;[1ff5] 00
                    nop                                     ;[1ff6] 00
                    nop                                     ;[1ff7] 00
                    nop                                     ;[1ff8] 00
                    nop                                     ;[1ff9] 00
                    nop                                     ;[1ffa] 00
                    nop                                     ;[1ffb] 00
                    nop                                     ;[1ffc] 00
                    nop                                     ;[1ffd] 00
                    nop                                     ;[1ffe] 00
                    nop                                     ;[1fff] 00
