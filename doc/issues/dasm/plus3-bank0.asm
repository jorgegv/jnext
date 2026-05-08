                    di                                      ;[0000] f3
                    ld        bc,$6c03                      ;[0001] 01 03 6c
                    dec       bc                            ;[0004] 0b
                    ld        a,b                           ;[0005] 78
                    or        c                             ;[0006] b1
                    jr        nz,$0004                      ;[0007] 20 fb
                    jp        $010f                         ;[0009] c3 0f 01
                    ld        b,l                           ;[000c] 45
                    ld        b,h                           ;[000d] 44
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
                    jp        $00ae                         ;[0034] c3 ae 00
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
                    di                                      ;[0049] f3
                    call      $0074                         ;[004a] cd 74 00
                    ei                                      ;[004d] fb
                    ret                                     ;[004e] c9

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
                    nop                                     ;[0060] 00
                    nop                                     ;[0061] 00
                    nop                                     ;[0062] 00
                    nop                                     ;[0063] 00
                    nop                                     ;[0064] 00
                    nop                                     ;[0065] 00
                    push      af                            ;[0066] f5
                    push      hl                            ;[0067] e5
                    ld        hl,($5cb0)                    ;[0068] 2a b0 5c
                    ld        a,h                           ;[006b] 7c
                    or        l                             ;[006c] b5
                    jr        z,$0070                       ;[006d] 28 01
                    jp        (hl)                          ;[006f] e9
                    pop       hl                            ;[0070] e1
                    pop       af                            ;[0071] f1
                    retn                                    ;[0072] ed 45

                    push      af                            ;[0074] f5
                    push      bc                            ;[0075] c5
                    ld        bc,$7ffd                      ;[0076] 01 fd 7f
                    ld        a,($5b5c)                     ;[0079] 3a 5c 5b
                    or        $07                           ;[007c] f6 07
                    out       (c),a                         ;[007e] ed 79
                    ld        a,($e600)                     ;[0080] 3a 00 e6
                    or        a                             ;[0083] b7
                    jr        z,$00a3                       ;[0084] 28 1d
                    ld        a,($5c78)                     ;[0086] 3a 78 5c
                    bit       0,a                           ;[0089] cb 47
                    jr        nz,$00a3                      ;[008b] 20 16
                    ld        a,($e600)                     ;[008d] 3a 00 e6
                    dec       a                             ;[0090] 3d
                    ld        ($e600),a                     ;[0091] 32 00 e6
                    jr        nz,$00a3                      ;[0094] 20 0d
                    ld        bc,$1ffd                      ;[0096] 01 fd 1f
                    ld        a,($5b67)                     ;[0099] 3a 67 5b
                    and       $f7                           ;[009c] e6 f7
                    ld        ($5b67),a                     ;[009e] 32 67 5b
                    out       (c),a                         ;[00a1] ed 79
                    ld        bc,$7ffd                      ;[00a3] 01 fd 7f
                    ld        a,($5b5c)                     ;[00a6] 3a 5c 5b
                    out       (c),a                         ;[00a9] ed 79
                    pop       bc                            ;[00ab] c1
                    pop       af                            ;[00ac] f1
                    ret                                     ;[00ad] c9

                    ld        ($5b58),hl                    ;[00ae] 22 58 5b
                    ld        hl,$5b21                      ;[00b1] 21 21 5b
                    ex        (sp),hl                       ;[00b4] e3
                    push      hl                            ;[00b5] e5
                    ld        hl,($5b58)                    ;[00b6] 2a 58 5b
                    ex        (sp),hl                       ;[00b9] e3
                    jp        $5b00                         ;[00ba] c3 00 5b
                    push      af                            ;[00bd] f5
                    push      bc                            ;[00be] c5
                    ld        bc,$7ffd                      ;[00bf] 01 fd 7f
                    ld        a,($5b5c)                     ;[00c2] 3a 5c 5b
                    xor       $10                           ;[00c5] ee 10
                    di                                      ;[00c7] f3
                    ld        ($5b5c),a                     ;[00c8] 32 5c 5b
                    out       (c),a                         ;[00cb] ed 79
                    ld        bc,$1ffd                      ;[00cd] 01 fd 1f
                    ld        a,($5b67)                     ;[00d0] 3a 67 5b
                    xor       $04                           ;[00d3] ee 04
                    ld        ($5b67),a                     ;[00d5] 32 67 5b
                    out       (c),a                         ;[00d8] ed 79
                    ei                                      ;[00da] fb
                    pop       bc                            ;[00db] c1
                    pop       af                            ;[00dc] f1
                    ret                                     ;[00dd] c9

                    call      $5b00                         ;[00de] cd 00 5b
                    push      hl                            ;[00e1] e5
                    ld        hl,($5b5a)                    ;[00e2] 2a 5a 5b
                    ex        (sp),hl                       ;[00e5] e3
                    ret                                     ;[00e6] c9

                    push      hl                            ;[00e7] e5
                    ld        hl,$5b34                      ;[00e8] 21 34 5b
                    ex        (sp),hl                       ;[00eb] e3
                    push      af                            ;[00ec] f5
                    push      bc                            ;[00ed] c5
                    jp        $5b10                         ;[00ee] c3 10 5b
                    push      hl                            ;[00f1] e5
                    ld        hl,($5b5a)                    ;[00f2] 2a 5a 5b
                    ex        (sp),hl                       ;[00f5] e3
                    ret                                     ;[00f6] c9

                    di                                      ;[00f7] f3
                    ld        a,$10                         ;[00f8] 3e 10
                    ld        bc,$1ffd                      ;[00fa] 01 fd 1f
                    out       (c),a                         ;[00fd] ed 79
                    ld        ($5b67),a                     ;[00ff] 32 67 5b
                    set       4,a                           ;[0102] cb e7
                    ld        bc,$7ffd                      ;[0104] 01 fd 7f
                    out       (c),a                         ;[0107] ed 79
                    ld        ($5b5c),a                     ;[0109] 32 5c 5b
                    jp        $26a7                         ;[010c] c3 a7 26
                    ld        b,$08                         ;[010f] 06 08
                    ld        a,b                           ;[0111] 78
                    exx                                     ;[0112] d9
                    dec       a                             ;[0113] 3d
                    ld        bc,$7ffd                      ;[0114] 01 fd 7f
                    out       (c),a                         ;[0117] ed 79
                    ld        hl,$c000                      ;[0119] 21 00 c0
                    ld        de,$c001                      ;[011c] 11 01 c0
                    ld        bc,$3fff                      ;[011f] 01 ff 3f
                    ld        (hl),$00                      ;[0122] 36 00
                    ldir                                    ;[0124] ed b0
                    exx                                     ;[0126] d9
                    djnz      $0111                         ;[0127] 10 e8
                    xor       a                             ;[0129] af
                    ld        hl,$dcba                      ;[012a] 21 ba dc
                    ld        bc,$7ffd                      ;[012d] 01 fd 7f
                    ld        de,$0108                      ;[0130] 11 08 01
                    out       (c),a                         ;[0133] ed 79
                    ex        af,af'                        ;[0135] 08
                    ld        a,d                           ;[0136] 7a
                    ld        (hl),a                        ;[0137] 77
                    ld        a,(hl)                        ;[0138] 7e
                    and       d                             ;[0139] a2
                    jp        z,$036c                       ;[013a] ca 6c 03
                    cpl                                     ;[013d] 2f
                    ld        (hl),a                        ;[013e] 77
                    ld        a,(hl)                        ;[013f] 7e
                    and       d                             ;[0140] a2
                    jp        nz,$036c                      ;[0141] c2 6c 03
                    rlc       d                             ;[0144] cb 02
                    dec       e                             ;[0146] 1d
                    jr        nz,$0136                      ;[0147] 20 ed
                    ex        af,af'                        ;[0149] 08
                    inc       a                             ;[014a] 3c
                    cp        $08                           ;[014b] fe 08
                    jr        nz,$0130                      ;[014d] 20 e1
                    ld        c,$fd                         ;[014f] 0e fd
                    ld        d,$ff                         ;[0151] 16 ff
                    ld        e,$bf                         ;[0153] 1e bf
                    ld        b,d                           ;[0155] 42
                    ld        a,$0e                         ;[0156] 3e 0e
                    out       (c),a                         ;[0158] ed 79
                    ld        b,e                           ;[015a] 43
                    ld        a,$ff                         ;[015b] 3e ff
                    out       (c),a                         ;[015d] ed 79
                    jr        $0167                         ;[015f] 18 06
                    exx                                     ;[0161] d9
                    ld        a,b                           ;[0162] 78
                    out       ($fe),a                       ;[0163] d3 fe
                    jr        $0165                         ;[0165] 18 fe
                    xor       a                             ;[0167] af
                    ex        af,af'                        ;[0168] 08
                    ld        sp,$6000                      ;[0169] 31 00 60
                    ld        b,d                           ;[016c] 42
                    ld        a,$07                         ;[016d] 3e 07
                    out       (c),a                         ;[016f] ed 79
                    ld        b,e                           ;[0171] 43
                    ld        a,$ff                         ;[0172] 3e ff
                    out       (c),a                         ;[0174] ed 79
                    ld        de,$5b00                      ;[0176] 11 00 5b
                    ld        hl,$00bd                      ;[0179] 21 bd 00
                    ld        bc,$0052                      ;[017c] 01 52 00
                    ldir                                    ;[017f] ed b0
                    ld        a,$cf                         ;[0181] 3e cf
                    ld        ($5b5d),a                     ;[0183] 32 5d 5b
                    ld        hl,$ffff                      ;[0186] 21 ff ff
                    ld        ($5cb4),hl                    ;[0189] 22 b4 5c
                    ld        de,$3eaf                      ;[018c] 11 af 3e
                    ld        bc,$00a8                      ;[018f] 01 a8 00
                    ex        de,hl                         ;[0192] eb
                    rst       $28                           ;[0193] ef
                    ld        h,c                           ;[0194] 61
                    ld        d,$eb                         ;[0195] 16 eb
                    inc       hl                            ;[0197] 23
                    ld        ($5c7b),hl                    ;[0198] 22 7b 5c
                    dec       hl                            ;[019b] 2b
                    ld        bc,$0040                      ;[019c] 01 40 00
                    ld        ($5c38),bc                    ;[019f] ed 43 38 5c
                    ld        ($5cb2),hl                    ;[01a3] 22 b2 5c
                    ld        hl,$5b66                      ;[01a6] 21 66 5b
                    res       7,(hl)                        ;[01a9] cb be
                    ld        hl,$5b7b                      ;[01ab] 21 7b 5b
                    ld        (hl),$09                      ;[01ae] 36 09
                    ld        hl,$3c00                      ;[01b0] 21 00 3c
                    ld        ($5c36),hl                    ;[01b3] 22 36 5c
                    im        1                             ;[01b6] ed 56
                    ld        iy,$5c3a                      ;[01b8] fd 21 3a 5c
                    set       4,(iy+$01)                    ;[01bc] fd cb 01 e6
                    ld        hl,$5b66                      ;[01c0] 21 66 5b
                    res       3,(hl)                        ;[01c3] cb 9e
                    set       2,(hl)                        ;[01c5] cb d6
                    ld        hl,$000b                      ;[01c7] 21 0b 00
                    ld        ($5b5f),hl                    ;[01ca] 22 5f 5b
                    ld        hl,$5c6a                      ;[01cd] 21 6a 5c
                    res       6,(hl)                        ;[01d0] cb b6
                    xor       a                             ;[01d2] af
                    ld        ($5b61),a                     ;[01d3] 32 61 5b
                    ld        ($5b63),a                     ;[01d6] 32 63 5b
                    ld        ($5b65),a                     ;[01d9] 32 65 5b
                    ld        hl,$ec00                      ;[01dc] 21 00 ec
                    ld        ($ff24),hl                    ;[01df] 22 24 ff
                    ld        a,$50                         ;[01e2] 3e 50
                    ld        ($5b64),a                     ;[01e4] 32 64 5b
                    ld        hl,$000a                      ;[01e7] 21 0a 00
                    ld        ($5b75),hl                    ;[01ea] 22 75 5b
                    ld        ($5b77),hl                    ;[01ed] 22 77 5b
                    ld        a,$54                         ;[01f0] 3e 54
                    ld        ($5b79),a                     ;[01f2] 32 79 5b
                    ld        ($5b7a),a                     ;[01f5] 32 7a 5b
                    ld        hl,$5cb6                      ;[01f8] 21 b6 5c
                    ld        ($5c4f),hl                    ;[01fb] 22 4f 5c
                    ld        de,$03bd                      ;[01fe] 11 bd 03
                    ld        bc,$0015                      ;[0201] 01 15 00
                    ex        de,hl                         ;[0204] eb
                    ldir                                    ;[0205] ed b0
                    ex        de,hl                         ;[0207] eb
                    dec       hl                            ;[0208] 2b
                    ld        ($5c57),hl                    ;[0209] 22 57 5c
                    inc       hl                            ;[020c] 23
                    ld        ($5c53),hl                    ;[020d] 22 53 5c
                    ld        ($5c4b),hl                    ;[0210] 22 4b 5c
                    ld        (hl),$80                      ;[0213] 36 80
                    inc       hl                            ;[0215] 23
                    ld        ($5c59),hl                    ;[0216] 22 59 5c
                    ld        (hl),$0d                      ;[0219] 36 0d
                    inc       hl                            ;[021b] 23
                    ld        (hl),$80                      ;[021c] 36 80
                    inc       hl                            ;[021e] 23
                    ld        ($5c61),hl                    ;[021f] 22 61 5c
                    ld        ($5c63),hl                    ;[0222] 22 63 5c
                    ld        ($5c65),hl                    ;[0225] 22 65 5c
                    ld        a,$38                         ;[0228] 3e 38
                    ld        ($5c8d),a                     ;[022a] 32 8d 5c
                    ld        ($5c8f),a                     ;[022d] 32 8f 5c
                    ld        ($5c48),a                     ;[0230] 32 48 5c
                    xor       a                             ;[0233] af
                    ld        ($ec13),a                     ;[0234] 32 13 ec
                    ld        a,$07                         ;[0237] 3e 07
                    out       ($fe),a                       ;[0239] d3 fe
                    ld        hl,$0523                      ;[023b] 21 23 05
                    ld        ($5c09),hl                    ;[023e] 22 09 5c
                    dec       (iy-$3a)                      ;[0241] fd 35 c6
                    dec       (iy-$36)                      ;[0244] fd 35 ca
                    ld        hl,$03d2                      ;[0247] 21 d2 03
                    ld        de,$5c10                      ;[024a] 11 10 5c
                    ld        bc,$000e                      ;[024d] 01 0e 00
                    ldir                                    ;[0250] ed b0
                    res       1,(iy+$01)                    ;[0252] fd cb 01 8e
                    ld        (iy+$00),$ff                  ;[0256] fd 36 00 ff
                    ld        (iy+$31),$02                  ;[025a] fd 36 31 02
                    ex        af,af'                        ;[025e] 08
                    cp        $52                           ;[025f] fe 52
                    jp        z,$2682                       ;[0261] ca 82 26
                    ld        hl,($5cb2)                    ;[0264] 2a b2 5c
                    inc       hl                            ;[0267] 23
                    ld        sp,hl                         ;[0268] f9
                    ei                                      ;[0269] fb
                    rst       $28                           ;[026a] ef
                    ld        l,e                           ;[026b] 6b
                    dec       c                             ;[026c] 0d
                    call      $02af                         ;[026d] cd af 02
                    ld        de,$03e0                      ;[0270] 11 e0 03
                    call      $02a3                         ;[0273] cd a3 02
                    ld        hl,$5bff                      ;[0276] 21 ff 5b
                    ld        ($5b6a),hl                    ;[0279] 22 6a 5b
                    call      $05d1                         ;[027c] cd d1 05
                    ld        a,$38                         ;[027f] 3e 38
                    ld        ($ec11),a                     ;[0281] 32 11 ec
                    ld        ($ec0f),a                     ;[0284] 32 0f ec
                    call      $05ac                         ;[0287] cd ac 05
                    call      $3e80                         ;[028a] cd 80 3e
                    ld        a,l                           ;[028d] 7d
                    dec       h                             ;[028e] 25
                    call      $05d1                         ;[028f] cd d1 05
                    ld        (iy+$31),$02                  ;[0292] fd 36 31 02
                    set       5,(iy+$02)                    ;[0296] fd cb 02 ee
                    call      $0638                         ;[029a] cd 38 06
                    call      $05ac                         ;[029d] cd ac 05
                    jp        $0653                         ;[02a0] c3 53 06
                    ld        a,(de)                        ;[02a3] 1a
                    and       $7f                           ;[02a4] e6 7f
                    push      de                            ;[02a6] d5
                    rst       $10                           ;[02a7] d7
                    pop       de                            ;[02a8] d1
                    ld        a,(de)                        ;[02a9] 1a
                    inc       de                            ;[02aa] 13
                    add       a                             ;[02ab] 87
                    jr        nc,$02a3                      ;[02ac] 30 f5
                    ret                                     ;[02ae] c9

                    ld        a,$7f                         ;[02af] 3e 7f
                    in        a,($fe)                       ;[02b1] db fe
                    rra                                     ;[02b3] 1f
                    ret       c                             ;[02b4] d8
                    ld        a,$fe                         ;[02b5] 3e fe
                    in        a,($fe)                       ;[02b7] db fe
                    rra                                     ;[02b9] 1f
                    ret       c                             ;[02ba] d8
                    ld        a,$07                         ;[02bb] 3e 07
                    out       ($fe),a                       ;[02bd] d3 fe
                    ld        a,$02                         ;[02bf] 3e 02
                    rst       $28                           ;[02c1] ef
                    ld        bc,$af16                      ;[02c2] 01 16 af
                    ld        ($5c3c),a                     ;[02c5] 32 3c 5c
                    ld        a,$16                         ;[02c8] 3e 16
                    rst       $10                           ;[02ca] d7
                    xor       a                             ;[02cb] af
                    rst       $10                           ;[02cc] d7
                    xor       a                             ;[02cd] af
                    rst       $10                           ;[02ce] d7
                    ld        e,$08                         ;[02cf] 1e 08
                    ld        b,e                           ;[02d1] 43
                    ld        d,b                           ;[02d2] 50
                    ld        a,b                           ;[02d3] 78
                    dec       a                             ;[02d4] 3d
                    rl        a                             ;[02d5] cb 17
                    rl        a                             ;[02d7] cb 17
                    rl        a                             ;[02d9] cb 17
                    add       d                             ;[02db] 82
                    dec       a                             ;[02dc] 3d
                    ld        ($5c8f),a                     ;[02dd] 32 8f 5c
                    ld        hl,$03b5                      ;[02e0] 21 b5 03
                    ld        c,e                           ;[02e3] 4b
                    ld        a,(hl)                        ;[02e4] 7e
                    rst       $10                           ;[02e5] d7
                    inc       hl                            ;[02e6] 23
                    dec       c                             ;[02e7] 0d
                    jr        nz,$02e4                      ;[02e8] 20 fa
                    djnz      $02d3                         ;[02ea] 10 e7
                    ld        b,e                           ;[02ec] 43
                    dec       d                             ;[02ed] 15
                    jr        nz,$02d3                      ;[02ee] 20 e3
                    ld        hl,$4800                      ;[02f0] 21 00 48
                    ld        d,h                           ;[02f3] 54
                    ld        e,l                           ;[02f4] 5d
                    inc       de                            ;[02f5] 13
                    xor       a                             ;[02f6] af
                    ld        (hl),a                        ;[02f7] 77
                    ld        bc,$0fff                      ;[02f8] 01 ff 0f
                    ldir                                    ;[02fb] ed b0
                    ex        de,hl                         ;[02fd] eb
                    ld        de,$5900                      ;[02fe] 11 00 59
                    ld        bc,$0200                      ;[0301] 01 00 02
                    ldir                                    ;[0304] ed b0
                    di                                      ;[0306] f3
                    ld        de,$0370                      ;[0307] 11 70 03
                    ld        l,$07                         ;[030a] 2e 07
                    ld        bc,$0099                      ;[030c] 01 99 00
                    dec       bc                            ;[030f] 0b
                    ld        a,b                           ;[0310] 78
                    or        c                             ;[0311] b1
                    jr        nz,$030f                      ;[0312] 20 fb
                    ld        a,l                           ;[0314] 7d
                    xor       $10                           ;[0315] ee 10
                    ld        l,a                           ;[0317] 6f
                    out       ($fe),a                       ;[0318] d3 fe
                    dec       de                            ;[031a] 1b
                    ld        a,d                           ;[031b] 7a
                    or        e                             ;[031c] b3
                    jr        nz,$030c                      ;[031d] 20 ed
                    ld        de,$2000                      ;[031f] 11 00 20
                    ld        ix,$03ad                      ;[0322] dd 21 ad 03
                    ld        l,(ix+$00)                    ;[0326] dd 6e 00
                    ld        h,(ix+$01)                    ;[0329] dd 66 01
                    inc       ix                            ;[032c] dd 23
                    inc       ix                            ;[032e] dd 23
                    ld        a,h                           ;[0330] 7c
                    or        l                             ;[0331] b5
                    jr        nz,$033a                      ;[0332] 20 06
                    ld        ix,$03ad                      ;[0334] dd 21 ad 03
                    jr        $0326                         ;[0338] 18 ec
                    inc       hl                            ;[033a] 23
                    ld        c,(hl)                        ;[033b] 4e
                    inc       hl                            ;[033c] 23
                    ld        b,(hl)                        ;[033d] 46
                    inc       hl                            ;[033e] 23
                    ld        a,b                           ;[033f] 78
                    or        c                             ;[0340] b1
                    jr        z,$0351                       ;[0341] 28 0e
                    in        a,(c)                         ;[0343] ed 78
                    and       $1f                           ;[0345] e6 1f
                    cp        (hl)                          ;[0347] be
                    jr        z,$033a                       ;[0348] 28 f0
                    dec       de                            ;[034a] 1b
                    ld        a,d                           ;[034b] 7a
                    or        e                             ;[034c] b3
                    jr        nz,$0326                      ;[034d] 20 d7
                    jr        $0307                         ;[034f] 18 b6
                    ld        c,(hl)                        ;[0351] 4e
                    inc       hl                            ;[0352] 23
                    ld        b,(hl)                        ;[0353] 46
                    push      bc                            ;[0354] c5
                    ret                                     ;[0355] c9

                    cp        $fb                           ;[0356] fe fb
                    ld        e,$fe                         ;[0358] 1e fe
                    ld        e,$fe                         ;[035a] fd 1e fe
                    cp        $1d                           ;[035d] fe 1d
                    cp        $df                           ;[035f] fe df
                    ld        e,$fe                         ;[0361] 1e fe
                    cp        a                             ;[0363] bf
                    dec       e                             ;[0364] 1d
                    cp        $7f                           ;[0365] fe 7f
                    dec       de                            ;[0367] 1b
                    nop                                     ;[0368] 00
                    nop                                     ;[0369] 00
                    call      po,$3e21                      ;[036a] e4 21 3e
                    ex        af,af'                        ;[036d] 08
                    sub       e                             ;[036e] 93
                    ex        af,af'                        ;[036f] 08
                    and       a                             ;[0370] a7
                    jr        nz,$0378                      ;[0371] 20 05
                    ex        af,af'                        ;[0373] 08
                    out       ($fe),a                       ;[0374] d3 fe
                    jr        $0376                         ;[0376] 18 fe
                    ex        af,af'                        ;[0378] 08
                    ld        c,a                           ;[0379] 4f
                    ld        b,$07                         ;[037a] 06 07
                    xor       b                             ;[037c] a8
                    ld        b,a                           ;[037d] 47
                    ld        a,c                           ;[037e] 79
                    out       ($fe),a                       ;[037f] d3 fe
                    ld        de,$0000                      ;[0381] 11 00 00
                    dec       de                            ;[0384] 1b
                    ld        a,d                           ;[0385] 7a
                    or        e                             ;[0386] b3
                    jr        nz,$0384                      ;[0387] 20 fb
                    ld        a,b                           ;[0389] 78
                    out       ($fe),a                       ;[038a] d3 fe
                    ld        de,$2aaa                      ;[038c] 11 aa 2a
                    dec       de                            ;[038f] 1b
                    ld        a,d                           ;[0390] 7a
                    or        e                             ;[0391] b3
                    jr        nz,$038f                      ;[0392] 20 fb
                    jr        $037e                         ;[0394] 18 e8
                    cp        $fb                           ;[0396] fe fb
                    dec       de                            ;[0398] 1b
                    cp        $df                           ;[0399] fe df
                    rla                                     ;[039b] 17
                    cp        $fd                           ;[039c] fe fd
                    ld        e,$00                         ;[039e] 1e 00
                    nop                                     ;[03a0] 00
                    push      de                            ;[03a1] d5
                    ld        ($7ffe),hl                    ;[03a2] 22 fe 7f
                    rrca                                    ;[03a5] 0f
                    cp        $fe                           ;[03a6] fe fe
                    rrca                                    ;[03a8] 0f
                    nop                                     ;[03a9] 00
                    nop                                     ;[03aa] 00
                    nop                                     ;[03ab] 00
                    nop                                     ;[03ac] 00
                    ld        d,l                           ;[03ad] 55
                    inc       bc                            ;[03ae] 03
                    sub       l                             ;[03af] 95
                    inc       bc                            ;[03b0] 03
                    and       d                             ;[03b1] a2
                    inc       bc                            ;[03b2] 03
                    nop                                     ;[03b3] 00
                    nop                                     ;[03b4] 00
                    inc       de                            ;[03b5] 13
                    nop                                     ;[03b6] 00
                    ld        sp,$1339                      ;[03b7] 31 39 13
                    ld        bc,$3738                      ;[03ba] 01 38 37
                    call      p,$a809                       ;[03bd] f4 09 a8
                    djnz      $040d                         ;[03c0] 10 4b
                    call      p,$c409                       ;[03c2] f4 09 c4
                    dec       d                             ;[03c5] 15
                    ld        d,e                           ;[03c6] 53
                    add       c                             ;[03c7] 81
                    rrca                                    ;[03c8] 0f
                    call      nz,$5815                      ;[03c9] c4 15 58
                    dec       b                             ;[03cc] 05
                    ld        a,($3a00)                     ;[03cd] 3a 00 3a
                    ld        d,b                           ;[03d0] 50
                    add       b                             ;[03d1] 80
                    ld        bc,$0600                      ;[03d2] 01 00 06
                    nop                                     ;[03d5] 00
                    dec       bc                            ;[03d6] 0b
                    nop                                     ;[03d7] 00
                    ld        bc,$0100                      ;[03d8] 01 00 01
                    nop                                     ;[03db] 00
                    ld        b,$00                         ;[03dc] 06 00
                    djnz      $03e0                         ;[03de] 10 00
                    ld        a,a                           ;[03e0] 7f
                    ld        sp,$3839                      ;[03e1] 31 39 38
                    ld        ($202c),a                     ;[03e4] 32 2c 20
                    ld        sp,$3839                      ;[03e7] 31 39 38
                    ld        (hl),$2c                      ;[03ea] 36 2c
                    jr        nz,$041f                      ;[03ec] 20 31
                    add       hl,sp                         ;[03ee] 39
                    jr        c,$0428                       ;[03ef] 38 37
                    jr        nz,$0434                      ;[03f1] 20 41
                    ld        l,l                           ;[03f3] 6d
                    ld        (hl),e                        ;[03f4] 73
                    ld        (hl),h                        ;[03f5] 74
                    ld        (hl),d                        ;[03f6] 72
                    ld        h,c                           ;[03f7] 61
                    ld        h,h                           ;[03f8] 64
                    jr        nz,$044b                      ;[03f9] 20 50
                    ld        l,h                           ;[03fb] 6c
                    ld        h,e                           ;[03fc] 63
                    ld        l,$8d                         ;[03fd] 2e 8d
                    ld        hl,$eef5                      ;[03ff] 21 f5 ee
                    res       0,(hl)                        ;[0402] cb 86
                    set       1,(hl)                        ;[0404] cb ce
                    ld        hl,($5c49)                    ;[0406] 2a 49 5c
                    ld        a,h                           ;[0409] 7c
                    or        l                             ;[040a] b5
                    jr        nz,$0410                      ;[040b] 20 03
                    ld        ($ec06),hl                    ;[040d] 22 06 ec
                    ld        a,($f9db)                     ;[0410] 3a db f9
                    push      af                            ;[0413] f5
                    ld        hl,($fc9a)                    ;[0414] 2a 9a fc
                    call      $141d                         ;[0417] cd 1d 14
                    ld        ($f9d7),hl                    ;[041a] 22 d7 f9
                    call      $12f5                         ;[041d] cd f5 12
                    call      $11a9                         ;[0420] cd a9 11
                    pop       af                            ;[0423] f1
                    or        a                             ;[0424] b7
                    jr        z,$0433                       ;[0425] 28 0c
                    push      af                            ;[0427] f5
                    call      $11b2                         ;[0428] cd b2 11
                    ex        de,hl                         ;[042b] eb
                    call      $133d                         ;[042c] cd 3d 13
                    pop       af                            ;[042f] f1
                    dec       a                             ;[0430] 3d
                    jr        $0424                         ;[0431] 18 f1
                    ld        c,$00                         ;[0433] 0e 00
                    call      $1187                         ;[0435] cd 87 11
                    ld        b,c                           ;[0438] 41
                    ld        a,($ec15)                     ;[0439] 3a 15 ec
                    ld        c,a                           ;[043c] 4f
                    push      bc                            ;[043d] c5
                    push      de                            ;[043e] d5
                    call      $11b2                         ;[043f] cd b2 11
                    ld        a,($eef5)                     ;[0442] 3a f5 ee
                    bit       1,a                           ;[0445] cb 4f
                    jr        z,$0466                       ;[0447] 28 1d
                    push      de                            ;[0449] d5
                    push      hl                            ;[044a] e5
                    ld        de,$0020                      ;[044b] 11 20 00
                    add       hl,de                         ;[044e] 19
                    bit       0,(hl)                        ;[044f] cb 46
                    jr        z,$0464                       ;[0451] 28 11
                    inc       hl                            ;[0453] 23
                    ld        d,(hl)                        ;[0454] 56
                    inc       hl                            ;[0455] 23
                    ld        e,(hl)                        ;[0456] 5e
                    or        a                             ;[0457] b7
                    ld        hl,($5c49)                    ;[0458] 2a 49 5c
                    sbc       hl,de                         ;[045b] ed 52
                    jr        nz,$0464                      ;[045d] 20 05
                    ld        hl,$eef5                      ;[045f] 21 f5 ee
                    set       0,(hl)                        ;[0462] cb c6
                    pop       hl                            ;[0464] e1
                    pop       de                            ;[0465] d1
                    push      bc                            ;[0466] c5
                    push      hl                            ;[0467] e5
                    ld        bc,$0023                      ;[0468] 01 23 00
                    ldir                                    ;[046b] ed b0
                    pop       hl                            ;[046d] e1
                    pop       bc                            ;[046e] c1
                    push      de                            ;[046f] d5
                    push      bc                            ;[0470] c5
                    ex        de,hl                         ;[0471] eb
                    ld        hl,$eef5                      ;[0472] 21 f5 ee
                    bit       0,(hl)                        ;[0475] cb 46
                    jr        z,$04a3                       ;[0477] 28 2a
                    ld        b,$00                         ;[0479] 06 00
                    ld        hl,($ec06)                    ;[047b] 2a 06 ec
                    ld        a,h                           ;[047e] 7c
                    or        l                             ;[047f] b5
                    jr        z,$0490                       ;[0480] 28 0e
                    push      hl                            ;[0482] e5
                    call      $0f14                         ;[0483] cd 14 0f
                    pop       hl                            ;[0486] e1
                    jr        nc,$049b                      ;[0487] 30 12
                    dec       hl                            ;[0489] 2b
                    inc       b                             ;[048a] 04
                    ld        ($ec06),hl                    ;[048b] 22 06 ec
                    jr        $047b                         ;[048e] 18 eb
                    call      $0f14                         ;[0490] cd 14 0f
                    call      nc,$0f36                      ;[0493] d4 36 0f
                    ld        hl,$eef5                      ;[0496] 21 f5 ee
                    ld        (hl),$00                      ;[0499] 36 00
                    ld        a,b                           ;[049b] 78
                    pop       bc                            ;[049c] c1
                    push      bc                            ;[049d] c5
                    ld        c,b                           ;[049e] 48
                    ld        b,a                           ;[049f] 47
                    call      $0ae1                         ;[04a0] cd e1 0a
                    pop       bc                            ;[04a3] c1
                    pop       de                            ;[04a4] d1
                    ld        a,c                           ;[04a5] 79
                    inc       b                             ;[04a6] 04
                    cp        b                             ;[04a7] b8
                    jr        nc,$043f                      ;[04a8] 30 95
                    ld        a,($eef5)                     ;[04aa] 3a f5 ee
                    bit       1,a                           ;[04ad] cb 4f
                    jr        z,$04d2                       ;[04af] 28 21
                    bit       0,a                           ;[04b1] cb 47
                    jr        nz,$04d2                      ;[04b3] 20 1d
                    ld        hl,($5c49)                    ;[04b5] 2a 49 5c
                    ld        a,h                           ;[04b8] 7c
                    or        l                             ;[04b9] b5
                    jr        z,$04c4                       ;[04ba] 28 08
                    ld        ($fc9a),hl                    ;[04bc] 22 9a fc
                    call      $12f5                         ;[04bf] cd f5 12
                    jr        $04cd                         ;[04c2] 18 09
                    ld        ($fc9a),hl                    ;[04c4] 22 9a fc
                    call      $1425                         ;[04c7] cd 25 14
                    ld        ($5c49),hl                    ;[04ca] 22 49 5c
                    pop       de                            ;[04cd] d1
                    pop       bc                            ;[04ce] c1
                    jp        $0406                         ;[04cf] c3 06 04
                    pop       de                            ;[04d2] d1
                    pop       bc                            ;[04d3] c1
                    cp        a                             ;[04d4] bf
                    push      af                            ;[04d5] f5
                    ld        a,c                           ;[04d6] 79
                    ld        c,b                           ;[04d7] 48
                    call      $1187                         ;[04d8] cd 87 11
                    ex        de,hl                         ;[04db] eb
                    push      af                            ;[04dc] f5
                    call      $1703                         ;[04dd] cd 03 17
                    pop       af                            ;[04e0] f1
                    ld        de,$0023                      ;[04e1] 11 23 00
                    add       hl,de                         ;[04e4] 19
                    inc       c                             ;[04e5] 0c
                    cp        c                             ;[04e6] b9
                    jr        nc,$04dc                      ;[04e7] 30 f3
                    pop       af                            ;[04e9] f1
                    ret       z                             ;[04ea] c8
                    call      $0ad7                         ;[04eb] cd d7 0a
                    call      $0c48                         ;[04ee] cd 48 0c
                    ld        hl,($ec06)                    ;[04f1] 2a 06 ec
                    dec       hl                            ;[04f4] 2b
                    ld        a,h                           ;[04f5] 7c
                    or        l                             ;[04f6] b5
                    ld        ($ec06),hl                    ;[04f7] 22 06 ec
                    jr        nz,$04ee                      ;[04fa] 20 f2
                    jp        $0ae1                         ;[04fc] c3 e1 0a
                    ret                                     ;[04ff] c9

                    ld        b,$00                         ;[0500] 06 00
                    ld        a,($ec15)                     ;[0502] 3a 15 ec
                    ld        d,a                           ;[0505] 57
                    jp        $1d70                         ;[0506] c3 70 1d
                    ld        b,$00                         ;[0509] 06 00
                    push      hl                            ;[050b] e5
                    ld        c,b                           ;[050c] 48
                    call      $1187                         ;[050d] cd 87 11
                    call      $133d                         ;[0510] cd 3d 13
                    pop       hl                            ;[0513] e1
                    ret       nc                            ;[0514] d0
                    call      $11b2                         ;[0515] cd b2 11
                    push      bc                            ;[0518] c5
                    push      hl                            ;[0519] e5
                    ld        hl,$0023                      ;[051a] 21 23 00
                    add       hl,de                         ;[051d] 19
                    ld        a,($ec15)                     ;[051e] 3a 15 ec
                    ld        c,a                           ;[0521] 4f
                    cp        b                             ;[0522] b8
                    jr        z,$0533                       ;[0523] 28 0e
                    push      bc                            ;[0525] c5
                    push      bc                            ;[0526] c5
                    ld        bc,$0023                      ;[0527] 01 23 00
                    ldir                                    ;[052a] ed b0
                    pop       bc                            ;[052c] c1
                    ld        a,c                           ;[052d] 79
                    inc       b                             ;[052e] 04
                    cp        b                             ;[052f] b8
                    jr        nz,$0526                      ;[0530] 20 f4
                    pop       bc                            ;[0532] c1
                    pop       hl                            ;[0533] e1
                    call      $1717                         ;[0534] cd 17 17
                    ld        bc,$0023                      ;[0537] 01 23 00
                    ldir                                    ;[053a] ed b0
                    scf                                     ;[053c] 37
                    pop       bc                            ;[053d] c1
                    ret                                     ;[053e] c9

                    ld        b,$00                         ;[053f] 06 00
                    call      $12fe                         ;[0541] cd fe 12
                    ret       nc                            ;[0544] d0
                    push      bc                            ;[0545] c5
                    push      hl                            ;[0546] e5
                    ld        a,($ec15)                     ;[0547] 3a 15 ec
                    ld        c,a                           ;[054a] 4f
                    call      $1187                         ;[054b] cd 87 11
                    call      $11f1                         ;[054e] cd f1 11
                    jr        nc,$0579                      ;[0551] 30 26
                    dec       de                            ;[0553] 1b
                    ld        hl,$0023                      ;[0554] 21 23 00
                    add       hl,de                         ;[0557] 19
                    ex        de,hl                         ;[0558] eb
                    push      bc                            ;[0559] c5
                    ld        a,b                           ;[055a] 78
                    cp        c                             ;[055b] b9
                    jr        z,$056a                       ;[055c] 28 0c
                    push      bc                            ;[055e] c5
                    ld        bc,$0023                      ;[055f] 01 23 00
                    lddr                                    ;[0562] ed b8
                    pop       bc                            ;[0564] c1
                    ld        a,b                           ;[0565] 78
                    dec       c                             ;[0566] 0d
                    cp        c                             ;[0567] b9
                    jr        c,$055e                       ;[0568] 38 f4
                    ex        de,hl                         ;[056a] eb
                    inc       de                            ;[056b] 13
                    pop       bc                            ;[056c] c1
                    pop       hl                            ;[056d] e1
                    call      $172b                         ;[056e] cd 2b 17
                    ld        bc,$0023                      ;[0571] 01 23 00
                    ldir                                    ;[0574] ed b0
                    scf                                     ;[0576] 37
                    pop       bc                            ;[0577] c1
                    ret                                     ;[0578] c9

                    pop       hl                            ;[0579] e1
                    pop       bc                            ;[057a] c1
                    ret                                     ;[057b] c9

                    push      de                            ;[057c] d5
                    ld        h,$00                         ;[057d] 26 00
                    ld        l,b                           ;[057f] 68
                    add       hl,de                         ;[0580] 19
                    ld        d,a                           ;[0581] 57
                    ld        a,b                           ;[0582] 78
                    ld        e,(hl)                        ;[0583] 5e
                    ld        (hl),d                        ;[0584] 72
                    ld        d,e                           ;[0585] 53
                    inc       hl                            ;[0586] 23
                    inc       a                             ;[0587] 3c
                    cp        $20                           ;[0588] fe 20
                    jr        c,$0583                       ;[058a] 38 f7
                    ld        a,e                           ;[058c] 7b
                    cp        $00                           ;[058d] fe 00
                    pop       de                            ;[058f] d1
                    ret                                     ;[0590] c9

                    push      de                            ;[0591] d5
                    ld        hl,$0020                      ;[0592] 21 20 00
                    add       hl,de                         ;[0595] 19
                    push      hl                            ;[0596] e5
                    ld        d,a                           ;[0597] 57
                    ld        a,$1f                         ;[0598] 3e 1f
                    jr        $05a3                         ;[059a] 18 07
                    ld        e,(hl)                        ;[059c] 5e
                    ld        (hl),d                        ;[059d] 72
                    ld        d,e                           ;[059e] 53
                    cp        b                             ;[059f] b8
                    jr        z,$05a6                       ;[05a0] 28 04
                    dec       a                             ;[05a2] 3d
                    dec       hl                            ;[05a3] 2b
                    jr        $059c                         ;[05a4] 18 f6
                    ld        a,e                           ;[05a6] 7b
                    cp        $00                           ;[05a7] fe 00
                    pop       hl                            ;[05a9] e1
                    pop       de                            ;[05aa] d1
                    ret                                     ;[05ab] c9

                    ex        af,af'                        ;[05ac] 08
                    ld        a,$00                         ;[05ad] 3e 00
                    di                                      ;[05af] f3
                    call      $05c6                         ;[05b0] cd c6 05
                    pop       af                            ;[05b3] f1
                    ld        ($5b58),hl                    ;[05b4] 22 58 5b
                    ld        hl,($5b6a)                    ;[05b7] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[05ba] ed 73 6a 5b
                    ld        sp,hl                         ;[05be] f9
                    ei                                      ;[05bf] fb
                    ld        hl,($5b58)                    ;[05c0] 2a 58 5b
                    push      af                            ;[05c3] f5
                    ex        af,af'                        ;[05c4] 08
                    ret                                     ;[05c5] c9

                    push      bc                            ;[05c6] c5
                    ld        bc,$7ffd                      ;[05c7] 01 fd 7f
                    out       (c),a                         ;[05ca] ed 79
                    ld        ($5b5c),a                     ;[05cc] 32 5c 5b
                    pop       bc                            ;[05cf] c1
                    ret                                     ;[05d0] c9

                    ex        af,af'                        ;[05d1] 08
                    di                                      ;[05d2] f3
                    pop       af                            ;[05d3] f1
                    ld        ($5b58),hl                    ;[05d4] 22 58 5b
                    ld        hl,($5b6a)                    ;[05d7] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[05da] ed 73 6a 5b
                    ld        sp,hl                         ;[05de] f9
                    ld        hl,($5b58)                    ;[05df] 2a 58 5b
                    push      af                            ;[05e2] f5
                    ld        a,$07                         ;[05e3] 3e 07
                    call      $05c6                         ;[05e5] cd c6 05
                    ei                                      ;[05e8] fb
                    ex        af,af'                        ;[05e9] 08
                    ret                                     ;[05ea] c9

                    dec       d                             ;[05eb] 15
                    dec       bc                            ;[05ec] 0b
                    ld        h,h                           ;[05ed] 64
                    dec       bc                            ;[05ee] 0b
                    ld        a,(bc)                        ;[05ef] 0a
                    add       l                             ;[05f0] 85
                    dec       bc                            ;[05f1] 0b
                    ex        af,af'                        ;[05f2] 08
                    and       a                             ;[05f3] a7
                    dec       bc                            ;[05f4] 0b
                    add       hl,bc                         ;[05f5] 09
                    or        e                             ;[05f6] b3
                    dec       bc                            ;[05f7] 0b
                    xor       l                             ;[05f8] ad
                    rra                                     ;[05f9] 1f
                    dec       bc                            ;[05fa] 0b
                    xor       h                             ;[05fb] ac
                    push      af                            ;[05fc] f5
                    ld        a,(bc)                        ;[05fd] 0a
                    xor       a                             ;[05fe] af
                    and       h                             ;[05ff] a4
                    ld        a,(bc)                        ;[0600] 0a
                    xor       (hl)                          ;[0601] ae
                    or        c                             ;[0602] b1
                    ld        a,(bc)                        ;[0603] 0a
                    and       (hl)                          ;[0604] a6
                    ld        d,e                           ;[0605] 53
                    ld        a,(bc)                        ;[0606] 0a
                    and       l                             ;[0607] a5
                    ld        a,e                           ;[0608] 7b
                    ld        a,(bc)                        ;[0609] 0a
                    xor       b                             ;[060a] a8
                    ld        d,a                           ;[060b] 57
                    dec       bc                            ;[060c] 0b
                    and       a                             ;[060d] a7
                    ld        c,d                           ;[060e] 4a
                    dec       bc                            ;[060f] 0b
                    xor       d                             ;[0610] aa
                    ex        de,hl                         ;[0611] eb
                    add       hl,bc                         ;[0612] 09
                    inc       c                             ;[0613] 0c
                    ei                                      ;[0614] fb
                    add       hl,bc                         ;[0615] 09
                    or        e                             ;[0616] b3
                    jp        pe,$b410                      ;[0617] ea 10 b4
                    adc       a                             ;[061a] 8f
                    djnz      $05cd                         ;[061b] 10 b0
                    ld        b,l                           ;[061d] 45
                    ld        de,$11b1                      ;[061e] 11 b1 11
                    ld        de,$140d                      ;[0621] 11 0d 14
                    ld        a,(bc)                        ;[0624] 0a
                    xor       c                             ;[0625] a9
                    ld        c,l                           ;[0626] 4d
                    rlca                                    ;[0627] 07
                    rlca                                    ;[0628] 07
                    or        (hl)                          ;[0629] b6
                    rlca                                    ;[062a] 07
                    inc       b                             ;[062b] 04
                    dec       bc                            ;[062c] 0b
                    pop       hl                            ;[062d] e1
                    rlca                                    ;[062e] 07
                    ld        a,(bc)                        ;[062f] 0a
                    call      po,$0707                      ;[0630] e4 07 07
                    jp        z,$0d07                       ;[0633] ca 07 0d
                    jp        z,$cd07                       ;[0636] ca 07 cd
                    adc       (hl)                          ;[0639] 8e
                    add       hl,bc                         ;[063a] 09
                    ld        hl,$0000                      ;[063b] 21 00 00
                    ld        ($fc9a),hl                    ;[063e] 22 9a fc
                    ld        a,$82                         ;[0641] 3e 82
                    ld        ($ec0d),a                     ;[0643] 32 0d ec
                    ld        hl,$0000                      ;[0646] 21 00 00
                    ld        ($5c49),hl                    ;[0649] 22 49 5c
                    call      $16bb                         ;[064c] cd bb 16
                    call      $175d                         ;[064f] cd 5d 17
                    ret                                     ;[0652] c9

                    ld        hl,$5bff                      ;[0653] 21 ff 5b
                    ld        ($5b6a),hl                    ;[0656] 22 6a 5b
                    call      $05d1                         ;[0659] cd d1 05
                    ld        a,$02                         ;[065c] 3e 02
                    rst       $28                           ;[065e] ef
                    ld        bc,$2116                      ;[065f] 01 16 21
                    rst       $30                           ;[0662] f7
                    rlca                                    ;[0663] 07
                    ld        ($f6ea),hl                    ;[0664] 22 ea f6
                    ld        hl,$0804                      ;[0667] 21 04 08
                    ld        ($f6ec),hl                    ;[066a] 22 ec f6
                    push      hl                            ;[066d] e5
                    ld        hl,$ec0d                      ;[066e] 21 0d ec
                    set       1,(hl)                        ;[0671] cb ce
                    res       4,(hl)                        ;[0673] cb a6
                    dec       hl                            ;[0675] 2b
                    ld        (hl),$00                      ;[0676] 36 00
                    pop       hl                            ;[0678] e1
                    xor       a                             ;[0679] af
                    call      $189f                         ;[067a] cd 9f 18
                    jp        $0708                         ;[067d] c3 08 07
                    ld        ix,$fd98                      ;[0680] dd 21 98 fd
                    ld        hl,$5bff                      ;[0684] 21 ff 5b
                    ld        ($5b6a),hl                    ;[0687] 22 6a 5b
                    call      $05d1                         ;[068a] cd d1 05
                    ld        a,$02                         ;[068d] 3e 02
                    rst       $28                           ;[068f] ef
                    ld        bc,$cd16                      ;[0690] 01 16 cd
                    ld        e,a                           ;[0693] 5f
                    jr        $06b7                         ;[0694] 18 21
                    dec       sp                            ;[0696] 3b
                    ld        e,h                           ;[0697] 5c
                    bit       5,(hl)                        ;[0698] cb 6e
                    jr        z,$0698                       ;[069a] 28 fc
                    ld        hl,$ec0d                      ;[069c] 21 0d ec
                    res       3,(hl)                        ;[069f] cb 9e
                    bit       6,(hl)                        ;[06a1] cb 76
                    jr        nz,$06b9                      ;[06a3] 20 14
                    ld        a,($ec0e)                     ;[06a5] 3a 0e ec
                    cp        $04                           ;[06a8] fe 04
                    jr        z,$06b6                       ;[06aa] 28 0a
                    cp        $00                           ;[06ac] fe 00
                    jp        nz,$0997                      ;[06ae] c2 97 09
                    call      $1a5f                         ;[06b1] cd 5f 1a
                    jr        $06b9                         ;[06b4] 18 03
                    call      $1a64                         ;[06b6] cd 64 1a
                    call      $11a9                         ;[06b9] cd a9 11
                    call      $12f5                         ;[06bc] cd f5 12
                    ld        a,($ec0e)                     ;[06bf] 3a 0e ec
                    cp        $04                           ;[06c2] fe 04
                    jr        z,$0708                       ;[06c4] 28 42
                    ld        hl,($5c49)                    ;[06c6] 2a 49 5c
                    ld        a,h                           ;[06c9] 7c
                    or        l                             ;[06ca] b5
                    jr        nz,$06e2                      ;[06cb] 20 15
                    ld        hl,($5c53)                    ;[06cd] 2a 53 5c
                    ld        bc,($5c4b)                    ;[06d0] ed 4b 4b 5c
                    and       a                             ;[06d4] a7
                    sbc       hl,bc                         ;[06d5] ed 42
                    jr        nz,$06df                      ;[06d7] 20 06
                    ld        hl,$0000                      ;[06d9] 21 00 00
                    ld        ($ec08),hl                    ;[06dc] 22 08 ec
                    ld        hl,($ec08)                    ;[06df] 2a 08 ec
                    call      $05ac                         ;[06e2] cd ac 05
                    rst       $28                           ;[06e5] ef
                    ld        l,(hl)                        ;[06e6] 6e
                    add       hl,de                         ;[06e7] 19
                    rst       $28                           ;[06e8] ef
                    sub       l                             ;[06e9] 95
                    ld        d,$cd                         ;[06ea] 16 cd
                    pop       de                            ;[06ec] d1
                    dec       b                             ;[06ed] 05
                    ld        ($5c49),de                    ;[06ee] ed 53 49 5c
                    ld        hl,$ec0d                      ;[06f2] 21 0d ec
                    bit       5,(hl)                        ;[06f5] cb 6e
                    jr        nz,$0708                      ;[06f7] 20 0f
                    ld        hl,$0000                      ;[06f9] 21 00 00
                    ld        ($ec06),hl                    ;[06fc] 22 06 ec
                    call      $03ff                         ;[06ff] cd ff 03
                    call      $0ac2                         ;[0702] cd c2 0a
                    call      $0a14                         ;[0705] cd 14 0a
                    ld        sp,$5bff                      ;[0708] 31 ff 5b
                    call      $1876                         ;[070b] cd 76 18
                    push      af                            ;[070e] f5
                    ld        a,($5c39)                     ;[070f] 3a 39 5c
                    call      $079e                         ;[0712] cd 9e 07
                    pop       af                            ;[0715] f1
                    call      $071b                         ;[0716] cd 1b 07
                    jr        $070b                         ;[0719] 18 f0
                    ld        hl,$ec0d                      ;[071b] 21 0d ec
                    bit       1,(hl)                        ;[071e] cb 4e
                    push      af                            ;[0720] f5
                    ld        hl,$062b                      ;[0721] 21 2b 06
                    jr        nz,$0729                      ;[0724] 20 03
                    ld        hl,$05eb                      ;[0726] 21 eb 05
                    call      $216b                         ;[0729] cd 6b 21
                    jr        nz,$0733                      ;[072c] 20 05
                    call      nc,$0799                      ;[072e] d4 99 07
                    pop       af                            ;[0731] f1
                    ret                                     ;[0732] c9

                    pop       af                            ;[0733] f1
                    jr        z,$073b                       ;[0734] 28 05
                    xor       a                             ;[0736] af
                    ld        ($5c41),a                     ;[0737] 32 41 5c
                    ret                                     ;[073a] c9

                    ld        hl,$ec0d                      ;[073b] 21 0d ec
                    bit       0,(hl)                        ;[073e] cb 46
                    jr        z,$0746                       ;[0740] 28 04
                    call      $0799                         ;[0742] cd 99 07
                    ret                                     ;[0745] c9

                    cp        $a3                           ;[0746] fe a3
                    jr        nc,$070b                      ;[0748] 30 c1
                    jp        $09c1                         ;[074a] c3 c1 09
                    ld        a,($ec0e)                     ;[074d] 3a 0e ec
                    cp        $04                           ;[0750] fe 04
                    ret       z                             ;[0752] c8
                    call      $0500                         ;[0753] cd 00 05
                    ld        hl,$ec0d                      ;[0756] 21 0d ec
                    res       3,(hl)                        ;[0759] cb 9e
                    ld        a,(hl)                        ;[075b] 7e
                    xor       $40                           ;[075c] ee 40
                    ld        (hl),a                        ;[075e] 77
                    and       $40                           ;[075f] e6 40
                    jr        z,$0768                       ;[0761] 28 05
                    call      $076d                         ;[0763] cd 6d 07
                    jr        $076b                         ;[0766] 18 03
                    call      $0780                         ;[0768] cd 80 07
                    scf                                     ;[076b] 37
                    ret                                     ;[076c] c9

                    call      $1a93                         ;[076d] cd 93 1a
                    ld        hl,$ec0d                      ;[0770] 21 0d ec
                    set       6,(hl)                        ;[0773] cb f6
                    call      $0f00                         ;[0775] cd 00 0f
                    call      $1c9a                         ;[0778] cd 9a 1c
                    call      $09af                         ;[077b] cd af 09
                    jr        $078b                         ;[077e] 18 0b
                    ld        hl,$ec0d                      ;[0780] 21 0d ec
                    res       6,(hl)                        ;[0783] cb b6
                    call      $098e                         ;[0785] cd 8e 09
                    call      $1a5f                         ;[0788] cd 5f 1a
                    ld        hl,($fc9a)                    ;[078b] 2a 9a fc
                    ld        a,h                           ;[078e] 7c
                    or        l                             ;[078f] b5
                    call      nz,$141d                      ;[0790] c4 1d 14
                    call      $03ff                         ;[0793] cd ff 03
                    jp        $0ac2                         ;[0796] c3 c2 0a
                    ld        a,($5c38)                     ;[0799] 3a 38 5c
                    srl       a                             ;[079c] cb 3f
                    push      ix                            ;[079e] dd e5
                    ld        d,$00                         ;[07a0] 16 00
                    ld        e,a                           ;[07a2] 5f
                    ld        hl,$0c80                      ;[07a3] 21 80 0c
                    rst       $28                           ;[07a6] ef
                    or        l                             ;[07a7] b5
                    inc       bc                            ;[07a8] 03
                    pop       ix                            ;[07a9] dd e1
                    ret                                     ;[07ab] c9

                    push      ix                            ;[07ac] dd e5
                    ld        de,$0030                      ;[07ae] 11 30 00
                    ld        hl,$0300                      ;[07b1] 21 00 03
                    jr        $07a6                         ;[07b4] 18 f0
                    call      $0abc                         ;[07b6] cd bc 0a
                    ld        hl,$ec0d                      ;[07b9] 21 0d ec
                    set       1,(hl)                        ;[07bc] cb ce
                    dec       hl                            ;[07be] 2b
                    ld        (hl),$00                      ;[07bf] 36 00
                    ld        hl,($f6ec)                    ;[07c1] 2a ec f6
                    xor       a                             ;[07c4] af
                    call      $189f                         ;[07c5] cd 9f 18
                    scf                                     ;[07c8] 37
                    ret                                     ;[07c9] c9

                    ld        hl,$ec0d                      ;[07ca] 21 0d ec
                    res       1,(hl)                        ;[07cd] cb 8e
                    dec       hl                            ;[07cf] 2b
                    ld        a,(hl)                        ;[07d0] 7e
                    ld        hl,($f6ea)                    ;[07d1] 2a ea f6
                    push      hl                            ;[07d4] e5
                    push      af                            ;[07d5] f5
                    call      $1955                         ;[07d6] cd 55 19
                    pop       af                            ;[07d9] f1
                    pop       hl                            ;[07da] e1
                    call      $216b                         ;[07db] cd 6b 21
                    jp        $0ac2                         ;[07de] c3 c2 0a
                    scf                                     ;[07e1] 37
                    jr        $07e5                         ;[07e2] 18 01
                    and       a                             ;[07e4] a7
                    ld        hl,$ec0c                      ;[07e5] 21 0c ec
                    ld        a,(hl)                        ;[07e8] 7e
                    push      hl                            ;[07e9] e5
                    ld        hl,($f6ec)                    ;[07ea] 2a ec f6
                    call      c,$19be                       ;[07ed] dc be 19
                    call      nc,$19cd                      ;[07f0] d4 cd 19
                    pop       hl                            ;[07f3] e1
                    ld        (hl),a                        ;[07f4] 77
                    scf                                     ;[07f5] 37
                    ret                                     ;[07f6] c9

                    inc       b                             ;[07f7] 04
                    nop                                     ;[07f8] 00
                    nop                                     ;[07f9] ed 08
                    ld        bc,$093c                      ;[07fb] 01 3c 09
                    ld        (bc),a                        ;[07fe] 02
                    ld        d,l                           ;[07ff] 55
                    add       hl,bc                         ;[0800] 09
                    inc       bc                            ;[0801] 03
                    call      po,$0508                      ;[0802] e4 08 05
                    ld        sp,$3832                      ;[0805] 31 32 38
                    jr        nz,$0835                      ;[0808] 20 2b
                    inc       sp                            ;[080a] 33
                    jr        nz,$082d                      ;[080b] 20 20
                    rst       $38                           ;[080d] ff
                    ld        c,h                           ;[080e] 4c
                    ld        l,a                           ;[080f] 6f
                    ld        h,c                           ;[0810] 61
                    ld        h,h                           ;[0811] 64
                    ld        h,l                           ;[0812] 65
                    jp        p,$332b                       ;[0813] f2 2b 33
                    jr        nz,$085a                      ;[0816] 20 42
                    ld        b,c                           ;[0818] 41
                    ld        d,e                           ;[0819] 53
                    ld        c,c                           ;[081a] 49
                    jp        $6143                         ;[081b] c3 43 61
                    ld        l,h                           ;[081e] 6c
                    ld        h,e                           ;[081f] 63
                    ld        (hl),l                        ;[0820] 75
                    ld        l,h                           ;[0821] 6c
                    ld        h,c                           ;[0822] 61
                    ld        (hl),h                        ;[0823] 74
                    ld        l,a                           ;[0824] 6f
                    jp        p,$3834                       ;[0825] f2 34 38
                    jr        nz,$086c                      ;[0828] 20 42
                    ld        b,c                           ;[082a] 41
                    ld        d,e                           ;[082b] 53
                    ld        c,c                           ;[082c] 49
                    jp        $05a0                         ;[082d] c3 a0 05
                    nop                                     ;[0830] 00
                    push      af                            ;[0831] f5
                    rlca                                    ;[0832] 07
                    ld        bc,$091c                      ;[0833] 01 1c 09
                    ld        (bc),a                        ;[0836] 02
                    jp        z,$0308                       ;[0837] ca 08 03
                    dec       l                             ;[083a] 2d
                    add       hl,bc                         ;[083b] 09
                    inc       b                             ;[083c] 04
                    rst       $08                           ;[083d] cf
                    ex        af,af'                        ;[083e] 08
                    ld        b,$4f                         ;[083f] 06 4f
                    ld        (hl),b                        ;[0841] 70
                    ld        (hl),h                        ;[0842] 74
                    ld        l,c                           ;[0843] 69
                    ld        l,a                           ;[0844] 6f
                    ld        l,(hl)                        ;[0845] 6e
                    ld        (hl),e                        ;[0846] 73
                    jr        nz,$0848                      ;[0847] 20 ff
                    dec       hl                            ;[0849] 2b
                    inc       sp                            ;[084a] 33
                    jr        nz,$088f                      ;[084b] 20 42
                    ld        b,c                           ;[084d] 41
                    ld        d,e                           ;[084e] 53
                    ld        c,c                           ;[084f] 49
                    jp        $6552                         ;[0850] c3 52 65
                    ld        l,(hl)                        ;[0853] 6e
                    ld        (hl),l                        ;[0854] 75
                    ld        l,l                           ;[0855] 6d
                    ld        h,d                           ;[0856] 62
                    ld        h,l                           ;[0857] 65
                    jp        p,$6353                       ;[0858] f2 53 63
                    ld        (hl),d                        ;[085b] 72
                    ld        h,l                           ;[085c] 65
                    ld        h,l                           ;[085d] 65
                    xor       $50                           ;[085e] ee 50
                    ld        (hl),d                        ;[0860] 72
                    ld        l,c                           ;[0861] 69
                    ld        l,(hl)                        ;[0862] 6e
                    call      p,$7845                       ;[0863] f4 45 78
                    ld        l,c                           ;[0866] 69
                    call      p,$02a0                       ;[0867] f4 a0 02
                    nop                                     ;[086a] 00
                    push      af                            ;[086b] f5
                    rlca                                    ;[086c] 07
                    ld        bc,$08cf                      ;[086d] 01 cf 08
                    inc       bc                            ;[0870] 03
                    ld        c,a                           ;[0871] 4f
                    ld        (hl),b                        ;[0872] 70
                    ld        (hl),h                        ;[0873] 74
                    ld        l,c                           ;[0874] 69
                    ld        l,a                           ;[0875] 6f
                    ld        l,(hl)                        ;[0876] 6e
                    ld        (hl),e                        ;[0877] 73
                    jr        nz,$0879                      ;[0878] 20 ff
                    ld        b,e                           ;[087a] 43
                    ld        h,c                           ;[087b] 61
                    ld        l,h                           ;[087c] 6c
                    ld        h,e                           ;[087d] 63
                    ld        (hl),l                        ;[087e] 75
                    ld        l,h                           ;[087f] 6c
                    ld        h,c                           ;[0880] 61
                    ld        (hl),h                        ;[0881] 74
                    ld        l,a                           ;[0882] 6f
                    jp        p,$7845                       ;[0883] f2 45 78
                    ld        l,c                           ;[0886] 69
                    call      p,$16a0                       ;[0887] f4 a0 16
                    nop                                     ;[088a] 00
                    nop                                     ;[088b] 00
                    djnz      $088e                         ;[088c] 10 00
                    ld        de,$1307                      ;[088e] 11 07 13
                    nop                                     ;[0891] 00
                    ld        c,c                           ;[0892] 49
                    ld        l,(hl)                        ;[0893] 6e
                    ld        (hl),e                        ;[0894] 73
                    ld        h,l                           ;[0895] 65
                    ld        (hl),d                        ;[0896] 72
                    ld        (hl),h                        ;[0897] 74
                    jr        nz,$090e                      ;[0898] 20 74
                    ld        h,c                           ;[089a] 61
                    ld        (hl),b                        ;[089b] 70
                    ld        h,l                           ;[089c] 65
                    jr        nz,$0900                      ;[089d] 20 61
                    ld        l,(hl)                        ;[089f] 6e
                    ld        h,h                           ;[08a0] 64
                    jr        nz,$0913                      ;[08a1] 20 70
                    ld        (hl),d                        ;[08a3] 72
                    ld        h,l                           ;[08a4] 65
                    ld        (hl),e                        ;[08a5] 73
                    ld        (hl),e                        ;[08a6] 73
                    jr        nz,$08f9                      ;[08a7] 20 50
                    ld        c,h                           ;[08a9] 4c
                    ld        b,c                           ;[08aa] 41
                    ld        e,c                           ;[08ab] 59
                    dec       c                             ;[08ac] 0d
                    ld        d,h                           ;[08ad] 54
                    ld        l,a                           ;[08ae] 6f
                    jr        nz,$0914                      ;[08af] 20 63
                    ld        h,c                           ;[08b1] 61
                    ld        l,(hl)                        ;[08b2] 6e
                    ld        h,e                           ;[08b3] 63
                    ld        h,l                           ;[08b4] 65
                    ld        l,h                           ;[08b5] 6c
                    jr        nz,$08e5                      ;[08b6] 20 2d
                    jr        nz,$092a                      ;[08b8] 20 70
                    ld        (hl),d                        ;[08ba] 72
                    ld        h,l                           ;[08bb] 65
                    ld        (hl),e                        ;[08bc] 73
                    ld        (hl),e                        ;[08bd] 73
                    jr        nz,$0902                      ;[08be] 20 42
                    ld        d,d                           ;[08c0] 52
                    ld        b,l                           ;[08c1] 45
                    ld        b,c                           ;[08c2] 41
                    ld        c,e                           ;[08c3] 4b
                    jr        nz,$093a                      ;[08c4] 20 74
                    ld        (hl),a                        ;[08c6] 77
                    ld        l,c                           ;[08c7] 69
                    ld        h,e                           ;[08c8] 63
                    push      hl                            ;[08c9] e5
                    call      $074d                         ;[08ca] cd 4d 07
                    jr        $0944                         ;[08cd] 18 75
                    ld        hl,$ec0d                      ;[08cf] 21 0d ec
                    res       6,(hl)                        ;[08d2] cb b6
                    call      $098e                         ;[08d4] cd 8e 09
                    ld        b,$00                         ;[08d7] 06 00
                    ld        d,$17                         ;[08d9] 16 17
                    call      $1d70                         ;[08db] cd 70 1d
                    call      $05ac                         ;[08de] cd ac 05
                    jp        $0653                         ;[08e1] c3 53 06
                    call      $05ac                         ;[08e4] cd ac 05
                    call      $3e80                         ;[08e7] cd 80 3e
                    and       h                             ;[08ea] a4
                    inc       d                             ;[08eb] 14
                    ret                                     ;[08ec] c9

                    call      $1a69                         ;[08ed] cd 69 1a
                    ld        hl,$5c3c                      ;[08f0] 21 3c 5c
                    set       0,(hl)                        ;[08f3] cb c6
                    ld        de,$0889                      ;[08f5] 11 89 08
                    push      hl                            ;[08f8] e5
                    ld        hl,$5b66                      ;[08f9] 21 66 5b
                    bit       4,(hl)                        ;[08fc] cb 66
                    pop       hl                            ;[08fe] e1
                    jr        nz,$0904                      ;[08ff] 20 03
                    call      $02a3                         ;[0901] cd a3 02
                    res       0,(hl)                        ;[0904] cb 86
                    set       6,(hl)                        ;[0906] cb f6
                    ld        a,$07                         ;[0908] 3e 07
                    ld        ($ec0e),a                     ;[090a] 32 0e ec
                    ld        bc,$0000                      ;[090d] 01 00 00
                    call      $1922                         ;[0910] cd 22 19
                    call      $05ac                         ;[0913] cd ac 05
                    call      $3e80                         ;[0916] cd 80 3e
                    inc       b                             ;[0919] 04
                    inc       de                            ;[091a] 13
                    ret                                     ;[091b] c9

                    call      $1a9a                         ;[091c] cd 9a 1a
                    call      nc,$0799                      ;[091f] d4 99 07
                    ld        hl,$0000                      ;[0922] 21 00 00
                    ld        ($5c49),hl                    ;[0925] 22 49 5c
                    ld        ($ec08),hl                    ;[0928] 22 08 ec
                    jr        $0935                         ;[092b] 18 08
                    call      $05ac                         ;[092d] cd ac 05
                    call      $3e80                         ;[0930] cd 80 3e
                    ld        l,l                           ;[0933] 6d
                    inc       d                             ;[0934] 14
                    ld        hl,$ec0d                      ;[0935] 21 0d ec
                    bit       6,(hl)                        ;[0938] cb 76
                    jr        nz,$0944                      ;[093a] 20 08
                    ld        hl,$5c3c                      ;[093c] 21 3c 5c
                    res       0,(hl)                        ;[093f] cb 86
                    call      $1a5f                         ;[0941] cd 5f 1a
                    ld        hl,$ec0d                      ;[0944] 21 0d ec
                    res       5,(hl)                        ;[0947] cb ae
                    res       4,(hl)                        ;[0949] cb a6
                    ld        a,$00                         ;[094b] 3e 00
                    ld        hl,$082f                      ;[094d] 21 2f 08
                    ld        de,$083f                      ;[0950] 11 3f 08
                    jr        $0981                         ;[0953] 18 2c
                    ld        hl,$ec0d                      ;[0955] 21 0d ec
                    set       5,(hl)                        ;[0958] cb ee
                    set       4,(hl)                        ;[095a] cb e6
                    res       6,(hl)                        ;[095c] cb b6
                    call      $098e                         ;[095e] cd 8e 09
                    call      $1a64                         ;[0961] cd 64 1a
                    ld        a,$04                         ;[0964] 3e 04
                    ld        ($ec0e),a                     ;[0966] 32 0e ec
                    ld        hl,$0000                      ;[0969] 21 00 00
                    ld        ($5c49),hl                    ;[096c] 22 49 5c
                    call      $03ff                         ;[096f] cd ff 03
                    ld        bc,$0000                      ;[0972] 01 00 00
                    ld        a,b                           ;[0975] 78
                    call      $0ac8                         ;[0976] cd c8 0a
                    ld        a,$04                         ;[0979] 3e 04
                    ld        hl,$0869                      ;[097b] 21 69 08
                    ld        de,$0870                      ;[097e] 11 70 08
                    ld        ($ec0e),a                     ;[0981] 32 0e ec
                    ld        ($f6ea),hl                    ;[0984] 22 ea f6
                    ld        ($f6ec),de                    ;[0987] ed 53 ec f6
                    jp        $06b9                         ;[098b] c3 b9 06
                    call      $0ef2                         ;[098e] cd f2 0e
                    call      $1c91                         ;[0991] cd 91 1c
                    jp        $09b8                         ;[0994] c3 b8 09
                    ld        b,$00                         ;[0997] 06 00
                    ld        d,$17                         ;[0999] 16 17
                    call      $1d70                         ;[099b] cd 70 1d
                    jp        $0661                         ;[099e] c3 61 06
                    ld        b,$00                         ;[09a1] 06 00
                    nop                                     ;[09a3] 00
                    nop                                     ;[09a4] 00
                    inc       b                             ;[09a5] 04
                    djnz      $09bc                         ;[09a6] 10 14
                    ld        b,$00                         ;[09a8] 06 00
                    nop                                     ;[09aa] 00
                    nop                                     ;[09ab] 00
                    nop                                     ;[09ac] 00
                    ld        bc,$2101                      ;[09ad] 01 01 21
                    xor       b                             ;[09b0] a8
                    add       hl,bc                         ;[09b1] 09
                    ld        de,$f6ee                      ;[09b2] 11 ee f6
                    jp        $2157                         ;[09b5] c3 57 21
                    ld        hl,$09a1                      ;[09b8] 21 a1 09
                    ld        de,$f6ee                      ;[09bb] 11 ee f6
                    jp        $2157                         ;[09be] c3 57 21
                    ld        hl,$ec0d                      ;[09c1] 21 0d ec
                    or        a                             ;[09c4] b7
                    or        a                             ;[09c5] b7
                    bit       0,(hl)                        ;[09c6] cb 46
                    jp        nz,$0ac2                      ;[09c8] c2 c2 0a
                    res       7,(hl)                        ;[09cb] cb be
                    set       3,(hl)                        ;[09cd] cb de
                    push      hl                            ;[09cf] e5
                    push      af                            ;[09d0] f5
                    call      $0abc                         ;[09d1] cd bc 0a
                    pop       af                            ;[09d4] f1
                    push      af                            ;[09d5] f5
                    call      $0f54                         ;[09d6] cd 54 0f
                    pop       af                            ;[09d9] f1
                    ld        a,b                           ;[09da] 78
                    call      $0c48                         ;[09db] cd 48 0c
                    pop       hl                            ;[09de] e1
                    set       7,(hl)                        ;[09df] cb fe
                    jp        nc,$0ac2                      ;[09e1] d2 c2 0a
                    ld        a,b                           ;[09e4] 78
                    jp        c,$0ac8                       ;[09e5] da c8 0a
                    jp        $0ac2                         ;[09e8] c3 c2 0a
                    ld        hl,$ec0d                      ;[09eb] 21 0d ec
                    set       3,(hl)                        ;[09ee] cb de
                    call      $0abc                         ;[09f0] cd bc 0a
                    call      $0fe5                         ;[09f3] cd e5 0f
                    scf                                     ;[09f6] 37
                    ld        a,b                           ;[09f7] 78
                    jp        $0ac8                         ;[09f8] c3 c8 0a
                    ld        hl,$ec0d                      ;[09fb] 21 0d ec
                    res       0,(hl)                        ;[09fe] cb 86
                    set       3,(hl)                        ;[0a00] cb de
                    call      $0abc                         ;[0a02] cd bc 0a
                    call      $0c2b                         ;[0a05] cd 2b 0c
                    ccf                                     ;[0a08] 3f
                    jp        c,$0ac2                       ;[0a09] da c2 0a
                    call      $0fe5                         ;[0a0c] cd e5 0f
                    scf                                     ;[0a0f] 37
                    ld        a,b                           ;[0a10] 78
                    jp        $0ac8                         ;[0a11] c3 c8 0a
                    call      $0abc                         ;[0a14] cd bc 0a
                    push      af                            ;[0a17] f5
                    call      $1187                         ;[0a18] cd 87 11
                    push      bc                            ;[0a1b] c5
                    ld        b,$00                         ;[0a1c] 06 00
                    call      $0f14                         ;[0a1e] cd 14 0f
                    pop       bc                            ;[0a21] c1
                    jr        c,$0a2e                       ;[0a22] 38 0a
                    ld        hl,$0020                      ;[0a24] 21 20 00
                    add       hl,de                         ;[0a27] 19
                    ld        a,(hl)                        ;[0a28] 7e
                    cpl                                     ;[0a29] 2f
                    and       $09                           ;[0a2a] e6 09
                    jr        z,$0a4a                       ;[0a2c] 28 1c
                    ld        a,($ec0d)                     ;[0a2e] 3a 0d ec
                    bit       3,a                           ;[0a31] cb 5f
                    jr        z,$0a3a                       ;[0a33] 28 05
                    call      $0d5e                         ;[0a35] cd 5e 0d
                    jr        nc,$0a4f                      ;[0a38] 30 15
                    call      $0d1c                         ;[0a3a] cd 1c 0d
                    call      $0c48                         ;[0a3d] cd 48 0c
                    call      $0fa1                         ;[0a40] cd a1 0f
                    ld        b,$00                         ;[0a43] 06 00
                    pop       af                            ;[0a45] f1
                    scf                                     ;[0a46] 37
                    jp        $0ac8                         ;[0a47] c3 c8 0a
                    pop       af                            ;[0a4a] f1
                    scf                                     ;[0a4b] 37
                    jp        $0ac2                         ;[0a4c] c3 c2 0a
                    pop       af                            ;[0a4f] f1
                    jp        $0ac2                         ;[0a50] c3 c2 0a
                    ld        a,($ec0e)                     ;[0a53] 3a 0e ec
                    cp        $04                           ;[0a56] fe 04
                    ret       z                             ;[0a58] c8
                    call      $0abc                         ;[0a59] cd bc 0a
                    ld        hl,$0000                      ;[0a5c] 21 00 00
                    call      $05ac                         ;[0a5f] cd ac 05
                    rst       $28                           ;[0a62] ef
                    ld        l,(hl)                        ;[0a63] 6e
                    add       hl,de                         ;[0a64] 19
                    rst       $28                           ;[0a65] ef
                    sub       l                             ;[0a66] 95
                    ld        d,$cd                         ;[0a67] 16 cd
                    pop       de                            ;[0a69] d1
                    dec       b                             ;[0a6a] 05
                    ld        ($5c49),de                    ;[0a6b] ed 53 49 5c
                    ld        a,$0f                         ;[0a6f] 3e 0f
                    call      $1ca8                         ;[0a71] cd a8 1c
                    call      $03ff                         ;[0a74] cd ff 03
                    scf                                     ;[0a77] 37
                    jp        $0ac2                         ;[0a78] c3 c2 0a
                    ld        a,($ec0e)                     ;[0a7b] 3a 0e ec
                    cp        $04                           ;[0a7e] fe 04
                    ret       z                             ;[0a80] c8
                    call      $0abc                         ;[0a81] cd bc 0a
                    ld        hl,$270f                      ;[0a84] 21 0f 27
                    call      $05ac                         ;[0a87] cd ac 05
                    rst       $28                           ;[0a8a] ef
                    ld        l,(hl)                        ;[0a8b] 6e
                    add       hl,de                         ;[0a8c] 19
                    ex        de,hl                         ;[0a8d] eb
                    rst       $28                           ;[0a8e] ef
                    sub       l                             ;[0a8f] 95
                    ld        d,$cd                         ;[0a90] 16 cd
                    pop       de                            ;[0a92] d1
                    dec       b                             ;[0a93] 05
                    ld        ($5c49),de                    ;[0a94] ed 53 49 5c
                    ld        a,$0f                         ;[0a98] 3e 0f
                    call      $1ca8                         ;[0a9a] cd a8 1c
                    call      $03ff                         ;[0a9d] cd ff 03
                    scf                                     ;[0aa0] 37
                    jp        $0ac2                         ;[0aa1] c3 c2 0a
                    call      $0abc                         ;[0aa4] cd bc 0a
                    call      $0cba                         ;[0aa7] cd ba 0c
                    jp        nc,$0ac2                      ;[0aaa] d2 c2 0a
                    ld        a,b                           ;[0aad] 78
                    jp        $0ac8                         ;[0aae] c3 c8 0a
                    call      $0abc                         ;[0ab1] cd bc 0a
                    call      $0cd9                         ;[0ab4] cd d9 0c
                    jr        nc,$0ac2                      ;[0ab7] 30 09
                    ld        a,b                           ;[0ab9] 78
                    jr        $0ac8                         ;[0aba] 18 0c
                    call      $0ad7                         ;[0abc] cd d7 0a
                    jp        $174e                         ;[0abf] c3 4e 17
                    call      $0ad7                         ;[0ac2] cd d7 0a
                    jp        $173f                         ;[0ac5] c3 3f 17
                    call      $0ae1                         ;[0ac8] cd e1 0a
                    push      af                            ;[0acb] f5
                    push      bc                            ;[0acc] c5
                    ld        a,$0f                         ;[0acd] 3e 0f
                    call      $1ca8                         ;[0acf] cd a8 1c
                    pop       bc                            ;[0ad2] c1
                    pop       af                            ;[0ad3] f1
                    jp        $173f                         ;[0ad4] c3 3f 17
                    ld        hl,$f6ee                      ;[0ad7] 21 ee f6
                    ld        c,(hl)                        ;[0ada] 4e
                    inc       hl                            ;[0adb] 23
                    ld        b,(hl)                        ;[0adc] 46
                    inc       hl                            ;[0add] 23
                    ld        a,(hl)                        ;[0ade] 7e
                    inc       hl                            ;[0adf] 23
                    ret                                     ;[0ae0] c9

                    ld        hl,$f6ee                      ;[0ae1] 21 ee f6
                    ld        (hl),c                        ;[0ae4] 71
                    inc       hl                            ;[0ae5] 23
                    ld        (hl),b                        ;[0ae6] 70
                    inc       hl                            ;[0ae7] 23
                    ld        (hl),a                        ;[0ae8] 77
                    ret                                     ;[0ae9] c9

                    push      hl                            ;[0aea] e5
                    call      $1187                         ;[0aeb] cd 87 11
                    ld        h,$00                         ;[0aee] 26 00
                    ld        l,b                           ;[0af0] 68
                    add       hl,de                         ;[0af1] 19
                    ld        a,(hl)                        ;[0af2] 7e
                    pop       hl                            ;[0af3] e1
                    ret                                     ;[0af4] c9

                    call      $0abc                         ;[0af5] cd bc 0a
                    ld        e,a                           ;[0af8] 5f
                    ld        d,$0a                         ;[0af9] 16 0a
                    push      de                            ;[0afb] d5
                    call      $0c00                         ;[0afc] cd 00 0c
                    pop       de                            ;[0aff] d1
                    jr        nc,$0ac2                      ;[0b00] 30 c0
                    ld        a,e                           ;[0b02] 7b
                    call      $0ae1                         ;[0b03] cd e1 0a
                    ld        b,e                           ;[0b06] 43
                    call      $0bc9                         ;[0b07] cd c9 0b
                    jr        nc,$0b12                      ;[0b0a] 30 06
                    dec       d                             ;[0b0c] 15
                    jr        nz,$0afb                      ;[0b0d] 20 ec
                    ld        a,e                           ;[0b0f] 7b
                    jr        c,$0ac8                       ;[0b10] 38 b6
                    push      de                            ;[0b12] d5
                    call      $0bdb                         ;[0b13] cd db 0b
                    pop       de                            ;[0b16] d1
                    ld        b,e                           ;[0b17] 43
                    call      $0bc9                         ;[0b18] cd c9 0b
                    ld        a,e                           ;[0b1b] 7b
                    or        a                             ;[0b1c] b7
                    jr        $0ac8                         ;[0b1d] 18 a9
                    call      $0abc                         ;[0b1f] cd bc 0a
                    ld        e,a                           ;[0b22] 5f
                    ld        d,$0a                         ;[0b23] 16 0a
                    push      de                            ;[0b25] d5
                    call      $0bdb                         ;[0b26] cd db 0b
                    pop       de                            ;[0b29] d1
                    jr        nc,$0ac2                      ;[0b2a] 30 96
                    ld        a,e                           ;[0b2c] 7b
                    call      $0ae1                         ;[0b2d] cd e1 0a
                    ld        b,e                           ;[0b30] 43
                    call      $0bd2                         ;[0b31] cd d2 0b
                    jr        nc,$0b3d                      ;[0b34] 30 07
                    dec       d                             ;[0b36] 15
                    jr        nz,$0b25                      ;[0b37] 20 ec
                    ld        a,e                           ;[0b39] 7b
                    jp        c,$0ac8                       ;[0b3a] da c8 0a
                    push      af                            ;[0b3d] f5
                    call      $0c00                         ;[0b3e] cd 00 0c
                    ld        b,$00                         ;[0b41] 06 00
                    call      $0ca4                         ;[0b43] cd a4 0c
                    pop       af                            ;[0b46] f1
                    jp        $0ac8                         ;[0b47] c3 c8 0a
                    call      $0abc                         ;[0b4a] cd bc 0a
                    call      $0d1c                         ;[0b4d] cd 1c 0d
                    jp        nc,$0ac2                      ;[0b50] d2 c2 0a
                    ld        a,b                           ;[0b53] 78
                    jp        $0ac8                         ;[0b54] c3 c8 0a
                    call      $0abc                         ;[0b57] cd bc 0a
                    call      $0d01                         ;[0b5a] cd 01 0d
                    jp        nc,$0ac2                      ;[0b5d] d2 c2 0a
                    ld        a,b                           ;[0b60] 78
                    jp        $0ac8                         ;[0b61] c3 c8 0a
                    call      $0abc                         ;[0b64] cd bc 0a
                    ld        e,a                           ;[0b67] 5f
                    push      de                            ;[0b68] d5
                    call      $0bdb                         ;[0b69] cd db 0b
                    pop       de                            ;[0b6c] d1
                    jp        nc,$0ac2                      ;[0b6d] d2 c2 0a
                    ld        b,e                           ;[0b70] 43
                    call      $0bd2                         ;[0b71] cd d2 0b
                    ld        a,e                           ;[0b74] 7b
                    jp        c,$0ac8                       ;[0b75] da c8 0a
                    push      af                            ;[0b78] f5
                    call      $0c00                         ;[0b79] cd 00 0c
                    ld        b,$00                         ;[0b7c] 06 00
                    call      $0bc9                         ;[0b7e] cd c9 0b
                    pop       af                            ;[0b81] f1
                    jp        $0ac8                         ;[0b82] c3 c8 0a
                    call      $0abc                         ;[0b85] cd bc 0a
                    ld        e,a                           ;[0b88] 5f
                    push      de                            ;[0b89] d5
                    call      $0c00                         ;[0b8a] cd 00 0c
                    pop       de                            ;[0b8d] d1
                    jp        nc,$0ac2                      ;[0b8e] d2 c2 0a
                    ld        b,e                           ;[0b91] 43
                    call      $0bd2                         ;[0b92] cd d2 0b
                    ld        a,e                           ;[0b95] 7b
                    jp        c,$0ac8                       ;[0b96] da c8 0a
                    push      de                            ;[0b99] d5
                    call      $0bdb                         ;[0b9a] cd db 0b
                    pop       de                            ;[0b9d] d1
                    ld        b,e                           ;[0b9e] 43
                    call      $0bc9                         ;[0b9f] cd c9 0b
                    ld        a,e                           ;[0ba2] 7b
                    or        a                             ;[0ba3] b7
                    jp        $0ac8                         ;[0ba4] c3 c8 0a
                    call      $0abc                         ;[0ba7] cd bc 0a
                    call      $0c2b                         ;[0baa] cd 2b 0c
                    jp        c,$0ac8                       ;[0bad] da c8 0a
                    jp        $0ac2                         ;[0bb0] c3 c2 0a
                    call      $0abc                         ;[0bb3] cd bc 0a
                    call      $0c48                         ;[0bb6] cd 48 0c
                    jp        c,$0ac8                       ;[0bb9] da c8 0a
                    push      af                            ;[0bbc] f5
                    call      $0bdb                         ;[0bbd] cd db 0b
                    ld        b,$1f                         ;[0bc0] 06 1f
                    call      $0caf                         ;[0bc2] cd af 0c
                    pop       af                            ;[0bc5] f1
                    jp        $0ac8                         ;[0bc6] c3 c8 0a
                    push      de                            ;[0bc9] d5
                    call      $0ca4                         ;[0bca] cd a4 0c
                    call      nc,$0caf                      ;[0bcd] d4 af 0c
                    pop       de                            ;[0bd0] d1
                    ret                                     ;[0bd1] c9

                    push      de                            ;[0bd2] d5
                    call      $0caf                         ;[0bd3] cd af 0c
                    call      nc,$0ca4                      ;[0bd6] d4 a4 0c
                    pop       de                            ;[0bd9] d1
                    ret                                     ;[0bda] c9

                    call      $0d4c                         ;[0bdb] cd 4c 0d
                    jr        nc,$0bff                      ;[0bde] 30 1f
                    push      bc                            ;[0be0] c5
                    call      $1187                         ;[0be1] cd 87 11
                    ld        b,$00                         ;[0be4] 06 00
                    call      $0f14                         ;[0be6] cd 14 0f
                    call      nc,$1053                      ;[0be9] d4 53 10
                    pop       bc                            ;[0bec] c1
                    ld        hl,$f6f1                      ;[0bed] 21 f1 f6
                    ld        a,(hl)                        ;[0bf0] 7e
                    cp        c                             ;[0bf1] b9
                    jr        c,$0bfd                       ;[0bf2] 38 09
                    push      bc                            ;[0bf4] c5
                    call      $053f                         ;[0bf5] cd 3f 05
                    pop       bc                            ;[0bf8] c1
                    ret       c                             ;[0bf9] d8
                    ld        a,c                           ;[0bfa] 79
                    or        a                             ;[0bfb] b7
                    ret       z                             ;[0bfc] c8
                    dec       c                             ;[0bfd] 0d
                    scf                                     ;[0bfe] 37
                    ret                                     ;[0bff] c9

                    push      bc                            ;[0c00] c5
                    call      $1187                         ;[0c01] cd 87 11
                    ld        b,$00                         ;[0c04] 06 00
                    call      $0f14                         ;[0c06] cd 14 0f
                    pop       bc                            ;[0c09] c1
                    jr        c,$0c0f                       ;[0c0a] 38 03
                    jp        $1053                         ;[0c0c] c3 53 10
                    call      $0d38                         ;[0c0f] cd 38 0d
                    jr        nc,$0c2a                      ;[0c12] 30 16
                    ld        hl,$f6f1                      ;[0c14] 21 f1 f6
                    inc       hl                            ;[0c17] 23
                    ld        a,c                           ;[0c18] 79
                    cp        (hl)                          ;[0c19] be
                    jr        c,$0c28                       ;[0c1a] 38 0c
                    push      bc                            ;[0c1c] c5
                    push      hl                            ;[0c1d] e5
                    call      $0509                         ;[0c1e] cd 09 05
                    pop       hl                            ;[0c21] e1
                    pop       bc                            ;[0c22] c1
                    ret       c                             ;[0c23] d8
                    inc       hl                            ;[0c24] 23
                    ld        a,(hl)                        ;[0c25] 7e
                    cp        c                             ;[0c26] b9
                    ret       z                             ;[0c27] c8
                    inc       c                             ;[0c28] 0c
                    scf                                     ;[0c29] 37
                    ret                                     ;[0c2a] c9

                    ld        d,a                           ;[0c2b] 57
                    dec       b                             ;[0c2c] 05
                    jp        m,$0c36                       ;[0c2d] fa 36 0c
                    ld        e,b                           ;[0c30] 58
                    call      $0caf                         ;[0c31] cd af 0c
                    ld        a,e                           ;[0c34] 7b
                    ret       c                             ;[0c35] d8
                    push      de                            ;[0c36] d5
                    call      $0bdb                         ;[0c37] cd db 0b
                    pop       de                            ;[0c3a] d1
                    ld        a,e                           ;[0c3b] 7b
                    ret       nc                            ;[0c3c] d0
                    ld        b,$1f                         ;[0c3d] 06 1f
                    call      $0caf                         ;[0c3f] cd af 0c
                    ld        a,b                           ;[0c42] 78
                    ret       c                             ;[0c43] d8
                    ld        a,d                           ;[0c44] 7a
                    ld        b,$00                         ;[0c45] 06 00
                    ret                                     ;[0c47] c9

                    ld        d,a                           ;[0c48] 57
                    inc       b                             ;[0c49] 04
                    ld        a,$1f                         ;[0c4a] 3e 1f
                    cp        b                             ;[0c4c] b8
                    jr        c,$0c55                       ;[0c4d] 38 06
                    ld        e,b                           ;[0c4f] 58
                    call      $0ca4                         ;[0c50] cd a4 0c
                    ld        a,e                           ;[0c53] 7b
                    ret       c                             ;[0c54] d8
                    dec       b                             ;[0c55] 05
                    push      bc                            ;[0c56] c5
                    push      hl                            ;[0c57] e5
                    ld        hl,$ec0d                      ;[0c58] 21 0d ec
                    bit       7,(hl)                        ;[0c5b] cb 7e
                    jr        nz,$0c90                      ;[0c5d] 20 31
                    call      $1187                         ;[0c5f] cd 87 11
                    ld        hl,$0020                      ;[0c62] 21 20 00
                    add       hl,de                         ;[0c65] 19
                    ld        a,(hl)                        ;[0c66] 7e
                    bit       1,a                           ;[0c67] cb 4f
                    jr        nz,$0c90                      ;[0c69] 20 25
                    set       1,(hl)                        ;[0c6b] cb ce
                    res       3,(hl)                        ;[0c6d] cb 9e
                    ld        hl,$0023                      ;[0c6f] 21 23 00
                    add       hl,de                         ;[0c72] 19
                    ex        de,hl                         ;[0c73] eb
                    pop       hl                            ;[0c74] e1
                    pop       bc                            ;[0c75] c1
                    push      af                            ;[0c76] f5
                    call      $0c00                         ;[0c77] cd 00 0c
                    pop       af                            ;[0c7a] f1
                    call      $1187                         ;[0c7b] cd 87 11
                    ld        hl,$0023                      ;[0c7e] 21 23 00
                    add       hl,de                         ;[0c81] 19
                    ex        de,hl                         ;[0c82] eb
                    res       0,a                           ;[0c83] cb 87
                    set       3,a                           ;[0c85] cb df
                    call      $0fa6                         ;[0c87] cd a6 0f
                    call      $16f3                         ;[0c8a] cd f3 16
                    ld        a,b                           ;[0c8d] 78
                    scf                                     ;[0c8e] 37
                    ret                                     ;[0c8f] c9

                    pop       hl                            ;[0c90] e1
                    pop       bc                            ;[0c91] c1
                    push      de                            ;[0c92] d5
                    call      $0c00                         ;[0c93] cd 00 0c
                    pop       de                            ;[0c96] d1
                    ld        a,b                           ;[0c97] 78
                    ret       nc                            ;[0c98] d0
                    ld        b,$00                         ;[0c99] 06 00
                    call      $0ca4                         ;[0c9b] cd a4 0c
                    ld        a,b                           ;[0c9e] 78
                    ret       c                             ;[0c9f] d8
                    ld        a,e                           ;[0ca0] 7b
                    ld        b,$00                         ;[0ca1] 06 00
                    ret                                     ;[0ca3] c9

                    push      de                            ;[0ca4] d5
                    push      hl                            ;[0ca5] e5
                    call      $1187                         ;[0ca6] cd 87 11
                    call      $0f14                         ;[0ca9] cd 14 0f
                    jp        $0d35                         ;[0cac] c3 35 0d
                    push      de                            ;[0caf] d5
                    push      hl                            ;[0cb0] e5
                    call      $1187                         ;[0cb1] cd 87 11
                    call      $0f36                         ;[0cb4] cd 36 0f
                    jp        $0d35                         ;[0cb7] c3 35 0d
                    push      de                            ;[0cba] d5
                    push      hl                            ;[0cbb] e5
                    call      $0c2b                         ;[0cbc] cd 2b 0c
                    jr        nc,$0cd7                      ;[0cbf] 30 16
                    call      $0aea                         ;[0cc1] cd ea 0a
                    cp        $20                           ;[0cc4] fe 20
                    jr        z,$0cbc                       ;[0cc6] 28 f4
                    call      $0c2b                         ;[0cc8] cd 2b 0c
                    jr        nc,$0cd7                      ;[0ccb] 30 0a
                    call      $0aea                         ;[0ccd] cd ea 0a
                    cp        $20                           ;[0cd0] fe 20
                    jr        nz,$0cc8                      ;[0cd2] 20 f4
                    call      $0c48                         ;[0cd4] cd 48 0c
                    jr        $0d35                         ;[0cd7] 18 5c
                    push      de                            ;[0cd9] d5
                    push      hl                            ;[0cda] e5
                    call      $0c48                         ;[0cdb] cd 48 0c
                    jr        nc,$0cfb                      ;[0cde] 30 1b
                    call      $0aea                         ;[0ce0] cd ea 0a
                    cp        $20                           ;[0ce3] fe 20
                    jr        nz,$0cdb                      ;[0ce5] 20 f4
                    call      $0c48                         ;[0ce7] cd 48 0c
                    jr        nc,$0cfb                      ;[0cea] 30 0f
                    call      $0f14                         ;[0cec] cd 14 0f
                    jr        nc,$0cfb                      ;[0cef] 30 0a
                    call      $0aea                         ;[0cf1] cd ea 0a
                    cp        $20                           ;[0cf4] fe 20
                    jr        z,$0ce7                       ;[0cf6] 28 ef
                    scf                                     ;[0cf8] 37
                    jr        $0d35                         ;[0cf9] 18 3a
                    call      nc,$0c2b                      ;[0cfb] d4 2b 0c
                    or        a                             ;[0cfe] b7
                    jr        $0d35                         ;[0cff] 18 34
                    push      de                            ;[0d01] d5
                    push      hl                            ;[0d02] e5
                    call      $1187                         ;[0d03] cd 87 11
                    ld        hl,$0020                      ;[0d06] 21 20 00
                    add       hl,de                         ;[0d09] 19
                    bit       0,(hl)                        ;[0d0a] cb 46
                    jr        nz,$0d15                      ;[0d0c] 20 07
                    call      $0bdb                         ;[0d0e] cd db 0b
                    jr        c,$0d03                       ;[0d11] 38 f0
                    jr        $0d35                         ;[0d13] 18 20
                    ld        b,$00                         ;[0d15] 06 00
                    call      $0ca4                         ;[0d17] cd a4 0c
                    jr        $0d35                         ;[0d1a] 18 19
                    push      de                            ;[0d1c] d5
                    push      hl                            ;[0d1d] e5
                    call      $1187                         ;[0d1e] cd 87 11
                    ld        hl,$0020                      ;[0d21] 21 20 00
                    add       hl,de                         ;[0d24] 19
                    bit       3,(hl)                        ;[0d25] cb 5e
                    jr        nz,$0d30                      ;[0d27] 20 07
                    call      $0c00                         ;[0d29] cd 00 0c
                    jr        c,$0d1e                       ;[0d2c] 38 f0
                    jr        $0d35                         ;[0d2e] 18 05
                    ld        b,$1f                         ;[0d30] 06 1f
                    call      $0caf                         ;[0d32] cd af 0c
                    pop       hl                            ;[0d35] e1
                    pop       de                            ;[0d36] d1
                    ret                                     ;[0d37] c9

                    ld        a,($ec0d)                     ;[0d38] 3a 0d ec
                    bit       3,a                           ;[0d3b] cb 5f
                    scf                                     ;[0d3d] 37
                    ret       z                             ;[0d3e] c8
                    call      $1187                         ;[0d3f] cd 87 11
                    ld        hl,$0020                      ;[0d42] 21 20 00
                    add       hl,de                         ;[0d45] 19
                    bit       3,(hl)                        ;[0d46] cb 5e
                    scf                                     ;[0d48] 37
                    ret       z                             ;[0d49] c8
                    jr        $0d5e                         ;[0d4a] 18 12
                    ld        a,($ec0d)                     ;[0d4c] 3a 0d ec
                    bit       3,a                           ;[0d4f] cb 5f
                    scf                                     ;[0d51] 37
                    ret       z                             ;[0d52] c8
                    call      $1187                         ;[0d53] cd 87 11
                    ld        hl,$0020                      ;[0d56] 21 20 00
                    add       hl,de                         ;[0d59] 19
                    bit       0,(hl)                        ;[0d5a] cb 46
                    scf                                     ;[0d5c] 37
                    ret       z                             ;[0d5d] c8
                    ld        a,$02                         ;[0d5e] 3e 02
                    call      $1187                         ;[0d60] cd 87 11
                    ld        hl,$0020                      ;[0d63] 21 20 00
                    add       hl,de                         ;[0d66] 19
                    bit       0,(hl)                        ;[0d67] cb 46
                    jr        nz,$0d73                      ;[0d69] 20 08
                    dec       c                             ;[0d6b] 0d
                    jp        p,$0d60                       ;[0d6c] f2 60 0d
                    ld        c,$00                         ;[0d6f] 0e 00
                    ld        a,$01                         ;[0d71] 3e 01
                    ld        hl,$ec00                      ;[0d73] 21 00 ec
                    ld        de,$ec03                      ;[0d76] 11 03 ec
                    or        $80                           ;[0d79] f6 80
                    ld        (hl),a                        ;[0d7b] 77
                    ld        (de),a                        ;[0d7c] 12
                    inc       hl                            ;[0d7d] 23
                    inc       de                            ;[0d7e] 13
                    ld        a,$00                         ;[0d7f] 3e 00
                    ld        (hl),a                        ;[0d81] 77
                    ld        (de),a                        ;[0d82] 12
                    inc       hl                            ;[0d83] 23
                    inc       de                            ;[0d84] 13
                    ld        a,c                           ;[0d85] 79
                    ld        (hl),a                        ;[0d86] 77
                    ld        (de),a                        ;[0d87] 12
                    ld        hl,$0000                      ;[0d88] 21 00 00
                    ld        ($ec06),hl                    ;[0d8b] 22 06 ec
                    call      $1432                         ;[0d8e] cd 32 14
                    call      $1dff                         ;[0d91] cd ff 1d
                    push      ix                            ;[0d94] dd e5
                    call      $05ac                         ;[0d96] cd ac 05
                    call      $3e80                         ;[0d99] cd 80 3e
                    ld        e,l                           ;[0d9c] 5d
                    ld        h,$cd                         ;[0d9d] 26 cd
                    pop       de                            ;[0d9f] d1
                    dec       b                             ;[0da0] 05
                    ei                                      ;[0da1] fb
                    pop       ix                            ;[0da2] dd e1
                    ld        a,($5c3a)                     ;[0da4] 3a 3a 5c
                    inc       a                             ;[0da7] 3c
                    jr        nz,$0dc2                      ;[0da8] 20 18
                    ld        hl,$ec0d                      ;[0daa] 21 0d ec
                    res       3,(hl)                        ;[0dad] cb 9e
                    call      $175d                         ;[0daf] cd 5d 17
                    ld        a,($ec0e)                     ;[0db2] 3a 0e ec
                    cp        $04                           ;[0db5] fe 04
                    call      nz,$03ff                      ;[0db7] c4 ff 03
                    call      $07ac                         ;[0dba] cd ac 07
                    call      $0ad7                         ;[0dbd] cd d7 0a
                    scf                                     ;[0dc0] 37
                    ret                                     ;[0dc1] c9

                    ld        hl,$ec00                      ;[0dc2] 21 00 ec
                    ld        de,$ec03                      ;[0dc5] 11 03 ec
                    ld        a,(de)                        ;[0dc8] 1a
                    res       7,a                           ;[0dc9] cb bf
                    ld        (hl),a                        ;[0dcb] 77
                    inc       hl                            ;[0dcc] 23
                    inc       de                            ;[0dcd] 13
                    ld        a,(de)                        ;[0dce] 1a
                    ld        (hl),a                        ;[0dcf] 77
                    inc       hl                            ;[0dd0] 23
                    inc       de                            ;[0dd1] 13
                    ld        a,(de)                        ;[0dd2] 1a
                    ld        (hl),a                        ;[0dd3] 77
                    call      $1dfb                         ;[0dd4] cd fb 1d
                    jr        c,$0ddd                       ;[0dd7] 38 04
                    ld        bc,($ec06)                    ;[0dd9] ed 4b 06 ec
                    ld        hl,($ec06)                    ;[0ddd] 2a 06 ec
                    or        a                             ;[0de0] b7
                    sbc       hl,bc                         ;[0de1] ed 42
                    push      af                            ;[0de3] f5
                    push      hl                            ;[0de4] e5
                    call      $0ad7                         ;[0de5] cd d7 0a
                    pop       hl                            ;[0de8] e1
                    pop       af                            ;[0de9] f1
                    jr        c,$0dfd                       ;[0dea] 38 11
                    jr        z,$0e18                       ;[0dec] 28 2a
                    push      hl                            ;[0dee] e5
                    ld        a,b                           ;[0def] 78
                    call      $0c2b                         ;[0df0] cd 2b 0c
                    pop       hl                            ;[0df3] e1
                    jr        nc,$0e18                      ;[0df4] 30 22
                    dec       hl                            ;[0df6] 2b
                    ld        a,h                           ;[0df7] 7c
                    or        l                             ;[0df8] b5
                    jr        nz,$0dee                      ;[0df9] 20 f3
                    jr        $0e18                         ;[0dfb] 18 1b
                    push      hl                            ;[0dfd] e5
                    ld        hl,$ec0d                      ;[0dfe] 21 0d ec
                    res       7,(hl)                        ;[0e01] cb be
                    pop       hl                            ;[0e03] e1
                    ex        de,hl                         ;[0e04] eb
                    ld        hl,$0000                      ;[0e05] 21 00 00
                    or        a                             ;[0e08] b7
                    sbc       hl,de                         ;[0e09] ed 52
                    push      hl                            ;[0e0b] e5
                    ld        a,b                           ;[0e0c] 78
                    call      $0c48                         ;[0e0d] cd 48 0c
                    pop       hl                            ;[0e10] e1
                    jr        nc,$0e18                      ;[0e11] 30 05
                    dec       hl                            ;[0e13] 2b
                    ld        a,h                           ;[0e14] 7c
                    or        l                             ;[0e15] b5
                    jr        nz,$0e0b                      ;[0e16] 20 f3
                    ld        hl,$ec0d                      ;[0e18] 21 0d ec
                    set       7,(hl)                        ;[0e1b] cb fe
                    call      $0ae1                         ;[0e1d] cd e1 0a
                    ld        a,$17                         ;[0e20] 3e 17
                    call      $1ca8                         ;[0e22] cd a8 1c
                    or        a                             ;[0e25] b7
                    ret                                     ;[0e26] c9

                    ld        hl,$ec00                      ;[0e27] 21 00 ec
                    bit       7,(hl)                        ;[0e2a] cb 7e
                    jr        z,$0e35                       ;[0e2c] 28 07
                    ld        hl,($ec06)                    ;[0e2e] 2a 06 ec
                    inc       hl                            ;[0e31] 23
                    ld        ($ec06),hl                    ;[0e32] 22 06 ec
                    ld        hl,$ec00                      ;[0e35] 21 00 ec
                    ld        a,(hl)                        ;[0e38] 7e
                    inc       hl                            ;[0e39] 23
                    ld        b,(hl)                        ;[0e3a] 46
                    inc       hl                            ;[0e3b] 23
                    ld        c,(hl)                        ;[0e3c] 4e
                    push      hl                            ;[0e3d] e5
                    and       $0f                           ;[0e3e] e6 0f
                    ld        hl,$0e58                      ;[0e40] 21 58 0e
                    call      $216b                         ;[0e43] cd 6b 21
                    ld        e,l                           ;[0e46] 5d
                    pop       hl                            ;[0e47] e1
                    jr        z,$0e4c                       ;[0e48] 28 02
                    ld        a,$0d                         ;[0e4a] 3e 0d
                    ld        (hl),c                        ;[0e4c] 71
                    dec       hl                            ;[0e4d] 2b
                    ld        (hl),b                        ;[0e4e] 70
                    dec       hl                            ;[0e4f] 2b
                    push      af                            ;[0e50] f5
                    ld        a,(hl)                        ;[0e51] 7e
                    and       $f0                           ;[0e52] e6 f0
                    or        e                             ;[0e54] b3
                    ld        (hl),a                        ;[0e55] 77
                    pop       af                            ;[0e56] f1
                    ret                                     ;[0e57] c9

                    inc       bc                            ;[0e58] 03
                    ld        (bc),a                        ;[0e59] 02
                    ld        a,a                           ;[0e5a] 7f
                    ld        c,$04                         ;[0e5b] 0e 04
                    cp        h                             ;[0e5d] bc
                    ld        c,$01                         ;[0e5e] 0e 01
                    ld        h,d                           ;[0e60] 62
                    ld        c,$cd                         ;[0e61] 0e cd
                    adc       d                             ;[0e63] 8a
                    inc       de                            ;[0e64] 13
                    call      $0ee1                         ;[0e65] cd e1 0e
                    jr        nc,$0e71                      ;[0e68] 30 07
                    cp        $00                           ;[0e6a] fe 00
                    jr        z,$0e65                       ;[0e6c] 28 f7
                    ld        l,$01                         ;[0e6e] 2e 01
                    ret                                     ;[0e70] c9

                    inc       c                             ;[0e71] 0c
                    ld        b,$00                         ;[0e72] 06 00
                    ld        hl,($f9db)                    ;[0e74] 2a db f9
                    ld        a,c                           ;[0e77] 79
                    cp        (hl)                          ;[0e78] be
                    jr        c,$0e62                       ;[0e79] 38 e7
                    ld        b,$00                         ;[0e7b] 06 00
                    ld        c,$00                         ;[0e7d] 0e 00
                    push      hl                            ;[0e7f] e5
                    ld        hl,$f6ee                      ;[0e80] 21 ee f6
                    ld        a,(hl)                        ;[0e83] 7e
                    cp        c                             ;[0e84] b9
                    jr        nz,$0e91                      ;[0e85] 20 0a
                    inc       hl                            ;[0e87] 23
                    ld        a,(hl)                        ;[0e88] 7e
                    cp        b                             ;[0e89] b8
                    jr        nz,$0e91                      ;[0e8a] 20 05
                    ld        hl,$ec00                      ;[0e8c] 21 00 ec
                    res       7,(hl)                        ;[0e8f] cb be
                    pop       hl                            ;[0e91] e1
                    call      $1187                         ;[0e92] cd 87 11
                    call      $0ee1                         ;[0e95] cd e1 0e
                    jr        nc,$0ea1                      ;[0e98] 30 07
                    cp        $00                           ;[0e9a] fe 00
                    jr        z,$0e7f                       ;[0e9c] 28 e1
                    ld        l,$02                         ;[0e9e] 2e 02
                    ret                                     ;[0ea0] c9

                    ld        hl,$0020                      ;[0ea1] 21 20 00
                    add       hl,de                         ;[0ea4] 19
                    bit       3,(hl)                        ;[0ea5] cb 5e
                    jr        z,$0eae                       ;[0ea7] 28 05
                    ld        l,$08                         ;[0ea9] 2e 08
                    ld        a,$0d                         ;[0eab] 3e 0d
                    ret                                     ;[0ead] c9

                    ld        hl,$f6f3                      ;[0eae] 21 f3 f6
                    inc       c                             ;[0eb1] 0c
                    ld        a,(hl)                        ;[0eb2] 7e
                    cp        c                             ;[0eb3] b9
                    ld        b,$00                         ;[0eb4] 06 00
                    jr        nc,$0e92                      ;[0eb6] 30 da
                    ld        b,$00                         ;[0eb8] 06 00
                    ld        c,$01                         ;[0eba] 0e 01
                    call      $1296                         ;[0ebc] cd 96 12
                    call      $0ee1                         ;[0ebf] cd e1 0e
                    jr        nc,$0ecb                      ;[0ec2] 30 07
                    cp        $00                           ;[0ec4] fe 00
                    jr        z,$0ebf                       ;[0ec6] 28 f7
                    ld        l,$04                         ;[0ec8] 2e 04
                    ret                                     ;[0eca] c9

                    ld        hl,$0020                      ;[0ecb] 21 20 00
                    add       hl,de                         ;[0ece] 19
                    bit       3,(hl)                        ;[0ecf] cb 5e
                    jr        nz,$0edc                      ;[0ed1] 20 09
                    inc       c                             ;[0ed3] 0c
                    ld        b,$00                         ;[0ed4] 06 00
                    ld        a,($f6f5)                     ;[0ed6] 3a f5 f6
                    cp        c                             ;[0ed9] b9
                    jr        nc,$0ebc                      ;[0eda] 30 e0
                    ld        l,$08                         ;[0edc] 2e 08
                    ld        a,$0d                         ;[0ede] 3e 0d
                    ret                                     ;[0ee0] c9

                    ld        a,$1f                         ;[0ee1] 3e 1f
                    cp        b                             ;[0ee3] b8
                    ccf                                     ;[0ee4] 3f
                    ret       nc                            ;[0ee5] d0
                    ld        l,b                           ;[0ee6] 68
                    ld        h,$00                         ;[0ee7] 26 00
                    add       hl,de                         ;[0ee9] 19
                    ld        a,(hl)                        ;[0eea] 7e
                    inc       b                             ;[0eeb] 04
                    scf                                     ;[0eec] 37
                    ret                                     ;[0eed] c9

                    ld        bc,$0114                      ;[0eee] 01 14 01
                    ld        bc,$3c21                      ;[0ef1] 01 21 3c
                    ld        e,h                           ;[0ef4] 5c
                    res       0,(hl)                        ;[0ef5] cb 86
                    ld        hl,$0eee                      ;[0ef7] 21 ee 0e
                    ld        de,$ec15                      ;[0efa] 11 15 ec
                    jp        $2157                         ;[0efd] c3 57 21
                    ld        hl,$5c3c                      ;[0f00] 21 3c 5c
                    set       0,(hl)                        ;[0f03] cb c6
                    ld        bc,$0000                      ;[0f05] 01 00 00
                    call      $1922                         ;[0f08] cd 22 19
                    ld        hl,$0ef0                      ;[0f0b] 21 f0 0e
                    ld        de,$ec15                      ;[0f0e] 11 15 ec
                    jp        $2157                         ;[0f11] c3 57 21
                    ld        h,$00                         ;[0f14] 26 00
                    ld        l,b                           ;[0f16] 68
                    add       hl,de                         ;[0f17] 19
                    ld        a,(hl)                        ;[0f18] 7e
                    cp        $00                           ;[0f19] fe 00
                    scf                                     ;[0f1b] 37
                    ret       nz                            ;[0f1c] c0
                    ld        a,b                           ;[0f1d] 78
                    or        a                             ;[0f1e] b7
                    jr        z,$0f2e                       ;[0f1f] 28 0d
                    push      hl                            ;[0f21] e5
                    dec       hl                            ;[0f22] 2b
                    ld        a,(hl)                        ;[0f23] 7e
                    cp        $00                           ;[0f24] fe 00
                    scf                                     ;[0f26] 37
                    pop       hl                            ;[0f27] e1
                    ret       nz                            ;[0f28] c0
                    ld        a,(hl)                        ;[0f29] 7e
                    cp        $00                           ;[0f2a] fe 00
                    scf                                     ;[0f2c] 37
                    ret       nz                            ;[0f2d] c0
                    inc       hl                            ;[0f2e] 23
                    inc       b                             ;[0f2f] 04
                    ld        a,b                           ;[0f30] 78
                    cp        $1f                           ;[0f31] fe 1f
                    jr        c,$0f29                       ;[0f33] 38 f4
                    ret                                     ;[0f35] c9

                    ld        h,$00                         ;[0f36] 26 00
                    ld        l,b                           ;[0f38] 68
                    add       hl,de                         ;[0f39] 19
                    ld        a,(hl)                        ;[0f3a] 7e
                    cp        $00                           ;[0f3b] fe 00
                    scf                                     ;[0f3d] 37
                    ret       nz                            ;[0f3e] c0
                    ld        a,(hl)                        ;[0f3f] 7e
                    cp        $00                           ;[0f40] fe 00
                    jr        nz,$0f4b                      ;[0f42] 20 07
                    ld        a,b                           ;[0f44] 78
                    or        a                             ;[0f45] b7
                    ret       z                             ;[0f46] c8
                    dec       hl                            ;[0f47] 2b
                    dec       b                             ;[0f48] 05
                    jr        $0f3f                         ;[0f49] 18 f4
                    inc       b                             ;[0f4b] 04
                    scf                                     ;[0f4c] 37
                    ret                                     ;[0f4d] c9

                    ld        h,$00                         ;[0f4e] 26 00
                    ld        l,b                           ;[0f50] 68
                    add       hl,de                         ;[0f51] 19
                    ld        a,(hl)                        ;[0f52] 7e
                    ret                                     ;[0f53] c9

                    ld        hl,$ec0d                      ;[0f54] 21 0d ec
                    or        a                             ;[0f57] b7
                    bit       0,(hl)                        ;[0f58] cb 46
                    ret       nz                            ;[0f5a] c0
                    push      bc                            ;[0f5b] c5
                    push      af                            ;[0f5c] f5
                    call      $1187                         ;[0f5d] cd 87 11
                    pop       af                            ;[0f60] f1
                    call      $057c                         ;[0f61] cd 7c 05
                    push      af                            ;[0f64] f5
                    ex        de,hl                         ;[0f65] eb
                    call      $1703                         ;[0f66] cd 03 17
                    ex        de,hl                         ;[0f69] eb
                    pop       af                            ;[0f6a] f1
                    ccf                                     ;[0f6b] 3f
                    jr        z,$0f9f                       ;[0f6c] 28 31
                    push      af                            ;[0f6e] f5
                    ld        b,$00                         ;[0f6f] 06 00
                    inc       c                             ;[0f71] 0c
                    ld        a,($ec15)                     ;[0f72] 3a 15 ec
                    cp        c                             ;[0f75] b9
                    jr        c,$0f9b                       ;[0f76] 38 23
                    ld        a,(hl)                        ;[0f78] 7e
                    ld        e,a                           ;[0f79] 5f
                    and       $d7                           ;[0f7a] e6 d7
                    cp        (hl)                          ;[0f7c] be
                    ld        (hl),a                        ;[0f7d] 77
                    ld        a,e                           ;[0f7e] 7b
                    set       1,(hl)                        ;[0f7f] cb ce
                    push      af                            ;[0f81] f5
                    call      $1187                         ;[0f82] cd 87 11
                    pop       af                            ;[0f85] f1
                    jr        z,$0f95                       ;[0f86] 28 0d
                    res       0,a                           ;[0f88] cb 87
                    call      $0fa6                         ;[0f8a] cd a6 0f
                    jr        nc,$0f9f                      ;[0f8d] 30 10
                    call      $16f3                         ;[0f8f] cd f3 16
                    pop       af                            ;[0f92] f1
                    jr        $0f61                         ;[0f93] 18 cc
                    call      $0f14                         ;[0f95] cd 14 0f
                    pop       af                            ;[0f98] f1
                    jr        $0f61                         ;[0f99] 18 c6
                    pop       af                            ;[0f9b] f1
                    call      $1241                         ;[0f9c] cd 41 12
                    pop       bc                            ;[0f9f] c1
                    ret                                     ;[0fa0] c9

                    call      $1187                         ;[0fa1] cd 87 11
                    ld        a,$09                         ;[0fa4] 3e 09
                    push      bc                            ;[0fa6] c5
                    push      de                            ;[0fa7] d5
                    ld        b,c                           ;[0fa8] 41
                    ld        hl,$0fc2                      ;[0fa9] 21 c2 0f
                    ld        c,a                           ;[0fac] 4f
                    push      bc                            ;[0fad] c5
                    call      $0545                         ;[0fae] cd 45 05
                    pop       bc                            ;[0fb1] c1
                    ld        a,c                           ;[0fb2] 79
                    jr        nc,$0fbf                      ;[0fb3] 30 0a
                    ld        c,b                           ;[0fb5] 48
                    call      $1187                         ;[0fb6] cd 87 11
                    ld        hl,$0020                      ;[0fb9] 21 20 00
                    add       hl,de                         ;[0fbc] 19
                    ld        (hl),a                        ;[0fbd] 77
                    scf                                     ;[0fbe] 37
                    pop       de                            ;[0fbf] d1
                    pop       bc                            ;[0fc0] c1
                    ret                                     ;[0fc1] c9

                    nop                                     ;[0fc2] 00
                    nop                                     ;[0fc3] 00
                    nop                                     ;[0fc4] 00
                    nop                                     ;[0fc5] 00
                    nop                                     ;[0fc6] 00
                    nop                                     ;[0fc7] 00
                    nop                                     ;[0fc8] 00
                    nop                                     ;[0fc9] 00
                    nop                                     ;[0fca] 00
                    nop                                     ;[0fcb] 00
                    nop                                     ;[0fcc] 00
                    nop                                     ;[0fcd] 00
                    nop                                     ;[0fce] 00
                    nop                                     ;[0fcf] 00
                    nop                                     ;[0fd0] 00
                    nop                                     ;[0fd1] 00
                    nop                                     ;[0fd2] 00
                    nop                                     ;[0fd3] 00
                    nop                                     ;[0fd4] 00
                    nop                                     ;[0fd5] 00
                    nop                                     ;[0fd6] 00
                    nop                                     ;[0fd7] 00
                    nop                                     ;[0fd8] 00
                    nop                                     ;[0fd9] 00
                    nop                                     ;[0fda] 00
                    nop                                     ;[0fdb] 00
                    nop                                     ;[0fdc] 00
                    nop                                     ;[0fdd] 00
                    nop                                     ;[0fde] 00
                    nop                                     ;[0fdf] 00
                    nop                                     ;[0fe0] 00
                    nop                                     ;[0fe1] 00
                    add       hl,bc                         ;[0fe2] 09
                    nop                                     ;[0fe3] 00
                    nop                                     ;[0fe4] 00
                    push      bc                            ;[0fe5] c5
                    call      $1187                         ;[0fe6] cd 87 11
                    push      bc                            ;[0fe9] c5
                    ld        hl,$0020                      ;[0fea] 21 20 00
                    add       hl,de                         ;[0fed] 19
                    bit       1,(hl)                        ;[0fee] cb 4e
                    ld        a,$00                         ;[0ff0] 3e 00
                    jr        z,$1004                       ;[0ff2] 28 10
                    inc       c                             ;[0ff4] 0c
                    ld        hl,$0023                      ;[0ff5] 21 23 00
                    add       hl,de                         ;[0ff8] 19
                    ex        de,hl                         ;[0ff9] eb
                    ld        a,($ec15)                     ;[0ffa] 3a 15 ec
                    cp        c                             ;[0ffd] b9
                    jr        nc,$0fea                      ;[0ffe] 30 ea
                    dec       c                             ;[1000] 0d
                    call      $129c                         ;[1001] cd 9c 12
                    pop       hl                            ;[1004] e1
                    push      hl                            ;[1005] e5
                    call      $1187                         ;[1006] cd 87 11
                    pop       hl                            ;[1009] e1
                    ld        b,a                           ;[100a] 47
                    ld        a,c                           ;[100b] 79
                    cp        l                             ;[100c] bd
                    ld        a,b                           ;[100d] 78
                    push      af                            ;[100e] f5
                    jr        nz,$1014                      ;[100f] 20 03
                    ld        b,h                           ;[1011] 44
                    jr        $101d                         ;[1012] 18 09
                    push      af                            ;[1014] f5
                    push      hl                            ;[1015] e5
                    ld        b,$00                         ;[1016] 06 00
                    call      $0f14                         ;[1018] cd 14 0f
                    pop       hl                            ;[101b] e1
                    pop       af                            ;[101c] f1
                    push      hl                            ;[101d] e5
                    ld        hl,$f6f4                      ;[101e] 21 f4 f6
                    set       0,(hl)                        ;[1021] cb c6
                    jr        z,$1027                       ;[1023] 28 02
                    res       0,(hl)                        ;[1025] cb 86
                    call      $0591                         ;[1027] cd 91 05
                    push      af                            ;[102a] f5
                    push      bc                            ;[102b] c5
                    push      de                            ;[102c] d5
                    ld        hl,$f6f4                      ;[102d] 21 f4 f6
                    bit       0,(hl)                        ;[1030] cb 46
                    jr        nz,$1042                      ;[1032] 20 0e
                    ld        b,$00                         ;[1034] 06 00
                    call      $0ca4                         ;[1036] cd a4 0c
                    jr        c,$1042                       ;[1039] 38 07
                    call      $1053                         ;[103b] cd 53 10
                    pop       de                            ;[103e] d1
                    pop       bc                            ;[103f] c1
                    jr        $1047                         ;[1040] 18 05
                    pop       hl                            ;[1042] e1
                    pop       bc                            ;[1043] c1
                    call      $1703                         ;[1044] cd 03 17
                    pop       af                            ;[1047] f1
                    dec       c                             ;[1048] 0d
                    ld        b,a                           ;[1049] 47
                    pop       hl                            ;[104a] e1
                    pop       af                            ;[104b] f1
                    ld        a,b                           ;[104c] 78
                    jp        nz,$1005                      ;[104d] c2 05 10
                    scf                                     ;[1050] 37
                    pop       bc                            ;[1051] c1
                    ret                                     ;[1052] c9

                    ld        hl,$0020                      ;[1053] 21 20 00
                    add       hl,de                         ;[1056] 19
                    ld        a,(hl)                        ;[1057] 7e
                    bit       0,(hl)                        ;[1058] cb 46
                    jr        nz,$1085                      ;[105a] 20 29
                    push      af                            ;[105c] f5
                    push      bc                            ;[105d] c5
                    ld        a,c                           ;[105e] 79
                    or        a                             ;[105f] b7
                    jr        nz,$1077                      ;[1060] 20 15
                    push      bc                            ;[1062] c5
                    ld        hl,($fc9a)                    ;[1063] 2a 9a fc
                    call      $141d                         ;[1066] cd 1d 14
                    ld        ($fc9a),hl                    ;[1069] 22 9a fc
                    ld        a,($f9db)                     ;[106c] 3a db f9
                    ld        c,a                           ;[106f] 4f
                    dec       c                             ;[1070] 0d
                    call      $138a                         ;[1071] cd 8a 13
                    pop       bc                            ;[1074] c1
                    jr        $107b                         ;[1075] 18 04
                    dec       c                             ;[1077] 0d
                    call      $1187                         ;[1078] cd 87 11
                    pop       bc                            ;[107b] c1
                    pop       af                            ;[107c] f1
                    ld        hl,$0020                      ;[107d] 21 20 00
                    add       hl,de                         ;[1080] 19
                    res       1,(hl)                        ;[1081] cb 8e
                    or        (hl)                          ;[1083] b6
                    ld        (hl),a                        ;[1084] 77
                    ld        b,c                           ;[1085] 41
                    call      $1187                         ;[1086] cd 87 11
                    call      $11b2                         ;[1089] cd b2 11
                    jp        $0518                         ;[108c] c3 18 05
                    call      $1157                         ;[108f] cd 57 11
                    push      hl                            ;[1092] e5
                    call      $1168                         ;[1093] cd 68 11
                    jr        z,$10ca                       ;[1096] 28 32
                    call      $0c2b                         ;[1098] cd 2b 0c
                    pop       hl                            ;[109b] e1
                    jr        nc,$10cb                      ;[109c] 30 2d
                    call      $0aea                         ;[109e] cd ea 0a
                    push      af                            ;[10a1] f5
                    push      hl                            ;[10a2] e5
                    call      $0fe5                         ;[10a3] cd e5 0f
                    pop       hl                            ;[10a6] e1
                    pop       af                            ;[10a7] f1
                    cp        $20                           ;[10a8] fe 20
                    jr        z,$1092                       ;[10aa] 28 e6
                    push      hl                            ;[10ac] e5
                    call      $1168                         ;[10ad] cd 68 11
                    jr        z,$10ca                       ;[10b0] 28 18
                    call      $0c2b                         ;[10b2] cd 2b 0c
                    pop       hl                            ;[10b5] e1
                    jr        nc,$10cb                      ;[10b6] 30 13
                    call      $0aea                         ;[10b8] cd ea 0a
                    cp        $20                           ;[10bb] fe 20
                    jr        z,$10c6                       ;[10bd] 28 07
                    push      hl                            ;[10bf] e5
                    call      $0fe5                         ;[10c0] cd e5 0f
                    pop       hl                            ;[10c3] e1
                    jr        $10ac                         ;[10c4] 18 e6
                    push      hl                            ;[10c6] e5
                    call      $0c48                         ;[10c7] cd 48 0c
                    pop       hl                            ;[10ca] e1
                    ld        a,b                           ;[10cb] 78
                    push      af                            ;[10cc] f5
                    push      hl                            ;[10cd] e5
                    ld        hl,$eef5                      ;[10ce] 21 f5 ee
                    res       2,(hl)                        ;[10d1] cb 96
                    ld        a,($ec15)                     ;[10d3] 3a 15 ec
                    push      bc                            ;[10d6] c5
                    ld        b,$00                         ;[10d7] 06 00
                    ld        c,a                           ;[10d9] 4f
                    cp        a                             ;[10da] bf
                    call      $04d5                         ;[10db] cd d5 04
                    pop       bc                            ;[10de] c1
                    ld        hl,$ec0d                      ;[10df] 21 0d ec
                    set       3,(hl)                        ;[10e2] cb de
                    pop       hl                            ;[10e4] e1
                    call      $0ac8                         ;[10e5] cd c8 0a
                    pop       af                            ;[10e8] f1
                    ret                                     ;[10e9] c9

                    call      $1157                         ;[10ea] cd 57 11
                    push      hl                            ;[10ed] e5
                    call      $0aea                         ;[10ee] cd ea 0a
                    pop       hl                            ;[10f1] e1
                    cp        $00                           ;[10f2] fe 00
                    scf                                     ;[10f4] 37
                    jr        z,$10cb                       ;[10f5] 28 d4
                    push      af                            ;[10f7] f5
                    push      hl                            ;[10f8] e5
                    call      $0fe5                         ;[10f9] cd e5 0f
                    pop       hl                            ;[10fc] e1
                    pop       af                            ;[10fd] f1
                    cp        $20                           ;[10fe] fe 20
                    jr        nz,$10ed                      ;[1100] 20 eb
                    call      $0aea                         ;[1102] cd ea 0a
                    cp        $20                           ;[1105] fe 20
                    scf                                     ;[1107] 37
                    jr        nz,$10cb                      ;[1108] 20 c1
                    push      hl                            ;[110a] e5
                    call      $0fe5                         ;[110b] cd e5 0f
                    pop       hl                            ;[110e] e1
                    jr        $1102                         ;[110f] 18 f1
                    call      $1157                         ;[1111] cd 57 11
                    push      hl                            ;[1114] e5
                    call      $1187                         ;[1115] cd 87 11
                    ld        hl,$0020                      ;[1118] 21 20 00
                    add       hl,de                         ;[111b] 19
                    bit       0,(hl)                        ;[111c] cb 46
                    jr        nz,$112c                      ;[111e] 20 0c
                    call      $0c2b                         ;[1120] cd 2b 0c
                    jr        nc,$1140                      ;[1123] 30 1b
                    call      $0fe5                         ;[1125] cd e5 0f
                    pop       hl                            ;[1128] e1
                    jr        $1114                         ;[1129] 18 e9
                    push      hl                            ;[112b] e5
                    ld        a,b                           ;[112c] 78
                    cp        $00                           ;[112d] fe 00
                    jr        z,$1140                       ;[112f] 28 0f
                    dec       b                             ;[1131] 05
                    call      $0aea                         ;[1132] cd ea 0a
                    inc       b                             ;[1135] 04
                    cp        $00                           ;[1136] fe 00
                    jr        z,$1140                       ;[1138] 28 06
                    dec       b                             ;[113a] 05
                    call      $0fe5                         ;[113b] cd e5 0f
                    jr        $112c                         ;[113e] 18 ec
                    pop       hl                            ;[1140] e1
                    scf                                     ;[1141] 37
                    jp        $10cb                         ;[1142] c3 cb 10
                    call      $1157                         ;[1145] cd 57 11
                    call      $0aea                         ;[1148] cd ea 0a
                    cp        $00                           ;[114b] fe 00
                    scf                                     ;[114d] 37
                    jr        z,$1141                       ;[114e] 28 f1
                    push      hl                            ;[1150] e5
                    call      $0fe5                         ;[1151] cd e5 0f
                    pop       hl                            ;[1154] e1
                    jr        $1148                         ;[1155] 18 f1
                    ld        hl,$ec0d                      ;[1157] 21 0d ec
                    res       0,(hl)                        ;[115a] cb 86
                    call      $0abc                         ;[115c] cd bc 0a
                    ld        hl,$eef5                      ;[115f] 21 f5 ee
                    set       2,(hl)                        ;[1162] cb d6
                    ld        hl,$f6f1                      ;[1164] 21 f1 f6
                    ret                                     ;[1167] c9

                    call      $1187                         ;[1168] cd 87 11
                    ld        hl,$0020                      ;[116b] 21 20 00
                    add       hl,de                         ;[116e] 19
                    bit       0,(hl)                        ;[116f] cb 46
                    jr        z,$1181                       ;[1171] 28 0e
                    ld        a,b                           ;[1173] 78
                    cp        $00                           ;[1174] fe 00
                    jr        z,$1185                       ;[1176] 28 0d
                    dec       b                             ;[1178] 05
                    call      $0aea                         ;[1179] cd ea 0a
                    inc       b                             ;[117c] 04
                    cp        $00                           ;[117d] fe 00
                    jr        z,$1185                       ;[117f] 28 04
                    ld        a,$01                         ;[1181] 3e 01
                    or        a                             ;[1183] b7
                    ret                                     ;[1184] c9

                    xor       a                             ;[1185] af
                    ret                                     ;[1186] c9

                    ld        hl,$ec16                      ;[1187] 21 16 ec
                    push      af                            ;[118a] f5
                    ld        a,c                           ;[118b] 79
                    ld        de,$0023                      ;[118c] 11 23 00
                    or        a                             ;[118f] b7
                    jr        z,$1196                       ;[1190] 28 04
                    add       hl,de                         ;[1192] 19
                    dec       a                             ;[1193] 3d
                    jr        $118f                         ;[1194] 18 f9
                    ex        de,hl                         ;[1196] eb
                    pop       af                            ;[1197] f1
                    ret                                     ;[1198] c9

                    push      de                            ;[1199] d5
                    call      $1187                         ;[119a] cd 87 11
                    ld        h,$00                         ;[119d] 26 00
                    ld        l,b                           ;[119f] 68
                    add       hl,de                         ;[11a0] 19
                    pop       de                            ;[11a1] d1
                    ret                                     ;[11a2] c9

                    dec       b                             ;[11a3] 05
                    nop                                     ;[11a4] 00
                    nop                                     ;[11a5] 00
                    nop                                     ;[11a6] 00
                    ret       m                             ;[11a7] f8
                    or        $21                           ;[11a8] f6 21
                    and       e                             ;[11aa] a3
                    ld        de,$f511                      ;[11ab] 11 11 f5
                    or        $c3                           ;[11ae] f6 c3
                    ld        d,a                           ;[11b0] 57
                    ld        hl,$d5c5                      ;[11b1] 21 c5 d5
                    ld        hl,$f6f5                      ;[11b4] 21 f5 f6
                    push      hl                            ;[11b7] e5
                    ld        a,(hl)                        ;[11b8] 7e
                    or        a                             ;[11b9] b7
                    jr        nz,$11d4                      ;[11ba] 20 18
                    push      hl                            ;[11bc] e5
                    call      $1432                         ;[11bd] cd 32 14
                    ld        hl,($f9d7)                    ;[11c0] 2a d7 f9
                    call      $1425                         ;[11c3] cd 25 14
                    jr        nc,$11cb                      ;[11c6] 30 03
                    ld        ($f9d7),hl                    ;[11c8] 22 d7 f9
                    ld        b,h                           ;[11cb] 44
                    ld        c,l                           ;[11cc] 4d
                    pop       hl                            ;[11cd] e1
                    call      $13a9                         ;[11ce] cd a9 13
                    dec       a                             ;[11d1] 3d
                    jr        $11e9                         ;[11d2] 18 15
                    ld        hl,$ec0d                      ;[11d4] 21 0d ec
                    res       0,(hl)                        ;[11d7] cb 86
                    ld        hl,$f6f8                      ;[11d9] 21 f8 f6
                    ld        d,h                           ;[11dc] 54
                    ld        e,l                           ;[11dd] 5d
                    ld        bc,$0023                      ;[11de] 01 23 00
                    add       hl,bc                         ;[11e1] 09
                    ld        bc,$02bc                      ;[11e2] 01 bc 02
                    ldir                                    ;[11e5] ed b0
                    dec       a                             ;[11e7] 3d
                    scf                                     ;[11e8] 37
                    pop       de                            ;[11e9] d1
                    ld        (de),a                        ;[11ea] 12
                    ld        hl,$f6f8                      ;[11eb] 21 f8 f6
                    pop       de                            ;[11ee] d1
                    pop       bc                            ;[11ef] c1
                    ret                                     ;[11f0] c9

                    push      bc                            ;[11f1] c5
                    push      de                            ;[11f2] d5
                    ld        hl,$0020                      ;[11f3] 21 20 00
                    add       hl,de                         ;[11f6] 19
                    ld        a,(hl)                        ;[11f7] 7e
                    cpl                                     ;[11f8] 2f
                    and       $11                           ;[11f9] e6 11
                    jr        nz,$1212                      ;[11fb] 20 15
                    push      hl                            ;[11fd] e5
                    push      de                            ;[11fe] d5
                    inc       hl                            ;[11ff] 23
                    ld        d,(hl)                        ;[1200] 56
                    inc       hl                            ;[1201] 23
                    ld        e,(hl)                        ;[1202] 5e
                    push      de                            ;[1203] d5
                    call      $1432                         ;[1204] cd 32 14
                    pop       hl                            ;[1207] e1
                    call      $141d                         ;[1208] cd 1d 14
                    jr        nc,$1210                      ;[120b] 30 03
                    ld        ($f9d7),hl                    ;[120d] 22 d7 f9
                    pop       de                            ;[1210] d1
                    pop       hl                            ;[1211] e1
                    bit       0,(hl)                        ;[1212] cb 46
                    ld        hl,$f6f5                      ;[1214] 21 f5 f6
                    push      hl                            ;[1217] e5
                    jr        z,$121f                       ;[1218] 28 05
                    ld        a,$00                         ;[121a] 3e 00
                    scf                                     ;[121c] 37
                    jr        $11e9                         ;[121d] 18 ca
                    ld        a,(hl)                        ;[121f] 7e
                    cp        $14                           ;[1220] fe 14
                    jr        z,$11e9                       ;[1222] 28 c5
                    ld        bc,$0023                      ;[1224] 01 23 00
                    ld        hl,$f6f8                      ;[1227] 21 f8 f6
                    ex        de,hl                         ;[122a] eb
                    ldir                                    ;[122b] ed b0
                    ld        hl,$f9d6                      ;[122d] 21 d6 f9
                    ld        d,h                           ;[1230] 54
                    ld        e,l                           ;[1231] 5d
                    ld        bc,$0023                      ;[1232] 01 23 00
                    or        a                             ;[1235] b7
                    sbc       hl,bc                         ;[1236] ed 42
                    ld        bc,$02bc                      ;[1238] 01 bc 02
                    lddr                                    ;[123b] ed b8
                    inc       a                             ;[123d] 3c
                    scf                                     ;[123e] 37
                    jr        $11e9                         ;[123f] 18 a8
                    push      bc                            ;[1241] c5
                    push      de                            ;[1242] d5
                    push      af                            ;[1243] f5
                    ld        b,$00                         ;[1244] 06 00
                    ld        c,$01                         ;[1246] 0e 01
                    push      hl                            ;[1248] e5
                    call      $1296                         ;[1249] cd 96 12
                    pop       hl                            ;[124c] e1
                    bit       3,(hl)                        ;[124d] cb 5e
                    res       3,(hl)                        ;[124f] cb 9e
                    jr        nz,$1273                      ;[1251] 20 20
                    call      $0f14                         ;[1253] cd 14 0f
                    pop       af                            ;[1256] f1
                    call      $057c                         ;[1257] cd 7c 05
                    jr        z,$128d                       ;[125a] 28 31
                    push      af                            ;[125c] f5
                    ld        b,$00                         ;[125d] 06 00
                    inc       c                             ;[125f] 0c
                    ld        a,c                           ;[1260] 79
                    cp        $15                           ;[1261] fe 15
                    jr        c,$1273                       ;[1263] 38 0e
                    dec       hl                            ;[1265] 2b
                    ld        a,(hl)                        ;[1266] 7e
                    inc       hl                            ;[1267] 23
                    cp        $00                           ;[1268] fe 00
                    jr        z,$1273                       ;[126a] 28 07
                    push      hl                            ;[126c] e5
                    ld        hl,$ec0d                      ;[126d] 21 0d ec
                    set       0,(hl)                        ;[1270] cb c6
                    pop       hl                            ;[1272] e1
                    bit       1,(hl)                        ;[1273] cb 4e
                    set       1,(hl)                        ;[1275] cb ce
                    res       3,(hl)                        ;[1277] cb 9e
                    call      $1296                         ;[1279] cd 96 12
                    jr        nz,$1253                      ;[127c] 20 d5
                    push      bc                            ;[127e] c5
                    push      de                            ;[127f] d5
                    call      $16e5                         ;[1280] cd e5 16
                    ld        (hl),$08                      ;[1283] 36 08
                    pop       de                            ;[1285] d1
                    pop       bc                            ;[1286] c1
                    call      $16f3                         ;[1287] cd f3 16
                    pop       af                            ;[128a] f1
                    jr        $1257                         ;[128b] 18 ca
                    ld        a,c                           ;[128d] 79
                    ld        ($f6f5),a                     ;[128e] 32 f5 f6
                    set       3,(hl)                        ;[1291] cb de
                    pop       de                            ;[1293] d1
                    pop       bc                            ;[1294] c1
                    ret                                     ;[1295] c9

                    ld        hl,$f6f8                      ;[1296] 21 f8 f6
                    jp        $118a                         ;[1299] c3 8a 11
                    push      bc                            ;[129c] c5
                    push      de                            ;[129d] d5
                    ld        hl,$ec0d                      ;[129e] 21 0d ec
                    res       0,(hl)                        ;[12a1] cb 86
                    ld        a,($f6f5)                     ;[12a3] 3a f5 f6
                    ld        c,a                           ;[12a6] 4f
                    or        a                             ;[12a7] b7
                    ld        a,$00                         ;[12a8] 3e 00
                    jr        z,$12ee                       ;[12aa] 28 42
                    call      $1296                         ;[12ac] cd 96 12
                    push      af                            ;[12af] f5
                    ld        b,$00                         ;[12b0] 06 00
                    call      $0f14                         ;[12b2] cd 14 0f
                    jr        nc,$12c5                      ;[12b5] 30 0e
                    pop       af                            ;[12b7] f1
                    call      $0591                         ;[12b8] cd 91 05
                    push      af                            ;[12bb] f5
                    push      bc                            ;[12bc] c5
                    ld        b,$00                         ;[12bd] 06 00
                    call      $0f14                         ;[12bf] cd 14 0f
                    pop       bc                            ;[12c2] c1
                    jr        c,$12e9                       ;[12c3] 38 24
                    inc       hl                            ;[12c5] 23
                    ld        a,(hl)                        ;[12c6] 7e
                    push      af                            ;[12c7] f5
                    push      bc                            ;[12c8] c5
                    ld        a,c                           ;[12c9] 79
                    cp        $01                           ;[12ca] fe 01
                    jr        nz,$12d7                      ;[12cc] 20 09
                    ld        a,($ec15)                     ;[12ce] 3a 15 ec
                    ld        c,a                           ;[12d1] 4f
                    call      $1187                         ;[12d2] cd 87 11
                    jr        $12db                         ;[12d5] 18 04
                    dec       c                             ;[12d7] 0d
                    call      $1296                         ;[12d8] cd 96 12
                    pop       bc                            ;[12db] c1
                    pop       af                            ;[12dc] f1
                    ld        hl,$0020                      ;[12dd] 21 20 00
                    add       hl,de                         ;[12e0] 19
                    res       1,(hl)                        ;[12e1] cb 8e
                    or        (hl)                          ;[12e3] b6
                    ld        (hl),a                        ;[12e4] 77
                    ld        hl,$f6f5                      ;[12e5] 21 f5 f6
                    dec       (hl)                          ;[12e8] 35
                    pop       af                            ;[12e9] f1
                    dec       c                             ;[12ea] 0d
                    jr        nz,$12ac                      ;[12eb] 20 bf
                    scf                                     ;[12ed] 37
                    pop       de                            ;[12ee] d1
                    pop       bc                            ;[12ef] c1
                    ret                                     ;[12f0] c9

                    inc       bc                            ;[12f1] 03
                    nop                                     ;[12f2] 00
                    sbc       $f9                           ;[12f3] de f9
                    ld        hl,$12f1                      ;[12f5] 21 f1 12
                    ld        de,$f9db                      ;[12f8] 11 db f9
                    jp        $2157                         ;[12fb] c3 57 21
                    push      bc                            ;[12fe] c5
                    push      de                            ;[12ff] d5
                    ld        hl,$f9db                      ;[1300] 21 db f9
                    push      hl                            ;[1303] e5
                    ld        a,(hl)                        ;[1304] 7e
                    or        a                             ;[1305] b7
                    jr        nz,$1326                      ;[1306] 20 1e
                    push      hl                            ;[1308] e5
                    call      $1432                         ;[1309] cd 32 14
                    ld        hl,($fc9a)                    ;[130c] 2a 9a fc
                    call      $141d                         ;[130f] cd 1d 14
                    jr        nc,$1317                      ;[1312] 30 03
                    ld        ($fc9a),hl                    ;[1314] 22 9a fc
                    ld        b,h                           ;[1317] 44
                    ld        c,l                           ;[1318] 4d
                    pop       hl                            ;[1319] e1
                    inc       hl                            ;[131a] 23
                    inc       hl                            ;[131b] 23
                    inc       hl                            ;[131c] 23
                    jr        nc,$1330                      ;[131d] 30 11
                    call      $13a9                         ;[131f] cd a9 13
                    dec       a                             ;[1322] 3d
                    ex        de,hl                         ;[1323] eb
                    jr        $1330                         ;[1324] 18 0a
                    ld        hl,($f9dc)                    ;[1326] 2a dc f9
                    ld        bc,$0023                      ;[1329] 01 23 00
                    sbc       hl,bc                         ;[132c] ed 42
                    scf                                     ;[132e] 37
                    dec       a                             ;[132f] 3d
                    ex        de,hl                         ;[1330] eb
                    pop       hl                            ;[1331] e1
                    jr        nc,$1335                      ;[1332] 30 01
                    ld        (hl),a                        ;[1334] 77
                    inc       hl                            ;[1335] 23
                    ld        (hl),e                        ;[1336] 73
                    inc       hl                            ;[1337] 23
                    ld        (hl),d                        ;[1338] 72
                    ex        de,hl                         ;[1339] eb
                    pop       de                            ;[133a] d1
                    pop       bc                            ;[133b] c1
                    ret                                     ;[133c] c9

                    push      bc                            ;[133d] c5
                    push      de                            ;[133e] d5
                    ld        hl,$0020                      ;[133f] 21 20 00
                    add       hl,de                         ;[1342] 19
                    ld        a,(hl)                        ;[1343] 7e
                    cpl                                     ;[1344] 2f
                    and       $11                           ;[1345] e6 11
                    jr        nz,$1355                      ;[1347] 20 0c
                    push      de                            ;[1349] d5
                    push      hl                            ;[134a] e5
                    inc       hl                            ;[134b] 23
                    ld        d,(hl)                        ;[134c] 56
                    inc       hl                            ;[134d] 23
                    ld        e,(hl)                        ;[134e] 5e
                    ld        ($fc9a),de                    ;[134f] ed 53 9a fc
                    pop       hl                            ;[1353] e1
                    pop       de                            ;[1354] d1
                    bit       3,(hl)                        ;[1355] cb 5e
                    ld        hl,$f9db                      ;[1357] 21 db f9
                    push      hl                            ;[135a] e5
                    jr        z,$1373                       ;[135b] 28 16
                    push      hl                            ;[135d] e5
                    call      $1432                         ;[135e] cd 32 14
                    ld        hl,($fc9a)                    ;[1361] 2a 9a fc
                    call      $1425                         ;[1364] cd 25 14
                    ld        ($fc9a),hl                    ;[1367] 22 9a fc
                    pop       hl                            ;[136a] e1
                    inc       hl                            ;[136b] 23
                    inc       hl                            ;[136c] 23
                    inc       hl                            ;[136d] 23
                    ld        a,$00                         ;[136e] 3e 00
                    scf                                     ;[1370] 37
                    jr        $1330                         ;[1371] 18 bd
                    ld        a,(hl)                        ;[1373] 7e
                    cp        $14                           ;[1374] fe 14
                    jr        z,$1386                       ;[1376] 28 0e
                    inc       a                             ;[1378] 3c
                    ld        hl,($f9dc)                    ;[1379] 2a dc f9
                    ld        bc,$0023                      ;[137c] 01 23 00
                    ex        de,hl                         ;[137f] eb
                    ldir                                    ;[1380] ed b0
                    ex        de,hl                         ;[1382] eb
                    scf                                     ;[1383] 37
                    jr        $1330                         ;[1384] 18 aa
                    pop       hl                            ;[1386] e1
                    pop       de                            ;[1387] d1
                    pop       bc                            ;[1388] c1
                    ret                                     ;[1389] c9

                    ld        hl,$f9de                      ;[138a] 21 de f9
                    jp        $118a                         ;[138d] c3 8a 11
                    ex        af,af'                        ;[1390] 08
                    dec       c                             ;[1391] 0d
                    rl        (hl)                          ;[1392] cb 16
                    ld        bc,$16d9                      ;[1394] 01 d9 16
                    ld        (de),a                        ;[1397] 12
                    dec       l                             ;[1398] 2d
                    inc       d                             ;[1399] 14
                    inc       de                            ;[139a] 13
                    dec       l                             ;[139b] 2d
                    inc       d                             ;[139c] 14
                    inc       d                             ;[139d] 14
                    dec       l                             ;[139e] 2d
                    inc       d                             ;[139f] 14
                    dec       d                             ;[13a0] 15
                    dec       l                             ;[13a1] 2d
                    inc       d                             ;[13a2] 14
                    djnz      $13d2                         ;[13a3] 10 2d
                    inc       d                             ;[13a5] 14
                    ld        de,$142d                      ;[13a6] 11 2d 14
                    ld        d,h                           ;[13a9] 54
                    ld        e,l                           ;[13aa] 5d
                    inc       de                            ;[13ab] 13
                    inc       de                            ;[13ac] 13
                    inc       de                            ;[13ad] 13
                    push      de                            ;[13ae] d5
                    ld        hl,$0020                      ;[13af] 21 20 00
                    add       hl,de                         ;[13b2] 19
                    ld        (hl),$01                      ;[13b3] 36 01
                    inc       hl                            ;[13b5] 23
                    ld        (hl),b                        ;[13b6] 70
                    inc       hl                            ;[13b7] 23
                    ld        (hl),c                        ;[13b8] 71
                    ld        c,$01                         ;[13b9] 0e 01
                    ld        b,$00                         ;[13bb] 06 00
                    push      bc                            ;[13bd] c5
                    push      de                            ;[13be] d5
                    ld        a,($ec0e)                     ;[13bf] 3a 0e ec
                    cp        $04                           ;[13c2] fe 04
                    call      nz,$1616                      ;[13c4] c4 16 16
                    pop       de                            ;[13c7] d1
                    pop       bc                            ;[13c8] c1
                    jr        c,$13da                       ;[13c9] 38 0f
                    ld        a,c                           ;[13cb] 79
                    cp        $01                           ;[13cc] fe 01
                    ld        a,$0d                         ;[13ce] 3e 0d
                    jr        nz,$13da                      ;[13d0] 20 08
                    ld        a,b                           ;[13d2] 78
                    or        a                             ;[13d3] b7
                    ld        a,$01                         ;[13d4] 3e 01
                    jr        z,$13da                       ;[13d6] 28 02
                    ld        a,$0d                         ;[13d8] 3e 0d
                    ld        hl,$1390                      ;[13da] 21 90 13
                    call      $216b                         ;[13dd] cd 6b 21
                    jr        c,$13ff                       ;[13e0] 38 1d
                    jr        z,$13bd                       ;[13e2] 28 d9
                    push      af                            ;[13e4] f5
                    ld        a,$1f                         ;[13e5] 3e 1f
                    cp        b                             ;[13e7] b8
                    jr        nc,$13f9                      ;[13e8] 30 0f
                    ld        a,$12                         ;[13ea] 3e 12
                    call      $1404                         ;[13ec] cd 04 14
                    jr        c,$13f6                       ;[13ef] 38 05
                    pop       af                            ;[13f1] f1
                    ld        a,$0d                         ;[13f2] 3e 0d
                    jr        $13da                         ;[13f4] 18 e4
                    call      $16f3                         ;[13f6] cd f3 16
                    pop       af                            ;[13f9] f1
                    call      $16c4                         ;[13fa] cd c4 16
                    jr        $13bd                         ;[13fd] 18 be
                    pop       hl                            ;[13ff] e1
                    ld        a,c                           ;[1400] 79
                    ret       z                             ;[1401] c8
                    scf                                     ;[1402] 37
                    ret                                     ;[1403] c9

                    push      af                            ;[1404] f5
                    call      $16e5                         ;[1405] cd e5 16
                    pop       af                            ;[1408] f1
                    xor       (hl)                          ;[1409] ae
                    ld        (hl),a                        ;[140a] 77
                    ld        a,c                           ;[140b] 79
                    cp        $14                           ;[140c] fe 14
                    ret       nc                            ;[140e] d0
                    inc       c                             ;[140f] 0c
                    ld        hl,$0023                      ;[1410] 21 23 00
                    add       hl,de                         ;[1413] 19
                    ex        de,hl                         ;[1414] eb
                    ld        hl,$0020                      ;[1415] 21 20 00
                    add       hl,de                         ;[1418] 19
                    ld        (hl),$00                      ;[1419] 36 00
                    scf                                     ;[141b] 37
                    ret                                     ;[141c] c9

                    call      $15b5                         ;[141d] cd b5 15
                    ret       c                             ;[1420] d8
                    ld        hl,$0000                      ;[1421] 21 00 00
                    ret                                     ;[1424] c9

                    call      $152f                         ;[1425] cd 2f 15
                    ret       c                             ;[1428] d8
                    ld        hl,$0000                      ;[1429] 21 00 00
                    ret                                     ;[142c] c9

                    call      $1616                         ;[142d] cd 16 16
                    ccf                                     ;[1430] 3f
                    ret       nc                            ;[1431] d0
                    ld        hl,$0000                      ;[1432] 21 00 00
                    ld        ($fc9f),hl                    ;[1435] 22 9f fc
                    ld        ($fca1),hl                    ;[1438] 22 a1 fc
                    ld        hl,$1447                      ;[143b] 21 47 14
                    ld        de,$fcae                      ;[143e] 11 ae fc
                    ld        bc,$00e8                      ;[1441] 01 e8 00
                    ldir                                    ;[1444] ed b0
                    ret                                     ;[1446] c9

                    di                                      ;[1447] f3
                    push      af                            ;[1448] f5
                    ld        bc,$7ffd                      ;[1449] 01 fd 7f
                    ld        a,$17                         ;[144c] 3e 17
                    out       (c),a                         ;[144e] ed 79
                    ld        bc,$1ffd                      ;[1450] 01 fd 1f
                    ld        a,($5b67)                     ;[1453] 3a 67 5b
                    set       2,a                           ;[1456] cb d7
                    out       (c),a                         ;[1458] ed 79
                    pop       af                            ;[145a] f1
                    cp        $50                           ;[145b] fe 50
                    jr        nc,$1490                      ;[145d] 30 31
                    cp        $40                           ;[145f] fe 40
                    jr        nc,$1489                      ;[1461] 30 26
                    cp        $30                           ;[1463] fe 30
                    jr        nc,$1482                      ;[1465] 30 1b
                    cp        $20                           ;[1467] fe 20
                    jr        nc,$147b                      ;[1469] 30 10
                    cp        $10                           ;[146b] fe 10
                    jr        nc,$1474                      ;[146d] 30 05
                    ld        hl,$0096                      ;[146f] 21 96 00
                    jr        $1495                         ;[1472] 18 21
                    sub       $10                           ;[1474] d6 10
                    ld        hl,$00cf                      ;[1476] 21 cf 00
                    jr        $1495                         ;[1479] 18 1a
                    sub       $20                           ;[147b] d6 20
                    ld        hl,$0100                      ;[147d] 21 00 01
                    jr        $1495                         ;[1480] 18 13
                    sub       $30                           ;[1482] d6 30
                    ld        hl,$013e                      ;[1484] 21 3e 01
                    jr        $1495                         ;[1487] 18 0c
                    sub       $40                           ;[1489] d6 40
                    ld        hl,$018b                      ;[148b] 21 8b 01
                    jr        $1495                         ;[148e] 18 05
                    sub       $50                           ;[1490] d6 50
                    ld        hl,$01d4                      ;[1492] 21 d4 01
                    ld        b,a                           ;[1495] 47
                    or        a                             ;[1496] b7
                    jr        z,$14a2                       ;[1497] 28 09
                    ld        a,(hl)                        ;[1499] 7e
                    inc       hl                            ;[149a] 23
                    and       $80                           ;[149b] e6 80
                    jr        z,$1499                       ;[149d] 28 fa
                    dec       b                             ;[149f] 05
                    jr        $1497                         ;[14a0] 18 f5
                    ld        de,$fca3                      ;[14a2] 11 a3 fc
                    ld        ($fca1),de                    ;[14a5] ed 53 a1 fc
                    ld        a,($fc9e)                     ;[14a9] 3a 9e fc
                    or        a                             ;[14ac] b7
                    ld        a,$00                         ;[14ad] 3e 00
                    ld        ($fc9e),a                     ;[14af] 32 9e fc
                    jr        nz,$14b8                      ;[14b2] 20 04
                    ld        a,$20                         ;[14b4] 3e 20
                    ld        (de),a                        ;[14b6] 12
                    inc       de                            ;[14b7] 13
                    ld        a,(hl)                        ;[14b8] 7e
                    ld        b,a                           ;[14b9] 47
                    inc       hl                            ;[14ba] 23
                    ld        (de),a                        ;[14bb] 12
                    inc       de                            ;[14bc] 13
                    and       $80                           ;[14bd] e6 80
                    jr        z,$14b8                       ;[14bf] 28 f7
                    ld        a,b                           ;[14c1] 78
                    and       $7f                           ;[14c2] e6 7f
                    dec       de                            ;[14c4] 1b
                    ld        (de),a                        ;[14c5] 12
                    inc       de                            ;[14c6] 13
                    ld        a,$a0                         ;[14c7] 3e a0
                    ld        (de),a                        ;[14c9] 12
                    ld        bc,$7ffd                      ;[14ca] 01 fd 7f
                    ld        a,($5b5c)                     ;[14cd] 3a 5c 5b
                    out       (c),a                         ;[14d0] ed 79
                    ld        bc,$1ffd                      ;[14d2] 01 fd 1f
                    ld        a,($5b67)                     ;[14d5] 3a 67 5b
                    out       (c),a                         ;[14d8] ed 79
                    ei                                      ;[14da] fb
                    ret                                     ;[14db] c9

                    di                                      ;[14dc] f3
                    push      af                            ;[14dd] f5
                    ld        bc,$7ffd                      ;[14de] 01 fd 7f
                    ld        a,$17                         ;[14e1] 3e 17
                    out       (c),a                         ;[14e3] ed 79
                    ld        bc,$1ffd                      ;[14e5] 01 fd 1f
                    ld        a,($5b67)                     ;[14e8] 3a 67 5b
                    set       2,a                           ;[14eb] cb d7
                    out       (c),a                         ;[14ed] ed 79
                    pop       af                            ;[14ef] f1
                    ld        hl,$0096                      ;[14f0] 21 96 00
                    ld        b,$a5                         ;[14f3] 06 a5
                    ld        de,$fda0                      ;[14f5] 11 a0 fd
                    ld        a,(de)                        ;[14f8] 1a
                    and       $7f                           ;[14f9] e6 7f
                    cp        $61                           ;[14fb] fe 61
                    ld        a,(de)                        ;[14fd] 1a
                    jr        c,$1502                       ;[14fe] 38 02
                    and       $df                           ;[1500] e6 df
                    cp        (hl)                          ;[1502] be
                    jr        nz,$150e                      ;[1503] 20 09
                    inc       hl                            ;[1505] 23
                    inc       de                            ;[1506] 13
                    and       $80                           ;[1507] e6 80
                    jr        z,$14f8                       ;[1509] 28 ed
                    scf                                     ;[150b] 37
                    jr        $151a                         ;[150c] 18 0c
                    inc       b                             ;[150e] 04
                    jr        z,$1519                       ;[150f] 28 08
                    ld        a,(hl)                        ;[1511] 7e
                    and       $80                           ;[1512] e6 80
                    inc       hl                            ;[1514] 23
                    jr        z,$1511                       ;[1515] 28 fa
                    jr        $14f5                         ;[1517] 18 dc
                    or        a                             ;[1519] b7
                    ld        a,b                           ;[151a] 78
                    ld        d,a                           ;[151b] 57
                    ld        bc,$7ffd                      ;[151c] 01 fd 7f
                    ld        a,($5b5c)                     ;[151f] 3a 5c 5b
                    out       (c),a                         ;[1522] ed 79
                    ld        bc,$1ffd                      ;[1524] 01 fd 1f
                    ld        a,($5b67)                     ;[1527] 3a 67 5b
                    out       (c),a                         ;[152a] ed 79
                    ld        a,d                           ;[152c] 7a
                    ei                                      ;[152d] fb
                    ret                                     ;[152e] c9

                    call      $15e9                         ;[152f] cd e9 15
                    or        a                             ;[1532] b7
                    ld        ($fc9e),a                     ;[1533] 32 9e fc
                    call      $05ac                         ;[1536] cd ac 05
                    call      $15f5                         ;[1539] cd f5 15
                    jr        nc,$1590                      ;[153c] 30 52
                    jr        nz,$154c                      ;[153e] 20 0c
                    ld        a,b                           ;[1540] 78
                    or        c                             ;[1541] b1
                    jr        z,$154c                       ;[1542] 28 08
                    call      $15ce                         ;[1544] cd ce 15
                    call      $15d8                         ;[1547] cd d8 15
                    jr        nc,$1590                      ;[154a] 30 44
                    ld        d,(hl)                        ;[154c] 56
                    inc       hl                            ;[154d] 23
                    ld        e,(hl)                        ;[154e] 5e
                    call      $05d1                         ;[154f] cd d1 05
                    push      de                            ;[1552] d5
                    push      hl                            ;[1553] e5
                    push      ix                            ;[1554] dd e5
                    ld        ix,$fca3                      ;[1556] dd 21 a3 fc
                    ld        ($fca1),ix                    ;[155a] dd 22 a1 fc
                    ex        de,hl                         ;[155e] eb
                    ld        b,$00                         ;[155f] 06 00
                    ld        de,$fc18                      ;[1561] 11 18 fc
                    call      $1594                         ;[1564] cd 94 15
                    ld        de,$ff9c                      ;[1567] 11 9c ff
                    call      $1594                         ;[156a] cd 94 15
                    ld        de,$fff6                      ;[156d] 11 f6 ff
                    call      $1594                         ;[1570] cd 94 15
                    ld        de,$ffff                      ;[1573] 11 ff ff
                    call      $1594                         ;[1576] cd 94 15
                    dec       ix                            ;[1579] dd 2b
                    ld        a,(ix+$00)                    ;[157b] dd 7e 00
                    or        $80                           ;[157e] f6 80
                    ld        (ix+$00),a                    ;[1580] dd 77 00
                    pop       ix                            ;[1583] dd e1
                    pop       hl                            ;[1585] e1
                    pop       de                            ;[1586] d1
                    inc       hl                            ;[1587] 23
                    inc       hl                            ;[1588] 23
                    inc       hl                            ;[1589] 23
                    ld        ($fc9f),hl                    ;[158a] 22 9f fc
                    ex        de,hl                         ;[158d] eb
                    scf                                     ;[158e] 37
                    ret                                     ;[158f] c9

                    call      $05d1                         ;[1590] cd d1 05
                    ret                                     ;[1593] c9

                    xor       a                             ;[1594] af
                    add       hl,de                         ;[1595] 19
                    inc       a                             ;[1596] 3c
                    jr        c,$1595                       ;[1597] 38 fc
                    sbc       hl,de                         ;[1599] ed 52
                    dec       a                             ;[159b] 3d
                    add       $30                           ;[159c] c6 30
                    ld        (ix+$00),a                    ;[159e] dd 77 00
                    cp        $30                           ;[15a1] fe 30
                    jr        nz,$15b0                      ;[15a3] 20 0b
                    ld        a,b                           ;[15a5] 78
                    or        a                             ;[15a6] b7
                    jr        nz,$15b2                      ;[15a7] 20 09
                    ld        a,$00                         ;[15a9] 3e 00
                    ld        (ix+$00),a                    ;[15ab] dd 77 00
                    jr        $15b2                         ;[15ae] 18 02
                    ld        b,$01                         ;[15b0] 06 01
                    inc       ix                            ;[15b2] dd 23
                    ret                                     ;[15b4] c9

                    call      $15e9                         ;[15b5] cd e9 15
                    or        a                             ;[15b8] b7
                    ld        ($fc9e),a                     ;[15b9] 32 9e fc
                    call      $05ac                         ;[15bc] cd ac 05
                    call      $15f5                         ;[15bf] cd f5 15
                    jr        nc,$1590                      ;[15c2] 30 cc
                    ex        de,hl                         ;[15c4] eb
                    ld        a,l                           ;[15c5] 7d
                    or        h                             ;[15c6] b4
                    scf                                     ;[15c7] 37
                    jp        nz,$154c                      ;[15c8] c2 4c 15
                    ccf                                     ;[15cb] 3f
                    jr        $1590                         ;[15cc] 18 c2
                    push      hl                            ;[15ce] e5
                    inc       hl                            ;[15cf] 23
                    inc       hl                            ;[15d0] 23
                    ld        e,(hl)                        ;[15d1] 5e
                    inc       hl                            ;[15d2] 23
                    ld        d,(hl)                        ;[15d3] 56
                    inc       hl                            ;[15d4] 23
                    add       hl,de                         ;[15d5] 19
                    pop       de                            ;[15d6] d1
                    ret                                     ;[15d7] c9

                    ld        a,(hl)                        ;[15d8] 7e
                    and       $c0                           ;[15d9] e6 c0
                    scf                                     ;[15db] 37
                    ret       z                             ;[15dc] c8
                    ccf                                     ;[15dd] 3f
                    ret                                     ;[15de] c9

                    ld        a,b                           ;[15df] 78
                    cp        (hl)                          ;[15e0] be
                    ret       nz                            ;[15e1] c0
                    ld        a,c                           ;[15e2] 79
                    inc       hl                            ;[15e3] 23
                    cp        (hl)                          ;[15e4] be
                    dec       hl                            ;[15e5] 2b
                    ret       nz                            ;[15e6] c0
                    scf                                     ;[15e7] 37
                    ret                                     ;[15e8] c9

                    push      hl                            ;[15e9] e5
                    ld        hl,$0000                      ;[15ea] 21 00 00
                    ld        ($fca1),hl                    ;[15ed] 22 a1 fc
                    ld        ($fc9f),hl                    ;[15f0] 22 9f fc
                    pop       hl                            ;[15f3] e1
                    ret                                     ;[15f4] c9

                    push      hl                            ;[15f5] e5
                    pop       bc                            ;[15f6] c1
                    ld        de,$0000                      ;[15f7] 11 00 00
                    ld        hl,($5c53)                    ;[15fa] 2a 53 5c
                    call      $15d8                         ;[15fd] cd d8 15
                    ret       nc                            ;[1600] d0
                    call      $15df                         ;[1601] cd df 15
                    ret       c                             ;[1604] d8
                    ld        a,b                           ;[1605] 78
                    or        c                             ;[1606] b1
                    scf                                     ;[1607] 37
                    ret       z                             ;[1608] c8
                    call      $15ce                         ;[1609] cd ce 15
                    call      $15d8                         ;[160c] cd d8 15
                    ret       nc                            ;[160f] d0
                    call      $15df                         ;[1610] cd df 15
                    jr        nc,$1609                      ;[1613] 30 f4
                    ret                                     ;[1615] c9

                    ld        hl,($fca1)                    ;[1616] 2a a1 fc
                    ld        a,l                           ;[1619] 7d
                    or        h                             ;[161a] b4
                    jr        z,$163b                       ;[161b] 28 1e
                    ld        a,(hl)                        ;[161d] 7e
                    inc       hl                            ;[161e] 23
                    cp        $a0                           ;[161f] fe a0
                    ld        b,a                           ;[1621] 47
                    ld        a,$00                         ;[1622] 3e 00
                    jr        nz,$1628                      ;[1624] 20 02
                    ld        a,$ff                         ;[1626] 3e ff
                    ld        ($fc9e),a                     ;[1628] 32 9e fc
                    ld        a,b                           ;[162b] 78
                    bit       7,a                           ;[162c] cb 7f
                    jr        z,$1633                       ;[162e] 28 03
                    ld        hl,$0000                      ;[1630] 21 00 00
                    ld        ($fca1),hl                    ;[1633] 22 a1 fc
                    and       $7f                           ;[1636] e6 7f
                    jp        $168e                         ;[1638] c3 8e 16
                    ld        hl,($fc9f)                    ;[163b] 2a 9f fc
                    ld        a,l                           ;[163e] 7d
                    or        h                             ;[163f] b4
                    jp        z,$1690                       ;[1640] ca 90 16
                    call      $05ac                         ;[1643] cd ac 05
                    ld        a,(hl)                        ;[1646] 7e
                    cp        $0e                           ;[1647] fe 0e
                    jr        nz,$1653                      ;[1649] 20 08
                    inc       hl                            ;[164b] 23
                    inc       hl                            ;[164c] 23
                    inc       hl                            ;[164d] 23
                    inc       hl                            ;[164e] 23
                    inc       hl                            ;[164f] 23
                    inc       hl                            ;[1650] 23
                    jr        $1646                         ;[1651] 18 f3
                    call      $05d1                         ;[1653] cd d1 05
                    inc       hl                            ;[1656] 23
                    ld        ($fc9f),hl                    ;[1657] 22 9f fc
                    cp        $a5                           ;[165a] fe a5
                    jr        c,$1666                       ;[165c] 38 08
                    sub       $a5                           ;[165e] d6 a5
                    call      $fcae                         ;[1660] cd ae fc
                    jp        $1616                         ;[1663] c3 16 16
                    cp        $a3                           ;[1666] fe a3
                    jr        c,$167a                       ;[1668] 38 10
                    jr        nz,$1671                      ;[166a] 20 05
                    ld        hl,$1693                      ;[166c] 21 93 16
                    jr        $1674                         ;[166f] 18 03
                    ld        hl,$169b                      ;[1671] 21 9b 16
                    call      $fd09                         ;[1674] cd 09 fd
                    jp        $1616                         ;[1677] c3 16 16
                    push      af                            ;[167a] f5
                    ld        a,$00                         ;[167b] 3e 00
                    ld        ($fc9e),a                     ;[167d] 32 9e fc
                    pop       af                            ;[1680] f1
                    cp        $0d                           ;[1681] fe 0d
                    jr        nz,$168e                      ;[1683] 20 09
                    ld        hl,$0000                      ;[1685] 21 00 00
                    ld        ($fca1),hl                    ;[1688] 22 a1 fc
                    ld        ($fc9f),hl                    ;[168b] 22 9f fc
                    scf                                     ;[168e] 37
                    ret                                     ;[168f] c9

                    scf                                     ;[1690] 37
                    ccf                                     ;[1691] 3f
                    ret                                     ;[1692] c9

                    ld        d,e                           ;[1693] 53
                    ld        d,b                           ;[1694] 50
                    ld        b,l                           ;[1695] 45
                    ld        b,e                           ;[1696] 43
                    ld        d,h                           ;[1697] 54
                    ld        d,d                           ;[1698] 52
                    ld        d,l                           ;[1699] 55
                    call      $4c50                         ;[169a] cd 50 4c
                    ld        b,c                           ;[169d] 41
                    exx                                     ;[169e] d9
                    ld        b,a                           ;[169f] 47
                    ld        c,a                           ;[16a0] 4f
                    ld        d,h                           ;[16a1] 54
                    rst       $08                           ;[16a2] cf
                    ld        b,a                           ;[16a3] 47
                    ld        c,a                           ;[16a4] 4f
                    ld        d,e                           ;[16a5] 53
                    ld        d,l                           ;[16a6] 55
                    jp        nz,$4544                      ;[16a7] c2 44 45
                    ld        b,(hl)                        ;[16aa] 46
                    ld        b,(hl)                        ;[16ab] 46
                    adc       $4f                           ;[16ac] ce 4f
                    ld        d,b                           ;[16ae] 50
                    ld        b,l                           ;[16af] 45
                    ld        c,(hl)                        ;[16b0] 4e
                    and       e                             ;[16b1] a3
                    ld        b,e                           ;[16b2] 43
                    ld        c,h                           ;[16b3] 4c
                    ld        c,a                           ;[16b4] 4f
                    ld        d,e                           ;[16b5] 53
                    ld        b,l                           ;[16b6] 45
                    and       e                             ;[16b7] a3
                    ld        (bc),a                        ;[16b8] 02
                    ld        bc,$2105                      ;[16b9] 01 05 21
                    cp        b                             ;[16bc] b8
                    ld        d,$11                         ;[16bd] 16 11
                    sub       (hl)                          ;[16bf] 96
                    jp        $2157                         ;[16c0] fd c3 57 21
                    ld        l,b                           ;[16c4] 68
                    ld        h,$00                         ;[16c5] 26 00
                    add       hl,de                         ;[16c7] 19
                    ld        (hl),a                        ;[16c8] 77
                    inc       b                             ;[16c9] 04
                    ret                                     ;[16ca] c9

                    call      $16e5                         ;[16cb] cd e5 16
                    ld        a,(hl)                        ;[16ce] 7e
                    or        $18                           ;[16cf] f6 18
                    ld        (hl),a                        ;[16d1] 77
                    ld        hl,$fd96                      ;[16d2] 21 96 fd
                    set       0,(hl)                        ;[16d5] cb c6
                    scf                                     ;[16d7] 37
                    ret                                     ;[16d8] c9

                    call      $16e5                         ;[16d9] cd e5 16
                    set       3,(hl)                        ;[16dc] cb de
                    ld        hl,$fd96                      ;[16de] 21 96 fd
                    set       0,(hl)                        ;[16e1] cb c6
                    scf                                     ;[16e3] 37
                    ret                                     ;[16e4] c9

                    ld        l,b                           ;[16e5] 68
                    ld        h,$00                         ;[16e6] 26 00
                    add       hl,de                         ;[16e8] 19
                    ld        a,$20                         ;[16e9] 3e 20
                    cp        b                             ;[16eb] b8
                    ret       z                             ;[16ec] c8
                    ld        (hl),$00                      ;[16ed] 36 00
                    inc       hl                            ;[16ef] 23
                    inc       b                             ;[16f0] 04
                    jr        $16eb                         ;[16f1] 18 f8
                    ld        a,($fd97)                     ;[16f3] 3a 97 fd
                    ld        b,$00                         ;[16f6] 06 00
                    ld        h,$00                         ;[16f8] 26 00
                    ld        l,b                           ;[16fa] 68
                    add       hl,de                         ;[16fb] 19
                    ld        (hl),$00                      ;[16fc] 36 00
                    inc       b                             ;[16fe] 04
                    dec       a                             ;[16ff] 3d
                    jr        nz,$16f8                      ;[1700] 20 f6
                    ret                                     ;[1702] c9

                    push      bc                            ;[1703] c5
                    push      de                            ;[1704] d5
                    push      hl                            ;[1705] e5
                    push      hl                            ;[1706] e5
                    ld        hl,$eef5                      ;[1707] 21 f5 ee
                    bit       2,(hl)                        ;[170a] cb 56
                    pop       hl                            ;[170c] e1
                    jr        nz,$1713                      ;[170d] 20 04
                    ld        b,c                           ;[170f] 41
                    call      $1d30                         ;[1710] cd 30 1d
                    pop       hl                            ;[1713] e1
                    pop       de                            ;[1714] d1
                    pop       bc                            ;[1715] c1
                    ret                                     ;[1716] c9

                    push      bc                            ;[1717] c5
                    push      de                            ;[1718] d5
                    push      hl                            ;[1719] e5
                    push      hl                            ;[171a] e5
                    ld        hl,$eef5                      ;[171b] 21 f5 ee
                    bit       2,(hl)                        ;[171e] cb 56
                    pop       hl                            ;[1720] e1
                    jr        nz,$1727                      ;[1721] 20 04
                    ld        e,c                           ;[1723] 59
                    call      $1cd1                         ;[1724] cd d1 1c
                    pop       hl                            ;[1727] e1
                    pop       de                            ;[1728] d1
                    pop       bc                            ;[1729] c1
                    ret                                     ;[172a] c9

                    push      bc                            ;[172b] c5
                    push      de                            ;[172c] d5
                    push      hl                            ;[172d] e5
                    push      hl                            ;[172e] e5
                    ld        hl,$eef5                      ;[172f] 21 f5 ee
                    bit       2,(hl)                        ;[1732] cb 56
                    pop       hl                            ;[1734] e1
                    jr        nz,$173b                      ;[1735] 20 04
                    ld        e,c                           ;[1737] 59
                    call      $1cd8                         ;[1738] cd d8 1c
                    pop       hl                            ;[173b] e1
                    pop       de                            ;[173c] d1
                    pop       bc                            ;[173d] c1
                    ret                                     ;[173e] c9

                    push      af                            ;[173f] f5
                    push      bc                            ;[1740] c5
                    push      de                            ;[1741] d5
                    push      hl                            ;[1742] e5
                    ld        a,b                           ;[1743] 78
                    ld        b,c                           ;[1744] 41
                    ld        c,a                           ;[1745] 4f
                    call      $1caf                         ;[1746] cd af 1c
                    pop       hl                            ;[1749] e1
                    pop       de                            ;[174a] d1
                    pop       bc                            ;[174b] c1
                    pop       af                            ;[174c] f1
                    ret                                     ;[174d] c9

                    push      af                            ;[174e] f5
                    push      bc                            ;[174f] c5
                    push      de                            ;[1750] d5
                    push      hl                            ;[1751] e5
                    ld        a,b                           ;[1752] 78
                    ld        b,c                           ;[1753] 41
                    ld        c,a                           ;[1754] 4f
                    call      $1cc4                         ;[1755] cd c4 1c
                    pop       hl                            ;[1758] e1
                    pop       de                            ;[1759] d1
                    pop       bc                            ;[175a] c1
                    pop       af                            ;[175b] f1
                    ret                                     ;[175c] c9

                    ld        a,$00                         ;[175d] 3e 00
                    ld        ($5c41),a                     ;[175f] 32 41 5c
                    ld        a,$02                         ;[1762] 3e 02
                    ld        ($5c0a),a                     ;[1764] 32 0a 5c
                    call      $185f                         ;[1767] cd 5f 18
                    ret                                     ;[176a] c9

                    ld        (hl),h                        ;[176b] 74
                    rst       $18                           ;[176c] df
                    sbc       $55                           ;[176d] de 55
                    djnz      $17c3                         ;[176f] 10 52
                    ret                                     ;[1771] c9

                    sbc       (hl)                          ;[1772] 9e
                    sbc       (hl)                          ;[1773] 9e
                    cp        l                             ;[1774] bd
                    ld        h,d                           ;[1775] 62
                    push      bc                            ;[1776] c5
                    ret       nz                            ;[1777] c0
                    ld        d,l                           ;[1778] 55
                    jp        nz,$1044                      ;[1779] c2 44 10
                    rla                                     ;[177c] 17
                    cp        $5f                           ;[177d] fe 5f
                    sub       b                             ;[177f] 90
                    cp        $d1                           ;[1780] fe d1
                    push      de                            ;[1782] dd d5
                    rla                                     ;[1784] 17
                    sub       b                             ;[1785] 90
                    rst       $30                           ;[1786] f7
                    rst       $18                           ;[1787] df
                    rst       $18                           ;[1788] df
                    call      nc,$d9c7                      ;[1789] d4 c7 d9
                    sbc       $c3                           ;[178c] de c3
                    cp        l                             ;[178e] bd
                    di                                      ;[178f] f3
                    call      c,$d659                       ;[1790] dc 59 d6
                    ld        d,(hl)                        ;[1793] 56
                    djnz      $17ad                         ;[1794] 10 17
                    ld        h,h                           ;[1796] 64
                    ld        b,a                           ;[1797] 47
                    ld        e,a                           ;[1798] 5f
                    sub       b                             ;[1799] 90
                    ld        h,b                           ;[179a] 60
                    rst       $18                           ;[179b] df
                    ld        b,b                           ;[179c] 40
                    ld        b,e                           ;[179d] 43
                    sub       a                             ;[179e] 97
                    djnz      $179d                         ;[179f] 10 fc
                    ld        d,c                           ;[17a1] 51
                    rst       $00                           ;[17a2] c7
                    ld        b,e                           ;[17a3] 43
                    ld        e,a                           ;[17a4] 5f
                    sbc       $bd                           ;[17a5] de bd
                    ld        h,(hl)                        ;[17a7] 66
                    exx                                     ;[17a8] d9
                    ld        e,e                           ;[17a9] 5b
                    sub       b                             ;[17aa] 90
                    rla                                     ;[17ab] 17
                    jp        po,$54d5                      ;[17ac] e2 d5 54
                    djnz      $17a9                         ;[17af] 10 f8
                    ld        d,l                           ;[17b1] 55
                    ld        b,d                           ;[17b2] 42
                    ld        b,d                           ;[17b3] 42
                    ld        e,c                           ;[17b4] 59
                    sbc       $57                           ;[17b5] de 57
                    rla                                     ;[17b7] 17
                    sub       b                             ;[17b8] 90
                    ld        a,a                           ;[17b9] 7f
                    ld        e,h                           ;[17ba] 5c
                    ld        e,h                           ;[17bb] 5c
                    exx                                     ;[17bc] d9
                    add       $55                           ;[17bd] c6 55
                    jp        nz,$5190                      ;[17bf] c2 90 51
                    sbc       $bd                           ;[17c2] de bd
                    call      po,$d5d8                      ;[17c4] e4 d8 d5
                    sub       b                             ;[17c7] 90
                    ld        (hl),h                        ;[17c8] 74
                    ld        e,a                           ;[17c9] 5f
                    jp        nz,$595b                      ;[17ca] c2 5b 59
                    ld        e,(hl)                        ;[17cd] 5e
                    rst       $10                           ;[17ce] d7
                    sub       b                             ;[17cf] 90
                    ld        a,l                           ;[17d0] 7d
                    ld        e,a                           ;[17d1] 5f
                    jp        nc,$64bd                      ;[17d2] d2 bd 64
                    ld        e,b                           ;[17d5] 58
                    pop       de                            ;[17d6] d1
                    ld        e,(hl)                        ;[17d7] 5e
                    in        a,($c3)                       ;[17d8] db c3
                    djnz      $17a0                         ;[17da] 10 c4
                    ld        e,a                           ;[17dc] 5f
                    ld        e,$1e                         ;[17dd] 1e 1e
                    cp        l                             ;[17df] bd
                    ld        h,h                           ;[17e0] 64
                    ret       c                             ;[17e1] d8
                    push      de                            ;[17e2] d5
                    djnz      $17d7                         ;[17e3] 10 f2
                    ld        b,d                           ;[17e5] 42
                    push      de                            ;[17e6] d5
                    ld        b,a                           ;[17e7] 47
                    push      de                            ;[17e8] d5
                    ld        b,d                           ;[17e9] 42
                    ret                                     ;[17ea] c9

                    sub       b                             ;[17eb] 90
                    call      po,$c0d1                      ;[17ec] e4 d1 c0
                    sub       b                             ;[17ef] 90
                    jr        $184a                         ;[17f0] 18 58
                    exx                                     ;[17f2] d9
                    out       ($19),a                       ;[17f3] d3 19
                    cp        l                             ;[17f5] bd
                    jp        po,$dc5f                      ;[17f6] e2 5f dc
                    ld        d,c                           ;[17f9] 51
                    sbc       $54                           ;[17fa] de 54
                    inc       e                             ;[17fc] 1c
                    sub       b                             ;[17fd] 90
                    ld        h,d                           ;[17fe] 62
                    exx                                     ;[17ff] d9
                    out       ($58),a                       ;[1800] d3 58
                    ld        d,c                           ;[1802] 51
                    jp        nz,$9054                      ;[1803] c2 54 90
                    ld        d,l                           ;[1806] 55
                    ld        b,h                           ;[1807] 44
                    sub       b                             ;[1808] 90
                    pop       de                            ;[1809] d1
                    ld        e,h                           ;[180a] 5c
                    cp        l                             ;[180b] bd
                    ld        d,c                           ;[180c] 51
                    ld        e,(hl)                        ;[180d] 5e
                    ld        d,h                           ;[180e] 54
                    sub       b                             ;[180f] 90
                    ld        sp,hl                         ;[1810] f9
                    add       $df                           ;[1811] c6 df
                    ld        b,d                           ;[1813] 42
                    djnz      $17ae                         ;[1814] 10 98
                    rst       $00                           ;[1816] c7
                    ret       c                             ;[1817] d8
                    ld        c,c                           ;[1818] 49
                    djnz      $17f9                         ;[1819] 10 de
                    ld        e,a                           ;[181b] 5f
                    call      nz,$bd19                      ;[181c] c4 19 bd
                    ld        h,a                           ;[181f] 67
                    ld        b,d                           ;[1820] 42
                    exx                                     ;[1821] d9
                    ld        b,h                           ;[1822] 44
                    call      nz,$5ed5                      ;[1823] c4 d5 5e
                    sub       b                             ;[1826] 90
                    rst       $18                           ;[1827] df
                    ld        e,(hl)                        ;[1828] 5e
                    sub       b                             ;[1829] 90
                    ld        h,b                           ;[182a] 60
                    di                                      ;[182b] f3
                    ld        h,a                           ;[182c] 67
                    sub       b                             ;[182d] 90
                    adc       b                             ;[182e] 88
                    dec       b                             ;[182f] 05
                    add       c                             ;[1830] 81
                    ld        (bc),a                        ;[1831] 02
                    ld        b,e                           ;[1832] 43
                    inc       e                             ;[1833] 1c
                    djnz      $187b                         ;[1834] 10 45
                    ld        b,e                           ;[1836] 43
                    exx                                     ;[1837] d9
                    ld        e,(hl)                        ;[1838] 5e
                    cp        l                             ;[1839] bd
                    ex        af,af'                        ;[183a] fd 08
                    nop                                     ;[183c] 00
                    sub       b                             ;[183d] 90
                    ld        d,c                           ;[183e] 51
                    sbc       $d4                           ;[183f] de d4
                    djnz      $183f                         ;[1841] 10 fc
                    ex        af,af'                        ;[1843] 08
                    nop                                     ;[1844] 00
                    djnz      $188c                         ;[1845] 10 45
                    ld        e,(hl)                        ;[1847] 5e
                    call      nc,$c255                      ;[1848] d4 55 c2
                    djnz      $1840                         ;[184b] 10 f3
                    ld        h,b                           ;[184d] 60
                    sbc       a                             ;[184e] 9f
                    sbc       e                             ;[184f] fd 9b
                    cp        l                             ;[1851] bd
                    cp        l                             ;[1852] bd
                    jp        $c490                         ;[1853] c3 90 c4
                    rst       $18                           ;[1856] df
                    ld        e,$9e                         ;[1857] 1e 9e
                    cp        l                             ;[1859] bd
                    ld        h,h                           ;[185a] 64
                    ret       c                             ;[185b] d8
                    ld        d,l                           ;[185c] 55
                    djnz      $181c                         ;[185d] 10 bd
                    ld        hl,$5c3b                      ;[185f] 21 3b 5c
                    ld        a,(hl)                        ;[1862] 7e
                    or        $0c                           ;[1863] f6 0c
                    ld        (hl),a                        ;[1865] 77
                    ld        hl,$ec0d                      ;[1866] 21 0d ec
                    bit       4,(hl)                        ;[1869] cb 66
                    ld        hl,$5b66                      ;[186b] 21 66 5b
                    jr        nz,$1873                      ;[186e] 20 03
                    res       0,(hl)                        ;[1870] cb 86
                    ret                                     ;[1872] c9

                    set       0,(hl)                        ;[1873] cb c6
                    ret                                     ;[1875] c9

                    push      hl                            ;[1876] e5
                    ld        hl,$5c3b                      ;[1877] 21 3b 5c
                    bit       5,(hl)                        ;[187a] cb 6e
                    jr        z,$187a                       ;[187c] 28 fc
                    res       5,(hl)                        ;[187e] cb ae
                    ld        a,($5c08)                     ;[1880] 3a 08 5c
                    ld        hl,$5c41                      ;[1883] 21 41 5c
                    res       0,(hl)                        ;[1886] cb 86
                    cp        $20                           ;[1888] fe 20
                    jr        nc,$1899                      ;[188a] 30 0d
                    cp        $10                           ;[188c] fe 10
                    jr        nc,$1877                      ;[188e] 30 e7
                    cp        $06                           ;[1890] fe 06
                    jr        c,$1877                       ;[1892] 38 e3
                    call      $189b                         ;[1894] cd 9b 18
                    jr        nc,$1877                      ;[1897] 30 de
                    pop       hl                            ;[1899] e1
                    ret                                     ;[189a] c9

                    rst       $28                           ;[189b] ef
                    in        a,($10)                       ;[189c] db 10
                    ret                                     ;[189e] c9

                    push      hl                            ;[189f] e5
                    call      $1952                         ;[18a0] cd 52 19
                    ld        hl,$5c3c                      ;[18a3] 21 3c 5c
                    res       0,(hl)                        ;[18a6] cb 86
                    pop       hl                            ;[18a8] e1
                    ld        e,(hl)                        ;[18a9] 5e
                    inc       hl                            ;[18aa] 23
                    push      hl                            ;[18ab] e5
                    ld        hl,$1a03                      ;[18ac] 21 03 1a
                    call      $192a                         ;[18af] cd 2a 19
                    pop       hl                            ;[18b2] e1
                    call      $192a                         ;[18b3] cd 2a 19
                    push      hl                            ;[18b6] e5
                    call      $1a39                         ;[18b7] cd 39 1a
                    ld        hl,$1a11                      ;[18ba] 21 11 1a
                    call      $192a                         ;[18bd] cd 2a 19
                    pop       hl                            ;[18c0] e1
                    push      de                            ;[18c1] d5
                    ld        bc,$0807                      ;[18c2] 01 07 08
                    call      $1922                         ;[18c5] cd 22 19
                    push      bc                            ;[18c8] c5
                    ld        b,$0c                         ;[18c9] 06 0c
                    ld        a,$20                         ;[18cb] 3e 20
                    rst       $10                           ;[18cd] d7
                    ld        a,(hl)                        ;[18ce] 7e
                    inc       hl                            ;[18cf] 23
                    cp        $80                           ;[18d0] fe 80
                    jr        nc,$18d7                      ;[18d2] 30 03
                    rst       $10                           ;[18d4] d7
                    djnz      $18ce                         ;[18d5] 10 f7
                    and       $7f                           ;[18d7] e6 7f
                    rst       $10                           ;[18d9] d7
                    ld        a,$20                         ;[18da] 3e 20
                    rst       $10                           ;[18dc] d7
                    djnz      $18da                         ;[18dd] 10 fb
                    pop       bc                            ;[18df] c1
                    inc       b                             ;[18e0] 04
                    call      $1922                         ;[18e1] cd 22 19
                    dec       e                             ;[18e4] 1d
                    jr        nz,$18c8                      ;[18e5] 20 e1
                    ld        hl,$6f38                      ;[18e7] 21 38 6f
                    pop       de                            ;[18ea] d1
                    sla       e                             ;[18eb] cb 23
                    sla       e                             ;[18ed] cb 23
                    sla       e                             ;[18ef] cb 23
                    ld        d,e                           ;[18f1] 53
                    dec       d                             ;[18f2] 15
                    ld        e,$6f                         ;[18f3] 1e 6f
                    ld        bc,$ff00                      ;[18f5] 01 00 ff
                    ld        a,d                           ;[18f8] 7a
                    call      $1910                         ;[18f9] cd 10 19
                    ld        bc,$0001                      ;[18fc] 01 01 00
                    ld        a,e                           ;[18ff] 7b
                    call      $1910                         ;[1900] cd 10 19
                    ld        bc,$0100                      ;[1903] 01 00 01
                    ld        a,d                           ;[1906] 7a
                    inc       a                             ;[1907] 3c
                    call      $1910                         ;[1908] cd 10 19
                    xor       a                             ;[190b] af
                    call      $19e1                         ;[190c] cd e1 19
                    ret                                     ;[190f] c9

                    push      af                            ;[1910] f5
                    push      hl                            ;[1911] e5
                    push      de                            ;[1912] d5
                    push      bc                            ;[1913] c5
                    ld        b,h                           ;[1914] 44
                    ld        c,l                           ;[1915] 4d
                    rst       $28                           ;[1916] ef
                    jp        (hl)                          ;[1917] e9
                    ld        ($d1c1),hl                    ;[1918] 22 c1 d1
                    pop       hl                            ;[191b] e1
                    pop       af                            ;[191c] f1
                    add       hl,bc                         ;[191d] 09
                    dec       a                             ;[191e] 3d
                    jr        nz,$1910                      ;[191f] 20 ef
                    ret                                     ;[1921] c9

                    ld        a,$16                         ;[1922] 3e 16
                    rst       $10                           ;[1924] d7
                    ld        a,b                           ;[1925] 78
                    rst       $10                           ;[1926] d7
                    ld        a,c                           ;[1927] 79
                    rst       $10                           ;[1928] d7
                    ret                                     ;[1929] c9

                    ld        a,(hl)                        ;[192a] 7e
                    inc       hl                            ;[192b] 23
                    cp        $ff                           ;[192c] fe ff
                    ret       z                             ;[192e] c8
                    cp        $2b                           ;[192f] fe 2b
                    jr        z,$1936                       ;[1931] 28 03
                    rst       $10                           ;[1933] d7
                    jr        $192a                         ;[1934] 18 f4
                    ld        a,(hl)                        ;[1936] 7e
                    cp        $33                           ;[1937] fe 33
                    ld        a,$2b                         ;[1939] 3e 2b
                    jr        nz,$1933                      ;[193b] 20 f6
                    push      hl                            ;[193d] e5
                    ld        hl,$5b66                      ;[193e] 21 66 5b
                    bit       4,(hl)                        ;[1941] cb 66
                    pop       hl                            ;[1943] e1
                    jr        nz,$1933                      ;[1944] 20 ed
                    inc       hl                            ;[1946] 23
                    inc       hl                            ;[1947] 23
                    ld        a,$2b                         ;[1948] 3e 2b
                    rst       $10                           ;[194a] d7
                    ld        a,$32                         ;[194b] 3e 32
                    rst       $10                           ;[194d] d7
                    ld        a,$41                         ;[194e] 3e 41
                    jr        $1933                         ;[1950] 18 e1
                    scf                                     ;[1952] 37
                    jr        $1956                         ;[1953] 18 01
                    and       a                             ;[1955] a7
                    ld        de,$eef6                      ;[1956] 11 f6 ee
                    ld        hl,$5c3c                      ;[1959] 21 3c 5c
                    jr        c,$195f                       ;[195c] 38 01
                    ex        de,hl                         ;[195e] eb
                    ldi                                     ;[195f] ed a0
                    jr        c,$1964                       ;[1961] 38 01
                    ex        de,hl                         ;[1963] eb
                    ld        hl,$5c7d                      ;[1964] 21 7d 5c
                    jr        c,$196a                       ;[1967] 38 01
                    ex        de,hl                         ;[1969] eb
                    ld        bc,$0014                      ;[196a] 01 14 00
                    ldir                                    ;[196d] ed b0
                    jr        c,$1972                       ;[196f] 38 01
                    ex        de,hl                         ;[1971] eb
                    ex        af,af'                        ;[1972] 08
                    ld        bc,$0707                      ;[1973] 01 07 07
                    call      $1da6                         ;[1976] cd a6 1d
                    ld        a,(ix+$01)                    ;[1979] dd 7e 01
                    add       b                             ;[197c] 80
                    ld        b,a                           ;[197d] 47
                    ld        a,$0c                         ;[197e] 3e 0c
                    push      bc                            ;[1980] c5
                    push      af                            ;[1981] f5
                    push      de                            ;[1982] d5
                    rst       $28                           ;[1983] ef
                    sbc       e                             ;[1984] 9b
                    ld        c,$01                         ;[1985] 0e 01
                    rlca                                    ;[1987] 07
                    nop                                     ;[1988] 00
                    add       hl,bc                         ;[1989] 09
                    pop       de                            ;[198a] d1
                    call      $1995                         ;[198b] cd 95 19
                    pop       af                            ;[198e] f1
                    pop       bc                            ;[198f] c1
                    dec       b                             ;[1990] 05
                    dec       a                             ;[1991] 3d
                    jr        nz,$1980                      ;[1992] 20 ec
                    ret                                     ;[1994] c9

                    ld        bc,$080e                      ;[1995] 01 0e 08
                    push      bc                            ;[1998] c5
                    ld        b,$00                         ;[1999] 06 00
                    push      hl                            ;[199b] e5
                    ex        af,af'                        ;[199c] 08
                    jr        c,$19a0                       ;[199d] 38 01
                    ex        de,hl                         ;[199f] eb
                    ldir                                    ;[19a0] ed b0
                    jr        c,$19a5                       ;[19a2] 38 01
                    ex        de,hl                         ;[19a4] eb
                    ex        af,af'                        ;[19a5] 08
                    pop       hl                            ;[19a6] e1
                    inc       h                             ;[19a7] 24
                    pop       bc                            ;[19a8] c1
                    djnz      $1998                         ;[19a9] 10 ed
                    push      bc                            ;[19ab] c5
                    push      de                            ;[19ac] d5
                    rst       $28                           ;[19ad] ef
                    adc       b                             ;[19ae] 88
                    ld        c,$eb                         ;[19af] 0e eb
                    pop       de                            ;[19b1] d1
                    pop       bc                            ;[19b2] c1
                    ex        af,af'                        ;[19b3] 08
                    jr        c,$19b7                       ;[19b4] 38 01
                    ex        de,hl                         ;[19b6] eb
                    ldir                                    ;[19b7] ed b0
                    jr        c,$19bc                       ;[19b9] 38 01
                    ex        de,hl                         ;[19bb] eb
                    ex        af,af'                        ;[19bc] 08
                    ret                                     ;[19bd] c9

                    call      $19e1                         ;[19be] cd e1 19
                    dec       a                             ;[19c1] 3d
                    jp        p,$19c8                       ;[19c2] f2 c8 19
                    ld        a,(hl)                        ;[19c5] 7e
                    dec       a                             ;[19c6] 3d
                    dec       a                             ;[19c7] 3d
                    call      $19e1                         ;[19c8] cd e1 19
                    scf                                     ;[19cb] 37
                    ret                                     ;[19cc] c9

                    push      de                            ;[19cd] d5
                    call      $19e1                         ;[19ce] cd e1 19
                    inc       a                             ;[19d1] 3c
                    ld        d,a                           ;[19d2] 57
                    ld        a,(hl)                        ;[19d3] 7e
                    dec       a                             ;[19d4] 3d
                    dec       a                             ;[19d5] 3d
                    cp        d                             ;[19d6] ba
                    ld        a,d                           ;[19d7] 7a
                    jp        p,$19dc                       ;[19d8] f2 dc 19
                    xor       a                             ;[19db] af
                    call      $19e1                         ;[19dc] cd e1 19
                    pop       de                            ;[19df] d1
                    ret                                     ;[19e0] c9

                    push      af                            ;[19e1] f5
                    push      hl                            ;[19e2] e5
                    push      de                            ;[19e3] d5
                    ld        hl,$5907                      ;[19e4] 21 07 59
                    ld        de,$0020                      ;[19e7] 11 20 00
                    and       a                             ;[19ea] a7
                    jr        z,$19f1                       ;[19eb] 28 04
                    add       hl,de                         ;[19ed] 19
                    dec       a                             ;[19ee] 3d
                    jr        nz,$19ed                      ;[19ef] 20 fc
                    ld        a,$78                         ;[19f1] 3e 78
                    cp        (hl)                          ;[19f3] be
                    jr        nz,$19f8                      ;[19f4] 20 02
                    ld        a,$68                         ;[19f6] 3e 68
                    ld        d,$0e                         ;[19f8] 16 0e
                    ld        (hl),a                        ;[19fa] 77
                    inc       hl                            ;[19fb] 23
                    dec       d                             ;[19fc] 15
                    jr        nz,$19fa                      ;[19fd] 20 fb
                    pop       de                            ;[19ff] d1
                    pop       hl                            ;[1a00] e1
                    pop       af                            ;[1a01] f1
                    ret                                     ;[1a02] c9

                    ld        d,$07                         ;[1a03] 16 07
                    rlca                                    ;[1a05] 07
                    dec       d                             ;[1a06] 15
                    nop                                     ;[1a07] 00
                    inc       d                             ;[1a08] 14
                    nop                                     ;[1a09] 00
                    djnz      $1a13                         ;[1a0a] 10 07
                    ld        de,$1300                      ;[1a0c] 11 00 13
                    ld        bc,$11ff                      ;[1a0f] 01 ff 11
                    nop                                     ;[1a12] 00
                    jr        nz,$1a26                      ;[1a13] 20 11
                    rlca                                    ;[1a15] 07
                    djnz      $1a18                         ;[1a16] 10 00
                    rst       $38                           ;[1a18] ff
                    ld        bc,$0703                      ;[1a19] 01 03 07
                    rrca                                    ;[1a1c] 0f
                    rra                                     ;[1a1d] 1f
                    ccf                                     ;[1a1e] 3f
                    ld        a,a                           ;[1a1f] 7f
                    rst       $38                           ;[1a20] ff
                    cp        $fc                           ;[1a21] fe fc
                    ret       m                             ;[1a23] f8
                    ret       p                             ;[1a24] f0
                    ret       po                            ;[1a25] e0
                    ret       nz                            ;[1a26] c0
                    add       b                             ;[1a27] 80
                    nop                                     ;[1a28] 00
                    djnz      $1a2d                         ;[1a29] 10 02
                    jr        nz,$1a3e                      ;[1a2b] 20 11
                    ld        b,$21                         ;[1a2d] 06 21
                    djnz      $1a35                         ;[1a2f] 10 04
                    jr        nz,$1a44                      ;[1a31] 20 11
                    dec       b                             ;[1a33] 05
                    ld        hl,$0010                      ;[1a34] 21 10 00
                    jr        nz,$1a38                      ;[1a37] 20 ff
                    push      bc                            ;[1a39] c5
                    push      de                            ;[1a3a] d5
                    push      hl                            ;[1a3b] e5
                    ld        hl,$1a19                      ;[1a3c] 21 19 1a
                    ld        de,$5b7c                      ;[1a3f] 11 7c 5b
                    ld        bc,$0010                      ;[1a42] 01 10 00
                    ldir                                    ;[1a45] ed b0
                    ld        hl,($5c36)                    ;[1a47] 2a 36 5c
                    push      hl                            ;[1a4a] e5
                    ld        hl,$5a7c                      ;[1a4b] 21 7c 5a
                    ld        ($5c36),hl                    ;[1a4e] 22 36 5c
                    ld        hl,$1a29                      ;[1a51] 21 29 1a
                    call      $192a                         ;[1a54] cd 2a 19
                    pop       hl                            ;[1a57] e1
                    ld        ($5c36),hl                    ;[1a58] 22 36 5c
                    pop       hl                            ;[1a5b] e1
                    pop       de                            ;[1a5c] d1
                    pop       bc                            ;[1a5d] c1
                    ret                                     ;[1a5e] c9

                    ld        hl,$0814                      ;[1a5f] 21 14 08
                    jr        $1a6c                         ;[1a62] 18 08
                    ld        hl,$081c                      ;[1a64] 21 1c 08
                    jr        $1a6c                         ;[1a67] 18 03
                    ld        hl,$080e                      ;[1a69] 21 0e 08
                    push      hl                            ;[1a6c] e5
                    call      $1a93                         ;[1a6d] cd 93 1a
                    ld        hl,$5aa0                      ;[1a70] 21 a0 5a
                    ld        b,$20                         ;[1a73] 06 20
                    ld        a,$40                         ;[1a75] 3e 40
                    ld        (hl),a                        ;[1a77] 77
                    inc       hl                            ;[1a78] 23
                    djnz      $1a77                         ;[1a79] 10 fc
                    ld        hl,$1a03                      ;[1a7b] 21 03 1a
                    call      $192a                         ;[1a7e] cd 2a 19
                    ld        bc,$1500                      ;[1a81] 01 00 15
                    call      $1922                         ;[1a84] cd 22 19
                    pop       de                            ;[1a87] d1
                    call      $02a3                         ;[1a88] cd a3 02
                    ld        c,$1a                         ;[1a8b] 0e 1a
                    call      $1922                         ;[1a8d] cd 22 19
                    jp        $1a39                         ;[1a90] c3 39 1a
                    ld        b,$15                         ;[1a93] 06 15
                    ld        d,$17                         ;[1a95] 16 17
                    jp        $1d70                         ;[1a97] c3 70 1d
                    call      $05ac                         ;[1a9a] cd ac 05
                    call      $1c17                         ;[1a9d] cd 17 1c
                    ld        a,d                           ;[1aa0] 7a
                    or        e                             ;[1aa1] b3
                    jp        z,$1bd2                       ;[1aa2] ca d2 1b
                    ld        hl,($5b77)                    ;[1aa5] 2a 77 5b
                    rst       $28                           ;[1aa8] ef
                    xor       c                             ;[1aa9] a9
                    jr        nc,$1a97                      ;[1aaa] 30 eb
                    ld        hl,($5b75)                    ;[1aac] 2a 75 5b
                    add       hl,de                         ;[1aaf] 19
                    ld        de,$2710                      ;[1ab0] 11 10 27
                    or        a                             ;[1ab3] b7
                    sbc       hl,de                         ;[1ab4] ed 52
                    jp        nc,$1bd2                      ;[1ab6] d2 d2 1b
                    ld        hl,($5c53)                    ;[1ab9] 2a 53 5c
                    rst       $28                           ;[1abc] ef
                    cp        b                             ;[1abd] b8
                    add       hl,de                         ;[1abe] 19
                    inc       hl                            ;[1abf] 23
                    inc       hl                            ;[1ac0] 23
                    ld        ($5b73),hl                    ;[1ac1] 22 73 5b
                    inc       hl                            ;[1ac4] 23
                    inc       hl                            ;[1ac5] 23
                    ld        ($5b96),de                    ;[1ac6] ed 53 96 5b
                    ld        a,(hl)                        ;[1aca] 7e
                    rst       $28                           ;[1acb] ef
                    or        (hl)                          ;[1acc] b6
                    jr        $1acd                         ;[1acd] 18 fe
                    dec       c                             ;[1acf] 0d
                    jr        z,$1ad7                       ;[1ad0] 28 05
                    call      $1b20                         ;[1ad2] cd 20 1b
                    jr        $1aca                         ;[1ad5] 18 f3
                    ld        de,($5b96)                    ;[1ad7] ed 5b 96 5b
                    ld        hl,($5c4b)                    ;[1adb] 2a 4b 5c
                    and       a                             ;[1ade] a7
                    sbc       hl,de                         ;[1adf] ed 52
                    ex        de,hl                         ;[1ae1] eb
                    jr        nz,$1abc                      ;[1ae2] 20 d8
                    call      $1c17                         ;[1ae4] cd 17 1c
                    ld        b,d                           ;[1ae7] 42
                    ld        c,e                           ;[1ae8] 4b
                    ld        de,$0000                      ;[1ae9] 11 00 00
                    ld        hl,($5c53)                    ;[1aec] 2a 53 5c
                    push      bc                            ;[1aef] c5
                    push      de                            ;[1af0] d5
                    push      hl                            ;[1af1] e5
                    ld        hl,($5b77)                    ;[1af2] 2a 77 5b
                    rst       $28                           ;[1af5] ef
                    xor       c                             ;[1af6] a9
                    jr        nc,$1ae6                      ;[1af7] 30 ed
                    ld        e,e                           ;[1af9] 5b
                    ld        (hl),l                        ;[1afa] 75
                    ld        e,e                           ;[1afb] 5b
                    add       hl,de                         ;[1afc] 19
                    ex        de,hl                         ;[1afd] eb
                    pop       hl                            ;[1afe] e1
                    ld        (hl),d                        ;[1aff] 72
                    inc       hl                            ;[1b00] 23
                    ld        (hl),e                        ;[1b01] 73
                    inc       hl                            ;[1b02] 23
                    ld        c,(hl)                        ;[1b03] 4e
                    inc       hl                            ;[1b04] 23
                    ld        b,(hl)                        ;[1b05] 46
                    inc       hl                            ;[1b06] 23
                    add       hl,bc                         ;[1b07] 09
                    pop       de                            ;[1b08] d1
                    inc       de                            ;[1b09] 13
                    pop       bc                            ;[1b0a] c1
                    dec       bc                            ;[1b0b] 0b
                    ld        a,b                           ;[1b0c] 78
                    or        c                             ;[1b0d] b1
                    jr        nz,$1aef                      ;[1b0e] 20 df
                    call      $05d1                         ;[1b10] cd d1 05
                    ld        ($5b73),bc                    ;[1b13] ed 43 73 5b
                    scf                                     ;[1b17] 37
                    ret                                     ;[1b18] c9

                    jp        z,$e1f0                       ;[1b19] ca f0 e1
                    call      pe,$e5ed                      ;[1b1c] ec ed e5
                    rst       $30                           ;[1b1f] f7
                    inc       hl                            ;[1b20] 23
                    ld        ($5b94),hl                    ;[1b21] 22 94 5b
                    ex        de,hl                         ;[1b24] eb
                    ld        bc,$0007                      ;[1b25] 01 07 00
                    ld        hl,$1b19                      ;[1b28] 21 19 1b
                    cpir                                    ;[1b2b] ed b1
                    ex        de,hl                         ;[1b2d] eb
                    ret       nz                            ;[1b2e] c0
                    ld        c,$00                         ;[1b2f] 0e 00
                    ld        a,(hl)                        ;[1b31] 7e
                    cp        $20                           ;[1b32] fe 20
                    jr        z,$1b51                       ;[1b34] 28 1b
                    rst       $28                           ;[1b36] ef
                    dec       de                            ;[1b37] 1b
                    dec       l                             ;[1b38] 2d
                    jr        nc,$1b51                      ;[1b39] 30 16
                    cp        $2e                           ;[1b3b] fe 2e
                    jr        z,$1b51                       ;[1b3d] 28 12
                    cp        $0e                           ;[1b3f] fe 0e
                    jr        z,$1b55                       ;[1b41] 28 12
                    or        $20                           ;[1b43] f6 20
                    cp        $65                           ;[1b45] fe 65
                    jr        nz,$1b4d                      ;[1b47] 20 04
                    ld        a,b                           ;[1b49] 78
                    or        c                             ;[1b4a] b1
                    jr        nz,$1b51                      ;[1b4b] 20 04
                    ld        hl,($5b94)                    ;[1b4d] 2a 94 5b
                    ret                                     ;[1b50] c9

                    inc       bc                            ;[1b51] 03
                    inc       hl                            ;[1b52] 23
                    jr        $1b31                         ;[1b53] 18 dc
                    ld        ($5b8c),bc                    ;[1b55] ed 43 8c 5b
                    push      hl                            ;[1b59] e5
                    rst       $28                           ;[1b5a] ef
                    or        (hl)                          ;[1b5b] b6
                    jr        $1b2b                         ;[1b5c] 18 cd
                    ld        c,b                           ;[1b5e] 48
                    inc       e                             ;[1b5f] 1c
                    ld        a,(hl)                        ;[1b60] 7e
                    pop       hl                            ;[1b61] e1
                    cp        $3a                           ;[1b62] fe 3a
                    jr        z,$1b69                       ;[1b64] 28 03
                    cp        $0d                           ;[1b66] fe 0d
                    ret       nz                            ;[1b68] c0
                    inc       hl                            ;[1b69] 23
                    rst       $28                           ;[1b6a] ef
                    or        h                             ;[1b6b] b4
                    inc       sp                            ;[1b6c] 33
                    rst       $28                           ;[1b6d] ef
                    and       d                             ;[1b6e] a2
                    dec       l                             ;[1b6f] 2d
                    ld        h,b                           ;[1b70] 60
                    ld        l,c                           ;[1b71] 69
                    rst       $28                           ;[1b72] ef
                    ld        l,(hl)                        ;[1b73] 6e
                    add       hl,de                         ;[1b74] 19
                    jr        z,$1b81                       ;[1b75] 28 0a
                    ld        a,(hl)                        ;[1b77] 7e
                    cp        $80                           ;[1b78] fe 80
                    jr        nz,$1b81                      ;[1b7a] 20 05
                    ld        hl,$270f                      ;[1b7c] 21 0f 27
                    jr        $1b92                         ;[1b7f] 18 11
                    ld        ($5b92),hl                    ;[1b81] 22 92 5b
                    call      $1c1d                         ;[1b84] cd 1d 1c
                    ld        hl,($5b77)                    ;[1b87] 2a 77 5b
                    rst       $28                           ;[1b8a] ef
                    xor       c                             ;[1b8b] a9
                    jr        nc,$1b7b                      ;[1b8c] 30 ed
                    ld        e,e                           ;[1b8e] 5b
                    ld        (hl),l                        ;[1b8f] 75
                    ld        e,e                           ;[1b90] 5b
                    add       hl,de                         ;[1b91] 19
                    ld        de,$5b8e                      ;[1b92] 11 8e 5b
                    push      hl                            ;[1b95] e5
                    call      $1c4e                         ;[1b96] cd 4e 1c
                    ld        e,b                           ;[1b99] 58
                    inc       e                             ;[1b9a] 1c
                    ld        d,$00                         ;[1b9b] 16 00
                    push      de                            ;[1b9d] d5
                    push      hl                            ;[1b9e] e5
                    ld        l,e                           ;[1b9f] 6b
                    ld        h,$00                         ;[1ba0] 26 00
                    ld        bc,($5b8c)                    ;[1ba2] ed 4b 8c 5b
                    or        a                             ;[1ba6] b7
                    sbc       hl,bc                         ;[1ba7] ed 42
                    ld        ($5b8c),hl                    ;[1ba9] 22 8c 5b
                    jr        z,$1be1                       ;[1bac] 28 33
                    jr        c,$1bd7                       ;[1bae] 38 27
                    ld        b,h                           ;[1bb0] 44
                    ld        c,l                           ;[1bb1] 4d
                    ld        hl,($5b94)                    ;[1bb2] 2a 94 5b
                    push      hl                            ;[1bb5] e5
                    push      de                            ;[1bb6] d5
                    ld        hl,($5c65)                    ;[1bb7] 2a 65 5c
                    add       hl,bc                         ;[1bba] 09
                    jr        c,$1bd0                       ;[1bbb] 38 13
                    ex        de,hl                         ;[1bbd] eb
                    ld        hl,$0082                      ;[1bbe] 21 82 00
                    add       hl,de                         ;[1bc1] 19
                    jr        c,$1bd0                       ;[1bc2] 38 0c
                    sbc       hl,sp                         ;[1bc4] ed 72
                    ccf                                     ;[1bc6] 3f
                    jr        c,$1bd0                       ;[1bc7] 38 07
                    pop       de                            ;[1bc9] d1
                    pop       hl                            ;[1bca] e1
                    rst       $28                           ;[1bcb] ef
                    ld        d,l                           ;[1bcc] 55
                    ld        d,$18                         ;[1bcd] 16 18
                    ld        de,$e1d1                      ;[1bcf] 11 d1 e1
                    call      $05d1                         ;[1bd2] cd d1 05
                    and       a                             ;[1bd5] a7
                    ret                                     ;[1bd6] c9

                    dec       bc                            ;[1bd7] 0b
                    dec       e                             ;[1bd8] 1d
                    jr        nz,$1bd7                      ;[1bd9] 20 fc
                    ld        hl,($5b94)                    ;[1bdb] 2a 94 5b
                    rst       $28                           ;[1bde] ef
                    ret       pe                            ;[1bdf] e8
                    add       hl,de                         ;[1be0] 19
                    ld        de,($5b94)                    ;[1be1] ed 5b 94 5b
                    pop       hl                            ;[1be5] e1
                    pop       bc                            ;[1be6] c1
                    ldir                                    ;[1be7] ed b0
                    ex        de,hl                         ;[1be9] eb
                    ld        (hl),$0e                      ;[1bea] 36 0e
                    pop       bc                            ;[1bec] c1
                    inc       hl                            ;[1bed] 23
                    push      hl                            ;[1bee] e5
                    rst       $28                           ;[1bef] ef
                    dec       hl                            ;[1bf0] 2b
                    dec       l                             ;[1bf1] 2d
                    pop       de                            ;[1bf2] d1
                    ld        bc,$0005                      ;[1bf3] 01 05 00
                    ldir                                    ;[1bf6] ed b0
                    ex        de,hl                         ;[1bf8] eb
                    push      hl                            ;[1bf9] e5
                    ld        hl,($5b73)                    ;[1bfa] 2a 73 5b
                    push      hl                            ;[1bfd] e5
                    ld        e,(hl)                        ;[1bfe] 5e
                    inc       hl                            ;[1bff] 23
                    ld        d,(hl)                        ;[1c00] 56
                    ld        hl,($5b8c)                    ;[1c01] 2a 8c 5b
                    add       hl,de                         ;[1c04] 19
                    ex        de,hl                         ;[1c05] eb
                    pop       hl                            ;[1c06] e1
                    ld        (hl),e                        ;[1c07] 73
                    inc       hl                            ;[1c08] 23
                    ld        (hl),d                        ;[1c09] 72
                    ld        hl,($5b96)                    ;[1c0a] 2a 96 5b
                    ld        de,($5b8c)                    ;[1c0d] ed 5b 8c 5b
                    add       hl,de                         ;[1c11] 19
                    ld        ($5b96),hl                    ;[1c12] 22 96 5b
                    pop       hl                            ;[1c15] e1
                    ret                                     ;[1c16] c9

                    ld        hl,($5c4b)                    ;[1c17] 2a 4b 5c
                    ld        ($5b92),hl                    ;[1c1a] 22 92 5b
                    ld        hl,($5c53)                    ;[1c1d] 2a 53 5c
                    ld        de,($5b92)                    ;[1c20] ed 5b 92 5b
                    or        a                             ;[1c24] b7
                    sbc       hl,de                         ;[1c25] ed 52
                    jr        z,$1c43                       ;[1c27] 28 1a
                    ld        hl,($5c53)                    ;[1c29] 2a 53 5c
                    ld        bc,$0000                      ;[1c2c] 01 00 00
                    push      bc                            ;[1c2f] c5
                    rst       $28                           ;[1c30] ef
                    cp        b                             ;[1c31] b8
                    add       hl,de                         ;[1c32] 19
                    ld        hl,($5b92)                    ;[1c33] 2a 92 5b
                    and       a                             ;[1c36] a7
                    sbc       hl,de                         ;[1c37] ed 52
                    jr        z,$1c40                       ;[1c39] 28 05
                    ex        de,hl                         ;[1c3b] eb
                    pop       bc                            ;[1c3c] c1
                    inc       bc                            ;[1c3d] 03
                    jr        $1c2f                         ;[1c3e] 18 ef
                    pop       de                            ;[1c40] d1
                    inc       de                            ;[1c41] 13
                    ret                                     ;[1c42] c9

                    ld        de,$0000                      ;[1c43] 11 00 00
                    ret                                     ;[1c46] c9

                    inc       hl                            ;[1c47] 23
                    ld        a,(hl)                        ;[1c48] 7e
                    cp        $20                           ;[1c49] fe 20
                    jr        z,$1c47                       ;[1c4b] 28 fa
                    ret                                     ;[1c4d] c9

                    push      de                            ;[1c4e] d5
                    ld        bc,$fc18                      ;[1c4f] 01 18 fc
                    call      $1c72                         ;[1c52] cd 72 1c
                    ld        bc,$ff9c                      ;[1c55] 01 9c ff
                    call      $1c72                         ;[1c58] cd 72 1c
                    ld        c,$f6                         ;[1c5b] 0e f6
                    call      $1c72                         ;[1c5d] cd 72 1c
                    ld        a,l                           ;[1c60] 7d
                    add       $30                           ;[1c61] c6 30
                    ld        (de),a                        ;[1c63] 12
                    inc       de                            ;[1c64] 13
                    ld        b,$03                         ;[1c65] 06 03
                    pop       hl                            ;[1c67] e1
                    ld        a,(hl)                        ;[1c68] 7e
                    cp        $30                           ;[1c69] fe 30
                    ret       nz                            ;[1c6b] c0
                    ld        (hl),$20                      ;[1c6c] 36 20
                    inc       hl                            ;[1c6e] 23
                    djnz      $1c68                         ;[1c6f] 10 f7
                    ret                                     ;[1c71] c9

                    xor       a                             ;[1c72] af
                    add       hl,bc                         ;[1c73] 09
                    inc       a                             ;[1c74] 3c
                    jr        c,$1c73                       ;[1c75] 38 fc
                    sbc       hl,bc                         ;[1c77] ed 42
                    dec       a                             ;[1c79] 3d
                    add       $30                           ;[1c7a] c6 30
                    ld        (de),a                        ;[1c7c] 12
                    inc       de                            ;[1c7d] 13
                    ret                                     ;[1c7e] c9

                    ex        af,af'                        ;[1c7f] 08
                    nop                                     ;[1c80] 00
                    nop                                     ;[1c81] 00
                    inc       d                             ;[1c82] 14
                    nop                                     ;[1c83] 00
                    nop                                     ;[1c84] 00
                    nop                                     ;[1c85] 00
                    rrca                                    ;[1c86] 0f
                    nop                                     ;[1c87] 00
                    ex        af,af'                        ;[1c88] 08
                    nop                                     ;[1c89] 00
                    ld        d,$01                         ;[1c8a] 16 01
                    nop                                     ;[1c8c] 00
                    nop                                     ;[1c8d] 00
                    nop                                     ;[1c8e] 00
                    rrca                                    ;[1c8f] 0f
                    nop                                     ;[1c90] 00
                    ld        ix,$fd98                      ;[1c91] dd 21 98 fd
                    ld        hl,$1c7f                      ;[1c95] 21 7f 1c
                    jr        $1c9d                         ;[1c98] 18 03
                    ld        hl,$1c88                      ;[1c9a] 21 88 1c
                    ld        de,$fd98                      ;[1c9d] 11 98 fd
                    jp        $2157                         ;[1ca0] c3 57 21
                    rst       $10                           ;[1ca3] d7
                    ld        a,d                           ;[1ca4] 7a
                    rst       $10                           ;[1ca5] d7
                    scf                                     ;[1ca6] 37
                    ret                                     ;[1ca7] c9

                    and       $3f                           ;[1ca8] e6 3f
                    ld        (ix+$06),a                    ;[1caa] dd 77 06
                    scf                                     ;[1cad] 37
                    ret                                     ;[1cae] c9

                    ld        a,(ix+$01)                    ;[1caf] dd 7e 01
                    add       b                             ;[1cb2] 80
                    ld        b,a                           ;[1cb3] 47
                    call      $1db2                         ;[1cb4] cd b2 1d
                    ld        a,(hl)                        ;[1cb7] 7e
                    ld        (ix+$07),a                    ;[1cb8] dd 77 07
                    cpl                                     ;[1cbb] 2f
                    and       $c0                           ;[1cbc] e6 c0
                    or        (ix+$06)                      ;[1cbe] dd b6 06
                    ld        (hl),a                        ;[1cc1] 77
                    scf                                     ;[1cc2] 37
                    ret                                     ;[1cc3] c9

                    ld        a,(ix+$01)                    ;[1cc4] dd 7e 01
                    add       b                             ;[1cc7] 80
                    ld        b,a                           ;[1cc8] 47
                    call      $1db2                         ;[1cc9] cd b2 1d
                    ld        a,(ix+$07)                    ;[1ccc] dd 7e 07
                    ld        (hl),a                        ;[1ccf] 77
                    ret                                     ;[1cd0] c9

                    push      hl                            ;[1cd1] e5
                    ld        h,$00                         ;[1cd2] 26 00
                    ld        a,e                           ;[1cd4] 7b
                    sub       b                             ;[1cd5] 90
                    jr        $1cdf                         ;[1cd6] 18 07
                    push      hl                            ;[1cd8] e5
                    ld        a,e                           ;[1cd9] 7b
                    ld        e,b                           ;[1cda] 58
                    ld        b,a                           ;[1cdb] 47
                    sub       e                             ;[1cdc] 93
                    ld        h,$ff                         ;[1cdd] 26 ff
                    ld        c,a                           ;[1cdf] 4f
                    ld        a,b                           ;[1ce0] 78
                    cp        e                             ;[1ce1] bb
                    jr        z,$1d2f                       ;[1ce2] 28 4b
                    push      de                            ;[1ce4] d5
                    call      $1daa                         ;[1ce5] cd aa 1d
                    push      bc                            ;[1ce8] c5
                    ld        c,h                           ;[1ce9] 4c
                    rst       $28                           ;[1cea] ef
                    sbc       e                             ;[1ceb] 9b
                    ld        c,$eb                         ;[1cec] 0e eb
                    xor       a                             ;[1cee] af
                    or        c                             ;[1cef] b1
                    jr        z,$1cf5                       ;[1cf0] 28 03
                    inc       b                             ;[1cf2] 04
                    jr        $1cf6                         ;[1cf3] 18 01
                    dec       b                             ;[1cf5] 05
                    push      de                            ;[1cf6] d5
                    rst       $28                           ;[1cf7] ef
                    sbc       e                             ;[1cf8] 9b
                    ld        c,$d1                         ;[1cf9] 0e d1
                    ld        a,c                           ;[1cfb] 79
                    ld        c,$20                         ;[1cfc] 0e 20
                    ld        b,$08                         ;[1cfe] 06 08
                    push      bc                            ;[1d00] c5
                    push      hl                            ;[1d01] e5
                    push      de                            ;[1d02] d5
                    ld        b,$00                         ;[1d03] 06 00
                    ldir                                    ;[1d05] ed b0
                    pop       de                            ;[1d07] d1
                    pop       hl                            ;[1d08] e1
                    pop       bc                            ;[1d09] c1
                    inc       h                             ;[1d0a] 24
                    inc       d                             ;[1d0b] 14
                    djnz      $1d00                         ;[1d0c] 10 f2
                    push      af                            ;[1d0e] f5
                    push      de                            ;[1d0f] d5
                    rst       $28                           ;[1d10] ef
                    adc       b                             ;[1d11] 88
                    ld        c,$eb                         ;[1d12] 0e eb
                    ex        (sp),hl                       ;[1d14] e3
                    rst       $28                           ;[1d15] ef
                    adc       b                             ;[1d16] 88
                    ld        c,$eb                         ;[1d17] 0e eb
                    ex        (sp),hl                       ;[1d19] e3
                    pop       de                            ;[1d1a] d1
                    ld        bc,$0020                      ;[1d1b] 01 20 00
                    ldir                                    ;[1d1e] ed b0
                    pop       af                            ;[1d20] f1
                    pop       bc                            ;[1d21] c1
                    and       a                             ;[1d22] a7
                    jr        z,$1d28                       ;[1d23] 28 03
                    inc       b                             ;[1d25] 04
                    jr        $1d29                         ;[1d26] 18 01
                    dec       b                             ;[1d28] 05
                    dec       c                             ;[1d29] 0d
                    ld        h,a                           ;[1d2a] 67
                    jr        nz,$1ce8                      ;[1d2b] 20 bb
                    pop       de                            ;[1d2d] d1
                    ld        b,e                           ;[1d2e] 43
                    pop       hl                            ;[1d2f] e1
                    call      $1dca                         ;[1d30] cd ca 1d
                    ex        de,hl                         ;[1d33] eb
                    ld        a,($5c3c)                     ;[1d34] 3a 3c 5c
                    push      af                            ;[1d37] f5
                    ld        hl,$ec0d                      ;[1d38] 21 0d ec
                    bit       6,(hl)                        ;[1d3b] cb 76
                    res       0,a                           ;[1d3d] cb 87
                    jr        z,$1d43                       ;[1d3f] 28 02
                    set       0,a                           ;[1d41] cb c7
                    ld        ($5c3c),a                     ;[1d43] 32 3c 5c
                    ld        c,$00                         ;[1d46] 0e 00
                    call      $1922                         ;[1d48] cd 22 19
                    ex        de,hl                         ;[1d4b] eb
                    ld        b,$20                         ;[1d4c] 06 20
                    ld        a,(hl)                        ;[1d4e] 7e
                    and       a                             ;[1d4f] a7
                    jr        nz,$1d54                      ;[1d50] 20 02
                    ld        a,$20                         ;[1d52] 3e 20
                    cp        $90                           ;[1d54] fe 90
                    jr        nc,$1d67                      ;[1d56] 30 0f
                    rst       $28                           ;[1d58] ef
                    djnz      $1d5b                         ;[1d59] 10 00
                    inc       hl                            ;[1d5b] 23
                    djnz      $1d4e                         ;[1d5c] 10 f0
                    pop       af                            ;[1d5e] f1
                    ld        ($5c3c),a                     ;[1d5f] 32 3c 5c
                    call      $1dca                         ;[1d62] cd ca 1d
                    scf                                     ;[1d65] 37
                    ret                                     ;[1d66] c9

                    call      $05ac                         ;[1d67] cd ac 05
                    rst       $10                           ;[1d6a] d7
                    call      $05d1                         ;[1d6b] cd d1 05
                    jr        $1d5b                         ;[1d6e] 18 eb
                    call      $1dca                         ;[1d70] cd ca 1d
                    ld        a,d                           ;[1d73] 7a
                    sub       b                             ;[1d74] 90
                    inc       a                             ;[1d75] 3c
                    ld        c,a                           ;[1d76] 4f
                    call      $1daa                         ;[1d77] cd aa 1d
                    push      bc                            ;[1d7a] c5
                    rst       $28                           ;[1d7b] ef
                    sbc       e                             ;[1d7c] 9b
                    ld        c,$0e                         ;[1d7d] 0e 0e
                    ex        af,af'                        ;[1d7f] 08
                    push      hl                            ;[1d80] e5
                    ld        b,$20                         ;[1d81] 06 20
                    xor       a                             ;[1d83] af
                    ld        (hl),a                        ;[1d84] 77
                    inc       hl                            ;[1d85] 23
                    djnz      $1d84                         ;[1d86] 10 fc
                    pop       hl                            ;[1d88] e1
                    inc       h                             ;[1d89] 24
                    dec       c                             ;[1d8a] 0d
                    jr        nz,$1d80                      ;[1d8b] 20 f3
                    ld        b,$20                         ;[1d8d] 06 20
                    push      bc                            ;[1d8f] c5
                    rst       $28                           ;[1d90] ef
                    adc       b                             ;[1d91] 88
                    ld        c,$eb                         ;[1d92] 0e eb
                    pop       bc                            ;[1d94] c1
                    ld        a,($5c8d)                     ;[1d95] 3a 8d 5c
                    ld        (hl),a                        ;[1d98] 77
                    inc       hl                            ;[1d99] 23
                    djnz      $1d98                         ;[1d9a] 10 fc
                    pop       bc                            ;[1d9c] c1
                    dec       b                             ;[1d9d] 05
                    dec       c                             ;[1d9e] 0d
                    jr        nz,$1d7a                      ;[1d9f] 20 d9
                    call      $1dca                         ;[1da1] cd ca 1d
                    scf                                     ;[1da4] 37
                    ret                                     ;[1da5] c9

                    ld        a,$21                         ;[1da6] 3e 21
                    sub       c                             ;[1da8] 91
                    ld        c,a                           ;[1da9] 4f
                    ld        a,$18                         ;[1daa] 3e 18
                    sub       b                             ;[1dac] 90
                    sub       (ix+$01)                      ;[1dad] dd 96 01
                    ld        b,a                           ;[1db0] 47
                    ret                                     ;[1db1] c9

                    push      bc                            ;[1db2] c5
                    xor       a                             ;[1db3] af
                    ld        d,b                           ;[1db4] 50
                    ld        e,a                           ;[1db5] 5f
                    rr        d                             ;[1db6] cb 1a
                    rr        e                             ;[1db8] cb 1b
                    rr        d                             ;[1dba] cb 1a
                    rr        e                             ;[1dbc] cb 1b
                    rr        d                             ;[1dbe] cb 1a
                    rr        e                             ;[1dc0] cb 1b
                    ld        hl,$5800                      ;[1dc2] 21 00 58
                    ld        b,a                           ;[1dc5] 47
                    add       hl,bc                         ;[1dc6] 09
                    add       hl,de                         ;[1dc7] 19
                    pop       bc                            ;[1dc8] c1
                    ret                                     ;[1dc9] c9

                    push      af                            ;[1dca] f5
                    push      hl                            ;[1dcb] e5
                    push      de                            ;[1dcc] d5
                    ld        hl,($5c8d)                    ;[1dcd] 2a 8d 5c
                    ld        de,($5c8f)                    ;[1dd0] ed 5b 8f 5c
                    exx                                     ;[1dd4] d9
                    ld        hl,($ec0f)                    ;[1dd5] 2a 0f ec
                    ld        de,($ec11)                    ;[1dd8] ed 5b 11 ec
                    ld        ($5c8d),hl                    ;[1ddc] 22 8d 5c
                    ld        ($5c8f),de                    ;[1ddf] ed 53 8f 5c
                    exx                                     ;[1de3] d9
                    ld        ($ec0f),hl                    ;[1de4] 22 0f ec
                    ld        ($ec11),de                    ;[1de7] ed 53 11 ec
                    ld        hl,$ec13                      ;[1deb] 21 13 ec
                    ld        a,($5c91)                     ;[1dee] 3a 91 5c
                    ld        d,(hl)                        ;[1df1] 56
                    ld        (hl),a                        ;[1df2] 77
                    ld        a,d                           ;[1df3] 7a
                    ld        ($5c91),a                     ;[1df4] 32 91 5c
                    pop       de                            ;[1df7] d1
                    pop       hl                            ;[1df8] e1
                    pop       af                            ;[1df9] f1
                    ret                                     ;[1dfa] c9

                    ld        a,$01                         ;[1dfb] 3e 01
                    jr        $1e01                         ;[1dfd] 18 02
                    ld        a,$00                         ;[1dff] 3e 00
                    ld        ($fdb6),a                     ;[1e01] 32 b6 fd
                    ld        hl,$0000                      ;[1e04] 21 00 00
                    ld        ($fdb1),hl                    ;[1e07] 22 b1 fd
                    ld        ($fdb3),hl                    ;[1e0a] 22 b3 fd
                    add       hl,sp                         ;[1e0d] 39
                    ld        ($fdb7),hl                    ;[1e0e] 22 b7 fd
                    call      $15e9                         ;[1e11] cd e9 15
                    ld        a,$00                         ;[1e14] 3e 00
                    ld        ($fdb0),a                     ;[1e16] 32 b0 fd
                    ld        hl,$fda0                      ;[1e19] 21 a0 fd
                    ld        ($fda9),hl                    ;[1e1c] 22 a9 fd
                    call      $05ac                         ;[1e1f] cd ac 05
                    rst       $28                           ;[1e22] ef
                    or        b                             ;[1e23] b0
                    ld        d,$cd                         ;[1e24] 16 cd
                    pop       de                            ;[1e26] d1
                    dec       b                             ;[1e27] 05
                    ld        a,$00                         ;[1e28] 3e 00
                    ld        ($fdad),a                     ;[1e2a] 32 ad fd
                    ld        hl,($5c59)                    ;[1e2d] 2a 59 5c
                    ld        ($fdae),hl                    ;[1e30] 22 ae fd
                    ld        hl,$0000                      ;[1e33] 21 00 00
                    ld        ($fdab),hl                    ;[1e36] 22 ab fd
                    ld        hl,($fdb1)                    ;[1e39] 2a b1 fd
                    inc       hl                            ;[1e3c] 23
                    ld        ($fdb1),hl                    ;[1e3d] 22 b1 fd
                    call      $1f35                         ;[1e40] cd 35 1f
                    ld        c,a                           ;[1e43] 4f
                    ld        a,($fdad)                     ;[1e44] 3a ad fd
                    cp        $00                           ;[1e47] fe 00
                    jr        nz,$1e8c                      ;[1e49] 20 41
                    ld        a,c                           ;[1e4b] 79
                    and       $04                           ;[1e4c] e6 04
                    jr        z,$1e85                       ;[1e4e] 28 35
                    call      $1f81                         ;[1e50] cd 81 1f
                    jr        nc,$1e5c                      ;[1e53] 30 07
                    ld        a,$01                         ;[1e55] 3e 01
                    ld        ($fdad),a                     ;[1e57] 32 ad fd
                    jr        $1e39                         ;[1e5a] 18 dd
                    ld        hl,($fdab)                    ;[1e5c] 2a ab fd
                    ld        a,l                           ;[1e5f] 7d
                    or        h                             ;[1e60] b4
                    jp        nz,$1eb6                      ;[1e61] c2 b6 1e
                    push      bc                            ;[1e64] c5
                    call      $1f65                         ;[1e65] cd 65 1f
                    pop       bc                            ;[1e68] c1
                    ld        a,$00                         ;[1e69] 3e 00
                    ld        ($fdad),a                     ;[1e6b] 32 ad fd
                    ld        a,c                           ;[1e6e] 79
                    and       $01                           ;[1e6f] e6 01
                    jr        nz,$1e4b                      ;[1e71] 20 d8
                    ld        a,b                           ;[1e73] 78
                    call      $1fae                         ;[1e74] cd ae 1f
                    ret       nc                            ;[1e77] d0
                    ld        hl,($fdb1)                    ;[1e78] 2a b1 fd
                    inc       hl                            ;[1e7b] 23
                    ld        ($fdb1),hl                    ;[1e7c] 22 b1 fd
                    call      $1f35                         ;[1e7f] cd 35 1f
                    ld        c,a                           ;[1e82] 4f
                    jr        $1e6e                         ;[1e83] 18 e9
                    ld        a,b                           ;[1e85] 78
                    call      $1fae                         ;[1e86] cd ae 1f
                    ret       nc                            ;[1e89] d0
                    jr        $1e39                         ;[1e8a] 18 ad
                    cp        $01                           ;[1e8c] fe 01
                    jr        nz,$1e85                      ;[1e8e] 20 f5
                    ld        a,c                           ;[1e90] 79
                    and       $01                           ;[1e91] e6 01
                    jr        z,$1e50                       ;[1e93] 28 bb
                    push      bc                            ;[1e95] c5
                    call      $2118                         ;[1e96] cd 18 21
                    pop       bc                            ;[1e99] c1
                    jr        c,$1f15                       ;[1e9a] 38 79
                    ld        hl,($fdab)                    ;[1e9c] 2a ab fd
                    ld        a,h                           ;[1e9f] 7c
                    or        l                             ;[1ea0] b5
                    jr        nz,$1eb6                      ;[1ea1] 20 13
                    ld        a,c                           ;[1ea3] 79
                    and       $02                           ;[1ea4] e6 02
                    jr        z,$1e64                       ;[1ea6] 28 bc
                    call      $1f81                         ;[1ea8] cd 81 1f
                    jr        nc,$1e5c                      ;[1eab] 30 af
                    ld        hl,($fda9)                    ;[1ead] 2a a9 fd
                    dec       hl                            ;[1eb0] 2b
                    ld        ($fdab),hl                    ;[1eb1] 22 ab fd
                    jr        $1e39                         ;[1eb4] 18 83
                    push      bc                            ;[1eb6] c5
                    ld        hl,$fda0                      ;[1eb7] 21 a0 fd
                    ld        de,($fdab)                    ;[1eba] ed 5b ab fd
                    ld        a,d                           ;[1ebe] 7a
                    cp        h                             ;[1ebf] bc
                    jr        nz,$1ec7                      ;[1ec0] 20 05
                    ld        a,e                           ;[1ec2] 7b
                    cp        l                             ;[1ec3] bd
                    jr        nz,$1ec7                      ;[1ec4] 20 01
                    inc       de                            ;[1ec6] 13
                    dec       de                            ;[1ec7] 1b
                    jr        $1ecb                         ;[1ec8] 18 01
                    inc       hl                            ;[1eca] 23
                    ld        a,(hl)                        ;[1ecb] 7e
                    and       $7f                           ;[1ecc] e6 7f
                    push      hl                            ;[1ece] e5
                    push      de                            ;[1ecf] d5
                    call      $1fae                         ;[1ed0] cd ae 1f
                    pop       de                            ;[1ed3] d1
                    pop       hl                            ;[1ed4] e1
                    ld        a,h                           ;[1ed5] 7c
                    cp        d                             ;[1ed6] ba
                    jr        nz,$1eca                      ;[1ed7] 20 f1
                    ld        a,l                           ;[1ed9] 7d
                    cp        e                             ;[1eda] bb
                    jr        nz,$1eca                      ;[1edb] 20 ed
                    ld        de,($fdab)                    ;[1edd] ed 5b ab fd
                    ld        hl,$fda0                      ;[1ee1] 21 a0 fd
                    ld        ($fdab),hl                    ;[1ee4] 22 ab fd
                    ld        bc,($fda9)                    ;[1ee7] ed 4b a9 fd
                    dec       bc                            ;[1eeb] 0b
                    ld        a,d                           ;[1eec] 7a
                    cp        h                             ;[1eed] bc
                    jr        nz,$1f08                      ;[1eee] 20 18
                    ld        a,e                           ;[1ef0] 7b
                    cp        l                             ;[1ef1] bd
                    jr        nz,$1f08                      ;[1ef2] 20 14
                    inc       de                            ;[1ef4] 13
                    push      hl                            ;[1ef5] e5
                    ld        hl,$0000                      ;[1ef6] 21 00 00
                    ld        ($fdab),hl                    ;[1ef9] 22 ab fd
                    pop       hl                            ;[1efc] e1
                    ld        a,b                           ;[1efd] 78
                    cp        h                             ;[1efe] bc
                    jr        nz,$1f08                      ;[1eff] 20 07
                    ld        a,c                           ;[1f01] 79
                    cp        l                             ;[1f02] bd
                    jr        nz,$1f08                      ;[1f03] 20 03
                    pop       bc                            ;[1f05] c1
                    jr        $1f27                         ;[1f06] 18 1f
                    ld        a,(de)                        ;[1f08] 1a
                    ld        (hl),a                        ;[1f09] 77
                    inc       hl                            ;[1f0a] 23
                    inc       de                            ;[1f0b] 13
                    and       $80                           ;[1f0c] e6 80
                    jr        z,$1f08                       ;[1f0e] 28 f8
                    ld        ($fda9),hl                    ;[1f10] 22 a9 fd
                    jr        $1e96                         ;[1f13] 18 81
                    push      bc                            ;[1f15] c5
                    call      $1fae                         ;[1f16] cd ae 1f
                    pop       bc                            ;[1f19] c1
                    ld        hl,$0000                      ;[1f1a] 21 00 00
                    ld        ($fdab),hl                    ;[1f1d] 22 ab fd
                    ld        a,($fdad)                     ;[1f20] 3a ad fd
                    cp        $04                           ;[1f23] fe 04
                    jr        z,$1f2c                       ;[1f25] 28 05
                    ld        a,$00                         ;[1f27] 3e 00
                    ld        ($fdad),a                     ;[1f29] 32 ad fd
                    ld        hl,$fda0                      ;[1f2c] 21 a0 fd
                    ld        ($fda9),hl                    ;[1f2f] 22 a9 fd
                    jp        $1e4b                         ;[1f32] c3 4b 1e
                    call      $0e27                         ;[1f35] cd 27 0e
                    ld        b,a                           ;[1f38] 47
                    cp        $3f                           ;[1f39] fe 3f
                    jr        c,$1f47                       ;[1f3b] 38 0a
                    or        $20                           ;[1f3d] f6 20
                    call      $1f5e                         ;[1f3f] cd 5e 1f
                    jr        c,$1f5b                       ;[1f42] 38 17
                    ld        a,$01                         ;[1f44] 3e 01
                    ret                                     ;[1f46] c9

                    cp        $20                           ;[1f47] fe 20
                    jr        z,$1f58                       ;[1f49] 28 0d
                    cp        $23                           ;[1f4b] fe 23
                    jr        z,$1f55                       ;[1f4d] 28 06
                    jr        c,$1f44                       ;[1f4f] 38 f3
                    cp        $24                           ;[1f51] fe 24
                    jr        nz,$1f44                      ;[1f53] 20 ef
                    ld        a,$02                         ;[1f55] 3e 02
                    ret                                     ;[1f57] c9

                    ld        a,$03                         ;[1f58] 3e 03
                    ret                                     ;[1f5a] c9

                    ld        a,$06                         ;[1f5b] 3e 06
                    ret                                     ;[1f5d] c9

                    cp        $7b                           ;[1f5e] fe 7b
                    ret       nc                            ;[1f60] d0
                    cp        $61                           ;[1f61] fe 61
                    ccf                                     ;[1f63] 3f
                    ret                                     ;[1f64] c9

                    ld        hl,$fda0                      ;[1f65] 21 a0 fd
                    ld        ($fda9),hl                    ;[1f68] 22 a9 fd
                    sub       a                             ;[1f6b] 97
                    ld        ($fdab),a                     ;[1f6c] 32 ab fd
                    ld        ($fdac),a                     ;[1f6f] 32 ac fd
                    ld        a,(hl)                        ;[1f72] 7e
                    and       $7f                           ;[1f73] e6 7f
                    push      hl                            ;[1f75] e5
                    call      $2034                         ;[1f76] cd 34 20
                    pop       hl                            ;[1f79] e1
                    ld        a,(hl)                        ;[1f7a] 7e
                    and       $80                           ;[1f7b] e6 80
                    ret       nz                            ;[1f7d] c0
                    inc       hl                            ;[1f7e] 23
                    jr        $1f72                         ;[1f7f] 18 f1
                    ld        hl,($fda9)                    ;[1f81] 2a a9 fd
                    ld        de,$fda9                      ;[1f84] 11 a9 fd
                    ld        a,d                           ;[1f87] 7a
                    cp        h                             ;[1f88] bc
                    jr        nz,$1f90                      ;[1f89] 20 05
                    ld        a,e                           ;[1f8b] 7b
                    cp        l                             ;[1f8c] bd
                    jp        z,$1fab                       ;[1f8d] ca ab 1f
                    ld        de,$fda0                      ;[1f90] 11 a0 fd
                    ld        a,d                           ;[1f93] 7a
                    cp        h                             ;[1f94] bc
                    jr        nz,$1f9b                      ;[1f95] 20 04
                    ld        a,e                           ;[1f97] 7b
                    cp        l                             ;[1f98] bd
                    jr        z,$1fa1                       ;[1f99] 28 06
                    dec       hl                            ;[1f9b] 2b
                    ld        a,(hl)                        ;[1f9c] 7e
                    and       $7f                           ;[1f9d] e6 7f
                    ld        (hl),a                        ;[1f9f] 77
                    inc       hl                            ;[1fa0] 23
                    ld        a,b                           ;[1fa1] 78
                    or        $80                           ;[1fa2] f6 80
                    ld        (hl),a                        ;[1fa4] 77
                    inc       hl                            ;[1fa5] 23
                    ld        ($fda9),hl                    ;[1fa6] 22 a9 fd
                    scf                                     ;[1fa9] 37
                    ret                                     ;[1faa] c9

                    scf                                     ;[1fab] 37
                    ccf                                     ;[1fac] 3f
                    ret                                     ;[1fad] c9

                    push      af                            ;[1fae] f5
                    ld        a,($fdb5)                     ;[1faf] 3a b5 fd
                    or        a                             ;[1fb2] b7
                    jr        nz,$1fc7                      ;[1fb3] 20 12
                    pop       af                            ;[1fb5] f1
                    cp        $3e                           ;[1fb6] fe 3e
                    jr        z,$1fc2                       ;[1fb8] 28 08
                    cp        $3c                           ;[1fba] fe 3c
                    jr        z,$1fc2                       ;[1fbc] 28 04
                    call      $1ffc                         ;[1fbe] cd fc 1f
                    ret                                     ;[1fc1] c9

                    ld        ($fdb5),a                     ;[1fc2] 32 b5 fd
                    scf                                     ;[1fc5] 37
                    ret                                     ;[1fc6] c9

                    cp        $3c                           ;[1fc7] fe 3c
                    ld        a,$00                         ;[1fc9] 3e 00
                    ld        ($fdb5),a                     ;[1fcb] 32 b5 fd
                    jr        nz,$1fea                      ;[1fce] 20 1a
                    pop       af                            ;[1fd0] f1
                    cp        $3e                           ;[1fd1] fe 3e
                    jr        nz,$1fd9                      ;[1fd3] 20 04
                    ld        a,$c9                         ;[1fd5] 3e c9
                    jr        $1fbe                         ;[1fd7] 18 e5
                    cp        $3d                           ;[1fd9] fe 3d
                    jr        nz,$1fe1                      ;[1fdb] 20 04
                    ld        a,$c7                         ;[1fdd] 3e c7
                    jr        $1fbe                         ;[1fdf] 18 dd
                    push      af                            ;[1fe1] f5
                    ld        a,$3c                         ;[1fe2] 3e 3c
                    call      $1ffc                         ;[1fe4] cd fc 1f
                    pop       af                            ;[1fe7] f1
                    jr        $1fbe                         ;[1fe8] 18 d4
                    pop       af                            ;[1fea] f1
                    cp        $3d                           ;[1feb] fe 3d
                    jr        nz,$1ff3                      ;[1fed] 20 04
                    ld        a,$c8                         ;[1fef] 3e c8
                    jr        $1fbe                         ;[1ff1] 18 cb
                    push      af                            ;[1ff3] f5
                    ld        a,$3e                         ;[1ff4] 3e 3e
                    call      $1ffc                         ;[1ff6] cd fc 1f
                    pop       af                            ;[1ff9] f1
                    jr        $1fbe                         ;[1ffa] 18 c2
                    cp        $0d                           ;[1ffc] fe 0d
                    jr        z,$2020                       ;[1ffe] 28 20
                    cp        $ea                           ;[2000] fe ea
                    ld        b,a                           ;[2002] 47
                    jr        nz,$200c                      ;[2003] 20 07
                    ld        a,$04                         ;[2005] 3e 04
                    ld        ($fdad),a                     ;[2007] 32 ad fd
                    jr        $201a                         ;[200a] 18 0e
                    cp        $22                           ;[200c] fe 22
                    jr        nz,$201a                      ;[200e] 20 0a
                    ld        a,($fdad)                     ;[2010] 3a ad fd
                    and       $fe                           ;[2013] e6 fe
                    xor       $02                           ;[2015] ee 02
                    ld        ($fdad),a                     ;[2017] 32 ad fd
                    ld        a,b                           ;[201a] 78
                    call      $2034                         ;[201b] cd 34 20
                    scf                                     ;[201e] 37
                    ret                                     ;[201f] c9

                    ld        a,($fdb6)                     ;[2020] 3a b6 fd
                    cp        $00                           ;[2023] fe 00
                    jr        z,$2031                       ;[2025] 28 0a
                    ld        bc,($fdb1)                    ;[2027] ed 4b b1 fd
                    ld        hl,($fdb7)                    ;[202b] 2a b7 fd
                    ld        sp,hl                         ;[202e] f9
                    scf                                     ;[202f] 37
                    ret                                     ;[2030] c9

                    scf                                     ;[2031] 37
                    ccf                                     ;[2032] 3f
                    ret                                     ;[2033] c9

                    ld        e,a                           ;[2034] 5f
                    ld        a,($fdb0)                     ;[2035] 3a b0 fd
                    ld        d,a                           ;[2038] 57
                    ld        a,e                           ;[2039] 7b
                    cp        $20                           ;[203a] fe 20
                    jr        nz,$205e                      ;[203c] 20 20
                    ld        a,d                           ;[203e] 7a
                    and       $01                           ;[203f] e6 01
                    jr        nz,$2057                      ;[2041] 20 14
                    ld        a,d                           ;[2043] 7a
                    and       $02                           ;[2044] e6 02
                    jr        nz,$204f                      ;[2046] 20 07
                    ld        a,d                           ;[2048] 7a
                    or        $02                           ;[2049] f6 02
                    ld        ($fdb0),a                     ;[204b] 32 b0 fd
                    ret                                     ;[204e] c9

                    ld        a,e                           ;[204f] 7b
                    call      $2093                         ;[2050] cd 93 20
                    ld        a,($fdb0)                     ;[2053] 3a b0 fd
                    ret                                     ;[2056] c9

                    ld        a,d                           ;[2057] 7a
                    and       $fe                           ;[2058] e6 fe
                    ld        ($fdb0),a                     ;[205a] 32 b0 fd
                    ret                                     ;[205d] c9

                    cp        $a3                           ;[205e] fe a3
                    jr        nc,$2086                      ;[2060] 30 24
                    ld        a,d                           ;[2062] 7a
                    and       $02                           ;[2063] e6 02
                    jr        nz,$2072                      ;[2065] 20 0b
                    ld        a,d                           ;[2067] 7a
                    and       $fe                           ;[2068] e6 fe
                    ld        ($fdb0),a                     ;[206a] 32 b0 fd
                    ld        a,e                           ;[206d] 7b
                    call      $2093                         ;[206e] cd 93 20
                    ret                                     ;[2071] c9

                    push      de                            ;[2072] d5
                    ld        a,$20                         ;[2073] 3e 20
                    call      $2093                         ;[2075] cd 93 20
                    pop       de                            ;[2078] d1
                    ld        a,d                           ;[2079] 7a
                    and       $fe                           ;[207a] e6 fe
                    and       $fd                           ;[207c] e6 fd
                    ld        ($fdb0),a                     ;[207e] 32 b0 fd
                    ld        a,e                           ;[2081] 7b
                    call      $2093                         ;[2082] cd 93 20
                    ret                                     ;[2085] c9

                    ld        a,d                           ;[2086] 7a
                    and       $fd                           ;[2087] e6 fd
                    or        $01                           ;[2089] f6 01
                    ld        ($fdb0),a                     ;[208b] 32 b0 fd
                    ld        a,e                           ;[208e] 7b
                    call      $2093                         ;[208f] cd 93 20
                    ret                                     ;[2092] c9

                    ld        hl,($fdb3)                    ;[2093] 2a b3 fd
                    inc       hl                            ;[2096] 23
                    ld        ($fdb3),hl                    ;[2097] 22 b3 fd
                    ld        hl,($fdae)                    ;[209a] 2a ae fd
                    ld        b,a                           ;[209d] 47
                    ld        a,($fdb6)                     ;[209e] 3a b6 fd
                    cp        $00                           ;[20a1] fe 00
                    ld        a,b                           ;[20a3] 78
                    jr        z,$20cb                       ;[20a4] 28 25
                    ld        de,($5c5f)                    ;[20a6] ed 5b 5f 5c
                    ld        a,h                           ;[20aa] 7c
                    cp        d                             ;[20ab] ba
                    jr        nz,$20c8                      ;[20ac] 20 1a
                    ld        a,l                           ;[20ae] 7d
                    cp        e                             ;[20af] bb
                    jr        nz,$20c8                      ;[20b0] 20 16
                    ld        bc,($fdb1)                    ;[20b2] ed 4b b1 fd
                    ld        hl,($fdb3)                    ;[20b6] 2a b3 fd
                    and       a                             ;[20b9] a7
                    sbc       hl,bc                         ;[20ba] ed 42
                    jr        nc,$20c2                      ;[20bc] 30 04
                    ld        bc,($fdb3)                    ;[20be] ed 4b b3 fd
                    ld        hl,($fdb7)                    ;[20c2] 2a b7 fd
                    ld        sp,hl                         ;[20c5] f9
                    scf                                     ;[20c6] 37
                    ret                                     ;[20c7] c9

                    scf                                     ;[20c8] 37
                    jr        $20cd                         ;[20c9] 18 02
                    scf                                     ;[20cb] 37
                    ccf                                     ;[20cc] 3f
                    call      $05ac                         ;[20cd] cd ac 05
                    jr        nc,$20df                      ;[20d0] 30 0d
                    ld        a,(hl)                        ;[20d2] 7e
                    ex        de,hl                         ;[20d3] eb
                    cp        $0e                           ;[20d4] fe 0e
                    jr        nz,$20f5                      ;[20d6] 20 1d
                    inc       de                            ;[20d8] 13
                    inc       de                            ;[20d9] 13
                    inc       de                            ;[20da] 13
                    inc       de                            ;[20db] 13
                    inc       de                            ;[20dc] 13
                    jr        $20f5                         ;[20dd] 18 16
                    push      af                            ;[20df] f5
                    ld        bc,$0001                      ;[20e0] 01 01 00
                    push      hl                            ;[20e3] e5
                    push      de                            ;[20e4] d5
                    call      $20fe                         ;[20e5] cd fe 20
                    pop       de                            ;[20e8] d1
                    pop       hl                            ;[20e9] e1
                    rst       $28                           ;[20ea] ef
                    ld        h,h                           ;[20eb] 64
                    ld        d,$2a                         ;[20ec] 16 2a
                    ld        h,l                           ;[20ee] 65
                    ld        e,h                           ;[20ef] 5c
                    ex        de,hl                         ;[20f0] eb
                    lddr                                    ;[20f1] ed b8
                    pop       af                            ;[20f3] f1
                    ld        (de),a                        ;[20f4] 12
                    inc       de                            ;[20f5] 13
                    call      $05d1                         ;[20f6] cd d1 05
                    ld        ($fdae),de                    ;[20f9] ed 53 ae fd
                    ret                                     ;[20fd] c9

                    ld        hl,($5c65)                    ;[20fe] 2a 65 5c
                    add       hl,bc                         ;[2101] 09
                    jr        c,$210e                       ;[2102] 38 0a
                    ex        de,hl                         ;[2104] eb
                    ld        hl,$0082                      ;[2105] 21 82 00
                    add       hl,de                         ;[2108] 19
                    jr        c,$210e                       ;[2109] 38 03
                    sbc       hl,sp                         ;[210b] ed 72
                    ret       c                             ;[210d] d8
                    ld        a,$03                         ;[210e] 3e 03
                    ld        ($5c3a),a                     ;[2110] 32 3a 5c
                    call      $3e80                         ;[2113] cd 80 3e
                    add       hl,sp                         ;[2116] 39
                    daa                                     ;[2117] 27
                    call      $1432                         ;[2118] cd 32 14
                    call      $fd43                         ;[211b] cd 43 fd
                    ret       c                             ;[211e] d8
                    ld        b,$f9                         ;[211f] 06 f9
                    ld        de,$fda0                      ;[2121] 11 a0 fd
                    ld        hl,$1693                      ;[2124] 21 93 16
                    call      $fd5c                         ;[2127] cd 5c fd
                    ret       nc                            ;[212a] d0
                    cp        $ff                           ;[212b] fe ff
                    jr        nz,$2133                      ;[212d] 20 04
                    ld        a,$d4                         ;[212f] 3e d4
                    jr        $2155                         ;[2131] 18 22
                    cp        $fe                           ;[2133] fe fe
                    jr        nz,$213b                      ;[2135] 20 04
                    ld        a,$d3                         ;[2137] 3e d3
                    jr        $2155                         ;[2139] 18 1a
                    cp        $fd                           ;[213b] fe fd
                    jr        nz,$2143                      ;[213d] 20 04
                    ld        a,$ce                         ;[213f] 3e ce
                    jr        $2155                         ;[2141] 18 12
                    cp        $fc                           ;[2143] fe fc
                    jr        nz,$214b                      ;[2145] 20 04
                    ld        a,$ed                         ;[2147] 3e ed
                    jr        $2155                         ;[2149] 18 0a
                    cp        $fb                           ;[214b] fe fb
                    jr        nz,$2153                      ;[214d] 20 04
                    ld        a,$ec                         ;[214f] 3e ec
                    jr        $2155                         ;[2151] 18 02
                    sub       $56                           ;[2153] d6 56
                    scf                                     ;[2155] 37
                    ret                                     ;[2156] c9

                    ld        b,(hl)                        ;[2157] 46
                    inc       hl                            ;[2158] 23
                    ld        a,(hl)                        ;[2159] 7e
                    ld        (de),a                        ;[215a] 12
                    inc       de                            ;[215b] 13
                    inc       hl                            ;[215c] 23
                    djnz      $2159                         ;[215d] 10 fa
                    ret                                     ;[215f] c9

                    cp        $30                           ;[2160] fe 30
                    ccf                                     ;[2162] 3f
                    ret       nc                            ;[2163] d0
                    cp        $3a                           ;[2164] fe 3a
                    ret       nc                            ;[2166] d0
                    sub       $30                           ;[2167] d6 30
                    scf                                     ;[2169] 37
                    ret                                     ;[216a] c9

                    push      bc                            ;[216b] c5
                    push      de                            ;[216c] d5
                    ld        b,(hl)                        ;[216d] 46
                    inc       hl                            ;[216e] 23
                    cp        (hl)                          ;[216f] be
                    inc       hl                            ;[2170] 23
                    ld        e,(hl)                        ;[2171] 5e
                    inc       hl                            ;[2172] 23
                    ld        d,(hl)                        ;[2173] 56
                    jr        z,$217e                       ;[2174] 28 08
                    inc       hl                            ;[2176] 23
                    djnz      $216f                         ;[2177] 10 f6
                    scf                                     ;[2179] 37
                    ccf                                     ;[217a] 3f
                    pop       de                            ;[217b] d1
                    pop       bc                            ;[217c] c1
                    ret                                     ;[217d] c9

                    ex        de,hl                         ;[217e] eb
                    pop       de                            ;[217f] d1
                    pop       bc                            ;[2180] c1
                    call      $218b                         ;[2181] cd 8b 21
                    jr        c,$2188                       ;[2184] 38 02
                    cp        a                             ;[2186] bf
                    ret                                     ;[2187] c9

                    cp        a                             ;[2188] bf
                    scf                                     ;[2189] 37
                    ret                                     ;[218a] c9

                    jp        (hl)                          ;[218b] e9
                    jr        z,$2191                       ;[218c] 28 03
                    ld        de,$0000                      ;[218e] 11 00 00
                    push      de                            ;[2191] d5
                    push      hl                            ;[2192] e5
                    ld        a,$fd                         ;[2193] 3e fd
                    rst       $28                           ;[2195] ef
                    ld        bc,$e116                      ;[2196] 01 16 e1
                    push      hl                            ;[2199] e5
                    ld        b,$20                         ;[219a] 06 20
                    ld        a,(hl)                        ;[219c] 7e
                    cp        $20                           ;[219d] fe 20
                    jr        nz,$21a3                      ;[219f] 20 02
                    ld        d,h                           ;[21a1] 54
                    ld        e,l                           ;[21a2] 5d
                    cp        $ff                           ;[21a3] fe ff
                    jr        z,$21b0                       ;[21a5] 28 09
                    inc       hl                            ;[21a7] 23
                    djnz      $219c                         ;[21a8] 10 f2
                    ex        de,hl                         ;[21aa] eb
                    ld        a,$0d                         ;[21ab] 3e 0d
                    ld        (hl),a                        ;[21ad] 77
                    jr        $219a                         ;[21ae] 18 ea
                    ld        a,$16                         ;[21b0] 3e 16
                    rst       $10                           ;[21b2] d7
                    ld        a,$00                         ;[21b3] 3e 00
                    rst       $10                           ;[21b5] d7
                    ld        a,$00                         ;[21b6] 3e 00
                    rst       $10                           ;[21b8] d7
                    pop       hl                            ;[21b9] e1
                    ld        a,(hl)                        ;[21ba] 7e
                    cp        $ff                           ;[21bb] fe ff
                    jr        z,$21c3                       ;[21bd] 28 04
                    rst       $10                           ;[21bf] d7
                    inc       hl                            ;[21c0] 23
                    jr        $21ba                         ;[21c1] 18 f7
                    call      $1876                         ;[21c3] cd 76 18
                    ld        b,a                           ;[21c6] 47
                    pop       hl                            ;[21c7] e1
                    ld        a,h                           ;[21c8] 7c
                    or        l                             ;[21c9] b5
                    push      hl                            ;[21ca] e5
                    jr        z,$21d8                       ;[21cb] 28 0b
                    ld        a,(hl)                        ;[21cd] 7e
                    cp        b                             ;[21ce] b8
                    jr        z,$21d8                       ;[21cf] 28 07
                    inc       hl                            ;[21d1] 23
                    cp        $ff                           ;[21d2] fe ff
                    jr        nz,$21cd                      ;[21d4] 20 f7
                    jr        $21c3                         ;[21d6] 18 eb
                    push      af                            ;[21d8] f5
                    rst       $28                           ;[21d9] ef
                    ld        l,(hl)                        ;[21da] 6e
                    dec       c                             ;[21db] 0d
                    ld        a,$fe                         ;[21dc] 3e fe
                    rst       $28                           ;[21de] ef
                    ld        bc,$f116                      ;[21df] 01 16 f1
                    pop       hl                            ;[21e2] e1
                    ret                                     ;[21e3] c9

                    di                                      ;[21e4] f3
                    ld        ix,$ffff                      ;[21e5] dd 21 ff ff
                    ld        a,$07                         ;[21e9] 3e 07
                    out       ($fe),a                       ;[21eb] d3 fe
                    ld        sp,$7fff                      ;[21ed] 31 ff 7f
                    call      $2904                         ;[21f0] cd 04 29
                    di                                      ;[21f3] f3
                    ld        a,$07                         ;[21f4] 3e 07
                    out       ($fe),a                       ;[21f6] d3 fe
                    call      $2727                         ;[21f8] cd 27 27
                    ld        bc,$0700                      ;[21fb] 01 00 07
                    call      $2718                         ;[21fe] cd 18 27
                    ld        hl,$326e                      ;[2201] 21 6e 32
                    call      $2710                         ;[2204] cd 10 27
                    call      $26a7                         ;[2207] cd a7 26
                    ld        a,$04                         ;[220a] 3e 04
                    out       ($fe),a                       ;[220c] d3 fe
                    ld        de,$0002                      ;[220e] 11 02 00
                    ld        a,$00                         ;[2211] 3e 00
                    ld        bc,$7ffd                      ;[2213] 01 fd 7f
                    out       (c),a                         ;[2216] ed 79
                    ex        af,af'                        ;[2218] 08
                    ld        hl,$c000                      ;[2219] 21 00 c0
                    ld        (hl),d                        ;[221c] 72
                    inc       hl                            ;[221d] 23
                    ld        a,l                           ;[221e] 7d
                    or        h                             ;[221f] b4
                    jr        nz,$221c                      ;[2220] 20 fa
                    ex        af,af'                        ;[2222] 08
                    inc       a                             ;[2223] 3c
                    cp        $08                           ;[2224] fe 08
                    jr        nz,$2216                      ;[2226] 20 ee
                    ld        a,$00                         ;[2228] 3e 00
                    out       (c),a                         ;[222a] ed 79
                    ex        af,af'                        ;[222c] 08
                    ld        hl,$c000                      ;[222d] 21 00 c0
                    ld        a,(hl)                        ;[2230] 7e
                    cp        d                             ;[2231] ba
                    jr        nz,$2267                      ;[2232] 20 33
                    cpl                                     ;[2234] 2f
                    ld        (hl),a                        ;[2235] 77
                    inc       hl                            ;[2236] 23
                    ld        a,l                           ;[2237] 7d
                    or        h                             ;[2238] b4
                    jr        nz,$2230                      ;[2239] 20 f5
                    ex        af,af'                        ;[223b] 08
                    inc       a                             ;[223c] 3c
                    cp        $08                           ;[223d] fe 08
                    jr        nz,$222a                      ;[223f] 20 e9
                    dec       a                             ;[2241] 3d
                    ex        af,af'                        ;[2242] 08
                    ld        hl,$0000                      ;[2243] 21 00 00
                    dec       hl                            ;[2246] 2b
                    ld        a,h                           ;[2247] 7c
                    cp        $bf                           ;[2248] fe bf
                    jr        z,$2254                       ;[224a] 28 08
                    ld        a,d                           ;[224c] 7a
                    cpl                                     ;[224d] 2f
                    cp        (hl)                          ;[224e] be
                    jr        nz,$2267                      ;[224f] 20 16
                    ld        (hl),d                        ;[2251] 72
                    jr        $2246                         ;[2252] 18 f2
                    ex        af,af'                        ;[2254] 08
                    cp        $00                           ;[2255] fe 00
                    jr        z,$225e                       ;[2257] 28 05
                    dec       a                             ;[2259] 3d
                    out       (c),a                         ;[225a] ed 79
                    jr        $2242                         ;[225c] 18 e4
                    dec       e                             ;[225e] 1d
                    jp        z,$232e                       ;[225f] ca 2e 23
                    ld        d,$ff                         ;[2262] 16 ff
                    jp        $2211                         ;[2264] c3 11 22
                    ex        af,af'                        ;[2267] 08
                    push      af                            ;[2268] f5
                    push      hl                            ;[2269] e5
                    call      $267c                         ;[226a] cd 7c 26
                    xor       a                             ;[226d] af
                    ld        bc,$7ffd                      ;[226e] 01 fd 7f
                    ld        ($5b5c),a                     ;[2271] 32 5c 5b
                    out       (c),a                         ;[2274] ed 79
                    ld        bc,$0700                      ;[2276] 01 00 07
                    call      $2718                         ;[2279] cd 18 27
                    call      $2727                         ;[227c] cd 27 27
                    ld        hl,$22bf                      ;[227f] 21 bf 22
                    call      $2710                         ;[2282] cd 10 27
                    pop       hl                            ;[2285] e1
                    ld        a,h                           ;[2286] 7c
                    call      $229e                         ;[2287] cd 9e 22
                    ld        a,l                           ;[228a] 7d
                    call      $229e                         ;[228b] cd 9e 22
                    exx                                     ;[228e] d9
                    ld        hl,$2303                      ;[228f] 21 03 23
                    call      $2710                         ;[2292] cd 10 27
                    pop       af                            ;[2295] f1
                    and       $07                           ;[2296] e6 07
                    exx                                     ;[2298] d9
                    call      $229e                         ;[2299] cd 9e 22
                    di                                      ;[229c] f3
                    halt                                    ;[229d] 76
                    push      hl                            ;[229e] e5
                    push      af                            ;[229f] f5
                    push      af                            ;[22a0] f5
                    srl       a                             ;[22a1] cb 3f
                    srl       a                             ;[22a3] cb 3f
                    srl       a                             ;[22a5] cb 3f
                    srl       a                             ;[22a7] cb 3f
                    ld        b,$02                         ;[22a9] 06 02
                    exx                                     ;[22ab] d9
                    ld        d,$00                         ;[22ac] 16 00
                    ld        e,a                           ;[22ae] 5f
                    ld        hl,$230b                      ;[22af] 21 0b 23
                    add       hl,de                         ;[22b2] 19
                    ld        a,(hl)                        ;[22b3] 7e
                    call      $2723                         ;[22b4] cd 23 27
                    pop       af                            ;[22b7] f1
                    and       $0f                           ;[22b8] e6 0f
                    exx                                     ;[22ba] d9
                    djnz      $22ab                         ;[22bb] 10 ee
                    pop       hl                            ;[22bd] e1
                    ret                                     ;[22be] c9

                    ld        d,$0a                         ;[22bf] 16 0a
                    nop                                     ;[22c1] 00
                    ld        d,d                           ;[22c2] 52
                    ld        b,c                           ;[22c3] 41
                    ld        c,l                           ;[22c4] 4d
                    jr        nz,$232d                      ;[22c5] 20 66
                    ld        h,c                           ;[22c7] 61
                    ld        l,c                           ;[22c8] 69
                    ld        l,h                           ;[22c9] 6c
                    ld        a,($6120)                     ;[22ca] 3a 20 61
                    ld        h,h                           ;[22cd] 64
                    ld        h,h                           ;[22ce] 64
                    ld        (hl),d                        ;[22cf] 72
                    ld        h,l                           ;[22d0] 65
                    ld        (hl),e                        ;[22d1] 73
                    ld        (hl),e                        ;[22d2] 73
                    jr        nz,$22d4                      ;[22d3] 20 ff
                    ld        a,$00                         ;[22d5] 3e 00
                    out       ($fe),a                       ;[22d7] d3 fe
                    ld        hl,$4000                      ;[22d9] 21 00 40
                    ld        de,$4001                      ;[22dc] 11 01 40
                    ld        bc,$1800                      ;[22df] 01 00 18
                    ld        (hl),$00                      ;[22e2] 36 00
                    ldir                                    ;[22e4] ed b0
                    ld        hl,$5800                      ;[22e6] 21 00 58
                    ld        bc,$0300                      ;[22e9] 01 00 03
                    in        a,($fe)                       ;[22ec] db fe
                    and       $40                           ;[22ee] e6 40
                    or        c                             ;[22f0] b1
                    ld        c,a                           ;[22f1] 4f
                    rr        c                             ;[22f2] cb 19
                    inc       ix                            ;[22f4] dd 23
                    dec       ix                            ;[22f6] dd 2b
                    djnz      $22ec                         ;[22f8] 10 f2
                    ld        (hl),c                        ;[22fa] 71
                    inc       hl                            ;[22fb] 23
                    ld        a,h                           ;[22fc] 7c
                    cp        $5b                           ;[22fd] fe 5b
                    jr        nz,$22e9                      ;[22ff] 20 e8
                    jr        $22e6                         ;[2301] 18 e3
                    inc       l                             ;[2303] 2c
                    jr        nz,$2376                      ;[2304] 20 70
                    ld        h,c                           ;[2306] 61
                    ld        h,a                           ;[2307] 67
                    ld        h,l                           ;[2308] 65
                    jr        nz,$230a                      ;[2309] 20 ff
                    jr        nc,$233e                      ;[230b] 30 31
                    ld        ($3433),a                     ;[230d] 32 33 34
                    dec       (hl)                          ;[2310] 35
                    ld        (hl),$37                      ;[2311] 36 37
                    jr        c,$234e                       ;[2313] 38 39
                    ld        b,c                           ;[2315] 41
                    ld        b,d                           ;[2316] 42
                    ld        b,e                           ;[2317] 43
                    ld        b,h                           ;[2318] 44
                    ld        b,l                           ;[2319] 45
                    ld        b,(hl)                        ;[231a] 46
                    ld        d,$0a                         ;[231b] 16 0a
                    nop                                     ;[231d] 00
                    ld        d,d                           ;[231e] 52
                    ld        b,c                           ;[231f] 41
                    ld        c,l                           ;[2320] 4d
                    jr        nz,$2397                      ;[2321] 20 74
                    ld        h,l                           ;[2323] 65
                    ld        (hl),e                        ;[2324] 73
                    ld        (hl),h                        ;[2325] 74
                    jr        nz,$2398                      ;[2326] 20 70
                    ld        h,c                           ;[2328] 61
                    ld        (hl),e                        ;[2329] 73
                    ld        (hl),e                        ;[232a] 73
                    ld        h,l                           ;[232b] 65
                    ld        h,h                           ;[232c] 64
                    rst       $38                           ;[232d] ff
                    ld        a,$00                         ;[232e] 3e 00
                    ld        hl,$cafe                      ;[2330] 21 fe ca
                    ld        bc,$7ffd                      ;[2333] 01 fd 7f
                    out       (c),a                         ;[2336] ed 79
                    ld        (hl),a                        ;[2338] 77
                    inc       a                             ;[2339] 3c
                    cp        $08                           ;[233a] fe 08
                    jr        nz,$2336                      ;[233c] 20 f8
                    dec       a                             ;[233e] 3d
                    out       (c),a                         ;[233f] ed 79
                    cp        (hl)                          ;[2341] be
                    jp        nz,$245c                      ;[2342] c2 5c 24
                    and       a                             ;[2345] a7
                    jr        nz,$233e                      ;[2346] 20 f6
                    di                                      ;[2348] f3
                    ld        bc,$7ffd                      ;[2349] 01 fd 7f
                    ld        a,$00                         ;[234c] 3e 00
                    out       (c),a                         ;[234e] ed 79
                    ld        ($e000),a                     ;[2350] 32 00 e0
                    inc       a                             ;[2353] 3c
                    cp        $08                           ;[2354] fe 08
                    jr        nz,$234e                      ;[2356] 20 f6
                    dec       a                             ;[2358] 3d
                    call      $2364                         ;[2359] cd 64 23
                    ld        a,$03                         ;[235c] 3e 03
                    call      $2364                         ;[235e] cd 64 23
                    jp        $d000                         ;[2361] c3 00 d0
                    ld        bc,$7ffd                      ;[2364] 01 fd 7f
                    out       (c),a                         ;[2367] ed 79
                    ld        hl,$2375                      ;[2369] 21 75 23
                    ld        de,$d000                      ;[236c] 11 00 d0
                    ld        bc,$00b9                      ;[236f] 01 b9 00
                    ldir                                    ;[2372] ed b0
                    ret                                     ;[2374] c9

                    ld        a,$01                         ;[2375] 3e 01
                    ld        de,$d0a9                      ;[2377] 11 a9 d0
                    ld        hl,$2000                      ;[237a] 21 00 20
                    ld        bc,$1ffd                      ;[237d] 01 fd 1f
                    out       (c),a                         ;[2380] ed 79
                    ex        af,af'                        ;[2382] 08
                    ld        bc,$4000                      ;[2383] 01 00 40
                    ld        a,(de)                        ;[2386] 1a
                    inc       de                            ;[2387] 13
                    cp        (hl)                          ;[2388] be
                    jr        nz,$2399                      ;[2389] 20 0e
                    add       hl,bc                         ;[238b] 09
                    jr        nc,$2386                      ;[238c] 30 f8
                    ex        af,af'                        ;[238e] 08
                    add       $02                           ;[238f] c6 02
                    bit       3,a                           ;[2391] cb 5f
                    jr        z,$237d                       ;[2393] 28 e8
                    ld        d,$01                         ;[2395] 16 01
                    jr        $239c                         ;[2397] 18 03
                    ex        af,af'                        ;[2399] 08
                    ld        d,$00                         ;[239a] 16 00
                    exx                                     ;[239c] d9
                    ld        bc,$7ffd                      ;[239d] 01 fd 7f
                    ld        a,$03                         ;[23a0] 3e 03
                    out       (c),a                         ;[23a2] ed 79
                    ld        b,$1f                         ;[23a4] 06 1f
                    xor       a                             ;[23a6] af
                    out       (c),a                         ;[23a7] ed 79
                    call      $d09b                         ;[23a9] cd 9b d0
                    jr        nz,$23f7                      ;[23ac] 20 49
                    ld        bc,$7ffd                      ;[23ae] 01 fd 7f
                    ld        a,$13                         ;[23b1] 3e 13
                    out       (c),a                         ;[23b3] ed 79
                    call      $d09b                         ;[23b5] cd 9b d0
                    jr        nz,$23f7                      ;[23b8] 20 3d
                    scf                                     ;[23ba] 37
                    call      $d08e                         ;[23bb] cd 8e d0
                    ld        bc,$1ffd                      ;[23be] 01 fd 1f
                    ld        a,$04                         ;[23c1] 3e 04
                    out       (c),a                         ;[23c3] ed 79
                    ld        bc,$7ffd                      ;[23c5] 01 fd 7f
                    ld        a,$03                         ;[23c8] 3e 03
                    out       (c),a                         ;[23ca] ed 79
                    call      $d09b                         ;[23cc] cd 9b d0
                    jr        nz,$23fd                      ;[23cf] 20 2c
                    ld        a,$0b                         ;[23d1] 3e 0b
                    ld        bc,$7ffd                      ;[23d3] 01 fd 7f
                    out       (c),a                         ;[23d6] ed 79
                    call      $d09b                         ;[23d8] cd 9b d0
                    jr        nz,$23fd                      ;[23db] 20 20
                    scf                                     ;[23dd] 37
                    call      $d08e                         ;[23de] cd 8e d0
                    ld        a,$03                         ;[23e1] 3e 03
                    ld        bc,$7ffd                      ;[23e3] 01 fd 7f
                    out       (c),a                         ;[23e6] ed 79
                    ld        a,$00                         ;[23e8] 3e 00
                    ld        b,$1f                         ;[23ea] 06 1f
                    out       (c),a                         ;[23ec] ed 79
                    ld        ($5b5c),a                     ;[23ee] 32 5c 5b
                    ld        ($5b67),a                     ;[23f1] 32 67 5b
                    jp        $243b                         ;[23f4] c3 3b 24
                    xor       a                             ;[23f7] af
                    call      $d08e                         ;[23f8] cd 8e d0
                    jr        $23be                         ;[23fb] 18 c1
                    xor       a                             ;[23fd] af
                    call      $d08e                         ;[23fe] cd 8e d0
                    jr        $23e1                         ;[2401] 18 de
                    push      de                            ;[2403] d5
                    push      ix                            ;[2404] dd e5
                    pop       de                            ;[2406] d1
                    rl        e                             ;[2407] cb 13
                    rl        d                             ;[2409] cb 12
                    push      de                            ;[240b] d5
                    pop       ix                            ;[240c] dd e1
                    pop       de                            ;[240e] d1
                    ret                                     ;[240f] c9

                    xor       a                             ;[2410] af
                    ld        h,a                           ;[2411] 67
                    ld        l,a                           ;[2412] 6f
                    add       (hl)                          ;[2413] 86
                    inc       hl                            ;[2414] 23
                    ld        d,a                           ;[2415] 57
                    ld        a,h                           ;[2416] 7c
                    cp        $40                           ;[2417] fe 40
                    ld        a,d                           ;[2419] 7a
                    jr        nz,$2413                      ;[241a] 20 f7
                    and       a                             ;[241c] a7
                    ret                                     ;[241d] c9

                    nop                                     ;[241e] 00
                    ld        bc,$0302                      ;[241f] 01 02 03
                    inc       b                             ;[2422] 04
                    dec       b                             ;[2423] 05
                    ld        b,$07                         ;[2424] 06 07
                    inc       b                             ;[2426] 04
                    dec       b                             ;[2427] 05
                    ld        b,$03                         ;[2428] 06 03
                    inc       b                             ;[242a] 04
                    rlca                                    ;[242b] 07
                    ld        b,$03                         ;[242c] 06 03
                    push      de                            ;[242e] d5
                    push      ix                            ;[242f] dd e5
                    pop       de                            ;[2431] d1
                    rl        e                             ;[2432] cb 13
                    rl        d                             ;[2434] cb 12
                    push      de                            ;[2436] d5
                    pop       ix                            ;[2437] dd e1
                    pop       de                            ;[2439] d1
                    ret                                     ;[243a] c9

                    ld        bc,$7ffd                      ;[243b] 01 fd 7f
                    ld        a,$00                         ;[243e] 3e 00
                    out       (c),a                         ;[2440] ed 79
                    ld        sp,$7fff                      ;[2442] 31 ff 7f
                    exx                                     ;[2445] d9
                    xor       a                             ;[2446] af
                    cp        d                             ;[2447] ba
                    call      $242e                         ;[2448] cd 2e 24
                    call      $267c                         ;[244b] cd 7c 26
                    call      $2727                         ;[244e] cd 27 27
                    ld        hl,$231b                      ;[2451] 21 1b 23
                    call      $2710                         ;[2454] cd 10 27
                    call      $26a7                         ;[2457] cd a7 26
                    jr        $2466                         ;[245a] 18 0a
                    call      $267c                         ;[245c] cd 7c 26
                    ex        af,af'                        ;[245f] 08
                    ld        hl,$0000                      ;[2460] 21 00 00
                    jp        $2267                         ;[2463] c3 67 22
                    call      $2ca6                         ;[2466] cd a6 2c
                    call      $28c3                         ;[2469] cd c3 28
                    call      $296a                         ;[246c] cd 6a 29
                    call      $275a                         ;[246f] cd 5a 27
                    call      $2c2c                         ;[2472] cd 2c 2c
                    call      $2c7b                         ;[2475] cd 7b 2c
                    call      $29dd                         ;[2478] cd dd 29
                    call      $2b31                         ;[247b] cd 31 2b
                    call      $2a58                         ;[247e] cd 58 2a
                    call      $248d                         ;[2481] cd 8d 24
                    call      $3565                         ;[2484] cd 65 35
                    call      $35b0                         ;[2487] cd b0 35
                    jp        $2b9e                         ;[248a] c3 9e 2b
                    call      $2727                         ;[248d] cd 27 27
                    ld        hl,$25e2                      ;[2490] 21 e2 25
                    call      $2710                         ;[2493] cd 10 27
                    ld        hl,$2600                      ;[2496] 21 00 26
                    call      $2710                         ;[2499] cd 10 27
                    ld        hl,$2620                      ;[249c] 21 20 26
                    call      $2710                         ;[249f] cd 10 27
                    ld        hl,$262b                      ;[24a2] 21 2b 26
                    call      $2710                         ;[24a5] cd 10 27
                    call      $254a                         ;[24a8] cd 4a 25
                    ld        bc,$0ffd                      ;[24ab] 01 fd 0f
                    in        a,(c)                         ;[24ae] ed 78
                    bit       0,a                           ;[24b0] cb 47
                    jr        z,$24c9                       ;[24b2] 28 15
                    ld        hl,$2615                      ;[24b4] 21 15 26
                    call      $2710                         ;[24b7] cd 10 27
                    call      $254a                         ;[24ba] cd 4a 25
                    ld        bc,$0ffd                      ;[24bd] 01 fd 0f
                    in        a,(c)                         ;[24c0] ed 78
                    bit       0,a                           ;[24c2] cb 47
                    jr        nz,$24c9                      ;[24c4] 20 03
                    scf                                     ;[24c6] 37
                    jr        $24cb                         ;[24c7] 18 02
                    scf                                     ;[24c9] 37
                    ccf                                     ;[24ca] 3f
                    call      $242e                         ;[24cb] cd 2e 24
                    call      $2727                         ;[24ce] cd 27 27
                    ld        hl,$5b67                      ;[24d1] 21 67 5b
                    set       4,(hl)                        ;[24d4] cb e6
                    ld        hl,$2556                      ;[24d6] 21 56 25
                    call      $2710                         ;[24d9] cd 10 27
                    ld        hl,$256b                      ;[24dc] 21 6b 25
                    call      $2710                         ;[24df] cd 10 27
                    ld        e,$00                         ;[24e2] 1e 00
                    ld        b,$03                         ;[24e4] 06 03
                    dec       b                             ;[24e6] 05
                    jr        z,$253d                       ;[24e7] 28 54
                    ld        a,$20                         ;[24e9] 3e 20
                    push      bc                            ;[24eb] c5
                    push      af                            ;[24ec] f5
                    call      $2500                         ;[24ed] cd 00 25
                    pop       af                            ;[24f0] f1
                    pop       bc                            ;[24f1] c1
                    inc       a                             ;[24f2] 3c
                    cp        $80                           ;[24f3] fe 80
                    jr        z,$24fc                       ;[24f5] 28 05
                    or        a                             ;[24f7] b7
                    jr        z,$24e6                       ;[24f8] 28 ec
                    jr        $24eb                         ;[24fa] 18 ef
                    ld        a,$a0                         ;[24fc] 3e a0
                    jr        $24eb                         ;[24fe] 18 eb
                    call      $2513                         ;[2500] cd 13 25
                    push      af                            ;[2503] f5
                    inc       e                             ;[2504] 1c
                    ld        a,e                           ;[2505] 7b
                    cp        $48                           ;[2506] fe 48
                    jr        nz,$2511                      ;[2508] 20 07
                    ld        e,$00                         ;[250a] 1e 00
                    ld        a,$0d                         ;[250c] 3e 0d
                    call      $2513                         ;[250e] cd 13 25
                    pop       af                            ;[2511] f1
                    ret                                     ;[2512] c9

                    push      af                            ;[2513] f5
                    ld        a,$fb                         ;[2514] 3e fb
                    in        a,($fe)                       ;[2516] db fe
                    rra                                     ;[2518] 1f
                    jr        nc,$2538                      ;[2519] 30 1d
                    ld        bc,$0ffd                      ;[251b] 01 fd 0f
                    in        a,(c)                         ;[251e] ed 78
                    bit       0,a                           ;[2520] cb 47
                    jr        nz,$2514                      ;[2522] 20 f0
                    pop       af                            ;[2524] f1
                    out       (c),a                         ;[2525] ed 79
                    di                                      ;[2527] f3
                    ld        a,($5b67)                     ;[2528] 3a 67 5b
                    ld        bc,$1ffd                      ;[252b] 01 fd 1f
                    xor       $10                           ;[252e] ee 10
                    out       (c),a                         ;[2530] ed 79
                    bit       4,a                           ;[2532] cb 67
                    jr        z,$252b                       ;[2534] 28 f5
                    ei                                      ;[2536] fb
                    ret                                     ;[2537] c9

                    pop       af                            ;[2538] f1
                    pop       af                            ;[2539] f1
                    pop       af                            ;[253a] f1
                    pop       af                            ;[253b] f1
                    pop       af                            ;[253c] f1
                    ld        hl,$25a4                      ;[253d] 21 a4 25
                    call      $2710                         ;[2540] cd 10 27
                    call      $26bc                         ;[2543] cd bc 26
                    call      $242e                         ;[2546] cd 2e 24
                    ret                                     ;[2549] c9

                    ld        hl,$5c3b                      ;[254a] 21 3b 5c
                    res       5,(hl)                        ;[254d] cb ae
                    bit       5,(hl)                        ;[254f] cb 6e
                    jr        z,$254f                       ;[2551] 28 fc
                    res       5,(hl)                        ;[2553] cb ae
                    ret                                     ;[2555] c9

                    ld        d,$04                         ;[2556] 16 04
                    inc       b                             ;[2558] 04
                    ld        d,b                           ;[2559] 50
                    ld        (hl),d                        ;[255a] 72
                    ld        l,c                           ;[255b] 69
                    ld        l,(hl)                        ;[255c] 6e
                    ld        (hl),h                        ;[255d] 74
                    ld        h,l                           ;[255e] 65
                    ld        (hl),d                        ;[255f] 72
                    jr        nz,$25c6                      ;[2560] 20 64
                    ld        h,c                           ;[2562] 61
                    ld        (hl),h                        ;[2563] 74
                    ld        h,c                           ;[2564] 61
                    jr        nz,$25db                      ;[2565] 20 74
                    ld        h,l                           ;[2567] 65
                    ld        (hl),e                        ;[2568] 73
                    ld        (hl),h                        ;[2569] 74
                    rst       $38                           ;[256a] ff
                    ld        d,$08                         ;[256b] 16 08
                    ld        bc,$614d                      ;[256d] 01 4d 61
                    ld        l,e                           ;[2570] 6b
                    ld        h,l                           ;[2571] 65
                    jr        nz,$25e7                      ;[2572] 20 73
                    ld        (hl),l                        ;[2574] 75
                    ld        (hl),d                        ;[2575] 72
                    ld        h,l                           ;[2576] 65
                    jr        nz,$25e9                      ;[2577] 20 70
                    ld        (hl),d                        ;[2579] 72
                    ld        l,c                           ;[257a] 69
                    ld        l,(hl)                        ;[257b] 6e
                    ld        (hl),h                        ;[257c] 74
                    ld        h,l                           ;[257d] 65
                    ld        (hl),d                        ;[257e] 72
                    jr        nz,$25ea                      ;[257f] 20 69
                    ld        (hl),e                        ;[2581] 73
                    jr        nz,$25f6                      ;[2582] 20 72
                    ld        h,l                           ;[2584] 65
                    ld        h,c                           ;[2585] 61
                    ld        h,h                           ;[2586] 64
                    ld        a,c                           ;[2587] 79
                    ld        d,$0a                         ;[2588] 16 0a
                    ld        bc,$7250                      ;[258a] 01 50 72
                    ld        h,l                           ;[258d] 65
                    ld        (hl),e                        ;[258e] 73
                    ld        (hl),e                        ;[258f] 73
                    jr        nz,$25e3                      ;[2590] 20 51
                    jr        nz,$2608                      ;[2592] 20 74
                    ld        l,a                           ;[2594] 6f
                    jr        nz,$2608                      ;[2595] 20 71
                    ld        (hl),l                        ;[2597] 75
                    ld        l,c                           ;[2598] 69
                    ld        (hl),h                        ;[2599] 74
                    jr        nz,$260c                      ;[259a] 20 70
                    ld        (hl),d                        ;[259c] 72
                    ld        l,c                           ;[259d] 69
                    ld        l,(hl)                        ;[259e] 6e
                    ld        (hl),h                        ;[259f] 74
                    ld        l,c                           ;[25a0] 69
                    ld        l,(hl)                        ;[25a1] 6e
                    ld        h,a                           ;[25a2] 67
                    rst       $38                           ;[25a3] ff
                    ld        d,$0c                         ;[25a4] 16 0c
                    ld        bc,$6649                      ;[25a6] 01 49 66
                    jr        nz,$260e                      ;[25a9] 20 63
                    ld        l,b                           ;[25ab] 68
                    ld        h,c                           ;[25ac] 61
                    ld        (hl),d                        ;[25ad] 72
                    ld        h,c                           ;[25ae] 61
                    ld        h,e                           ;[25af] 63
                    ld        (hl),h                        ;[25b0] 74
                    ld        h,l                           ;[25b1] 65
                    ld        (hl),d                        ;[25b2] 72
                    ld        (hl),e                        ;[25b3] 73
                    jr        nz,$2626                      ;[25b4] 20 70
                    ld        (hl),d                        ;[25b6] 72
                    ld        l,c                           ;[25b7] 69
                    ld        l,(hl)                        ;[25b8] 6e
                    ld        (hl),h                        ;[25b9] 74
                    ld        h,l                           ;[25ba] 65
                    ld        h,h                           ;[25bb] 64
                    jr        nz,$260d                      ;[25bc] 20 4f
                    ld        c,e                           ;[25be] 4b
                    inc       l                             ;[25bf] 2c
                    dec       c                             ;[25c0] 0d
                    ld        d,b                           ;[25c1] 50
                    ld        (hl),d                        ;[25c2] 72
                    ld        h,l                           ;[25c3] 65
                    ld        (hl),e                        ;[25c4] 73
                    ld        (hl),e                        ;[25c5] 73
                    jr        nz,$2623                      ;[25c6] 20 5b
                    ld        b,l                           ;[25c8] 45
                    ld        c,(hl)                        ;[25c9] 4e
                    ld        d,h                           ;[25ca] 54
                    ld        b,l                           ;[25cb] 45
                    ld        d,d                           ;[25cc] 52
                    ld        e,l                           ;[25cd] 5d
                    inc       l                             ;[25ce] 2c
                    jr        nz,$2640                      ;[25cf] 20 6f
                    ld        (hl),h                        ;[25d1] 74
                    ld        l,b                           ;[25d2] 68
                    ld        h,l                           ;[25d3] 65
                    ld        (hl),d                        ;[25d4] 72
                    ld        (hl),a                        ;[25d5] 77
                    ld        l,c                           ;[25d6] 69
                    ld        (hl),e                        ;[25d7] 73
                    ld        h,l                           ;[25d8] 65
                    jr        nz,$2636                      ;[25d9] 20 5b
                    ld        d,e                           ;[25db] 53
                    ld        d,b                           ;[25dc] 50
                    ld        b,c                           ;[25dd] 41
                    ld        b,e                           ;[25de] 43
                    ld        b,l                           ;[25df] 45
                    ld        e,l                           ;[25e0] 5d
                    rst       $38                           ;[25e1] ff
                    dec       d                             ;[25e2] 15
                    nop                                     ;[25e3] 00
                    ld        d,$04                         ;[25e4] 16 04
                    inc       b                             ;[25e6] 04
                    ld        d,b                           ;[25e7] 50
                    ld        (hl),d                        ;[25e8] 72
                    ld        l,c                           ;[25e9] 69
                    ld        l,(hl)                        ;[25ea] 6e
                    ld        (hl),h                        ;[25eb] 74
                    ld        h,l                           ;[25ec] 65
                    ld        (hl),d                        ;[25ed] 72
                    jr        nz,$2632                      ;[25ee] 20 42
                    ld        d,l                           ;[25f0] 55
                    ld        d,e                           ;[25f1] 53
                    ld        e,c                           ;[25f2] 59
                    jr        nz,$2668                      ;[25f3] 20 73
                    ld        l,c                           ;[25f5] 69
                    ld        h,a                           ;[25f6] 67
                    ld        l,(hl)                        ;[25f7] 6e
                    ld        h,c                           ;[25f8] 61
                    ld        l,h                           ;[25f9] 6c
                    jr        nz,$2670                      ;[25fa] 20 74
                    ld        h,l                           ;[25fc] 65
                    ld        (hl),e                        ;[25fd] 73
                    ld        (hl),h                        ;[25fe] 74
                    rst       $38                           ;[25ff] ff
                    ld        d,$08                         ;[2600] 16 08
                    ex        af,af'                        ;[2602] 08
                    ld        d,h                           ;[2603] 54
                    ld        (hl),l                        ;[2604] 75
                    ld        (hl),d                        ;[2605] 72
                    ld        l,(hl)                        ;[2606] 6e
                    jr        nz,$267d                      ;[2607] 20 74
                    ld        l,b                           ;[2609] 68
                    ld        h,l                           ;[260a] 65
                    jr        nz,$267d                      ;[260b] 20 70
                    ld        (hl),d                        ;[260d] 72
                    ld        l,c                           ;[260e] 69
                    ld        l,(hl)                        ;[260f] 6e
                    ld        (hl),h                        ;[2610] 74
                    ld        h,l                           ;[2611] 65
                    ld        (hl),d                        ;[2612] 72
                    jr        nz,$2614                      ;[2613] 20 ff
                    ld        d,$0a                         ;[2615] 16 0a
                    inc       c                             ;[2617] 0c
                    ld        c,a                           ;[2618] 4f
                    ld        c,(hl)                        ;[2619] 4e
                    ld        c,h                           ;[261a] 4c
                    ld        c,c                           ;[261b] 49
                    ld        c,(hl)                        ;[261c] 4e
                    ld        b,l                           ;[261d] 45
                    jr        nz,$261f                      ;[261e] 20 ff
                    ld        d,$0a                         ;[2620] 16 0a
                    inc       c                             ;[2622] 0c
                    ld        c,a                           ;[2623] 4f
                    ld        b,(hl)                        ;[2624] 46
                    ld        b,(hl)                        ;[2625] 46
                    ld        c,h                           ;[2626] 4c
                    ld        c,c                           ;[2627] 49
                    ld        c,(hl)                        ;[2628] 4e
                    ld        b,l                           ;[2629] 45
                    rst       $38                           ;[262a] ff
                    ld        d,$0c                         ;[262b] 16 0c
                    inc       b                             ;[262d] 04
                    ld        d,b                           ;[262e] 50
                    ld        (hl),d                        ;[262f] 72
                    ld        h,l                           ;[2630] 65
                    ld        (hl),e                        ;[2631] 73
                    ld        (hl),e                        ;[2632] 73
                    jr        nz,$2696                      ;[2633] 20 61
                    ld        l,(hl)                        ;[2635] 6e
                    ld        a,c                           ;[2636] 79
                    jr        nz,$26a4                      ;[2637] 20 6b
                    ld        h,l                           ;[2639] 65
                    ld        a,c                           ;[263a] 79
                    jr        nz,$26b1                      ;[263b] 20 74
                    ld        l,a                           ;[263d] 6f
                    jr        nz,$26a3                      ;[263e] 20 63
                    ld        l,a                           ;[2640] 6f
                    ld        l,(hl)                        ;[2641] 6e
                    ld        (hl),h                        ;[2642] 74
                    ld        l,c                           ;[2643] 69
                    ld        l,(hl)                        ;[2644] 6e
                    ld        (hl),l                        ;[2645] 75
                    ld        h,l                           ;[2646] 65
                    rst       $38                           ;[2647] ff
                    ld        d,$10                         ;[2648] 16 10
                    dec       b                             ;[264a] 05
                    ld        d,b                           ;[264b] 50
                    ld        h,c                           ;[264c] 61
                    ld        (hl),e                        ;[264d] 73
                    ld        (hl),e                        ;[264e] 73
                    ld        h,l                           ;[264f] 65
                    ld        h,h                           ;[2650] 64
                    jr        nz,$2680                      ;[2651] 20 2d
                    jr        nz,$26c5                      ;[2653] 20 70
                    ld        (hl),d                        ;[2655] 72
                    ld        h,l                           ;[2656] 65
                    ld        (hl),e                        ;[2657] 73
                    ld        (hl),e                        ;[2658] 73
                    jr        nz,$26b6                      ;[2659] 20 5b
                    ld        b,l                           ;[265b] 45
                    ld        c,(hl)                        ;[265c] 4e
                    ld        d,h                           ;[265d] 54
                    ld        b,l                           ;[265e] 45
                    ld        d,d                           ;[265f] 52
                    ld        e,l                           ;[2660] 5d
                    rst       $38                           ;[2661] ff
                    ld        d,$10                         ;[2662] 16 10
                    dec       b                             ;[2664] 05
                    ld        b,(hl)                        ;[2665] 46
                    ld        h,c                           ;[2666] 61
                    ld        l,c                           ;[2667] 69
                    ld        l,h                           ;[2668] 6c
                    ld        h,l                           ;[2669] 65
                    ld        h,h                           ;[266a] 64
                    jr        nz,$269a                      ;[266b] 20 2d
                    jr        nz,$26df                      ;[266d] 20 70
                    ld        (hl),d                        ;[266f] 72
                    ld        h,l                           ;[2670] 65
                    ld        (hl),e                        ;[2671] 73
                    ld        (hl),e                        ;[2672] 73
                    jr        nz,$26d0                      ;[2673] 20 5b
                    ld        d,e                           ;[2675] 53
                    ld        d,b                           ;[2676] 50
                    ld        b,c                           ;[2677] 41
                    ld        b,e                           ;[2678] 43
                    ld        b,l                           ;[2679] 45
                    ld        e,l                           ;[267a] 5d
                    rst       $38                           ;[267b] ff
                    ld        a,$52                         ;[267c] 3e 52
                    ex        af,af'                        ;[267e] 08
                    jp        $016c                         ;[267f] c3 6c 01
                    ld        a,$02                         ;[2682] 3e 02
                    rst       $28                           ;[2684] ef
                    ld        bc,$2116                      ;[2685] 01 16 21
                    adc       (hl)                          ;[2688] 8e
                    ld        h,$cd                         ;[2689] 26 cd
                    djnz      $26b4                         ;[268b] 10 27
                    ret                                     ;[268d] c9

                    djnz      $2690                         ;[268e] 10 00
                    ld        de,$1307                      ;[2690] 11 07 13
                    nop                                     ;[2693] 00
                    inc       d                             ;[2694] 14
                    nop                                     ;[2695] 00
                    dec       d                             ;[2696] 15
                    nop                                     ;[2697] 00
                    ld        (de),a                        ;[2698] 12
                    nop                                     ;[2699] 00
                    rst       $38                           ;[269a] ff
                    push      bc                            ;[269b] c5
                    push      hl                            ;[269c] e5
                    ld        b,$19                         ;[269d] 06 19
                    ei                                      ;[269f] fb
                    halt                                    ;[26a0] 76
                    djnz      $26a0                         ;[26a1] 10 fd
                    pop       hl                            ;[26a3] e1
                    pop       bc                            ;[26a4] c1
                    di                                      ;[26a5] f3
                    ret                                     ;[26a6] c9

                    ld        b,$28                         ;[26a7] 06 28
                    ei                                      ;[26a9] fb
                    halt                                    ;[26aa] 76
                    djnz      $26aa                         ;[26ab] 10 fd
                    di                                      ;[26ad] f3
                    ret                                     ;[26ae] c9

                    ld        hl,$3000                      ;[26af] 21 00 30
                    dec       hl                            ;[26b2] 2b
                    push      ix                            ;[26b3] dd e5
                    pop       ix                            ;[26b5] dd e1
                    ld        a,l                           ;[26b7] 7d
                    or        h                             ;[26b8] b4
                    jr        nz,$26b2                      ;[26b9] 20 f7
                    ret                                     ;[26bb] c9

                    push      hl                            ;[26bc] e5
                    push      de                            ;[26bd] d5
                    push      bc                            ;[26be] c5
                    ld        bc,$00fe                      ;[26bf] 01 fe 00
                    in        a,(c)                         ;[26c2] ed 78
                    and       $1f                           ;[26c4] e6 1f
                    cp        $1f                           ;[26c6] fe 1f
                    jr        nz,$26c2                      ;[26c8] 20 f8
                    call      $26df                         ;[26ca] cd df 26
                    cp        $21                           ;[26cd] fe 21
                    scf                                     ;[26cf] 37
                    jr        z,$26d6                       ;[26d0] 28 04
                    cp        $20                           ;[26d2] fe 20
                    jr        nz,$26ca                      ;[26d4] 20 f4
                    push      af                            ;[26d6] f5
                    call      $272d                         ;[26d7] cd 2d 27
                    pop       af                            ;[26da] f1
                    pop       bc                            ;[26db] c1
                    pop       de                            ;[26dc] d1
                    pop       hl                            ;[26dd] e1
                    ret                                     ;[26de] c9

                    call      $26e3                         ;[26df] cd e3 26
                    ret                                     ;[26e2] c9

                    ld        l,$2f                         ;[26e3] 2e 2f
                    ld        de,$ffff                      ;[26e5] 11 ff ff
                    ld        bc,$fefe                      ;[26e8] 01 fe fe
                    in        a,(c)                         ;[26eb] ed 78
                    cpl                                     ;[26ed] 2f
                    and       $1f                           ;[26ee] e6 1f
                    jr        z,$2701                       ;[26f0] 28 0f
                    ld        h,a                           ;[26f2] 67
                    ld        a,l                           ;[26f3] 7d
                    inc       d                             ;[26f4] 14
                    jr        nz,$26e3                      ;[26f5] 20 ec
                    sub       $08                           ;[26f7] d6 08
                    srl       h                             ;[26f9] cb 3c
                    jr        nc,$26f7                      ;[26fb] 30 fa
                    ld        d,e                           ;[26fd] 53
                    ld        e,a                           ;[26fe] 5f
                    jr        nz,$26f4                      ;[26ff] 20 f3
                    dec       l                             ;[2701] 2d
                    rlc       b                             ;[2702] cb 00
                    jr        c,$26eb                       ;[2704] 38 e5
                    ld        a,e                           ;[2706] 7b
                    cp        $ff                           ;[2707] fe ff
                    ret       nz                            ;[2709] c0
                    ld        a,d                           ;[270a] 7a
                    cp        $ff                           ;[270b] fe ff
                    jr        z,$26e3                       ;[270d] 28 d4
                    ret                                     ;[270f] c9

                    ld        a,(hl)                        ;[2710] 7e
                    cp        $ff                           ;[2711] fe ff
                    ret       z                             ;[2713] c8
                    rst       $10                           ;[2714] d7
                    inc       hl                            ;[2715] 23
                    jr        $2710                         ;[2716] 18 f8
                    ld        a,$10                         ;[2718] 3e 10
                    rst       $10                           ;[271a] d7
                    ld        a,b                           ;[271b] 78
                    rst       $10                           ;[271c] d7
                    ld        a,$11                         ;[271d] 3e 11
                    rst       $10                           ;[271f] d7
                    ld        a,c                           ;[2720] 79
                    rst       $10                           ;[2721] d7
                    ret                                     ;[2722] c9

                    rst       $28                           ;[2723] ef
                    djnz      $2726                         ;[2724] 10 00
                    ret                                     ;[2726] c9

                    rst       $28                           ;[2727] ef
                    xor       a                             ;[2728] af
                    dec       c                             ;[2729] 0d
                    jp        $2687                         ;[272a] c3 87 26
                    ld        hl,$0100                      ;[272d] 21 00 01
                    ld        de,$00a0                      ;[2730] 11 a0 00
                    call      $34f6                         ;[2733] cd f6 34
                    di                                      ;[2736] f3
                    ret                                     ;[2737] c9

                    ld        a,$7f                         ;[2738] 3e 7f
                    in        a,($fe)                       ;[273a] db fe
                    rra                                     ;[273c] 1f
                    ret                                     ;[273d] c9

                    ld        a,$7f                         ;[273e] 3e 7f
                    in        a,($fe)                       ;[2740] db fe
                    rr        a                             ;[2742] cb 1f
                    ret                                     ;[2744] c9

                    ld        a,$7f                         ;[2745] 3e 7f
                    in        a,($fe)                       ;[2747] db fe
                    or        $e0                           ;[2749] f6 e0
                    cp        $fc                           ;[274b] fe fc
                    ret                                     ;[274d] c9

                    ld        a,$38                         ;[274e] 3e 38
                    ex        af,af'                        ;[2750] 08
                    ld        a,$20                         ;[2751] 3e 20
                    call      $2723                         ;[2753] cd 23 27
                    dec       d                             ;[2756] 15
                    jr        nz,$2751                      ;[2757] 20 f8
                    ret                                     ;[2759] c9

                    call      $2727                         ;[275a] cd 27 27
                    ld        a,$38                         ;[275d] 3e 38
                    ld        ($5c48),a                     ;[275f] 32 48 5c
                    ld        a,$07                         ;[2762] 3e 07
                    out       ($fe),a                       ;[2764] d3 fe
                    ld        hl,$2f24                      ;[2766] 21 24 2f
                    call      $2710                         ;[2769] cd 10 27
                    call      $26af                         ;[276c] cd af 26
                    call      $2773                         ;[276f] cd 73 27
                    ret                                     ;[2772] c9

                    ld        hl,$284e                      ;[2773] 21 4e 28
                    push      hl                            ;[2776] e5
                    pop       hl                            ;[2777] e1
                    ld        a,(hl)                        ;[2778] 7e
                    inc       a                             ;[2779] 3c
                    ret       z                             ;[277a] c8
                    push      hl                            ;[277b] e5
                    call      $26df                         ;[277c] cd df 26
                    ld        bc,$1000                      ;[277f] 01 00 10
                    dec       bc                            ;[2782] 0b
                    ld        a,b                           ;[2783] 78
                    or        c                             ;[2784] b1
                    jr        nz,$2782                      ;[2785] 20 fb
                    ld        a,d                           ;[2787] 7a
                    cp        $ff                           ;[2788] fe ff
                    jr        z,$27ed                       ;[278a] 28 61
                    cp        $27                           ;[278c] fe 27
                    jr        z,$279d                       ;[278e] 28 0d
                    cp        $18                           ;[2790] fe 18
                    jr        z,$27e8                       ;[2792] 28 54
                    ld        a,e                           ;[2794] 7b
                    ld        e,d                           ;[2795] 5a
                    ld        d,a                           ;[2796] 57
                    cp        $18                           ;[2797] fe 18
                    jr        z,$27e8                       ;[2799] 28 4d
                    jr        $2777                         ;[279b] 18 da
                    ld        a,e                           ;[279d] 7b
                    cp        $23                           ;[279e] fe 23
                    ld        e,$28                         ;[27a0] 1e 28
                    jr        z,$2806                       ;[27a2] 28 62
                    cp        $24                           ;[27a4] fe 24
                    ld        e,$29                         ;[27a6] 1e 29
                    jr        z,$2806                       ;[27a8] 28 5c
                    cp        $1c                           ;[27aa] fe 1c
                    ld        e,$2a                         ;[27ac] 1e 2a
                    jr        z,$2806                       ;[27ae] 28 56
                    cp        $14                           ;[27b0] fe 14
                    ld        e,$2b                         ;[27b2] 1e 2b
                    jr        z,$2806                       ;[27b4] 28 50
                    cp        $0c                           ;[27b6] fe 0c
                    ld        e,$2c                         ;[27b8] 1e 2c
                    jr        z,$2806                       ;[27ba] 28 4a
                    cp        $04                           ;[27bc] fe 04
                    ld        e,$2d                         ;[27be] 1e 2d
                    jr        z,$2806                       ;[27c0] 28 44
                    cp        $03                           ;[27c2] fe 03
                    ld        e,$2e                         ;[27c4] 1e 2e
                    jr        z,$2806                       ;[27c6] 28 3e
                    cp        $0b                           ;[27c8] fe 0b
                    ld        e,$2f                         ;[27ca] 1e 2f
                    jr        z,$2806                       ;[27cc] 28 38
                    cp        $13                           ;[27ce] fe 13
                    ld        e,$30                         ;[27d0] 1e 30
                    jr        z,$2806                       ;[27d2] 28 32
                    cp        $1b                           ;[27d4] fe 1b
                    ld        e,$31                         ;[27d6] 1e 31
                    jr        z,$2806                       ;[27d8] 28 2c
                    cp        $20                           ;[27da] fe 20
                    ld        e,$32                         ;[27dc] 1e 32
                    jr        z,$2806                       ;[27de] 28 26
                    cp        $18                           ;[27e0] fe 18
                    ld        e,$37                         ;[27e2] 1e 37
                    jr        z,$2806                       ;[27e4] 28 20
                    jr        $2777                         ;[27e6] 18 8f
                    ld        a,e                           ;[27e8] 7b
                    cp        $10                           ;[27e9] fe 10
                    ld        e,$33                         ;[27eb] 1e 33
                    jr        z,$2806                       ;[27ed] 28 17
                    cp        $08                           ;[27ef] fe 08
                    ld        e,$34                         ;[27f1] 1e 34
                    jr        z,$2806                       ;[27f3] 28 11
                    cp        $1a                           ;[27f5] fe 1a
                    ld        e,$35                         ;[27f7] 1e 35
                    jr        z,$2806                       ;[27f9] 28 0b
                    cp        $22                           ;[27fb] fe 22
                    ld        e,$36                         ;[27fd] 1e 36
                    jr        z,$2806                       ;[27ff] 28 05
                    ld        e,$37                         ;[2801] 1e 37
                    jp        nz,$2777                      ;[2803] c2 77 27
                    ld        a,e                           ;[2806] 7b
                    pop       hl                            ;[2807] e1
                    push      hl                            ;[2808] e5
                    cp        (hl)                          ;[2809] be
                    jp        nz,$2777                      ;[280a] c2 77 27
                    pop       hl                            ;[280d] e1
                    inc       hl                            ;[280e] 23
                    push      hl                            ;[280f] e5
                    ld        hl,$0080                      ;[2810] 21 80 00
                    ld        de,$0080                      ;[2813] 11 80 00
                    push      af                            ;[2816] f5
                    push      bc                            ;[2817] c5
                    push      ix                            ;[2818] dd e5
                    call      $2733                         ;[281a] cd 33 27
                    pop       ix                            ;[281d] dd e1
                    pop       bc                            ;[281f] c1
                    pop       af                            ;[2820] f1
                    pop       hl                            ;[2821] e1
                    push      hl                            ;[2822] e5
                    ld        a,$11                         ;[2823] 3e 11
                    rst       $10                           ;[2825] d7
                    ld        a,$02                         ;[2826] 3e 02
                    rst       $10                           ;[2828] d7
                    dec       hl                            ;[2829] 2b
                    ld        de,$003b                      ;[282a] 11 3b 00
                    add       hl,de                         ;[282d] 19
                    ld        a,(hl)                        ;[282e] 7e
                    and       $f0                           ;[282f] e6 f0
                    rra                                     ;[2831] 1f
                    rra                                     ;[2832] 1f
                    rra                                     ;[2833] 1f
                    ld        b,$06                         ;[2834] 06 06
                    add       b                             ;[2836] 80
                    ld        b,a                           ;[2837] 47
                    ld        a,(hl)                        ;[2838] 7e
                    and       $0f                           ;[2839] e6 0f
                    rla                                     ;[283b] 17
                    ld        c,$01                         ;[283c] 0e 01
                    add       c                             ;[283e] 81
                    ld        c,a                           ;[283f] 4f
                    ld        a,$16                         ;[2840] 3e 16
                    rst       $10                           ;[2842] d7
                    ld        a,b                           ;[2843] 78
                    dec       a                             ;[2844] 3d
                    rst       $10                           ;[2845] d7
                    ld        a,c                           ;[2846] 79
                    rst       $10                           ;[2847] d7
                    ld        a,$20                         ;[2848] 3e 20
                    rst       $10                           ;[284a] d7
                    jp        $2777                         ;[284b] c3 77 27
                    dec       hl                            ;[284e] 2b
                    inc       l                             ;[284f] 2c
                    inc       h                             ;[2850] 24
                    inc       e                             ;[2851] 1c
                    inc       d                             ;[2852] 14
                    inc       c                             ;[2853] 0c
                    inc       b                             ;[2854] 04
                    inc       bc                            ;[2855] 03
                    dec       bc                            ;[2856] 0b
                    inc       de                            ;[2857] 13
                    dec       de                            ;[2858] 1b
                    inc       hl                            ;[2859] 23
                    ld        ($3128),a                     ;[285a] 32 28 31
                    dec       h                             ;[285d] 25
                    dec       e                             ;[285e] 1d
                    dec       d                             ;[285f] 15
                    dec       c                             ;[2860] 0d
                    dec       b                             ;[2861] 05
                    ld        (bc),a                        ;[2862] 02
                    ld        a,(bc)                        ;[2863] 0a
                    ld        (de),a                        ;[2864] 12
                    ld        a,(de)                        ;[2865] 1a
                    ld        ($2937),hl                    ;[2866] 22 37 29
                    ld        h,$1e                         ;[2869] 26 1e
                    ld        d,$0e                         ;[286b] 16 0e
                    ld        b,$01                         ;[286d] 06 01
                    add       hl,bc                         ;[286f] 09
                    ld        de,$2119                      ;[2870] 11 19 21
                    daa                                     ;[2873] 27
                    ld        hl,($171f)                    ;[2874] 2a 1f 17
                    rrca                                    ;[2877] 0f
                    rlca                                    ;[2878] 07
                    nop                                     ;[2879] 00
                    ex        af,af'                        ;[287a] 08
                    djnz      $28b0                         ;[287b] 10 33
                    daa                                     ;[287d] 27
                    jr        $28b5                         ;[287e] 18 35
                    ld        (hl),$2d                      ;[2880] 36 2d
                    jr        nc,$28a4                      ;[2882] 30 20
                    cpl                                     ;[2884] 2f
                    ld        l,$34                         ;[2885] 2e 34
                    jr        $2888                         ;[2887] 18 ff
                    nop                                     ;[2889] 00
                    ld        bc,$0302                      ;[288a] 01 02 03
                    inc       b                             ;[288d] 04
                    dec       b                             ;[288e] 05
                    ld        b,$07                         ;[288f] 06 07
                    ex        af,af'                        ;[2891] 08
                    add       hl,bc                         ;[2892] 09
                    ld        a,(bc)                        ;[2893] 0a
                    dec       bc                            ;[2894] 0b
                    dec       c                             ;[2895] 0d
                    djnz      $28a9                         ;[2896] 10 11
                    inc       de                            ;[2898] 13
                    inc       d                             ;[2899] 14
                    dec       d                             ;[289a] 15
                    ld        d,$17                         ;[289b] 16 17
                    jr        $28b8                         ;[289d] 18 19
                    ld        a,(de)                        ;[289f] 1a
                    dec       de                            ;[28a0] 1b
                    inc       e                             ;[28a1] 1c
                    jr        nz,$28c5                      ;[28a2] 20 21
                    inc       hl                            ;[28a4] 23
                    inc       h                             ;[28a5] 24
                    dec       h                             ;[28a6] 25
                    ld        h,$27                         ;[28a7] 26 27
                    jr        z,$28d4                       ;[28a9] 28 29
                    ld        hl,($2d2b)                    ;[28ab] 2a 2b 2d
                    jr        nc,$28e1                      ;[28ae] 30 31
                    inc       sp                            ;[28b0] 33
                    inc       (hl)                          ;[28b1] 34
                    dec       (hl)                          ;[28b2] 35
                    ld        (hl),$37                      ;[28b3] 36 37
                    jr        c,$28f0                       ;[28b5] 38 39
                    ld        a,($403d)                     ;[28b7] 3a 3d 40
                    ld        b,c                           ;[28ba] 41
                    ld        b,d                           ;[28bb] 42
                    ld        b,e                           ;[28bc] 43
                    ld        b,h                           ;[28bd] 44
                    ld        b,a                           ;[28be] 47
                    ld        c,d                           ;[28bf] 4a
                    ld        c,e                           ;[28c0] 4b
                    ld        c,h                           ;[28c1] 4c
                    ld        c,l                           ;[28c2] 4d
                    call      $2727                         ;[28c3] cd 27 27
                    ld        a,$05                         ;[28c6] 3e 05
                    out       ($fe),a                       ;[28c8] d3 fe
                    ld        bc,$0600                      ;[28ca] 01 00 06
                    call      $2718                         ;[28cd] cd 18 27
                    ld        hl,$30e6                      ;[28d0] 21 e6 30
                    call      $2710                         ;[28d3] cd 10 27
                    call      $28f2                         ;[28d6] cd f2 28
                    jr        nz,$28d6                      ;[28d9] 20 fb
                    ld        bc,$0200                      ;[28db] 01 00 02
                    push      bc                            ;[28de] c5
                    call      $28f2                         ;[28df] cd f2 28
                    pop       bc                            ;[28e2] c1
                    jr        nz,$28ee                      ;[28e3] 20 09
                    dec       bc                            ;[28e5] 0b
                    ld        a,b                           ;[28e6] 78
                    or        c                             ;[28e7] b1
                    jr        nz,$28de                      ;[28e8] 20 f4
                    scf                                     ;[28ea] 37
                    jp        $242e                         ;[28eb] c3 2e 24
                    or        a                             ;[28ee] b7
                    jp        $242e                         ;[28ef] c3 2e 24
                    call      $26df                         ;[28f2] cd df 26
                    ld        a,d                           ;[28f5] 7a
                    cp        $18                           ;[28f6] fe 18
                    jr        z,$2900                       ;[28f8] 28 06
                    ld        a,e                           ;[28fa] 7b
                    ld        e,d                           ;[28fb] 5a
                    ld        d,a                           ;[28fc] 57
                    cp        $18                           ;[28fd] fe 18
                    ret       nz                            ;[28ff] c0
                    ld        a,e                           ;[2900] 7b
                    cp        $26                           ;[2901] fe 26
                    ret                                     ;[2903] c9

                    call      $267c                         ;[2904] cd 7c 26
                    call      $292a                         ;[2907] cd 2a 29
                    ld        bc,$0000                      ;[290a] 01 00 00
                    ld        hl,$312b                      ;[290d] 21 2b 31
                    call      $2710                         ;[2910] cd 10 27
                    call      $26bc                         ;[2913] cd bc 26
                    call      $242e                         ;[2916] cd 2e 24
                    ld        c,$fd                         ;[2919] 0e fd
                    ld        d,$ff                         ;[291b] 16 ff
                    ld        e,$bf                         ;[291d] 1e bf
                    ld        h,$ff                         ;[291f] 26 ff
                    ld        b,d                           ;[2921] 42
                    ld        a,$07                         ;[2922] 3e 07
                    out       (c),a                         ;[2924] ed 79
                    ld        b,e                           ;[2926] 43
                    out       (c),h                         ;[2927] ed 61
                    ret                                     ;[2929] c9

                    xor       a                             ;[292a] af
                    ld        hl,$5800                      ;[292b] 21 00 58
                    ld        b,$10                         ;[292e] 06 10
                    ld        (hl),a                        ;[2930] 77
                    inc       hl                            ;[2931] 23
                    ld        (hl),a                        ;[2932] 77
                    add       $08                           ;[2933] c6 08
                    inc       hl                            ;[2935] 23
                    djnz      $2930                         ;[2936] 10 f8
                    ld        de,$5820                      ;[2938] 11 20 58
                    ld        bc,$02df                      ;[293b] 01 df 02
                    ldir                                    ;[293e] ed b0
                    ld        c,$fd                         ;[2940] 0e fd
                    ld        d,$ff                         ;[2942] 16 ff
                    ld        e,$bf                         ;[2944] 1e bf
                    ld        hl,$2d19                      ;[2946] 21 19 2d
                    ld        a,(hl)                        ;[2949] 7e
                    inc       hl                            ;[294a] 23
                    bit       7,a                           ;[294b] cb 7f
                    jr        nz,$2959                      ;[294d] 20 0a
                    ld        b,d                           ;[294f] 42
                    out       (c),a                         ;[2950] ed 79
                    ld        a,(hl)                        ;[2952] 7e
                    inc       hl                            ;[2953] 23
                    ld        b,e                           ;[2954] 43
                    out       (c),a                         ;[2955] ed 79
                    jr        $2949                         ;[2957] 18 f0
                    ld        c,$fd                         ;[2959] 0e fd
                    ld        d,$ff                         ;[295b] 16 ff
                    ld        e,$bf                         ;[295d] 1e bf
                    ld        h,$fb                         ;[295f] 26 fb
                    ld        b,d                           ;[2961] 42
                    ld        a,$07                         ;[2962] 3e 07
                    out       (c),a                         ;[2964] ed 79
                    ld        b,e                           ;[2966] 43
                    out       (c),h                         ;[2967] ed 61
                    ret                                     ;[2969] c9

                    call      $2727                         ;[296a] cd 27 27
                    ld        hl,$325b                      ;[296d] 21 5b 32
                    call      $2710                         ;[2970] cd 10 27
                    ld        de,$6000                      ;[2973] 11 00 60
                    call      $2999                         ;[2976] cd 99 29
                    call      $6000                         ;[2979] cd 00 60
                    ld        a,$aa                         ;[297c] 3e aa
                    ld        ($8000),a                     ;[297e] 32 00 80
                    ld        a,($8000)                     ;[2981] 3a 00 80
                    cp        $aa                           ;[2984] fe aa
                    jr        nz,$2991                      ;[2986] 20 09
                    ld        de,$8000                      ;[2988] 11 00 80
                    call      $2999                         ;[298b] cd 99 29
                    call      $8000                         ;[298e] cd 00 80
                    ld        a,$06                         ;[2991] 3e 06
                    out       ($fe),a                       ;[2993] d3 fe
                    scf                                     ;[2995] 37
                    jp        $242e                         ;[2996] c3 2e 24
                    ld        hl,$29a2                      ;[2999] 21 a2 29
                    ld        bc,$003b                      ;[299c] 01 3b 00
                    ldir                                    ;[299f] ed b0
                    ret                                     ;[29a1] c9

                    ld        bc,$2000                      ;[29a2] 01 00 20
                    ld        a,$00                         ;[29a5] 3e 00
                    out       ($fe),a                       ;[29a7] d3 fe
                    cpl                                     ;[29a9] 2f
                    out       ($fe),a                       ;[29aa] d3 fe
                    cpl                                     ;[29ac] 2f
                    out       ($fe),a                       ;[29ad] d3 fe
                    cpl                                     ;[29af] 2f
                    out       ($fe),a                       ;[29b0] d3 fe
                    cpl                                     ;[29b2] 2f
                    out       ($fe),a                       ;[29b3] d3 fe
                    cpl                                     ;[29b5] 2f
                    out       ($fe),a                       ;[29b6] d3 fe
                    cpl                                     ;[29b8] 2f
                    out       ($fe),a                       ;[29b9] d3 fe
                    cpl                                     ;[29bb] 2f
                    out       ($fe),a                       ;[29bc] d3 fe
                    cpl                                     ;[29be] 2f
                    out       ($fe),a                       ;[29bf] d3 fe
                    cpl                                     ;[29c1] 2f
                    out       ($fe),a                       ;[29c2] d3 fe
                    cpl                                     ;[29c4] 2f
                    out       ($fe),a                       ;[29c5] d3 fe
                    cpl                                     ;[29c7] 2f
                    out       ($fe),a                       ;[29c8] d3 fe
                    cpl                                     ;[29ca] 2f
                    out       ($fe),a                       ;[29cb] d3 fe
                    cpl                                     ;[29cd] 2f
                    out       ($fe),a                       ;[29ce] d3 fe
                    cpl                                     ;[29d0] 2f
                    out       ($fe),a                       ;[29d1] d3 fe
                    cpl                                     ;[29d3] 2f
                    out       ($fe),a                       ;[29d4] d3 fe
                    cpl                                     ;[29d6] 2f
                    dec       bc                            ;[29d7] 0b
                    ld        a,b                           ;[29d8] 78
                    or        c                             ;[29d9] b1
                    jr        nz,$29a5                      ;[29da] 20 c9
                    ret                                     ;[29dc] c9

                    call      $2727                         ;[29dd] cd 27 27
                    ld        hl,$32ed                      ;[29e0] 21 ed 32
                    call      $2710                         ;[29e3] cd 10 27
                    ld        de,$1f1f                      ;[29e6] 11 1f 1f
                    push      de                            ;[29e9] d5
                    ld        bc,$effe                      ;[29ea] 01 fe ef
                    in        a,(c)                         ;[29ed] ed 78
                    cpl                                     ;[29ef] 2f
                    and       d                             ;[29f0] a2
                    xor       d                             ;[29f1] aa
                    ld        d,a                           ;[29f2] 57
                    ld        bc,$f7fe                      ;[29f3] 01 fe f7
                    in        a,(c)                         ;[29f6] ed 78
                    cpl                                     ;[29f8] 2f
                    and       e                             ;[29f9] a3
                    xor       e                             ;[29fa] ab
                    ld        e,a                           ;[29fb] 5f
                    pop       bc                            ;[29fc] c1
                    ld        a,d                           ;[29fd] 7a
                    cp        b                             ;[29fe] b8
                    jr        nz,$2a13                      ;[29ff] 20 12
                    ld        a,e                           ;[2a01] 7b
                    cp        c                             ;[2a02] b9
                    jr        nz,$2a13                      ;[2a03] 20 0e
                    call      $273e                         ;[2a05] cd 3e 27
                    jr        c,$29e9                       ;[2a08] 38 df
                    jr        z,$29e9                       ;[2a0a] 28 dd
                    and       a                             ;[2a0c] a7
                    jr        $2a10                         ;[2a0d] 18 01
                    scf                                     ;[2a0f] 37
                    jp        $242e                         ;[2a10] c3 2e 24
                    push      de                            ;[2a13] d5
                    ld        hl,$353d                      ;[2a14] 21 3d 35
                    ld        b,$05                         ;[2a17] 06 05
                    rrc       d                             ;[2a19] cb 0a
                    call      nc,$2a35                      ;[2a1b] d4 35 2a
                    inc       hl                            ;[2a1e] 23
                    inc       hl                            ;[2a1f] 23
                    djnz      $2a19                         ;[2a20] 10 f7
                    ld        b,$05                         ;[2a22] 06 05
                    rrc       e                             ;[2a24] cb 0b
                    call      nc,$2a35                      ;[2a26] d4 35 2a
                    inc       hl                            ;[2a29] 23
                    inc       hl                            ;[2a2a] 23
                    djnz      $2a24                         ;[2a2b] 10 f7
                    pop       de                            ;[2a2d] d1
                    ld        a,d                           ;[2a2e] 7a
                    or        e                             ;[2a2f] b3
                    jr        z,$2a0f                       ;[2a30] 28 dd
                    jp        $29e9                         ;[2a32] c3 e9 29
                    push      bc                            ;[2a35] c5
                    ld        b,(hl)                        ;[2a36] 46
                    inc       hl                            ;[2a37] 23
                    ld        c,(hl)                        ;[2a38] 4e
                    ld        a,$16                         ;[2a39] 3e 16
                    rst       $10                           ;[2a3b] d7
                    ld        a,b                           ;[2a3c] 78
                    dec       a                             ;[2a3d] 3d
                    dec       a                             ;[2a3e] 3d
                    dec       a                             ;[2a3f] 3d
                    rst       $10                           ;[2a40] d7
                    ld        a,c                           ;[2a41] 79
                    rst       $10                           ;[2a42] d7
                    ld        a,$11                         ;[2a43] 3e 11
                    rst       $10                           ;[2a45] d7
                    ld        a,$02                         ;[2a46] 3e 02
                    rst       $10                           ;[2a48] d7
                    ld        a,$20                         ;[2a49] 3e 20
                    rst       $10                           ;[2a4b] d7
                    ld        a,$20                         ;[2a4c] 3e 20
                    rst       $10                           ;[2a4e] d7
                    ld        a,$11                         ;[2a4f] 3e 11
                    rst       $10                           ;[2a51] d7
                    ld        a,$07                         ;[2a52] 3e 07
                    rst       $10                           ;[2a54] d7
                    dec       hl                            ;[2a55] 2b
                    pop       bc                            ;[2a56] c1
                    ret                                     ;[2a57] c9

                    call      $2727                         ;[2a58] cd 27 27
                    di                                      ;[2a5b] f3
                    ld        hl,$5800                      ;[2a5c] 21 00 58
                    ld        de,$5801                      ;[2a5f] 11 01 58
                    ld        bc,$02ff                      ;[2a62] 01 ff 02
                    ld        (hl),$00                      ;[2a65] 36 00
                    ldir                                    ;[2a67] ed b0
                    ld        hl,$2ad8                      ;[2a69] 21 d8 2a
                    call      $2710                         ;[2a6c] cd 10 27
                    ld        hl,$2af5                      ;[2a6f] 21 f5 2a
                    call      $2710                         ;[2a72] cd 10 27
                    ld        a,($5b5c)                     ;[2a75] 3a 5c 5b
                    push      af                            ;[2a78] f5
                    or        $07                           ;[2a79] f6 07
                    ld        ($5b5c),a                     ;[2a7b] 32 5c 5b
                    ld        bc,$7ffd                      ;[2a7e] 01 fd 7f
                    out       (c),a                         ;[2a81] ed 79
                    ld        hl,$4000                      ;[2a83] 21 00 40
                    ld        de,$c000                      ;[2a86] 11 00 c0
                    ld        bc,$1800                      ;[2a89] 01 00 18
                    ldir                                    ;[2a8c] ed b0
                    ld        hl,$2ad8                      ;[2a8e] 21 d8 2a
                    call      $2710                         ;[2a91] cd 10 27
                    ld        hl,$2b13                      ;[2a94] 21 13 2b
                    call      $2710                         ;[2a97] cd 10 27
                    ld        a,($5b5c)                     ;[2a9a] 3a 5c 5b
                    set       3,a                           ;[2a9d] cb df
                    ld        ($5b5c),a                     ;[2a9f] 32 5c 5b
                    ld        bc,$7ffd                      ;[2aa2] 01 fd 7f
                    out       (c),a                         ;[2aa5] ed 79
                    ld        hl,$5800                      ;[2aa7] 21 00 58
                    ld        de,$5801                      ;[2aaa] 11 01 58
                    ld        bc,$02ff                      ;[2aad] 01 ff 02
                    ld        (hl),$38                      ;[2ab0] 36 38
                    ldir                                    ;[2ab2] ed b0
                    ld        hl,$d800                      ;[2ab4] 21 00 d8
                    ld        de,$d801                      ;[2ab7] 11 01 d8
                    ld        bc,$02ff                      ;[2aba] 01 ff 02
                    ld        (hl),$38                      ;[2abd] 36 38
                    ldir                                    ;[2abf] ed b0
                    call      $26bc                         ;[2ac1] cd bc 26
                    call      $242e                         ;[2ac4] cd 2e 24
                    call      $2727                         ;[2ac7] cd 27 27
                    pop       af                            ;[2aca] f1
                    ld        ($5b5c),a                     ;[2acb] 32 5c 5b
                    ld        bc,$7ffd                      ;[2ace] 01 fd 7f
                    out       (c),a                         ;[2ad1] ed 79
                    call      $2727                         ;[2ad3] cd 27 27
                    ei                                      ;[2ad6] fb
                    ret                                     ;[2ad7] c9

                    ld        d,$08                         ;[2ad8] 16 08
                    inc       bc                            ;[2ada] 03
                    ld        de,$1000                      ;[2adb] 11 00 10
                    nop                                     ;[2ade] 00
                    ld        d,e                           ;[2adf] 53
                    ld        h,e                           ;[2ae0] 63
                    ld        (hl),d                        ;[2ae1] 72
                    ld        h,l                           ;[2ae2] 65
                    ld        h,l                           ;[2ae3] 65
                    ld        l,(hl)                        ;[2ae4] 6e
                    jr        nz,$2b5a                      ;[2ae5] 20 73
                    ld        (hl),a                        ;[2ae7] 77
                    ld        l,c                           ;[2ae8] 69
                    ld        (hl),h                        ;[2ae9] 74
                    ld        h,e                           ;[2aea] 63
                    ld        l,b                           ;[2aeb] 68
                    ld        l,c                           ;[2aec] 69
                    ld        l,(hl)                        ;[2aed] 6e
                    ld        h,a                           ;[2aee] 67
                    jr        nz,$2b65                      ;[2aef] 20 74
                    ld        h,l                           ;[2af1] 65
                    ld        (hl),e                        ;[2af2] 73
                    ld        (hl),h                        ;[2af3] 74
                    rst       $38                           ;[2af4] ff
                    ld        d,$0c                         ;[2af5] 16 0c
                    inc       bc                            ;[2af7] 03
                    ld        de,$1000                      ;[2af8] 11 00 10
                    nop                                     ;[2afb] 00
                    ld        d,b                           ;[2afc] 50
                    ld        h,c                           ;[2afd] 61
                    ld        (hl),e                        ;[2afe] 73
                    ld        (hl),e                        ;[2aff] 73
                    ld        h,l                           ;[2b00] 65
                    ld        h,h                           ;[2b01] 64
                    jr        nz,$2b31                      ;[2b02] 20 2d
                    jr        nz,$2b76                      ;[2b04] 20 70
                    ld        (hl),d                        ;[2b06] 72
                    ld        h,l                           ;[2b07] 65
                    ld        (hl),e                        ;[2b08] 73
                    ld        (hl),e                        ;[2b09] 73
                    jr        nz,$2b67                      ;[2b0a] 20 5b
                    ld        b,l                           ;[2b0c] 45
                    ld        c,(hl)                        ;[2b0d] 4e
                    ld        d,h                           ;[2b0e] 54
                    ld        b,l                           ;[2b0f] 45
                    ld        d,d                           ;[2b10] 52
                    ld        e,l                           ;[2b11] 5d
                    rst       $38                           ;[2b12] ff
                    ld        d,$0c                         ;[2b13] 16 0c
                    inc       bc                            ;[2b15] 03
                    ld        de,$1000                      ;[2b16] 11 00 10
                    nop                                     ;[2b19] 00
                    ld        b,(hl)                        ;[2b1a] 46
                    ld        h,c                           ;[2b1b] 61
                    ld        l,c                           ;[2b1c] 69
                    ld        l,h                           ;[2b1d] 6c
                    ld        h,l                           ;[2b1e] 65
                    ld        h,h                           ;[2b1f] 64
                    jr        nz,$2b4f                      ;[2b20] 20 2d
                    jr        nz,$2b94                      ;[2b22] 20 70
                    ld        (hl),d                        ;[2b24] 72
                    ld        h,l                           ;[2b25] 65
                    ld        (hl),e                        ;[2b26] 73
                    ld        (hl),e                        ;[2b27] 73
                    jr        nz,$2b85                      ;[2b28] 20 5b
                    ld        d,e                           ;[2b2a] 53
                    ld        d,b                           ;[2b2b] 50
                    ld        b,c                           ;[2b2c] 41
                    ld        b,e                           ;[2b2d] 43
                    ld        b,l                           ;[2b2e] 45
                    ld        e,l                           ;[2b2f] 5d
                    rst       $38                           ;[2b30] ff
                    call      $2727                         ;[2b31] cd 27 27
                    ld        a,$02                         ;[2b34] 3e 02
                    out       ($fe),a                       ;[2b36] d3 fe
                    ld        a,$08                         ;[2b38] 3e 08
                    ld        ($5c48),a                     ;[2b3a] 32 48 5c
                    ld        hl,$32ce                      ;[2b3d] 21 ce 32
                    call      $2710                         ;[2b40] cd 10 27
                    ld        hl,$0100                      ;[2b43] 21 00 01
                    ld        de,$0a00                      ;[2b46] 11 00 0a
                    di                                      ;[2b49] f3
                    push      ix                            ;[2b4a] dd e5
                    ld        a,l                           ;[2b4c] 7d
                    srl       l                             ;[2b4d] cb 3d
                    srl       l                             ;[2b4f] cb 3d
                    cpl                                     ;[2b51] 2f
                    and       $03                           ;[2b52] e6 03
                    ld        c,a                           ;[2b54] 4f
                    ld        b,$00                         ;[2b55] 06 00
                    ld        ix,$2b67                      ;[2b57] dd 21 67 2b
                    add       ix,bc                         ;[2b5b] dd 09
                    ld        a,($5c48)                     ;[2b5d] 3a 48 5c
                    and       $38                           ;[2b60] e6 38
                    rrca                                    ;[2b62] 0f
                    rrca                                    ;[2b63] 0f
                    rrca                                    ;[2b64] 0f
                    or        $10                           ;[2b65] f6 10
                    nop                                     ;[2b67] 00
                    nop                                     ;[2b68] 00
                    nop                                     ;[2b69] 00
                    inc       b                             ;[2b6a] 04
                    inc       c                             ;[2b6b] 0c
                    dec       c                             ;[2b6c] 0d
                    jr        nz,$2b6c                      ;[2b6d] 20 fd
                    ld        c,$3f                         ;[2b6f] 0e 3f
                    dec       b                             ;[2b71] 05
                    jp        nz,$2b6c                      ;[2b72] c2 6c 2b
                    xor       $08                           ;[2b75] ee 08
                    out       ($fe),a                       ;[2b77] d3 fe
                    ld        b,h                           ;[2b79] 44
                    ld        c,a                           ;[2b7a] 4f
                    bit       3,a                           ;[2b7b] cb 5f
                    jr        nz,$2b88                      ;[2b7d] 20 09
                    ld        a,d                           ;[2b7f] 7a
                    or        e                             ;[2b80] b3
                    jr        z,$2b8c                       ;[2b81] 28 09
                    ld        a,c                           ;[2b83] 79
                    ld        c,l                           ;[2b84] 4d
                    dec       de                            ;[2b85] 1b
                    jp        (ix)                          ;[2b86] dd e9
                    ld        c,l                           ;[2b88] 4d
                    inc       c                             ;[2b89] 0c
                    jp        (ix)                          ;[2b8a] dd e9
                    pop       ix                            ;[2b8c] dd e1
                    ld        bc,$1200                      ;[2b8e] 01 00 12
                    ld        hl,$2d2e                      ;[2b91] 21 2e 2d
                    call      $2710                         ;[2b94] cd 10 27
                    call      $26bc                         ;[2b97] cd bc 26
                    call      $242e                         ;[2b9a] cd 2e 24
                    ret                                     ;[2b9d] c9

                    call      $2727                         ;[2b9e] cd 27 27
                    push      ix                            ;[2ba1] dd e5
                    pop       de                            ;[2ba3] d1
                    ld        a,e                           ;[2ba4] 7b
                    and       d                             ;[2ba5] a2
                    cp        $ff                           ;[2ba6] fe ff
                    jr        nz,$2bb6                      ;[2ba8] 20 0c
                    ld        a,$04                         ;[2baa] 3e 04
                    out       ($fe),a                       ;[2bac] d3 fe
                    ld        hl,$347c                      ;[2bae] 21 7c 34
                    call      $2710                         ;[2bb1] cd 10 27
                    jr        $2bfb                         ;[2bb4] 18 45
                    ld        a,$02                         ;[2bb6] 3e 02
                    out       ($fe),a                       ;[2bb8] d3 fe
                    ld        hl,$3493                      ;[2bba] 21 93 34
                    call      $2710                         ;[2bbd] cd 10 27
                    ld        bc,$0807                      ;[2bc0] 01 07 08
                    push      ix                            ;[2bc3] dd e5
                    pop       de                            ;[2bc5] d1
                    push      de                            ;[2bc6] d5
                    ld        hl,$2ed4                      ;[2bc7] 21 d4 2e
                    ld        d,$08                         ;[2bca] 16 08
                    rr        e                             ;[2bcc] cb 1b
                    jr        c,$2bdb                       ;[2bce] 38 0b
                    push      hl                            ;[2bd0] e5
                    push      de                            ;[2bd1] d5
                    ld        a,(hl)                        ;[2bd2] 7e
                    inc       hl                            ;[2bd3] 23
                    ld        h,(hl)                        ;[2bd4] 66
                    ld        l,a                           ;[2bd5] 6f
                    call      $2710                         ;[2bd6] cd 10 27
                    pop       de                            ;[2bd9] d1
                    pop       hl                            ;[2bda] e1
                    inc       hl                            ;[2bdb] 23
                    inc       hl                            ;[2bdc] 23
                    dec       d                             ;[2bdd] 15
                    jr        nz,$2bcc                      ;[2bde] 20 ec
                    pop       de                            ;[2be0] d1
                    ld        e,d                           ;[2be1] 5a
                    ld        d,$08                         ;[2be2] 16 08
                    rr        e                             ;[2be4] cb 1b
                    jr        c,$2bf6                       ;[2be6] 38 0e
                    push      hl                            ;[2be8] e5
                    push      de                            ;[2be9] d5
                    push      bc                            ;[2bea] c5
                    ld        a,(hl)                        ;[2beb] 7e
                    inc       hl                            ;[2bec] 23
                    ld        h,(hl)                        ;[2bed] 66
                    ld        l,a                           ;[2bee] 6f
                    call      $2710                         ;[2bef] cd 10 27
                    pop       bc                            ;[2bf2] c1
                    pop       de                            ;[2bf3] d1
                    pop       hl                            ;[2bf4] e1
                    inc       b                             ;[2bf5] 04
                    inc       hl                            ;[2bf6] 23
                    inc       hl                            ;[2bf7] 23
                    dec       d                             ;[2bf8] 15
                    jr        nz,$2be4                      ;[2bf9] 20 e9
                    ld        hl,$2c0d                      ;[2bfb] 21 0d 2c
                    ld        bc,$1000                      ;[2bfe] 01 00 10
                    call      $2710                         ;[2c01] cd 10 27
                    call      $26bc                         ;[2c04] cd bc 26
                    jr        c,$2c04                       ;[2c07] 38 fb
                    di                                      ;[2c09] f3
                    jp        $0000                         ;[2c0a] c3 00 00
                    dec       c                             ;[2c0d] 0d
                    dec       c                             ;[2c0e] 0d
                    ld        c,b                           ;[2c0f] 48
                    ld        l,a                           ;[2c10] 6f
                    ld        l,h                           ;[2c11] 6c
                    ld        h,h                           ;[2c12] 64
                    jr        nz,$2c70                      ;[2c13] 20 5b
                    ld        b,d                           ;[2c15] 42
                    ld        d,d                           ;[2c16] 52
                    ld        b,l                           ;[2c17] 45
                    ld        b,c                           ;[2c18] 41
                    ld        c,e                           ;[2c19] 4b
                    ld        e,l                           ;[2c1a] 5d
                    jr        nz,$2c91                      ;[2c1b] 20 74
                    ld        l,a                           ;[2c1d] 6f
                    jr        nz,$2c92                      ;[2c1e] 20 72
                    ld        h,l                           ;[2c20] 65
                    ld        (hl),b                        ;[2c21] 70
                    ld        h,l                           ;[2c22] 65
                    ld        h,c                           ;[2c23] 61
                    ld        (hl),h                        ;[2c24] 74
                    jr        nz,$2c9b                      ;[2c25] 20 74
                    ld        h,l                           ;[2c27] 65
                    ld        (hl),e                        ;[2c28] 73
                    ld        (hl),h                        ;[2c29] 74
                    ld        (hl),e                        ;[2c2a] 73
                    rst       $38                           ;[2c2b] ff
                    ld        c,$fd                         ;[2c2c] 0e fd
                    ld        d,$ff                         ;[2c2e] 16 ff
                    ld        e,$bf                         ;[2c30] 1e bf
                    ld        b,d                           ;[2c32] 42
                    ld        a,$0e                         ;[2c33] 3e 0e
                    out       (c),a                         ;[2c35] ed 79
                    ld        a,$ff                         ;[2c37] 3e ff
                    ld        b,e                           ;[2c39] 43
                    out       (c),a                         ;[2c3a] ed 79
                    ld        b,d                           ;[2c3c] 42
                    in        a,(c)                         ;[2c3d] ed 78
                    cp        $ff                           ;[2c3f] fe ff
                    jr        nz,$2c77                      ;[2c41] 20 34
                    ld        a,$fe                         ;[2c43] 3e fe
                    ld        b,e                           ;[2c45] 43
                    out       (c),a                         ;[2c46] ed 79
                    ld        b,d                           ;[2c48] 42
                    in        a,(c)                         ;[2c49] ed 78
                    cp        $7e                           ;[2c4b] fe 7e
                    jr        nz,$2c77                      ;[2c4d] 20 28
                    ld        a,$fd                         ;[2c4f] 3e fd
                    ld        b,e                           ;[2c51] 43
                    out       (c),a                         ;[2c52] ed 79
                    ld        b,d                           ;[2c54] 42
                    in        a,(c)                         ;[2c55] ed 78
                    cp        $bd                           ;[2c57] fe bd
                    jr        nz,$2c77                      ;[2c59] 20 1c
                    ld        a,$fb                         ;[2c5b] 3e fb
                    ld        b,e                           ;[2c5d] 43
                    out       (c),a                         ;[2c5e] ed 79
                    ld        b,d                           ;[2c60] 42
                    in        a,(c)                         ;[2c61] ed 78
                    cp        $db                           ;[2c63] fe db
                    jr        nz,$2c77                      ;[2c65] 20 10
                    ld        a,$f7                         ;[2c67] 3e f7
                    ld        b,e                           ;[2c69] 43
                    out       (c),a                         ;[2c6a] ed 79
                    ld        b,d                           ;[2c6c] 42
                    in        a,(c)                         ;[2c6d] ed 78
                    cp        $e7                           ;[2c6f] fe e7
                    jr        nz,$2c77                      ;[2c71] 20 04
                    scf                                     ;[2c73] 37
                    jp        $242e                         ;[2c74] c3 2e 24
                    or        a                             ;[2c77] b7
                    jp        $242e                         ;[2c78] c3 2e 24
                    call      $2727                         ;[2c7b] cd 27 27
                    ld        a,$02                         ;[2c7e] 3e 02
                    out       ($fe),a                       ;[2c80] d3 fe
                    ld        a,$08                         ;[2c82] 3e 08
                    ld        ($5c48),a                     ;[2c84] 32 48 5c
                    ld        hl,$32b3                      ;[2c87] 21 b3 32
                    call      $2710                         ;[2c8a] cd 10 27
                    ld        hl,$0100                      ;[2c8d] 21 00 01
                    ld        de,$0a00                      ;[2c90] 11 00 0a
                    call      $2733                         ;[2c93] cd 33 27
                    ld        bc,$1200                      ;[2c96] 01 00 12
                    ld        hl,$2d2e                      ;[2c99] 21 2e 2d
                    call      $2710                         ;[2c9c] cd 10 27
                    call      $26bc                         ;[2c9f] cd bc 26
                    call      $242e                         ;[2ca2] cd 2e 24
                    ret                                     ;[2ca5] c9

                    call      $2727                         ;[2ca6] cd 27 27
                    ld        a,$05                         ;[2ca9] 3e 05
                    out       ($fe),a                       ;[2cab] d3 fe
                    ld        hl,$329a                      ;[2cad] 21 9a 32
                    call      $2710                         ;[2cb0] cd 10 27
                    ld        c,$fd                         ;[2cb3] 0e fd
                    ld        d,$ff                         ;[2cb5] 16 ff
                    ld        e,$bf                         ;[2cb7] 1e bf
                    ld        hl,$2d19                      ;[2cb9] 21 19 2d
                    ld        a,(hl)                        ;[2cbc] 7e
                    inc       hl                            ;[2cbd] 23
                    bit       7,a                           ;[2cbe] cb 7f
                    jr        nz,$2ccc                      ;[2cc0] 20 0a
                    ld        b,d                           ;[2cc2] 42
                    out       (c),a                         ;[2cc3] ed 79
                    ld        a,(hl)                        ;[2cc5] 7e
                    inc       hl                            ;[2cc6] 23
                    ld        b,e                           ;[2cc7] 43
                    out       (c),a                         ;[2cc8] ed 79
                    jr        $2cbc                         ;[2cca] 18 f0
                    ld        c,$fd                         ;[2ccc] 0e fd
                    ld        d,$ff                         ;[2cce] 16 ff
                    ld        e,$bf                         ;[2cd0] 1e bf
                    ld        l,$03                         ;[2cd2] 2e 03
                    ld        h,$fe                         ;[2cd4] 26 fe
                    ld        b,d                           ;[2cd6] 42
                    ld        a,$07                         ;[2cd7] 3e 07
                    out       (c),a                         ;[2cd9] ed 79
                    ld        b,e                           ;[2cdb] 43
                    out       (c),h                         ;[2cdc] ed 61
                    push      hl                            ;[2cde] e5
                    push      de                            ;[2cdf] d5
                    push      bc                            ;[2ce0] c5
                    call      $26a7                         ;[2ce1] cd a7 26
                    pop       bc                            ;[2ce4] c1
                    pop       de                            ;[2ce5] d1
                    pop       hl                            ;[2ce6] e1
                    scf                                     ;[2ce7] 37
                    rl        h                             ;[2ce8] cb 14
                    dec       l                             ;[2cea] 2d
                    jr        nz,$2cd6                      ;[2ceb] 20 e9
                    ld        h,$f8                         ;[2ced] 26 f8
                    ld        b,d                           ;[2cef] 42
                    ld        a,$07                         ;[2cf0] 3e 07
                    out       (c),a                         ;[2cf2] ed 79
                    ld        b,e                           ;[2cf4] 43
                    out       (c),h                         ;[2cf5] ed 61
                    push      hl                            ;[2cf7] e5
                    push      de                            ;[2cf8] d5
                    push      bc                            ;[2cf9] c5
                    call      $26a7                         ;[2cfa] cd a7 26
                    pop       bc                            ;[2cfd] c1
                    pop       de                            ;[2cfe] d1
                    pop       hl                            ;[2cff] e1
                    ld        h,$ff                         ;[2d00] 26 ff
                    ld        b,d                           ;[2d02] 42
                    ld        a,$07                         ;[2d03] 3e 07
                    out       (c),a                         ;[2d05] ed 79
                    ld        b,e                           ;[2d07] 43
                    out       (c),h                         ;[2d08] ed 61
                    ld        bc,$0a00                      ;[2d0a] 01 00 0a
                    ld        hl,$34b5                      ;[2d0d] 21 b5 34
                    call      $2710                         ;[2d10] cd 10 27
                    call      $26bc                         ;[2d13] cd bc 26
                    jp        $242e                         ;[2d16] c3 2e 24
                    nop                                     ;[2d19] 00
                    ld        b,b                           ;[2d1a] 40
                    ld        bc,$0200                      ;[2d1b] 01 00 02
                    add       b                             ;[2d1e] 80
                    inc       bc                            ;[2d1f] 03
                    nop                                     ;[2d20] 00
                    inc       b                             ;[2d21] 04
                    nop                                     ;[2d22] 00
                    dec       b                             ;[2d23] 05
                    ld        bc,$1f06                      ;[2d24] 01 06 1f
                    ex        af,af'                        ;[2d27] 08
                    rrca                                    ;[2d28] 0f
                    add       hl,bc                         ;[2d29] 09
                    rrca                                    ;[2d2a] 0f
                    ld        a,(bc)                        ;[2d2b] 0a
                    rrca                                    ;[2d2c] 0f
                    add       b                             ;[2d2d] 80
                    ld        d,b                           ;[2d2e] 50
                    ld        (hl),d                        ;[2d2f] 72
                    ld        h,l                           ;[2d30] 65
                    ld        (hl),e                        ;[2d31] 73
                    ld        (hl),e                        ;[2d32] 73
                    jr        nz,$2d90                      ;[2d33] 20 5b
                    ld        b,l                           ;[2d35] 45
                    ld        c,(hl)                        ;[2d36] 4e
                    ld        d,h                           ;[2d37] 54
                    ld        b,l                           ;[2d38] 45
                    ld        d,d                           ;[2d39] 52
                    ld        e,l                           ;[2d3a] 5d
                    jr        nz,$2da6                      ;[2d3b] 20 69
                    ld        h,(hl)                        ;[2d3d] 66
                    jr        nz,$2db9                      ;[2d3e] 20 79
                    ld        l,a                           ;[2d40] 6f
                    ld        (hl),l                        ;[2d41] 75
                    jr        nz,$2dac                      ;[2d42] 20 68
                    ld        h,l                           ;[2d44] 65
                    ld        h,c                           ;[2d45] 61
                    ld        (hl),d                        ;[2d46] 72
                    ld        h,h                           ;[2d47] 64
                    jr        nz,$2dbe                      ;[2d48] 20 74
                    ld        l,b                           ;[2d4a] 68
                    ld        h,l                           ;[2d4b] 65
                    jr        nz,$2d6e                      ;[2d4c] 20 20
                    ld        (hl),h                        ;[2d4e] 74
                    ld        l,a                           ;[2d4f] 6f
                    ld        l,(hl)                        ;[2d50] 6e
                    ld        h,l                           ;[2d51] 65
                    inc       l                             ;[2d52] 2c
                    jr        nz,$2dba                      ;[2d53] 20 65
                    ld        l,h                           ;[2d55] 6c
                    ld        (hl),e                        ;[2d56] 73
                    ld        h,l                           ;[2d57] 65
                    jr        nz,$2dca                      ;[2d58] 20 70
                    ld        (hl),d                        ;[2d5a] 72
                    ld        h,l                           ;[2d5b] 65
                    ld        (hl),e                        ;[2d5c] 73
                    ld        (hl),e                        ;[2d5d] 73
                    jr        nz,$2dbb                      ;[2d5e] 20 5b
                    ld        d,e                           ;[2d60] 53
                    ld        d,b                           ;[2d61] 50
                    ld        b,c                           ;[2d62] 41
                    ld        b,e                           ;[2d63] 43
                    ld        b,l                           ;[2d64] 45
                    ld        e,l                           ;[2d65] 5d
                    rst       $38                           ;[2d66] ff
                    ld        h,e                           ;[2d67] 63
                    ld        l,a                           ;[2d68] 6f
                    ld        l,h                           ;[2d69] 6c
                    ld        l,a                           ;[2d6a] 6f
                    ld        (hl),l                        ;[2d6b] 75
                    ld        (hl),d                        ;[2d6c] 72
                    jr        nz,$2de3                      ;[2d6d] 20 74
                    ld        h,l                           ;[2d6f] 65
                    ld        (hl),e                        ;[2d70] 73
                    ld        (hl),h                        ;[2d71] 74
                    jr        nz,$2dda                      ;[2d72] 20 66
                    ld        h,c                           ;[2d74] 61
                    ld        l,c                           ;[2d75] 69
                    ld        l,h                           ;[2d76] 6c
                    ld        h,l                           ;[2d77] 65
                    ld        h,h                           ;[2d78] 64
                    dec       c                             ;[2d79] 0d
                    rst       $38                           ;[2d7a] ff
                    ld        d,l                           ;[2d7b] 55
                    ld        c,h                           ;[2d7c] 4c
                    ld        b,c                           ;[2d7d] 41
                    jr        nz,$2df3                      ;[2d7e] 20 73
                    ld        l,a                           ;[2d80] 6f
                    ld        (hl),l                        ;[2d81] 75
                    ld        l,(hl)                        ;[2d82] 6e
                    ld        h,h                           ;[2d83] 64
                    jr        nz,$2dfa                      ;[2d84] 20 74
                    ld        h,l                           ;[2d86] 65
                    ld        (hl),e                        ;[2d87] 73
                    ld        (hl),h                        ;[2d88] 74
                    jr        nz,$2df1                      ;[2d89] 20 66
                    ld        h,c                           ;[2d8b] 61
                    ld        l,c                           ;[2d8c] 69
                    ld        l,h                           ;[2d8d] 6c
                    ld        h,l                           ;[2d8e] 65
                    ld        h,h                           ;[2d8f] 64
                    dec       c                             ;[2d90] 0d
                    rst       $38                           ;[2d91] ff
                    ld        d,e                           ;[2d92] 53
                    ld        a,c                           ;[2d93] 79
                    ld        l,l                           ;[2d94] 6d
                    ld        (hl),e                        ;[2d95] 73
                    ld        l,b                           ;[2d96] 68
                    ld        h,(hl)                        ;[2d97] 66
                    ld        (hl),h                        ;[2d98] 74
                    cpl                                     ;[2d99] 2f
                    ld        b,c                           ;[2d9a] 41
                    jr        nz,$2e08                      ;[2d9b] 20 6b
                    ld        h,l                           ;[2d9d] 65
                    ld        a,c                           ;[2d9e] 79
                    jr        nz,$2e15                      ;[2d9f] 20 74
                    ld        h,l                           ;[2da1] 65
                    ld        (hl),e                        ;[2da2] 73
                    ld        (hl),h                        ;[2da3] 74
                    jr        nz,$2e0c                      ;[2da4] 20 66
                    ld        h,c                           ;[2da6] 61
                    ld        l,c                           ;[2da7] 69
                    ld        l,h                           ;[2da8] 6c
                    ld        h,l                           ;[2da9] 65
                    ld        h,h                           ;[2daa] 64
                    dec       c                             ;[2dab] 0d
                    rst       $38                           ;[2dac] ff
                    ld        d,l                           ;[2dad] 55
                    ld        c,h                           ;[2dae] 4c
                    ld        b,c                           ;[2daf] 41
                    jr        nz,$2e26                      ;[2db0] 20 74
                    ld        h,l                           ;[2db2] 65
                    ld        (hl),e                        ;[2db3] 73
                    ld        (hl),h                        ;[2db4] 74
                    jr        nz,$2e1d                      ;[2db5] 20 66
                    ld        h,c                           ;[2db7] 61
                    ld        l,c                           ;[2db8] 69
                    ld        l,h                           ;[2db9] 6c
                    ld        h,l                           ;[2dba] 65
                    ld        h,h                           ;[2dbb] 64
                    dec       c                             ;[2dbc] 0d
                    rst       $38                           ;[2dbd] ff
                    ld        d,d                           ;[2dbe] 52
                    ld        d,e                           ;[2dbf] 53
                    ld        ($3233),a                     ;[2dc0] 32 33 32
                    jr        nz,$2e39                      ;[2dc3] 20 74
                    ld        h,l                           ;[2dc5] 65
                    ld        (hl),e                        ;[2dc6] 73
                    ld        (hl),h                        ;[2dc7] 74
                    jr        nz,$2e30                      ;[2dc8] 20 66
                    ld        h,c                           ;[2dca] 61
                    ld        l,c                           ;[2dcb] 69
                    ld        l,h                           ;[2dcc] 6c
                    ld        h,l                           ;[2dcd] 65
                    ld        h,h                           ;[2dce] 64
                    dec       c                             ;[2dcf] 0d
                    rst       $38                           ;[2dd0] ff
                    ld        b,a                           ;[2dd1] 47
                    ld        c,c                           ;[2dd2] 49
                    jr        nz,$2e48                      ;[2dd3] 20 73
                    ld        l,a                           ;[2dd5] 6f
                    ld        (hl),l                        ;[2dd6] 75
                    ld        l,(hl)                        ;[2dd7] 6e
                    ld        h,h                           ;[2dd8] 64
                    jr        nz,$2e4f                      ;[2dd9] 20 74
                    ld        h,l                           ;[2ddb] 65
                    ld        (hl),e                        ;[2ddc] 73
                    ld        (hl),h                        ;[2ddd] 74
                    jr        nz,$2e46                      ;[2dde] 20 66
                    ld        h,c                           ;[2de0] 61
                    ld        l,c                           ;[2de1] 69
                    ld        l,h                           ;[2de2] 6c
                    ld        h,l                           ;[2de3] 65
                    ld        h,h                           ;[2de4] 64
                    dec       c                             ;[2de5] 0d
                    rst       $38                           ;[2de6] ff
                    ld        b,c                           ;[2de7] 41
                    ld        l,h                           ;[2de8] 6c
                    ld        l,h                           ;[2de9] 6c
                    dec       l                             ;[2dea] 2d
                    ld        d,d                           ;[2deb] 52
                    ld        b,c                           ;[2dec] 41
                    ld        c,l                           ;[2ded] 4d
                    jr        nz,$2e60                      ;[2dee] 20 70
                    ld        h,c                           ;[2df0] 61
                    ld        h,a                           ;[2df1] 67
                    ld        h,l                           ;[2df2] 65
                    jr        nz,$2e69                      ;[2df3] 20 74
                    ld        h,l                           ;[2df5] 65
                    ld        (hl),e                        ;[2df6] 73
                    ld        (hl),h                        ;[2df7] 74
                    jr        nz,$2e60                      ;[2df8] 20 66
                    ld        h,c                           ;[2dfa] 61
                    ld        l,c                           ;[2dfb] 69
                    ld        l,h                           ;[2dfc] 6c
                    ld        h,l                           ;[2dfd] 65
                    ld        h,h                           ;[2dfe] 64
                    dec       c                             ;[2dff] 0d
                    rst       $38                           ;[2e00] ff
                    ld        c,d                           ;[2e01] 4a
                    ld        l,a                           ;[2e02] 6f
                    ld        a,c                           ;[2e03] 79
                    ld        (hl),e                        ;[2e04] 73
                    ld        (hl),h                        ;[2e05] 74
                    ld        l,c                           ;[2e06] 69
                    ld        h,e                           ;[2e07] 63
                    ld        l,e                           ;[2e08] 6b
                    jr        nz,$2e7f                      ;[2e09] 20 74
                    ld        h,l                           ;[2e0b] 65
                    ld        (hl),e                        ;[2e0c] 73
                    ld        (hl),h                        ;[2e0d] 74
                    jr        nz,$2e76                      ;[2e0e] 20 66
                    ld        h,c                           ;[2e10] 61
                    ld        l,c                           ;[2e11] 69
                    ld        l,h                           ;[2e12] 6c
                    ld        h,l                           ;[2e13] 65
                    ld        h,h                           ;[2e14] 64
                    dec       c                             ;[2e15] 0d
                    rst       $38                           ;[2e16] ff
                    ld        c,c                           ;[2e17] 49
                    ld        b,e                           ;[2e18] 43
                    jr        nz,$2e52                      ;[2e19] 20 37
                    jr        nz,$2e80                      ;[2e1b] 20 63
                    ld        l,b                           ;[2e1d] 68
                    ld        h,l                           ;[2e1e] 65
                    ld        h,e                           ;[2e1f] 63
                    ld        l,e                           ;[2e20] 6b
                    ld        (hl),e                        ;[2e21] 73
                    ld        (hl),l                        ;[2e22] 75
                    ld        l,l                           ;[2e23] 6d
                    jr        nz,$2e8c                      ;[2e24] 20 66
                    ld        h,c                           ;[2e26] 61
                    ld        l,c                           ;[2e27] 69
                    ld        l,h                           ;[2e28] 6c
                    ld        h,l                           ;[2e29] 65
                    ld        h,h                           ;[2e2a] 64
                    dec       c                             ;[2e2b] 0d
                    rst       $38                           ;[2e2c] ff
                    ld        c,c                           ;[2e2d] 49
                    ld        b,e                           ;[2e2e] 43
                    jr        nz,$2e69                      ;[2e2f] 20 38
                    jr        nz,$2e96                      ;[2e31] 20 63
                    ld        l,b                           ;[2e33] 68
                    ld        h,l                           ;[2e34] 65
                    ld        h,e                           ;[2e35] 63
                    ld        l,e                           ;[2e36] 6b
                    ld        (hl),e                        ;[2e37] 73
                    ld        (hl),l                        ;[2e38] 75
                    ld        l,l                           ;[2e39] 6d
                    jr        nz,$2ea2                      ;[2e3a] 20 66
                    ld        h,c                           ;[2e3c] 61
                    ld        l,c                           ;[2e3d] 69
                    ld        l,h                           ;[2e3e] 6c
                    ld        h,l                           ;[2e3f] 65
                    ld        h,h                           ;[2e40] 64
                    dec       c                             ;[2e41] 0d
                    rst       $38                           ;[2e42] ff
                    ld        b,h                           ;[2e43] 44
                    ld        l,c                           ;[2e44] 69
                    ld        (hl),e                        ;[2e45] 73
                    ld        l,e                           ;[2e46] 6b
                    jr        nz,$2ebd                      ;[2e47] 20 74
                    ld        h,l                           ;[2e49] 65
                    ld        (hl),e                        ;[2e4a] 73
                    ld        (hl),h                        ;[2e4b] 74
                    ld        (hl),e                        ;[2e4c] 73
                    jr        nz,$2eb5                      ;[2e4d] 20 66
                    ld        h,c                           ;[2e4f] 61
                    ld        l,c                           ;[2e50] 69
                    ld        l,h                           ;[2e51] 6c
                    ld        h,l                           ;[2e52] 65
                    ld        h,h                           ;[2e53] 64
                    dec       c                             ;[2e54] 0d
                    rst       $38                           ;[2e55] ff
                    ld        d,e                           ;[2e56] 53
                    ld        h,l                           ;[2e57] 65
                    ld        h,e                           ;[2e58] 63
                    ld        l,a                           ;[2e59] 6f
                    ld        l,(hl)                        ;[2e5a] 6e
                    ld        h,h                           ;[2e5b] 64
                    jr        nz,$2eb1                      ;[2e5c] 20 53
                    ld        h,e                           ;[2e5e] 63
                    ld        (hl),d                        ;[2e5f] 72
                    ld        h,l                           ;[2e60] 65
                    ld        h,l                           ;[2e61] 65
                    ld        l,(hl)                        ;[2e62] 6e
                    jr        nz,$2ed9                      ;[2e63] 20 74
                    ld        h,l                           ;[2e65] 65
                    ld        (hl),e                        ;[2e66] 73
                    ld        (hl),h                        ;[2e67] 74
                    jr        nz,$2ed0                      ;[2e68] 20 66
                    ld        h,c                           ;[2e6a] 61
                    ld        l,c                           ;[2e6b] 69
                    ld        l,h                           ;[2e6c] 6c
                    ld        h,l                           ;[2e6d] 65
                    ld        h,h                           ;[2e6e] 64
                    dec       c                             ;[2e6f] 0d
                    rst       $38                           ;[2e70] ff
                    ld        b,e                           ;[2e71] 43
                    ld        h,c                           ;[2e72] 61
                    ld        (hl),e                        ;[2e73] 73
                    ld        (hl),e                        ;[2e74] 73
                    ld        h,l                           ;[2e75] 65
                    ld        (hl),h                        ;[2e76] 74
                    ld        (hl),h                        ;[2e77] 74
                    ld        h,l                           ;[2e78] 65
                    jr        nz,$2eca                      ;[2e79] 20 4f
                    ld        (hl),l                        ;[2e7b] 75
                    ld        (hl),h                        ;[2e7c] 74
                    ld        (hl),b                        ;[2e7d] 70
                    ld        (hl),l                        ;[2e7e] 75
                    ld        (hl),h                        ;[2e7f] 74
                    jr        nz,$2ee8                      ;[2e80] 20 66
                    ld        h,c                           ;[2e82] 61
                    ld        l,c                           ;[2e83] 69
                    ld        l,h                           ;[2e84] 6c
                    ld        h,l                           ;[2e85] 65
                    ld        h,h                           ;[2e86] 64
                    dec       c                             ;[2e87] 0d
                    rst       $38                           ;[2e88] ff
                    ld        b,e                           ;[2e89] 43
                    ld        h,c                           ;[2e8a] 61
                    ld        (hl),e                        ;[2e8b] 73
                    ld        (hl),e                        ;[2e8c] 73
                    ld        h,l                           ;[2e8d] 65
                    ld        (hl),h                        ;[2e8e] 74
                    ld        (hl),h                        ;[2e8f] 74
                    ld        h,l                           ;[2e90] 65
                    jr        nz,$2edc                      ;[2e91] 20 49
                    ld        l,(hl)                        ;[2e93] 6e
                    ld        (hl),b                        ;[2e94] 70
                    ld        (hl),l                        ;[2e95] 75
                    ld        (hl),h                        ;[2e96] 74
                    jr        nz,$2eff                      ;[2e97] 20 66
                    ld        h,c                           ;[2e99] 61
                    ld        l,c                           ;[2e9a] 69
                    ld        l,h                           ;[2e9b] 6c
                    ld        h,l                           ;[2e9c] 65
                    ld        h,h                           ;[2e9d] 64
                    dec       c                             ;[2e9e] 0d
                    rst       $38                           ;[2e9f] ff
                    ld        d,b                           ;[2ea0] 50
                    ld        (hl),d                        ;[2ea1] 72
                    ld        l,c                           ;[2ea2] 69
                    ld        l,(hl)                        ;[2ea3] 6e
                    ld        (hl),h                        ;[2ea4] 74
                    ld        h,l                           ;[2ea5] 65
                    ld        (hl),d                        ;[2ea6] 72
                    jr        nz,$2eeb                      ;[2ea7] 20 42
                    ld        d,l                           ;[2ea9] 55
                    ld        d,e                           ;[2eaa] 53
                    ld        e,c                           ;[2eab] 59
                    jr        nz,$2f22                      ;[2eac] 20 74
                    ld        h,l                           ;[2eae] 65
                    ld        (hl),e                        ;[2eaf] 73
                    ld        (hl),h                        ;[2eb0] 74
                    jr        nz,$2f19                      ;[2eb1] 20 66
                    ld        h,c                           ;[2eb3] 61
                    ld        l,c                           ;[2eb4] 69
                    ld        l,h                           ;[2eb5] 6c
                    ld        h,l                           ;[2eb6] 65
                    ld        h,h                           ;[2eb7] 64
                    dec       c                             ;[2eb8] 0d
                    rst       $38                           ;[2eb9] ff
                    ld        d,b                           ;[2eba] 50
                    ld        (hl),d                        ;[2ebb] 72
                    ld        l,c                           ;[2ebc] 69
                    ld        l,(hl)                        ;[2ebd] 6e
                    ld        (hl),h                        ;[2ebe] 74
                    ld        h,l                           ;[2ebf] 65
                    ld        (hl),d                        ;[2ec0] 72
                    jr        nz,$2f07                      ;[2ec1] 20 44
                    ld        b,c                           ;[2ec3] 41
                    ld        d,h                           ;[2ec4] 54
                    ld        b,c                           ;[2ec5] 41
                    jr        nz,$2f3c                      ;[2ec6] 20 74
                    ld        h,l                           ;[2ec8] 65
                    ld        (hl),e                        ;[2ec9] 73
                    ld        (hl),h                        ;[2eca] 74
                    jr        nz,$2f33                      ;[2ecb] 20 66
                    ld        h,c                           ;[2ecd] 61
                    ld        l,c                           ;[2ece] 69
                    ld        l,h                           ;[2ecf] 6c
                    ld        h,l                           ;[2ed0] 65
                    ld        h,h                           ;[2ed1] 64
                    dec       c                             ;[2ed2] 0d
                    rst       $38                           ;[2ed3] ff
                    adc       c                             ;[2ed4] 89
                    ld        l,$43                         ;[2ed5] 2e 43
                    ld        l,$ba                         ;[2ed7] 2e ba
                    ld        l,$a0                         ;[2ed9] 2e a0
                    ld        l,$56                         ;[2edb] 2e 56
                    ld        l,$71                         ;[2edd] 2e 71
                    ld        l,$01                         ;[2edf] 2e 01
                    ld        l,$7b                         ;[2ee1] 2e 7b
                    dec       l                             ;[2ee3] 2d
                    cp        (hl)                          ;[2ee4] be
                    dec       l                             ;[2ee5] 2d
                    xor       l                             ;[2ee6] ad
                    dec       l                             ;[2ee7] 2d
                    sub       d                             ;[2ee8] 92
                    dec       l                             ;[2ee9] 2d
                    pop       de                            ;[2eea] d1
                    dec       l                             ;[2eeb] 2d
                    rst       $20                           ;[2eec] e7
                    dec       l                             ;[2eed] 2d
                    dec       l                             ;[2eee] 2d
                    ld        l,$17                         ;[2eef] 2e 17
                    ld        l,$67                         ;[2ef1] 2e 67
                    dec       l                             ;[2ef3] 2d
                    ld        d,$05                         ;[2ef4] 16 05
                    dec       b                             ;[2ef6] 05
                    ld        (de),a                        ;[2ef7] 12
                    ld        bc,$4f52                      ;[2ef8] 01 52 4f
                    ld        c,l                           ;[2efb] 4d
                    jr        nz,$2f52                      ;[2efc] 20 54
                    ld        b,l                           ;[2efe] 45
                    ld        d,e                           ;[2eff] 53
                    ld        d,h                           ;[2f00] 54
                    jr        nz,$2f49                      ;[2f01] 20 46
                    ld        b,c                           ;[2f03] 41
                    ld        c,c                           ;[2f04] 49
                    ld        c,h                           ;[2f05] 4c
                    ld        b,l                           ;[2f06] 45
                    ld        b,h                           ;[2f07] 44
                    ld        (de),a                        ;[2f08] 12
                    nop                                     ;[2f09] 00
                    dec       c                             ;[2f0a] 0d
                    rst       $38                           ;[2f0b] ff
                    ld        d,$05                         ;[2f0c] 16 05
                    dec       b                             ;[2f0e] 05
                    ld        (de),a                        ;[2f0f] 12
                    ld        bc,$4152                      ;[2f10] 01 52 41
                    ld        c,l                           ;[2f13] 4d
                    jr        nz,$2f6a                      ;[2f14] 20 54
                    ld        b,l                           ;[2f16] 45
                    ld        d,e                           ;[2f17] 53
                    ld        d,h                           ;[2f18] 54
                    jr        nz,$2f61                      ;[2f19] 20 46
                    ld        b,c                           ;[2f1b] 41
                    ld        c,c                           ;[2f1c] 49
                    ld        c,h                           ;[2f1d] 4c
                    ld        b,l                           ;[2f1e] 45
                    ld        b,h                           ;[2f1f] 44
                    ld        (de),a                        ;[2f20] 12
                    nop                                     ;[2f21] 00
                    dec       c                             ;[2f22] 0d
                    rst       $38                           ;[2f23] ff
                    ld        d,$02                         ;[2f24] 16 02
                    nop                                     ;[2f26] 00
                    jr        nz,$2f67                      ;[2f27] 20 3e
                    ld        a,$3e                         ;[2f29] 3e 3e
                    jr        nz,$2f80                      ;[2f2b] 20 53
                    ld        d,b                           ;[2f2d] 50
                    ld        b,l                           ;[2f2e] 45
                    ld        b,e                           ;[2f2f] 43
                    ld        d,h                           ;[2f30] 54
                    ld        d,d                           ;[2f31] 52
                    ld        d,l                           ;[2f32] 55
                    ld        c,l                           ;[2f33] 4d
                    jr        nz,$2f81                      ;[2f34] 20 4b
                    ld        b,l                           ;[2f36] 45
                    ld        e,c                           ;[2f37] 59
                    ld        b,d                           ;[2f38] 42
                    ld        c,a                           ;[2f39] 4f
                    ld        b,c                           ;[2f3a] 41
                    ld        d,d                           ;[2f3b] 52
                    ld        b,h                           ;[2f3c] 44
                    jr        nz,$2f93                      ;[2f3d] 20 54
                    ld        b,l                           ;[2f3f] 45
                    ld        d,e                           ;[2f40] 53
                    ld        d,h                           ;[2f41] 54
                    jr        nz,$2f80                      ;[2f42] 20 3c
                    inc       a                             ;[2f44] 3c
                    inc       a                             ;[2f45] 3c
                    jr        nz,$2f55                      ;[2f46] 20 0d
                    dec       c                             ;[2f48] 0d
                    inc       d                             ;[2f49] 14
                    ld        bc,$2020                      ;[2f4a] 01 20 20
                    jr        nz,$2f6f                      ;[2f4d] 20 20
                    jr        nz,$2f71                      ;[2f4f] 20 20
                    jr        nz,$2f73                      ;[2f51] 20 20
                    jr        nz,$2f75                      ;[2f53] 20 20
                    jr        nz,$2f77                      ;[2f55] 20 20
                    jr        nz,$2f79                      ;[2f57] 20 20
                    jr        nz,$2f7b                      ;[2f59] 20 20
                    jr        nz,$2f7d                      ;[2f5b] 20 20
                    jr        nz,$2f7f                      ;[2f5d] 20 20
                    jr        nz,$2f81                      ;[2f5f] 20 20
                    jr        nz,$2f83                      ;[2f61] 20 20
                    jr        nz,$2f85                      ;[2f63] 20 20
                    jr        nz,$2f87                      ;[2f65] 20 20
                    jr        nz,$2f89                      ;[2f67] 20 20
                    jr        nz,$2f8b                      ;[2f69] 20 20
                    jr        nz,$2fe1                      ;[2f6b] 20 74
                    jr        nz,$2fd8                      ;[2f6d] 20 69
                    jr        nz,$2fa2                      ;[2f6f] 20 31
                    jr        nz,$2fa5                      ;[2f71] 20 32
                    jr        nz,$2fa8                      ;[2f73] 20 33
                    jr        nz,$2fab                      ;[2f75] 20 34
                    jr        nz,$2fae                      ;[2f77] 20 35
                    jr        nz,$2fb1                      ;[2f79] 20 36
                    jr        nz,$2fb4                      ;[2f7b] 20 37
                    jr        nz,$2fb7                      ;[2f7d] 20 38
                    jr        nz,$2fba                      ;[2f7f] 20 39
                    jr        nz,$2fb3                      ;[2f81] 20 30
                    jr        nz,$2fa5                      ;[2f83] 20 20
                    jr        nz,$2fe9                      ;[2f85] 20 62
                    jr        nz,$2fa9                      ;[2f87] 20 20
                    jr        nz,$2fab                      ;[2f89] 20 20
                    jr        nz,$2fad                      ;[2f8b] 20 20
                    jr        nz,$2faf                      ;[2f8d] 20 20
                    jr        nz,$2fb1                      ;[2f8f] 20 20
                    jr        nz,$2fb3                      ;[2f91] 20 20
                    jr        nz,$2fb5                      ;[2f93] 20 20
                    jr        nz,$2fb7                      ;[2f95] 20 20
                    jr        nz,$2fb9                      ;[2f97] 20 20
                    jr        nz,$2fbb                      ;[2f99] 20 20
                    jr        nz,$2fbd                      ;[2f9b] 20 20
                    jr        nz,$2fbf                      ;[2f9d] 20 20
                    jr        nz,$2fc1                      ;[2f9f] 20 20
                    jr        nz,$2fc3                      ;[2fa1] 20 20
                    jr        nz,$2fc5                      ;[2fa3] 20 20
                    jr        nz,$2fc7                      ;[2fa5] 20 20
                    jr        nz,$2fc9                      ;[2fa7] 20 20
                    jr        nz,$2fcb                      ;[2fa9] 20 20
                    jr        nz,$3011                      ;[2fab] 20 64
                    jr        nz,$3016                      ;[2fad] 20 67
                    jr        nz,$2fd1                      ;[2faf] 20 20
                    jr        nz,$3004                      ;[2fb1] 20 51
                    jr        nz,$300c                      ;[2fb3] 20 57
                    jr        nz,$2ffc                      ;[2fb5] 20 45
                    jr        nz,$300b                      ;[2fb7] 20 52
                    jr        nz,$300f                      ;[2fb9] 20 54
                    jr        nz,$3016                      ;[2fbb] 20 59
                    jr        nz,$3014                      ;[2fbd] 20 55
                    jr        nz,$300a                      ;[2fbf] 20 49
                    jr        nz,$3012                      ;[2fc1] 20 4f
                    jr        nz,$3015                      ;[2fc3] 20 50
                    jr        nz,$2fe7                      ;[2fc5] 20 20
                    jr        nz,$2fe9                      ;[2fc7] 20 20
                    jr        nz,$2feb                      ;[2fc9] 20 20
                    jr        nz,$2fed                      ;[2fcb] 20 20
                    jr        nz,$2fef                      ;[2fcd] 20 20
                    jr        nz,$2ff1                      ;[2fcf] 20 20
                    jr        nz,$2ff3                      ;[2fd1] 20 20
                    jr        nz,$2ff5                      ;[2fd3] 20 20
                    jr        nz,$2ff7                      ;[2fd5] 20 20
                    jr        nz,$2ff9                      ;[2fd7] 20 20
                    jr        nz,$2ffb                      ;[2fd9] 20 20
                    jr        nz,$2ffd                      ;[2fdb] 20 20
                    jr        nz,$2fff                      ;[2fdd] 20 20
                    jr        nz,$3001                      ;[2fdf] 20 20
                    jr        nz,$3003                      ;[2fe1] 20 20
                    jr        nz,$3005                      ;[2fe3] 20 20
                    jr        nz,$3007                      ;[2fe5] 20 20
                    jr        nz,$3009                      ;[2fe7] 20 20
                    jr        nz,$300b                      ;[2fe9] 20 20
                    jr        nz,$3052                      ;[2feb] 20 65
                    jr        nz,$3054                      ;[2fed] 20 65
                    jr        nz,$3011                      ;[2fef] 20 20
                    jr        nz,$3034                      ;[2ff1] 20 41
                    jr        nz,$3048                      ;[2ff3] 20 53
                    jr        nz,$303b                      ;[2ff5] 20 44
                    jr        nz,$303f                      ;[2ff7] 20 46
                    jr        nz,$3042                      ;[2ff9] 20 47
                    jr        nz,$3045                      ;[2ffb] 20 48
                    jr        nz,$3049                      ;[2ffd] 20 4a
                    jr        nz,$304c                      ;[2fff] 20 4b
                    jr        nz,$304f                      ;[3001] 20 4c
                    jr        nz,$3025                      ;[3003] 20 20
                    jr        nz,$306c                      ;[3005] 20 65
                    jr        nz,$3029                      ;[3007] 20 20
                    jr        nz,$302b                      ;[3009] 20 20
                    jr        nz,$302d                      ;[300b] 20 20
                    jr        nz,$302f                      ;[300d] 20 20
                    jr        nz,$3031                      ;[300f] 20 20
                    jr        nz,$3033                      ;[3011] 20 20
                    jr        nz,$3035                      ;[3013] 20 20
                    jr        nz,$3037                      ;[3015] 20 20
                    jr        nz,$3039                      ;[3017] 20 20
                    jr        nz,$303b                      ;[3019] 20 20
                    jr        nz,$303d                      ;[301b] 20 20
                    jr        nz,$303f                      ;[301d] 20 20
                    jr        nz,$3041                      ;[301f] 20 20
                    jr        nz,$3043                      ;[3021] 20 20
                    jr        nz,$3045                      ;[3023] 20 20
                    jr        nz,$3047                      ;[3025] 20 20
                    jr        nz,$3049                      ;[3027] 20 20
                    jr        nz,$304b                      ;[3029] 20 20
                    jr        nz,$3090                      ;[302b] 20 63
                    jr        nz,$3092                      ;[302d] 20 63
                    jr        nz,$3051                      ;[302f] 20 20
                    jr        nz,$308d                      ;[3031] 20 5a
                    jr        nz,$308d                      ;[3033] 20 58
                    jr        nz,$307a                      ;[3035] 20 43
                    jr        nz,$308f                      ;[3037] 20 56
                    jr        nz,$307d                      ;[3039] 20 42
                    jr        nz,$308b                      ;[303b] 20 4e
                    jr        nz,$308c                      ;[303d] 20 4d
                    jr        nz,$306f                      ;[303f] 20 2e
                    jr        nz,$3063                      ;[3041] 20 20
                    jr        nz,$3065                      ;[3043] 20 20
                    jr        nz,$30aa                      ;[3045] 20 63
                    jr        nz,$3069                      ;[3047] 20 20
                    jr        nz,$306b                      ;[3049] 20 20
                    jr        nz,$306d                      ;[304b] 20 20
                    jr        nz,$306f                      ;[304d] 20 20
                    jr        nz,$3071                      ;[304f] 20 20
                    jr        nz,$3073                      ;[3051] 20 20
                    jr        nz,$3075                      ;[3053] 20 20
                    jr        nz,$3077                      ;[3055] 20 20
                    jr        nz,$3079                      ;[3057] 20 20
                    jr        nz,$307b                      ;[3059] 20 20
                    jr        nz,$307d                      ;[305b] 20 20
                    jr        nz,$307f                      ;[305d] 20 20
                    jr        nz,$3081                      ;[305f] 20 20
                    jr        nz,$3083                      ;[3061] 20 20
                    jr        nz,$3085                      ;[3063] 20 20
                    jr        nz,$3087                      ;[3065] 20 20
                    jr        nz,$3089                      ;[3067] 20 20
                    jr        nz,$308b                      ;[3069] 20 20
                    jr        nz,$30e0                      ;[306b] 20 73
                    jr        nz,$30aa                      ;[306d] 20 3b
                    jr        nz,$3093                      ;[306f] 20 22
                    jr        nz,$30af                      ;[3071] 20 3c
                    jr        nz,$30b3                      ;[3073] 20 3e
                    jr        nz,$3097                      ;[3075] 20 20
                    jr        nz,$3099                      ;[3077] 20 20
                    jr        nz,$30ce                      ;[3079] 20 53
                    jr        nz,$309d                      ;[307b] 20 20
                    jr        nz,$309f                      ;[307d] 20 20
                    jr        nz,$30df                      ;[307f] 20 5e
                    jr        nz,$30f9                      ;[3081] 20 76
                    jr        nz,$30b1                      ;[3083] 20 2c
                    jr        nz,$30fa                      ;[3085] 20 73
                    jr        nz,$30a9                      ;[3087] 20 20
                    jr        nz,$30ab                      ;[3089] 20 20
                    jr        nz,$30ad                      ;[308b] 20 20
                    jr        nz,$30af                      ;[308d] 20 20
                    jr        nz,$30b1                      ;[308f] 20 20
                    jr        nz,$30b3                      ;[3091] 20 20
                    jr        nz,$30b5                      ;[3093] 20 20
                    jr        nz,$30b7                      ;[3095] 20 20
                    jr        nz,$30b9                      ;[3097] 20 20
                    jr        nz,$30bb                      ;[3099] 20 20
                    jr        nz,$30bd                      ;[309b] 20 20
                    jr        nz,$30bf                      ;[309d] 20 20
                    jr        nz,$30c1                      ;[309f] 20 20
                    jr        nz,$30c3                      ;[30a1] 20 20
                    jr        nz,$30c5                      ;[30a3] 20 20
                    jr        nz,$30c7                      ;[30a5] 20 20
                    jr        nz,$30c9                      ;[30a7] 20 20
                    jr        nz,$30cb                      ;[30a9] 20 20
                    jr        nz,$30cd                      ;[30ab] 20 20
                    jr        nz,$30cf                      ;[30ad] 20 20
                    jr        nz,$30d1                      ;[30af] 20 20
                    jr        nz,$30d3                      ;[30b1] 20 20
                    jr        nz,$30d5                      ;[30b3] 20 20
                    jr        nz,$30d7                      ;[30b5] 20 20
                    jr        nz,$30d9                      ;[30b7] 20 20
                    jr        nz,$30db                      ;[30b9] 20 20
                    jr        nz,$30dd                      ;[30bb] 20 20
                    jr        nz,$30df                      ;[30bd] 20 20
                    jr        nz,$30e1                      ;[30bf] 20 20
                    jr        nz,$30e3                      ;[30c1] 20 20
                    jr        nz,$30e5                      ;[30c3] 20 20
                    jr        nz,$30e7                      ;[30c5] 20 20
                    jr        nz,$30e9                      ;[30c7] 20 20
                    jr        nz,$30eb                      ;[30c9] 20 20
                    inc       d                             ;[30cb] 14
                    nop                                     ;[30cc] 00
                    ld        d,$14                         ;[30cd] 16 14
                    dec       b                             ;[30cf] 05
                    ld        (de),a                        ;[30d0] 12
                    ld        bc,$4554                      ;[30d1] 01 54 45
                    ld        d,e                           ;[30d4] 53
                    ld        d,h                           ;[30d5] 54
                    jr        nz,$3119                      ;[30d6] 20 41
                    ld        c,h                           ;[30d8] 4c
                    ld        c,h                           ;[30d9] 4c
                    jr        nz,$3130                      ;[30da] 20 54
                    ld        c,b                           ;[30dc] 48
                    ld        b,l                           ;[30dd] 45
                    jr        nz,$312b                      ;[30de] 20 4b
                    ld        b,l                           ;[30e0] 45
                    ld        e,c                           ;[30e1] 59
                    ld        d,e                           ;[30e2] 53
                    ld        (de),a                        ;[30e3] 12
                    nop                                     ;[30e4] 00
                    rst       $38                           ;[30e5] ff
                    ld        d,$05                         ;[30e6] 16 05
                    dec       b                             ;[30e8] 05
                    inc       d                             ;[30e9] 14
                    ld        bc,$5320                      ;[30ea] 01 20 53
                    ld        e,c                           ;[30ed] 59
                    ld        c,l                           ;[30ee] 4d
                    jr        nz,$3144                      ;[30ef] 20 53
                    ld        c,b                           ;[30f1] 48
                    ld        b,(hl)                        ;[30f2] 46
                    ld        d,h                           ;[30f3] 54
                    cpl                                     ;[30f4] 2f
                    ld        b,c                           ;[30f5] 41
                    jr        nz,$3118                      ;[30f6] 20 20
                    ld        d,h                           ;[30f8] 54
                    ld        b,l                           ;[30f9] 45
                    ld        d,e                           ;[30fa] 53
                    ld        d,h                           ;[30fb] 54
                    jr        nz,$3114                      ;[30fc] 20 16
                    ld        a,(bc)                        ;[30fe] 0a
                    dec       b                             ;[30ff] 05
                    jr        nz,$3122                      ;[3100] 20 20
                    jr        nz,$3154                      ;[3102] 20 50
                    ld        (hl),d                        ;[3104] 72
                    ld        h,l                           ;[3105] 65
                    ld        (hl),e                        ;[3106] 73
                    ld        (hl),e                        ;[3107] 73
                    jr        nz,$3120                      ;[3108] 20 16
                    inc       c                             ;[310a] 0c
                    dec       b                             ;[310b] 05
                    ld        (de),a                        ;[310c] 12
                    ld        bc,$5953                      ;[310d] 01 53 59
                    ld        c,l                           ;[3110] 4d
                    jr        nz,$3166                      ;[3111] 20 53
                    ld        c,b                           ;[3113] 48
                    ld        b,(hl)                        ;[3114] 46
                    ld        d,h                           ;[3115] 54
                    cpl                                     ;[3116] 2f
                    ld        b,c                           ;[3117] 41
                    ld        (de),a                        ;[3118] 12
                    nop                                     ;[3119] 00
                    ld        d,$0e                         ;[311a] 16 0e
                    dec       b                             ;[311c] 05
                    jr        nz,$3185                      ;[311d] 20 66
                    ld        l,a                           ;[311f] 6f
                    ld        (hl),d                        ;[3120] 72
                    jr        nz,$3154                      ;[3121] 20 31
                    jr        nz,$3198                      ;[3123] 20 73
                    ld        h,l                           ;[3125] 65
                    ld        h,e                           ;[3126] 63
                    jr        nz,$313d                      ;[3127] 20 14
                    nop                                     ;[3129] 00
                    rst       $38                           ;[312a] ff
                    ld        d,$09                         ;[312b] 16 09
                    nop                                     ;[312d] 00
                    inc       d                             ;[312e] 14
                    ld        bc,$5320                      ;[312f] 01 20 53
                    ld        d,b                           ;[3132] 50
                    ld        b,l                           ;[3133] 45
                    ld        b,e                           ;[3134] 43
                    ld        d,h                           ;[3135] 54
                    ld        d,d                           ;[3136] 52
                    ld        d,l                           ;[3137] 55
                    ld        c,l                           ;[3138] 4d
                    jr        nz,$3166                      ;[3139] 20 2b
                    inc       sp                            ;[313b] 33
                    jr        nz,$31b2                      ;[313c] 20 74
                    ld        h,l                           ;[313e] 65
                    ld        (hl),e                        ;[313f] 73
                    ld        (hl),h                        ;[3140] 74
                    jr        nz,$31b3                      ;[3141] 20 70
                    ld        (hl),d                        ;[3143] 72
                    ld        l,a                           ;[3144] 6f
                    ld        h,a                           ;[3145] 67
                    ld        (hl),d                        ;[3146] 72
                    ld        h,c                           ;[3147] 61
                    ld        l,l                           ;[3148] 6d
                    jr        nz,$31a1                      ;[3149] 20 56
                    jr        nz,$3181                      ;[314b] 20 34
                    ld        l,$31                         ;[314d] 2e 31
                    jr        nz,$3171                      ;[314f] 20 20
                    ld        b,c                           ;[3151] 41
                    ld        c,l                           ;[3152] 4d
                    ld        d,e                           ;[3153] 53
                    ld        d,h                           ;[3154] 54
                    ld        d,d                           ;[3155] 52
                    ld        b,c                           ;[3156] 41
                    ld        b,h                           ;[3157] 44
                    jr        nz,$318b                      ;[3158] 20 31
                    add       hl,sp                         ;[315a] 39
                    jr        c,$3194                       ;[315b] 38 37
                    ld        l,$2e                         ;[315d] 2e 2e
                    ld        l,$20                         ;[315f] 2e 20
                    jr        nz,$31c5                      ;[3161] 20 62
                    ld        a,c                           ;[3163] 79
                    jr        nz,$31b8                      ;[3164] 20 52
                    ld        b,a                           ;[3166] 47
                    cpl                                     ;[3167] 2f
                    ld        b,e                           ;[3168] 43
                    ld        c,h                           ;[3169] 4c
                    cpl                                     ;[316a] 2f
                    ld        d,(hl)                        ;[316b] 56
                    ld        c,a                           ;[316c] 4f
                    jr        nz,$317c                      ;[316d] 20 0d
                    jr        nz,$3191                      ;[316f] 20 20
                    jr        nz,$3193                      ;[3171] 20 20
                    jr        nz,$3195                      ;[3173] 20 20
                    jr        nz,$3197                      ;[3175] 20 20
                    ld        b,e                           ;[3177] 43
                    ld        l,b                           ;[3178] 68
                    ld        h,l                           ;[3179] 65
                    ld        h,e                           ;[317a] 63
                    ld        l,e                           ;[317b] 6b
                    jr        nz,$31d2                      ;[317c] 20 54
                    ld        d,(hl)                        ;[317e] 56
                    jr        nz,$31f5                      ;[317f] 20 74
                    ld        (hl),l                        ;[3181] 75
                    ld        l,(hl)                        ;[3182] 6e
                    ld        l,c                           ;[3183] 69
                    ld        l,(hl)                        ;[3184] 6e
                    ld        h,a                           ;[3185] 67
                    jr        nz,$31a8                      ;[3186] 20 20
                    jr        nz,$31aa                      ;[3188] 20 20
                    jr        nz,$31ac                      ;[318a] 20 20
                    jr        nz,$31ae                      ;[318c] 20 20
                    jr        nz,$31f3                      ;[318e] 20 63
                    ld        l,a                           ;[3190] 6f
                    ld        l,(hl)                        ;[3191] 6e
                    ld        l,(hl)                        ;[3192] 6e
                    ld        h,l                           ;[3193] 65
                    ld        h,e                           ;[3194] 63
                    ld        (hl),h                        ;[3195] 74
                    jr        nz,$320c                      ;[3196] 20 74
                    ld        l,b                           ;[3198] 68
                    ld        h,l                           ;[3199] 65
                    jr        nz,$3208                      ;[319a] 20 6c
                    ld        l,a                           ;[319c] 6f
                    ld        l,a                           ;[319d] 6f
                    ld        (hl),b                        ;[319e] 70
                    ld        h,d                           ;[319f] 62
                    ld        h,c                           ;[31a0] 61
                    ld        h,e                           ;[31a1] 63
                    ld        l,e                           ;[31a2] 6b
                    jr        nz,$3208                      ;[31a3] 20 63
                    ld        l,a                           ;[31a5] 6f
                    ld        l,(hl)                        ;[31a6] 6e
                    ld        l,(hl)                        ;[31a7] 6e
                    ld        h,l                           ;[31a8] 65
                    ld        h,e                           ;[31a9] 63
                    ld        (hl),h                        ;[31aa] 74
                    ld        l,a                           ;[31ab] 6f
                    ld        (hl),d                        ;[31ac] 72
                    ld        hl,$0d20                      ;[31ad] 21 20 0d
                    ld        d,b                           ;[31b0] 50
                    ld        (hl),d                        ;[31b1] 72
                    ld        h,l                           ;[31b2] 65
                    ld        (hl),e                        ;[31b3] 73
                    ld        (hl),e                        ;[31b4] 73
                    jr        nz,$3212                      ;[31b5] 20 5b
                    ld        b,l                           ;[31b7] 45
                    ld        c,(hl)                        ;[31b8] 4e
                    ld        d,h                           ;[31b9] 54
                    ld        b,l                           ;[31ba] 45
                    ld        d,d                           ;[31bb] 52
                    ld        e,l                           ;[31bc] 5d
                    jr        nz,$3228                      ;[31bd] 20 69
                    ld        h,(hl)                        ;[31bf] 66
                    jr        nz,$3225                      ;[31c0] 20 63
                    ld        l,a                           ;[31c2] 6f
                    ld        l,h                           ;[31c3] 6c
                    ld        l,a                           ;[31c4] 6f
                    ld        (hl),l                        ;[31c5] 75
                    ld        (hl),d                        ;[31c6] 72
                    jr        nz,$3232                      ;[31c7] 20 69
                    ld        (hl),e                        ;[31c9] 73
                    jr        nz,$321b                      ;[31ca] 20 4f
                    ld        c,e                           ;[31cc] 4b
                    inc       l                             ;[31cd] 2c
                    jr        nz,$31f0                      ;[31ce] 20 20
                    ld        (hl),b                        ;[31d0] 70
                    ld        (hl),d                        ;[31d1] 72
                    ld        h,l                           ;[31d2] 65
                    ld        (hl),e                        ;[31d3] 73
                    ld        (hl),e                        ;[31d4] 73
                    jr        nz,$3232                      ;[31d5] 20 5b
                    ld        d,e                           ;[31d7] 53
                    ld        d,b                           ;[31d8] 50
                    ld        b,c                           ;[31d9] 41
                    ld        b,e                           ;[31da] 43
                    ld        b,l                           ;[31db] 45
                    ld        e,l                           ;[31dc] 5d
                    jr        nz,$3248                      ;[31dd] 20 69
                    ld        h,(hl)                        ;[31df] 66
                    jr        nz,$324b                      ;[31e0] 20 69
                    ld        (hl),h                        ;[31e2] 74
                    jr        nz,$324e                      ;[31e3] 20 69
                    ld        (hl),e                        ;[31e5] 73
                    jr        nz,$3256                      ;[31e6] 20 6e
                    ld        l,a                           ;[31e8] 6f
                    ld        (hl),h                        ;[31e9] 74
                    jr        nz,$320c                      ;[31ea] 20 20
                    jr        nz,$320e                      ;[31ec] 20 20
                    jr        nz,$3210                      ;[31ee] 20 20
                    dec       c                             ;[31f0] 0d
                    ld        (de),a                        ;[31f1] 12
                    ld        bc,$540d                      ;[31f2] 01 0d 54
                    ld        b,c                           ;[31f5] 41
                    ld        c,e                           ;[31f6] 4b
                    ld        b,l                           ;[31f7] 45
                    jr        nz,$323d                      ;[31f8] 20 43
                    ld        b,c                           ;[31fa] 41
                    ld        d,d                           ;[31fb] 52
                    ld        b,l                           ;[31fc] 45
                    jr        nz,$322c                      ;[31fd] 20 2d
                    jr        nz,$3255                      ;[31ff] 20 54
                    ld        c,b                           ;[3201] 48
                    ld        b,l                           ;[3202] 45
                    ld        d,e                           ;[3203] 53
                    ld        b,l                           ;[3204] 45
                    jr        nz,$325b                      ;[3205] 20 54
                    ld        b,l                           ;[3207] 45
                    ld        d,e                           ;[3208] 53
                    ld        d,h                           ;[3209] 54
                    ld        d,e                           ;[320a] 53
                    jr        nz,$3250                      ;[320b] 20 43
                    ld        c,a                           ;[320d] 4f
                    ld        d,d                           ;[320e] 52
                    ld        d,d                           ;[320f] 52
                    ld        d,l                           ;[3210] 55
                    ld        d,b                           ;[3211] 50
                    ld        d,h                           ;[3212] 54
                    jr        nz,$3259                      ;[3213] 20 44
                    ld        c,c                           ;[3215] 49
                    ld        d,e                           ;[3216] 53
                    ld        c,e                           ;[3217] 4b
                    ld        d,e                           ;[3218] 53
                    inc       l                             ;[3219] 2c
                    jr        nz,$325d                      ;[321a] 20 41
                    ld        c,(hl)                        ;[321c] 4e
                    ld        b,h                           ;[321d] 44
                    jr        nz,$3272                      ;[321e] 20 52
                    ld        b,l                           ;[3220] 45
                    ld        d,c                           ;[3221] 51
                    ld        d,l                           ;[3222] 55
                    ld        c,c                           ;[3223] 49
                    ld        d,d                           ;[3224] 52
                    ld        b,l                           ;[3225] 45
                    jr        nz,$326e                      ;[3226] 20 46
                    ld        b,c                           ;[3228] 41
                    ld        b,e                           ;[3229] 43
                    ld        d,h                           ;[322a] 54
                    ld        c,a                           ;[322b] 4f
                    ld        d,d                           ;[322c] 52
                    ld        e,c                           ;[322d] 59
                    jr        nz,$3284                      ;[322e] 20 54
                    ld        b,l                           ;[3230] 45
                    ld        d,e                           ;[3231] 53
                    ld        d,h                           ;[3232] 54
                    jr        nz,$327a                      ;[3233] 20 45
                    ld        d,c                           ;[3235] 51
                    ld        d,l                           ;[3236] 55
                    ld        c,c                           ;[3237] 49
                    ld        d,b                           ;[3238] 50
                    ld        c,l                           ;[3239] 4d
                    ld        b,l                           ;[323a] 45
                    ld        c,(hl)                        ;[323b] 4e
                    ld        d,h                           ;[323c] 54
                    ld        hl,$1420                      ;[323d] 21 20 14
                    nop                                     ;[3240] 00
                    ld        e,c                           ;[3241] 59
                    ld        c,a                           ;[3242] 4f
                    ld        d,l                           ;[3243] 55
                    jr        nz,$328e                      ;[3244] 20 48
                    ld        b,c                           ;[3246] 41
                    ld        d,(hl)                        ;[3247] 56
                    ld        b,l                           ;[3248] 45
                    jr        nz,$328d                      ;[3249] 20 42
                    ld        b,l                           ;[324b] 45
                    ld        b,l                           ;[324c] 45
                    ld        c,(hl)                        ;[324d] 4e
                    jr        nz,$32a7                      ;[324e] 20 57
                    ld        b,c                           ;[3250] 41
                    ld        d,d                           ;[3251] 52
                    ld        c,(hl)                        ;[3252] 4e
                    ld        b,l                           ;[3253] 45
                    ld        b,h                           ;[3254] 44
                    ld        hl,$0014                      ;[3255] 21 14 00
                    ld        (de),a                        ;[3258] 12
                    nop                                     ;[3259] 00
                    rst       $38                           ;[325a] ff
                    ld        d,$05                         ;[325b] 16 05
                    dec       b                             ;[325d] 05
                    inc       d                             ;[325e] 14
                    ld        bc,$5520                      ;[325f] 01 20 55
                    ld        c,h                           ;[3262] 4c
                    ld        b,c                           ;[3263] 41
                    jr        nz,$32ba                      ;[3264] 20 54
                    ld        b,l                           ;[3266] 45
                    ld        d,e                           ;[3267] 53
                    ld        d,h                           ;[3268] 54
                    jr        nz,$327f                      ;[3269] 20 14
                    nop                                     ;[326b] 00
                    dec       c                             ;[326c] 0d
                    rst       $38                           ;[326d] ff
                    ld        d,$05                         ;[326e] 16 05
                    rlca                                    ;[3270] 07
                    inc       d                             ;[3271] 14
                    ld        bc,$4152                      ;[3272] 01 52 41
                    ld        c,l                           ;[3275] 4d
                    jr        nz,$32bc                      ;[3276] 20 44
                    ld        b,c                           ;[3278] 41
                    ld        d,h                           ;[3279] 54
                    ld        b,c                           ;[327a] 41
                    jr        nz,$32d1                      ;[327b] 20 54
                    ld        b,l                           ;[327d] 45
                    ld        d,e                           ;[327e] 53
                    ld        d,h                           ;[327f] 54
                    ld        d,e                           ;[3280] 53
                    ld        d,$0a                         ;[3281] 16 0a
                    dec       b                             ;[3283] 05
                    ld        l,$2e                         ;[3284] 2e 2e
                    jr        nz,$32db                      ;[3286] 20 53
                    ld        d,h                           ;[3288] 54
                    ld        b,c                           ;[3289] 41
                    ld        d,d                           ;[328a] 52
                    ld        d,h                           ;[328b] 54
                    ld        c,c                           ;[328c] 49
                    ld        c,(hl)                        ;[328d] 4e
                    ld        b,a                           ;[328e] 47
                    jr        nz,$32df                      ;[328f] 20 4e
                    ld        c,a                           ;[3291] 4f
                    ld        d,a                           ;[3292] 57
                    jr        nz,$32c3                      ;[3293] 20 2e
                    ld        l,$14                         ;[3295] 2e 14
                    nop                                     ;[3297] 00
                    dec       c                             ;[3298] 0d
                    rst       $38                           ;[3299] ff
                    ld        d,$05                         ;[329a] 16 05
                    dec       b                             ;[329c] 05
                    inc       d                             ;[329d] 14
                    ld        bc,$4720                      ;[329e] 01 20 47
                    ld        c,c                           ;[32a1] 49
                    jr        nz,$32f7                      ;[32a2] 20 53
                    ld        c,a                           ;[32a4] 4f
                    ld        d,l                           ;[32a5] 55
                    ld        c,(hl)                        ;[32a6] 4e
                    ld        b,h                           ;[32a7] 44
                    jr        nz,$32fe                      ;[32a8] 20 54
                    ld        b,l                           ;[32aa] 45
                    ld        d,e                           ;[32ab] 53
                    ld        d,h                           ;[32ac] 54
                    jr        nz,$32bc                      ;[32ad] 20 0d
                    dec       c                             ;[32af] 0d
                    inc       d                             ;[32b0] 14
                    nop                                     ;[32b1] 00
                    rst       $38                           ;[32b2] ff
                    ld        d,$05                         ;[32b3] 16 05
                    dec       b                             ;[32b5] 05
                    inc       d                             ;[32b6] 14
                    ld        bc,$5520                      ;[32b7] 01 20 55
                    ld        c,h                           ;[32ba] 4c
                    ld        b,c                           ;[32bb] 41
                    jr        nz,$3311                      ;[32bc] 20 53
                    ld        c,a                           ;[32be] 4f
                    ld        d,l                           ;[32bf] 55
                    ld        c,(hl)                        ;[32c0] 4e
                    ld        b,h                           ;[32c1] 44
                    jr        nz,$3318                      ;[32c2] 20 54
                    ld        b,l                           ;[32c4] 45
                    ld        d,e                           ;[32c5] 53
                    ld        d,h                           ;[32c6] 54
                    jr        nz,$32d6                      ;[32c7] 20 0d
                    dec       c                             ;[32c9] 0d
                    inc       d                             ;[32ca] 14
                    nop                                     ;[32cb] 00
                    rst       $38                           ;[32cc] ff
                    nop                                     ;[32cd] 00
                    ld        d,$05                         ;[32ce] 16 05
                    dec       b                             ;[32d0] 05
                    inc       d                             ;[32d1] 14
                    ld        bc,$4143                      ;[32d2] 01 43 41
                    ld        d,e                           ;[32d5] 53
                    ld        d,e                           ;[32d6] 53
                    ld        b,l                           ;[32d7] 45
                    ld        d,h                           ;[32d8] 54
                    ld        d,h                           ;[32d9] 54
                    ld        b,l                           ;[32da] 45
                    jr        nz,$332c                      ;[32db] 20 4f
                    ld        d,l                           ;[32dd] 55
                    ld        d,h                           ;[32de] 54
                    ld        d,b                           ;[32df] 50
                    ld        d,l                           ;[32e0] 55
                    ld        d,h                           ;[32e1] 54
                    jr        nz,$3338                      ;[32e2] 20 54
                    ld        b,l                           ;[32e4] 45
                    ld        d,e                           ;[32e5] 53
                    ld        d,h                           ;[32e6] 54
                    inc       d                             ;[32e7] 14
                    nop                                     ;[32e8] 00
                    dec       c                             ;[32e9] 0d
                    dec       c                             ;[32ea] 0d
                    rst       $38                           ;[32eb] ff
                    nop                                     ;[32ec] 00
                    ld        d,$00                         ;[32ed] 16 00
                    ld        a,(bc)                        ;[32ef] 0a
                    inc       d                             ;[32f0] 14
                    ld        bc,$4f4a                      ;[32f1] 01 4a 4f
                    ld        e,c                           ;[32f4] 59
                    ld        d,e                           ;[32f5] 53
                    ld        d,h                           ;[32f6] 54
                    ld        c,c                           ;[32f7] 49
                    ld        b,e                           ;[32f8] 43
                    ld        c,e                           ;[32f9] 4b
                    jr        nz,$3350                      ;[32fa] 20 54
                    ld        b,l                           ;[32fc] 45
                    ld        d,e                           ;[32fd] 53
                    ld        d,h                           ;[32fe] 54
                    inc       d                             ;[32ff] 14
                    nop                                     ;[3300] 00
                    dec       c                             ;[3301] 0d
                    dec       c                             ;[3302] 0d
                    ld        c,l                           ;[3303] 4d
                    ld        l,a                           ;[3304] 6f
                    halt                                    ;[3305] 76
                    ld        h,l                           ;[3306] 65
                    jr        nz,$336b                      ;[3307] 20 62
                    ld        l,a                           ;[3309] 6f
                    ld        (hl),h                        ;[330a] 74
                    ld        l,b                           ;[330b] 68
                    jr        nz,$3378                      ;[330c] 20 6a
                    ld        l,a                           ;[330e] 6f
                    ld        a,c                           ;[330f] 79
                    ld        (hl),e                        ;[3310] 73
                    ld        (hl),h                        ;[3311] 74
                    ld        l,c                           ;[3312] 69
                    ld        h,e                           ;[3313] 63
                    ld        l,e                           ;[3314] 6b
                    ld        (hl),e                        ;[3315] 73
                    jr        nz,$3379                      ;[3316] 20 61
                    ld        l,(hl)                        ;[3318] 6e
                    ld        h,h                           ;[3319] 64
                    jr        nz,$338c                      ;[331a] 20 70
                    ld        (hl),d                        ;[331c] 72
                    ld        h,l                           ;[331d] 65
                    ld        (hl),e                        ;[331e] 73
                    ld        (hl),e                        ;[331f] 73
                    dec       c                             ;[3320] 0d
                    ld        (hl),h                        ;[3321] 74
                    ld        l,b                           ;[3322] 68
                    ld        h,l                           ;[3323] 65
                    jr        nz,$336c                      ;[3324] 20 46
                    ld        c,c                           ;[3326] 49
                    ld        d,d                           ;[3327] 52
                    ld        b,l                           ;[3328] 45
                    jr        nz,$338d                      ;[3329] 20 62
                    ld        (hl),l                        ;[332b] 75
                    ld        (hl),h                        ;[332c] 74
                    ld        (hl),h                        ;[332d] 74
                    ld        l,a                           ;[332e] 6f
                    ld        l,(hl)                        ;[332f] 6e
                    ld        (hl),e                        ;[3330] 73
                    jr        nz,$33a8                      ;[3331] 20 75
                    ld        l,(hl)                        ;[3333] 6e
                    ld        (hl),h                        ;[3334] 74
                    ld        l,c                           ;[3335] 69
                    ld        l,h                           ;[3336] 6c
                    jr        nz,$33ad                      ;[3337] 20 74
                    ld        l,b                           ;[3339] 68
                    ld        h,l                           ;[333a] 65
                    dec       c                             ;[333b] 0d
                    ld        l,h                           ;[333c] 6c
                    ld        h,l                           ;[333d] 65
                    ld        (hl),h                        ;[333e] 74
                    ld        (hl),h                        ;[333f] 74
                    ld        h,l                           ;[3340] 65
                    ld        (hl),d                        ;[3341] 72
                    ld        (hl),e                        ;[3342] 73
                    jr        nz,$33a7                      ;[3343] 20 62
                    ld        h,l                           ;[3345] 65
                    ld        l,h                           ;[3346] 6c
                    ld        l,a                           ;[3347] 6f
                    ld        (hl),a                        ;[3348] 77
                    jr        nz,$33ac                      ;[3349] 20 61
                    ld        (hl),d                        ;[334b] 72
                    ld        h,l                           ;[334c] 65
                    jr        nz,$33c6                      ;[334d] 20 77
                    ld        l,c                           ;[334f] 69
                    ld        (hl),b                        ;[3350] 70
                    ld        h,l                           ;[3351] 65
                    ld        h,h                           ;[3352] 64
                    jr        nz,$33c4                      ;[3353] 20 6f
                    ld        (hl),l                        ;[3355] 75
                    ld        (hl),h                        ;[3356] 74
                    dec       c                             ;[3357] 0d
                    dec       c                             ;[3358] 0d
                    ld        d,b                           ;[3359] 50
                    ld        (hl),d                        ;[335a] 72
                    ld        h,l                           ;[335b] 65
                    ld        (hl),e                        ;[335c] 73
                    ld        (hl),e                        ;[335d] 73
                    jr        nz,$33bb                      ;[335e] 20 5b
                    ld        d,e                           ;[3360] 53
                    ld        d,b                           ;[3361] 50
                    ld        b,c                           ;[3362] 41
                    ld        b,e                           ;[3363] 43
                    ld        b,l                           ;[3364] 45
                    ld        e,l                           ;[3365] 5d
                    jr        nz,$33dc                      ;[3366] 20 74
                    ld        l,a                           ;[3368] 6f
                    jr        nz,$33d2                      ;[3369] 20 67
                    ld        l,c                           ;[336b] 69
                    halt                                    ;[336c] 76
                    ld        h,l                           ;[336d] 65
                    jr        nz,$33e5                      ;[336e] 20 75
                    ld        (hl),b                        ;[3370] 70
                    ld        l,$0d                         ;[3371] 2e 0d
                    dec       c                             ;[3373] 0d
                    dec       c                             ;[3374] 0d
                    dec       c                             ;[3375] 0d
                    dec       hl                            ;[3376] 2b
                    dec       l                             ;[3377] 2d
                    dec       l                             ;[3378] 2d
                    dec       l                             ;[3379] 2d
                    dec       l                             ;[337a] 2d
                    dec       l                             ;[337b] 2d
                    ld        c,d                           ;[337c] 4a
                    ld        sp,$2d2d                      ;[337d] 31 2d 2d
                    dec       l                             ;[3380] 2d
                    dec       l                             ;[3381] 2d
                    dec       l                             ;[3382] 2d
                    dec       l                             ;[3383] 2d
                    dec       l                             ;[3384] 2d
                    dec       l                             ;[3385] 2d
                    dec       l                             ;[3386] 2d
                    dec       l                             ;[3387] 2d
                    dec       l                             ;[3388] 2d
                    ld        c,d                           ;[3389] 4a
                    ld        ($2d2d),a                     ;[338a] 32 2d 2d
                    dec       l                             ;[338d] 2d
                    dec       l                             ;[338e] 2d
                    dec       l                             ;[338f] 2d
                    dec       l                             ;[3390] 2d
                    dec       hl                            ;[3391] 2b
                    dec       c                             ;[3392] 0d
                    ld        hl,$2020                      ;[3393] 21 20 20
                    jr        nz,$33b8                      ;[3396] 20 20
                    jr        nz,$33ba                      ;[3398] 20 20
                    jr        nz,$33bc                      ;[339a] 20 20
                    jr        nz,$33be                      ;[339c] 20 20
                    jr        nz,$33c0                      ;[339e] 20 20
                    jr        nz,$33c2                      ;[33a0] 20 20
                    jr        nz,$33c4                      ;[33a2] 20 20
                    jr        nz,$33c6                      ;[33a4] 20 20
                    jr        nz,$33c8                      ;[33a6] 20 20
                    jr        nz,$33ca                      ;[33a8] 20 20
                    jr        nz,$33cc                      ;[33aa] 20 20
                    jr        nz,$33ce                      ;[33ac] 20 20
                    ld        hl,$210d                      ;[33ae] 21 0d 21
                    jr        nz,$33d3                      ;[33b1] 20 20
                    jr        nz,$33d5                      ;[33b3] 20 20
                    jr        nz,$340c                      ;[33b5] 20 55
                    ld        d,b                           ;[33b7] 50
                    jr        nz,$33da                      ;[33b8] 20 20
                    jr        nz,$33dc                      ;[33ba] 20 20
                    jr        nz,$33de                      ;[33bc] 20 20
                    jr        nz,$33e0                      ;[33be] 20 20
                    jr        nz,$33e2                      ;[33c0] 20 20
                    jr        nz,$3419                      ;[33c2] 20 55
                    ld        d,b                           ;[33c4] 50
                    jr        nz,$33e7                      ;[33c5] 20 20
                    jr        nz,$33e9                      ;[33c7] 20 20
                    jr        nz,$33eb                      ;[33c9] 20 20
                    ld        hl,$210d                      ;[33cb] 21 0d 21
                    jr        nz,$33f0                      ;[33ce] 20 20
                    jr        nz,$33f2                      ;[33d0] 20 20
                    jr        nz,$33f4                      ;[33d2] 20 20
                    jr        nz,$33f6                      ;[33d4] 20 20
                    jr        nz,$33f8                      ;[33d6] 20 20
                    jr        nz,$33fa                      ;[33d8] 20 20
                    jr        nz,$33fc                      ;[33da] 20 20
                    jr        nz,$33fe                      ;[33dc] 20 20
                    jr        nz,$3400                      ;[33de] 20 20
                    jr        nz,$3402                      ;[33e0] 20 20
                    jr        nz,$3404                      ;[33e2] 20 20
                    jr        nz,$3406                      ;[33e4] 20 20
                    jr        nz,$3408                      ;[33e6] 20 20
                    ld        hl,$210d                      ;[33e8] 21 0d 21
                    jr        nz,$340d                      ;[33eb] 20 20
                    ld        c,h                           ;[33ed] 4c
                    ld        b,(hl)                        ;[33ee] 46
                    jr        nz,$3437                      ;[33ef] 20 46
                    ld        c,c                           ;[33f1] 49
                    jr        nz,$3446                      ;[33f2] 20 52
                    ld        c,c                           ;[33f4] 49
                    jr        nz,$3417                      ;[33f5] 20 20
                    jr        nz,$3419                      ;[33f7] 20 20
                    jr        nz,$3447                      ;[33f9] 20 4c
                    ld        b,(hl)                        ;[33fb] 46
                    jr        nz,$3444                      ;[33fc] 20 46
                    ld        c,c                           ;[33fe] 49
                    jr        nz,$3453                      ;[33ff] 20 52
                    ld        c,c                           ;[3401] 49
                    jr        nz,$3424                      ;[3402] 20 20
                    jr        nz,$3427                      ;[3404] 20 21
                    dec       c                             ;[3406] 0d
                    ld        hl,$2020                      ;[3407] 21 20 20
                    jr        nz,$342c                      ;[340a] 20 20
                    jr        nz,$342e                      ;[340c] 20 20
                    jr        nz,$3430                      ;[340e] 20 20
                    jr        nz,$3432                      ;[3410] 20 20
                    jr        nz,$3434                      ;[3412] 20 20
                    jr        nz,$3436                      ;[3414] 20 20
                    jr        nz,$3438                      ;[3416] 20 20
                    jr        nz,$343a                      ;[3418] 20 20
                    jr        nz,$343c                      ;[341a] 20 20
                    jr        nz,$343e                      ;[341c] 20 20
                    jr        nz,$3440                      ;[341e] 20 20
                    jr        nz,$3442                      ;[3420] 20 20
                    ld        hl,$210d                      ;[3422] 21 0d 21
                    jr        nz,$3447                      ;[3425] 20 20
                    jr        nz,$3449                      ;[3427] 20 20
                    jr        nz,$346f                      ;[3429] 20 44
                    ld        c,(hl)                        ;[342b] 4e
                    jr        nz,$344e                      ;[342c] 20 20
                    jr        nz,$3450                      ;[342e] 20 20
                    jr        nz,$3452                      ;[3430] 20 20
                    jr        nz,$3454                      ;[3432] 20 20
                    jr        nz,$3456                      ;[3434] 20 20
                    jr        nz,$347c                      ;[3436] 20 44
                    ld        c,(hl)                        ;[3438] 4e
                    jr        nz,$345b                      ;[3439] 20 20
                    jr        nz,$345d                      ;[343b] 20 20
                    jr        nz,$345f                      ;[343d] 20 20
                    ld        hl,$210d                      ;[343f] 21 0d 21
                    jr        nz,$3464                      ;[3442] 20 20
                    jr        nz,$3466                      ;[3444] 20 20
                    jr        nz,$3468                      ;[3446] 20 20
                    jr        nz,$346a                      ;[3448] 20 20
                    jr        nz,$346c                      ;[344a] 20 20
                    jr        nz,$346e                      ;[344c] 20 20
                    jr        nz,$3470                      ;[344e] 20 20
                    jr        nz,$3472                      ;[3450] 20 20
                    jr        nz,$3474                      ;[3452] 20 20
                    jr        nz,$3476                      ;[3454] 20 20
                    jr        nz,$3478                      ;[3456] 20 20
                    jr        nz,$347a                      ;[3458] 20 20
                    jr        nz,$347c                      ;[345a] 20 20
                    ld        hl,$2b0d                      ;[345c] 21 0d 2b
                    dec       l                             ;[345f] 2d
                    dec       l                             ;[3460] 2d
                    dec       l                             ;[3461] 2d
                    dec       l                             ;[3462] 2d
                    dec       l                             ;[3463] 2d
                    dec       l                             ;[3464] 2d
                    dec       l                             ;[3465] 2d
                    dec       l                             ;[3466] 2d
                    dec       l                             ;[3467] 2d
                    dec       l                             ;[3468] 2d
                    dec       l                             ;[3469] 2d
                    dec       l                             ;[346a] 2d
                    dec       l                             ;[346b] 2d
                    dec       l                             ;[346c] 2d
                    dec       l                             ;[346d] 2d
                    dec       l                             ;[346e] 2d
                    dec       l                             ;[346f] 2d
                    dec       l                             ;[3470] 2d
                    dec       l                             ;[3471] 2d
                    dec       l                             ;[3472] 2d
                    dec       l                             ;[3473] 2d
                    dec       l                             ;[3474] 2d
                    dec       l                             ;[3475] 2d
                    dec       l                             ;[3476] 2d
                    dec       l                             ;[3477] 2d
                    dec       l                             ;[3478] 2d
                    dec       hl                            ;[3479] 2b
                    dec       c                             ;[347a] 0d
                    rst       $38                           ;[347b] ff
                    ld        d,$05                         ;[347c] 16 05
                    dec       b                             ;[347e] 05
                    jr        nz,$34c2                      ;[347f] 20 41
                    ld        c,h                           ;[3481] 4c
                    ld        c,h                           ;[3482] 4c
                    jr        nz,$34d9                      ;[3483] 20 54
                    ld        b,l                           ;[3485] 45
                    ld        d,e                           ;[3486] 53
                    ld        d,h                           ;[3487] 54
                    ld        d,e                           ;[3488] 53
                    jr        nz,$34db                      ;[3489] 20 50
                    ld        b,c                           ;[348b] 41
                    ld        d,e                           ;[348c] 53
                    ld        d,e                           ;[348d] 53
                    ld        b,l                           ;[348e] 45
                    ld        b,h                           ;[348f] 44
                    jr        nz,$349f                      ;[3490] 20 0d
                    rst       $38                           ;[3492] ff
                    ld        d,$00                         ;[3493] 16 00
                    nop                                     ;[3495] 00
                    ld        (de),a                        ;[3496] 12
                    ld        bc,$5420                      ;[3497] 01 20 54
                    ld        b,l                           ;[349a] 45
                    ld        d,e                           ;[349b] 53
                    ld        d,h                           ;[349c] 54
                    jr        nz,$34e5                      ;[349d] 20 46
                    ld        b,c                           ;[349f] 41
                    ld        c,c                           ;[34a0] 49
                    ld        c,h                           ;[34a1] 4c
                    ld        b,l                           ;[34a2] 45
                    ld        b,h                           ;[34a3] 44
                    inc       l                             ;[34a4] 2c
                    jr        nz,$3509                      ;[34a5] 20 62
                    ld        h,l                           ;[34a7] 65
                    ld        h,e                           ;[34a8] 63
                    ld        h,c                           ;[34a9] 61
                    ld        (hl),l                        ;[34aa] 75
                    ld        (hl),e                        ;[34ab] 73
                    ld        h,l                           ;[34ac] 65
                    ld        a,($202d)                     ;[34ad] 3a 2d 20
                    dec       c                             ;[34b0] 0d
                    ld        (de),a                        ;[34b1] 12
                    nop                                     ;[34b2] 00
                    dec       c                             ;[34b3] 0d
                    rst       $38                           ;[34b4] ff
                    ld        (hl),b                        ;[34b5] 70
                    ld        (hl),d                        ;[34b6] 72
                    ld        h,l                           ;[34b7] 65
                    ld        (hl),e                        ;[34b8] 73
                    ld        (hl),e                        ;[34b9] 73
                    jr        nz,$3517                      ;[34ba] 20 5b
                    ld        b,l                           ;[34bc] 45
                    ld        c,(hl)                        ;[34bd] 4e
                    ld        d,h                           ;[34be] 54
                    ld        b,l                           ;[34bf] 45
                    ld        d,d                           ;[34c0] 52
                    ld        e,l                           ;[34c1] 5d
                    jr        nz,$352d                      ;[34c2] 20 69
                    ld        h,(hl)                        ;[34c4] 66
                    jr        nz,$3540                      ;[34c5] 20 79
                    ld        l,a                           ;[34c7] 6f
                    ld        (hl),l                        ;[34c8] 75
                    jr        nz,$3533                      ;[34c9] 20 68
                    ld        h,l                           ;[34cb] 65
                    ld        h,c                           ;[34cc] 61
                    ld        (hl),d                        ;[34cd] 72
                    ld        h,h                           ;[34ce] 64
                    jr        nz,$3537                      ;[34cf] 20 66
                    ld        l,a                           ;[34d1] 6f
                    ld        (hl),l                        ;[34d2] 75
                    ld        (hl),d                        ;[34d3] 72
                    jr        nz,$3549                      ;[34d4] 20 73
                    ld        l,a                           ;[34d6] 6f
                    ld        (hl),l                        ;[34d7] 75
                    ld        l,(hl)                        ;[34d8] 6e
                    ld        h,h                           ;[34d9] 64
                    ld        (hl),e                        ;[34da] 73
                    inc       l                             ;[34db] 2c
                    jr        nz,$3543                      ;[34dc] 20 65
                    ld        l,h                           ;[34de] 6c
                    ld        (hl),e                        ;[34df] 73
                    ld        h,l                           ;[34e0] 65
                    jr        nz,$3553                      ;[34e1] 20 70
                    ld        (hl),d                        ;[34e3] 72
                    ld        h,l                           ;[34e4] 65
                    ld        (hl),e                        ;[34e5] 73
                    ld        (hl),e                        ;[34e6] 73
                    jr        nz,$3544                      ;[34e7] 20 5b
                    ld        d,e                           ;[34e9] 53
                    ld        d,b                           ;[34ea] 50
                    ld        b,c                           ;[34eb] 41
                    ld        b,e                           ;[34ec] 43
                    ld        b,l                           ;[34ed] 45
                    ld        e,l                           ;[34ee] 5d
                    ld        l,$20                         ;[34ef] 2e 20
                    jr        nz,$3513                      ;[34f1] 20 20
                    jr        nz,$3515                      ;[34f3] 20 20
                    rst       $38                           ;[34f5] ff
                    di                                      ;[34f6] f3
                    push      ix                            ;[34f7] dd e5
                    ld        a,l                           ;[34f9] 7d
                    srl       l                             ;[34fa] cb 3d
                    srl       l                             ;[34fc] cb 3d
                    cpl                                     ;[34fe] 2f
                    and       $03                           ;[34ff] e6 03
                    ld        c,a                           ;[3501] 4f
                    ld        b,$00                         ;[3502] 06 00
                    ld        ix,$3514                      ;[3504] dd 21 14 35
                    add       ix,bc                         ;[3508] dd 09
                    ld        a,($5c48)                     ;[350a] 3a 48 5c
                    and       $38                           ;[350d] e6 38
                    rrca                                    ;[350f] 0f
                    rrca                                    ;[3510] 0f
                    rrca                                    ;[3511] 0f
                    or        $08                           ;[3512] f6 08
                    nop                                     ;[3514] 00
                    nop                                     ;[3515] 00
                    nop                                     ;[3516] 00
                    inc       b                             ;[3517] 04
                    inc       c                             ;[3518] 0c
                    dec       c                             ;[3519] 0d
                    jr        nz,$3519                      ;[351a] 20 fd
                    ld        c,$3f                         ;[351c] 0e 3f
                    dec       b                             ;[351e] 05
                    jp        nz,$3519                      ;[351f] c2 19 35
                    xor       $10                           ;[3522] ee 10
                    out       ($fe),a                       ;[3524] d3 fe
                    ld        b,h                           ;[3526] 44
                    ld        c,a                           ;[3527] 4f
                    bit       4,a                           ;[3528] cb 67
                    jr        nz,$3535                      ;[352a] 20 09
                    ld        a,d                           ;[352c] 7a
                    or        e                             ;[352d] b3
                    jr        z,$3539                       ;[352e] 28 09
                    ld        a,c                           ;[3530] 79
                    ld        c,l                           ;[3531] 4d
                    dec       de                            ;[3532] 1b
                    jp        (ix)                          ;[3533] dd e9
                    ld        c,l                           ;[3535] 4d
                    inc       c                             ;[3536] 0c
                    jp        (ix)                          ;[3537] dd e9
                    ei                                      ;[3539] fb
                    pop       ix                            ;[353a] dd e1
                    ret                                     ;[353c] c9

                    ld        de,$0f06                      ;[353d] 11 06 0f
                    ld        b,$13                         ;[3540] 06 13
                    ld        b,$11                         ;[3542] 06 11
                    add       hl,bc                         ;[3544] 09
                    ld        de,$1103                      ;[3545] 11 03 11
                    djnz      $355b                         ;[3548] 10 11
                    ld        d,$13                         ;[354a] 16 13
                    inc       de                            ;[354c] 13
                    rrca                                    ;[354d] 0f
                    inc       de                            ;[354e] 13
                    ld        de,$4613                      ;[354f] 11 13 46
                    ld        c,c                           ;[3552] 49
                    ld        d,l                           ;[3553] 55
                    ld        d,b                           ;[3554] 50
                    ld        b,h                           ;[3555] 44
                    ld        c,(hl)                        ;[3556] 4e
                    ld        d,d                           ;[3557] 52
                    ld        c,c                           ;[3558] 49
                    ld        c,h                           ;[3559] 4c
                    ld        b,(hl)                        ;[355a] 46
                    ld        c,h                           ;[355b] 4c
                    ld        b,(hl)                        ;[355c] 46
                    ld        d,d                           ;[355d] 52
                    ld        c,c                           ;[355e] 49
                    ld        b,h                           ;[355f] 44
                    ld        c,(hl)                        ;[3560] 4e
                    ld        d,l                           ;[3561] 55
                    ld        d,b                           ;[3562] 50
                    ld        b,(hl)                        ;[3563] 46
                    ld        c,c                           ;[3564] 49
                    ld        hl,$3573                      ;[3565] 21 73 35
                    ld        de,$5f00                      ;[3568] 11 00 5f
                    ld        bc,$003d                      ;[356b] 01 3d 00
                    ldir                                    ;[356e] ed b0
                    jp        $5f00                         ;[3570] c3 00 5f
                    ld        a,$04                         ;[3573] 3e 04
                    ld        bc,$1ffd                      ;[3575] 01 fd 1f
                    out       (c),a                         ;[3578] ed 79
                    ld        hl,$2492                      ;[357a] 21 92 24
                    ld        de,$6000                      ;[357d] 11 00 60
                    ld        bc,$0c00                      ;[3580] 01 00 0c
                    ldir                                    ;[3583] ed b0
                    ld        a,$00                         ;[3585] 3e 00
                    ld        bc,$1ffd                      ;[3587] 01 fd 1f
                    out       (c),a                         ;[358a] ed 79
                    ld        ($5b67),a                     ;[358c] 32 67 5b
                    ld        a,$10                         ;[358f] 3e 10
                    ld        b,$7f                         ;[3591] 06 7f
                    out       (c),a                         ;[3593] ed 79
                    ld        ($5b5c),a                     ;[3595] 32 5c 5b
                    ei                                      ;[3598] fb
                    push      ix                            ;[3599] dd e5
                    call      $6000                         ;[359b] cd 00 60
                    pop       ix                            ;[359e] dd e1
                    push      af                            ;[35a0] f5
                    ld        a,$00                         ;[35a1] 3e 00
                    ld        bc,$7ffd                      ;[35a3] 01 fd 7f
                    out       (c),a                         ;[35a6] ed 79
                    ld        ($5b5c),a                     ;[35a8] 32 5c 5b
                    pop       af                            ;[35ab] f1
                    call      $242e                         ;[35ac] cd 2e 24
                    ret                                     ;[35af] c9

                    call      $2727                         ;[35b0] cd 27 27
                    ld        hl,$3638                      ;[35b3] 21 38 36
                    call      $2710                         ;[35b6] cd 10 27
                    call      $361f                         ;[35b9] cd 1f 36
                    di                                      ;[35bc] f3
                    ld        hl,$58e1                      ;[35bd] 21 e1 58
                    ld        de,$0006                      ;[35c0] 11 06 00
                    ld        b,e                           ;[35c3] 43
                    ld        a,d                           ;[35c4] 7a
                    ld        (hl),a                        ;[35c5] 77
                    add       hl,de                         ;[35c6] 19
                    djnz      $35c5                         ;[35c7] 10 fc
                    ld        hl,$0000                      ;[35c9] 21 00 00
                    ld        de,$1000                      ;[35cc] 11 00 10
                    ld        c,$fe                         ;[35cf] 0e fe
                    ld        b,$7f                         ;[35d1] 06 7f
                    in        a,(c)                         ;[35d3] ed 78
                    bit       0,a                           ;[35d5] cb 47
                    jp        z,$362c                       ;[35d7] ca 2c 36
                    ld        bc,$bffe                      ;[35da] 01 fe bf
                    in        a,(c)                         ;[35dd] ed 78
                    bit       0,a                           ;[35df] cb 47
                    jp        z,$3632                       ;[35e1] ca 32 36
                    dec       de                            ;[35e4] 1b
                    ld        a,d                           ;[35e5] 7a
                    or        e                             ;[35e6] b3
                    jr        z,$35f4                       ;[35e7] 28 0b
                    in        a,($fe)                       ;[35e9] db fe
                    and       $40                           ;[35eb] e6 40
                    cp        c                             ;[35ed] b9
                    jr        z,$35e4                       ;[35ee] 28 f4
                    inc       hl                            ;[35f0] 23
                    ld        c,a                           ;[35f1] 4f
                    jr        $35e4                         ;[35f2] 18 f0
                    rl        l                             ;[35f4] cb 15
                    rl        h                             ;[35f6] cb 14
                    rl        l                             ;[35f8] cb 15
                    rl        h                             ;[35fa] cb 14
                    rl        l                             ;[35fc] cb 15
                    rl        h                             ;[35fe] cb 14
                    ld        l,h                           ;[3600] 6c
                    ld        a,$20                         ;[3601] 3e 20
                    cp        h                             ;[3603] bc
                    jr        nc,$3608                      ;[3604] 30 02
                    ld        l,$20                         ;[3606] 2e 20
                    xor       a                             ;[3608] af
                    ld        h,a                           ;[3609] 67
                    ld        de,$591f                      ;[360a] 11 1f 59
                    ld        b,$20                         ;[360d] 06 20
                    ld        a,$48                         ;[360f] 3e 48
                    ei                                      ;[3611] fb
                    halt                                    ;[3612] 76
                    di                                      ;[3613] f3
                    ld        (de),a                        ;[3614] 12
                    dec       de                            ;[3615] 1b
                    djnz      $3614                         ;[3616] 10 fc
                    inc       de                            ;[3618] 13
                    add       hl,de                         ;[3619] 19
                    ld        a,$68                         ;[361a] 3e 68
                    ld        (hl),a                        ;[361c] 77
                    jr        $35c9                         ;[361d] 18 aa
                    ei                                      ;[361f] fb
                    ld        b,$19                         ;[3620] 06 19
                    halt                                    ;[3622] 76
                    djnz      $3622                         ;[3623] 10 fd
                    ld        hl,$5c3b                      ;[3625] 21 3b 5c
                    res       5,(hl)                        ;[3628] cb ae
                    scf                                     ;[362a] 37
                    ret                                     ;[362b] c9

                    and       a                             ;[362c] a7
                    call      $242e                         ;[362d] cd 2e 24
                    jr        $361f                         ;[3630] 18 ed
                    scf                                     ;[3632] 37
                    call      $242e                         ;[3633] cd 2e 24
                    jr        $361f                         ;[3636] 18 e7
                    ld        d,$00                         ;[3638] 16 00
                    nop                                     ;[363a] 00
                    ld        c,c                           ;[363b] 49
                    ld        l,(hl)                        ;[363c] 6e
                    ld        (hl),e                        ;[363d] 73
                    ld        h,l                           ;[363e] 65
                    ld        (hl),d                        ;[363f] 72
                    ld        (hl),h                        ;[3640] 74
                    jr        nz,$36b7                      ;[3641] 20 74
                    ld        h,l                           ;[3643] 65
                    ld        (hl),e                        ;[3644] 73
                    ld        (hl),h                        ;[3645] 74
                    jr        nz,$36bc                      ;[3646] 20 74
                    ld        h,c                           ;[3648] 61
                    ld        (hl),b                        ;[3649] 70
                    ld        h,l                           ;[364a] 65
                    inc       l                             ;[364b] 2c
                    jr        nz,$36be                      ;[364c] 20 70
                    ld        (hl),d                        ;[364e] 72
                    ld        h,l                           ;[364f] 65
                    ld        (hl),e                        ;[3650] 73
                    ld        (hl),e                        ;[3651] 73
                    jr        nz,$36a4                      ;[3652] 20 50
                    ld        c,h                           ;[3654] 4c
                    ld        b,c                           ;[3655] 41
                    ld        e,c                           ;[3656] 59
                    inc       l                             ;[3657] 2c
                    dec       c                             ;[3658] 0d
                    ld        h,c                           ;[3659] 61
                    ld        l,(hl)                        ;[365a] 6e
                    ld        h,h                           ;[365b] 64
                    jr        nz,$36bf                      ;[365c] 20 61
                    ld        h,h                           ;[365e] 64
                    ld        l,d                           ;[365f] 6a
                    ld        (hl),l                        ;[3660] 75
                    ld        (hl),e                        ;[3661] 73
                    ld        (hl),h                        ;[3662] 74
                    jr        nz,$36c6                      ;[3663] 20 61
                    ld        a,d                           ;[3665] 7a
                    ld        l,c                           ;[3666] 69
                    ld        l,l                           ;[3667] 6d
                    ld        (hl),l                        ;[3668] 75
                    ld        (hl),h                        ;[3669] 74
                    ld        l,b                           ;[366a] 68
                    jr        nz,$36e0                      ;[366b] 20 73
                    ld        h,e                           ;[366d] 63
                    ld        (hl),d                        ;[366e] 72
                    ld        h,l                           ;[366f] 65
                    ld        (hl),a                        ;[3670] 77
                    jr        nz,$36d9                      ;[3671] 20 66
                    ld        l,a                           ;[3673] 6f
                    ld        (hl),d                        ;[3674] 72
                    dec       c                             ;[3675] 0d
                    ld        l,l                           ;[3676] 6d
                    ld        h,c                           ;[3677] 61
                    ld        a,b                           ;[3678] 78
                    ld        l,c                           ;[3679] 69
                    ld        l,l                           ;[367a] 6d
                    ld        (hl),l                        ;[367b] 75
                    ld        l,l                           ;[367c] 6d
                    jr        nz,$36f1                      ;[367d] 20 72
                    ld        h,l                           ;[367f] 65
                    ld        h,c                           ;[3680] 61
                    ld        h,h                           ;[3681] 64
                    ld        l,c                           ;[3682] 69
                    ld        l,(hl)                        ;[3683] 6e
                    ld        h,a                           ;[3684] 67
                    jr        nz,$36f6                      ;[3685] 20 6f
                    ld        l,(hl)                        ;[3687] 6e
                    jr        nz,$36fd                      ;[3688] 20 73
                    ld        h,e                           ;[368a] 63
                    ld        (hl),d                        ;[368b] 72
                    ld        h,l                           ;[368c] 65
                    ld        h,l                           ;[368d] 65
                    ld        l,(hl)                        ;[368e] 6e
                    ld        l,$0d                         ;[368f] 2e 0d
                    ld        d,b                           ;[3691] 50
                    ld        (hl),d                        ;[3692] 72
                    ld        h,l                           ;[3693] 65
                    ld        (hl),e                        ;[3694] 73
                    ld        (hl),e                        ;[3695] 73
                    jr        nz,$36f3                      ;[3696] 20 5b
                    ld        b,l                           ;[3698] 45
                    ld        c,(hl)                        ;[3699] 4e
                    ld        d,h                           ;[369a] 54
                    ld        b,l                           ;[369b] 45
                    ld        d,d                           ;[369c] 52
                    ld        e,l                           ;[369d] 5d
                    jr        nz,$3709                      ;[369e] 20 69
                    ld        h,(hl)                        ;[36a0] 66
                    jr        nz,$3716                      ;[36a1] 20 73
                    ld        (hl),l                        ;[36a3] 75
                    ld        h,e                           ;[36a4] 63
                    ld        h,e                           ;[36a5] 63
                    ld        h,l                           ;[36a6] 65
                    ld        (hl),e                        ;[36a7] 73
                    ld        (hl),e                        ;[36a8] 73
                    ld        h,(hl)                        ;[36a9] 66
                    ld        (hl),l                        ;[36aa] 75
                    ld        l,h                           ;[36ab] 6c
                    inc       l                             ;[36ac] 2c
                    dec       c                             ;[36ad] 0d
                    ld        (hl),b                        ;[36ae] 70
                    ld        (hl),d                        ;[36af] 72
                    ld        h,l                           ;[36b0] 65
                    ld        (hl),e                        ;[36b1] 73
                    ld        (hl),e                        ;[36b2] 73
                    jr        nz,$3710                      ;[36b3] 20 5b
                    ld        d,e                           ;[36b5] 53
                    ld        d,b                           ;[36b6] 50
                    ld        b,c                           ;[36b7] 41
                    ld        b,e                           ;[36b8] 43
                    ld        b,l                           ;[36b9] 45
                    ld        e,l                           ;[36ba] 5d
                    jr        nz,$3726                      ;[36bb] 20 69
                    ld        h,(hl)                        ;[36bd] 66
                    jr        nz,$3726                      ;[36be] 20 66
                    ld        h,c                           ;[36c0] 61
                    ld        l,c                           ;[36c1] 69
                    ld        l,h                           ;[36c2] 6c
                    ld        h,l                           ;[36c3] 65
                    ld        h,h                           ;[36c4] 64
                    dec       c                             ;[36c5] 0d
                    rst       $38                           ;[36c6] ff
                    ld        (hl),h                        ;[36c7] 74
                    rst       $18                           ;[36c8] df
                    sbc       $55                           ;[36c9] de 55
                    djnz      $371f                         ;[36cb] 10 52
                    ret                                     ;[36cd] c9

                    sbc       (hl)                          ;[36ce] 9e
                    sbc       (hl)                          ;[36cf] 9e
                    cp        l                             ;[36d0] bd
                    ld        h,d                           ;[36d1] 62
                    push      bc                            ;[36d2] c5
                    ret       nz                            ;[36d3] c0
                    ld        d,l                           ;[36d4] 55
                    jp        nz,$1044                      ;[36d5] c2 44 10
                    rla                                     ;[36d8] 17
                    cp        $5f                           ;[36d9] fe 5f
                    sub       b                             ;[36db] 90
                    cp        $d1                           ;[36dc] fe d1
                    push      de                            ;[36de] dd d5
                    rla                                     ;[36e0] 17
                    sub       b                             ;[36e1] 90
                    rst       $30                           ;[36e2] f7
                    rst       $18                           ;[36e3] df
                    rst       $18                           ;[36e4] df
                    call      nc,$d9c7                      ;[36e5] d4 c7 d9
                    sbc       $c3                           ;[36e8] de c3
                    cp        l                             ;[36ea] bd
                    di                                      ;[36eb] f3
                    call      c,$d659                       ;[36ec] dc 59 d6
                    ld        d,(hl)                        ;[36ef] 56
                    djnz      $3709                         ;[36f0] 10 17
                    ld        h,h                           ;[36f2] 64
                    ld        b,a                           ;[36f3] 47
                    ld        e,a                           ;[36f4] 5f
                    sub       b                             ;[36f5] 90
                    ld        h,b                           ;[36f6] 60
                    rst       $18                           ;[36f7] df
                    ld        b,b                           ;[36f8] 40
                    ld        b,e                           ;[36f9] 43
                    sub       a                             ;[36fa] 97
                    djnz      $36f9                         ;[36fb] 10 fc
                    ld        d,c                           ;[36fd] 51
                    rst       $00                           ;[36fe] c7
                    ld        b,e                           ;[36ff] 43
                    ld        e,a                           ;[3700] 5f
                    sbc       $bd                           ;[3701] de bd
                    ld        h,(hl)                        ;[3703] 66
                    exx                                     ;[3704] d9
                    ld        e,e                           ;[3705] 5b
                    sub       b                             ;[3706] 90
                    rla                                     ;[3707] 17
                    jp        po,$54d5                      ;[3708] e2 d5 54
                    djnz      $3705                         ;[370b] 10 f8
                    ld        d,l                           ;[370d] 55
                    ld        b,d                           ;[370e] 42
                    ld        b,d                           ;[370f] 42
                    ld        e,c                           ;[3710] 59
                    sbc       $57                           ;[3711] de 57
                    rla                                     ;[3713] 17
                    sub       b                             ;[3714] 90
                    ld        a,a                           ;[3715] 7f
                    ld        e,h                           ;[3716] 5c
                    ld        e,h                           ;[3717] 5c
                    exx                                     ;[3718] d9
                    add       $55                           ;[3719] c6 55
                    jp        nz,$5190                      ;[371b] c2 90 51
                    sbc       $bd                           ;[371e] de bd
                    call      po,$d5d8                      ;[3720] e4 d8 d5
                    sub       b                             ;[3723] 90
                    ld        (hl),h                        ;[3724] 74
                    ld        e,a                           ;[3725] 5f
                    jp        nz,$595b                      ;[3726] c2 5b 59
                    ld        e,(hl)                        ;[3729] 5e
                    rst       $10                           ;[372a] d7
                    sub       b                             ;[372b] 90
                    ld        a,l                           ;[372c] 7d
                    ld        e,a                           ;[372d] 5f
                    jp        nc,$64bd                      ;[372e] d2 bd 64
                    ld        e,b                           ;[3731] 58
                    pop       de                            ;[3732] d1
                    ld        e,(hl)                        ;[3733] 5e
                    in        a,($c3)                       ;[3734] db c3
                    djnz      $36fc                         ;[3736] 10 c4
                    ld        e,a                           ;[3738] 5f
                    ld        e,$1e                         ;[3739] 1e 1e
                    cp        l                             ;[373b] bd
                    ld        h,h                           ;[373c] 64
                    ret       c                             ;[373d] d8
                    push      de                            ;[373e] d5
                    djnz      $3733                         ;[373f] 10 f2
                    ld        b,d                           ;[3741] 42
                    push      de                            ;[3742] d5
                    ld        b,a                           ;[3743] 47
                    push      de                            ;[3744] d5
                    ld        b,d                           ;[3745] 42
                    ret                                     ;[3746] c9

                    sub       b                             ;[3747] 90
                    call      po,$c0d1                      ;[3748] e4 d1 c0
                    sub       b                             ;[374b] 90
                    jr        $37a6                         ;[374c] 18 58
                    exx                                     ;[374e] d9
                    out       ($19),a                       ;[374f] d3 19
                    cp        l                             ;[3751] bd
                    jp        po,$dc5f                      ;[3752] e2 5f dc
                    ld        d,c                           ;[3755] 51
                    sbc       $54                           ;[3756] de 54
                    inc       e                             ;[3758] 1c
                    sub       b                             ;[3759] 90
                    ld        h,d                           ;[375a] 62
                    exx                                     ;[375b] d9
                    out       ($58),a                       ;[375c] d3 58
                    ld        d,c                           ;[375e] 51
                    jp        nz,$9054                      ;[375f] c2 54 90
                    ld        d,l                           ;[3762] 55
                    ld        b,h                           ;[3763] 44
                    sub       b                             ;[3764] 90
                    pop       de                            ;[3765] d1
                    ld        e,h                           ;[3766] 5c
                    cp        l                             ;[3767] bd
                    ld        d,c                           ;[3768] 51
                    ld        e,(hl)                        ;[3769] 5e
                    ld        d,h                           ;[376a] 54
                    sub       b                             ;[376b] 90
                    ld        sp,hl                         ;[376c] f9
                    add       $df                           ;[376d] c6 df
                    ld        b,d                           ;[376f] 42
                    djnz      $370a                         ;[3770] 10 98
                    rst       $00                           ;[3772] c7
                    ret       c                             ;[3773] d8
                    ld        c,c                           ;[3774] 49
                    djnz      $3755                         ;[3775] 10 de
                    ld        e,a                           ;[3777] 5f
                    call      nz,$bd19                      ;[3778] c4 19 bd
                    ld        h,a                           ;[377b] 67
                    ld        b,d                           ;[377c] 42
                    exx                                     ;[377d] d9
                    ld        b,h                           ;[377e] 44
                    call      nz,$5ed5                      ;[377f] c4 d5 5e
                    sub       b                             ;[3782] 90
                    rst       $18                           ;[3783] df
                    ld        e,(hl)                        ;[3784] 5e
                    sub       b                             ;[3785] 90
                    ld        h,b                           ;[3786] 60
                    di                                      ;[3787] f3
                    ld        h,a                           ;[3788] 67
                    sub       b                             ;[3789] 90
                    adc       b                             ;[378a] 88
                    dec       b                             ;[378b] 05
                    add       c                             ;[378c] 81
                    ld        (bc),a                        ;[378d] 02
                    ld        b,e                           ;[378e] 43
                    inc       e                             ;[378f] 1c
                    djnz      $37d7                         ;[3790] 10 45
                    ld        b,e                           ;[3792] 43
                    exx                                     ;[3793] d9
                    ld        e,(hl)                        ;[3794] 5e
                    cp        l                             ;[3795] bd
                    ex        af,af'                        ;[3796] fd 08
                    nop                                     ;[3798] 00
                    sub       b                             ;[3799] 90
                    ld        d,c                           ;[379a] 51
                    sbc       $d4                           ;[379b] de d4
                    djnz      $379b                         ;[379d] 10 fc
                    ex        af,af'                        ;[379f] 08
                    nop                                     ;[37a0] 00
                    djnz      $37e8                         ;[37a1] 10 45
                    ld        e,(hl)                        ;[37a3] 5e
                    call      nc,$c255                      ;[37a4] d4 55 c2
                    djnz      $379c                         ;[37a7] 10 f3
                    ld        h,b                           ;[37a9] 60
                    sbc       a                             ;[37aa] 9f
                    sbc       e                             ;[37ab] fd 9b
                    cp        l                             ;[37ad] bd
                    cp        l                             ;[37ae] bd
                    jp        $c490                         ;[37af] c3 90 c4
                    rst       $18                           ;[37b2] df
                    ld        e,$9e                         ;[37b3] 1e 9e
                    cp        l                             ;[37b5] bd
                    ld        h,h                           ;[37b6] 64
                    ret       c                             ;[37b7] d8
                    ld        d,l                           ;[37b8] 55
                    djnz      $3778                         ;[37b9] 10 bd
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
                    ld        ($5b52),hl                    ;[3e80] 22 52 5b
                    ld        ($5b54),bc                    ;[3e83] ed 43 54 5b
                    push      af                            ;[3e87] f5
                    pop       hl                            ;[3e88] e1
                    ld        ($5b56),hl                    ;[3e89] 22 56 5b
                    ex        (sp),hl                       ;[3e8c] e3
                    ld        c,(hl)                        ;[3e8d] 4e
                    inc       hl                            ;[3e8e] 23
                    ld        b,(hl)                        ;[3e8f] 46
                    inc       hl                            ;[3e90] 23
                    ex        (sp),hl                       ;[3e91] e3
                    push      bc                            ;[3e92] c5
                    pop       hl                            ;[3e93] e1
                    ld        a,($5b5c)                     ;[3e94] 3a 5c 5b
                    or        $10                           ;[3e97] f6 10
                    di                                      ;[3e99] f3
                    ld        ($5b5c),a                     ;[3e9a] 32 5c 5b
                    ld        bc,$7ffd                      ;[3e9d] 01 fd 7f
                    out       (c),a                         ;[3ea0] ed 79
                    ei                                      ;[3ea2] fb
                    ld        bc,$3eb5                      ;[3ea3] 01 b5 3e
                    push      bc                            ;[3ea6] c5
                    push      hl                            ;[3ea7] e5
                    ld        hl,($5b56)                    ;[3ea8] 2a 56 5b
                    push      hl                            ;[3eab] e5
                    pop       af                            ;[3eac] f1
                    ld        bc,($5b54)                    ;[3ead] ed 4b 54 5b
                    ld        hl,($5b52)                    ;[3eb1] 2a 52 5b
                    ret                                     ;[3eb4] c9

                    push      af                            ;[3eb5] f5
                    push      bc                            ;[3eb6] c5
                    ld        a,($5b5c)                     ;[3eb7] 3a 5c 5b
                    or        $10                           ;[3eba] f6 10
                    di                                      ;[3ebc] f3
                    ld        ($5b5c),a                     ;[3ebd] 32 5c 5b
                    ld        bc,$7ffd                      ;[3ec0] 01 fd 7f
                    out       (c),a                         ;[3ec3] ed 79
                    ei                                      ;[3ec5] fb
                    pop       bc                            ;[3ec6] c1
                    pop       af                            ;[3ec7] f1
                    ret                                     ;[3ec8] c9

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

                    nop                                     ;[3f63] 00
                    nop                                     ;[3f64] 00
                    nop                                     ;[3f65] 00
                    nop                                     ;[3f66] 00
                    nop                                     ;[3f67] 00
                    nop                                     ;[3f68] 00
                    nop                                     ;[3f69] 00
                    nop                                     ;[3f6a] 00
                    nop                                     ;[3f6b] 00
                    nop                                     ;[3f6c] 00
                    nop                                     ;[3f6d] 00
                    nop                                     ;[3f6e] 00
                    nop                                     ;[3f6f] 00
                    nop                                     ;[3f70] 00
                    nop                                     ;[3f71] 00
                    nop                                     ;[3f72] 00
                    nop                                     ;[3f73] 00
                    nop                                     ;[3f74] 00
                    nop                                     ;[3f75] 00
                    nop                                     ;[3f76] 00
                    nop                                     ;[3f77] 00
                    nop                                     ;[3f78] 00
                    nop                                     ;[3f79] 00
                    nop                                     ;[3f7a] 00
                    nop                                     ;[3f7b] 00
                    nop                                     ;[3f7c] 00
                    nop                                     ;[3f7d] 00
                    nop                                     ;[3f7e] 00
                    nop                                     ;[3f7f] 00
                    nop                                     ;[3f80] 00
                    nop                                     ;[3f81] 00
                    nop                                     ;[3f82] 00
                    nop                                     ;[3f83] 00
                    nop                                     ;[3f84] 00
                    nop                                     ;[3f85] 00
                    nop                                     ;[3f86] 00
                    nop                                     ;[3f87] 00
                    nop                                     ;[3f88] 00
                    nop                                     ;[3f89] 00
                    nop                                     ;[3f8a] 00
                    nop                                     ;[3f8b] 00
                    nop                                     ;[3f8c] 00
                    nop                                     ;[3f8d] 00
                    nop                                     ;[3f8e] 00
                    nop                                     ;[3f8f] 00
                    nop                                     ;[3f90] 00
                    nop                                     ;[3f91] 00
                    nop                                     ;[3f92] 00
                    nop                                     ;[3f93] 00
                    nop                                     ;[3f94] 00
                    nop                                     ;[3f95] 00
                    nop                                     ;[3f96] 00
                    nop                                     ;[3f97] 00
                    nop                                     ;[3f98] 00
                    nop                                     ;[3f99] 00
                    nop                                     ;[3f9a] 00
                    nop                                     ;[3f9b] 00
                    nop                                     ;[3f9c] 00
                    nop                                     ;[3f9d] 00
                    nop                                     ;[3f9e] 00
                    nop                                     ;[3f9f] 00
                    nop                                     ;[3fa0] 00
                    nop                                     ;[3fa1] 00
                    nop                                     ;[3fa2] 00
                    nop                                     ;[3fa3] 00
                    nop                                     ;[3fa4] 00
                    nop                                     ;[3fa5] 00
                    nop                                     ;[3fa6] 00
                    nop                                     ;[3fa7] 00
                    nop                                     ;[3fa8] 00
                    nop                                     ;[3fa9] 00
                    nop                                     ;[3faa] 00
                    nop                                     ;[3fab] 00
                    nop                                     ;[3fac] 00
                    nop                                     ;[3fad] 00
                    nop                                     ;[3fae] 00
                    nop                                     ;[3faf] 00
                    nop                                     ;[3fb0] 00
                    nop                                     ;[3fb1] 00
                    nop                                     ;[3fb2] 00
                    nop                                     ;[3fb3] 00
                    nop                                     ;[3fb4] 00
                    nop                                     ;[3fb5] 00
                    nop                                     ;[3fb6] 00
                    nop                                     ;[3fb7] 00
                    nop                                     ;[3fb8] 00
                    nop                                     ;[3fb9] 00
                    nop                                     ;[3fba] 00
                    nop                                     ;[3fbb] 00
                    nop                                     ;[3fbc] 00
                    nop                                     ;[3fbd] 00
                    nop                                     ;[3fbe] 00
                    nop                                     ;[3fbf] 00
                    nop                                     ;[3fc0] 00
                    nop                                     ;[3fc1] 00
                    nop                                     ;[3fc2] 00
                    nop                                     ;[3fc3] 00
                    nop                                     ;[3fc4] 00
                    nop                                     ;[3fc5] 00
                    nop                                     ;[3fc6] 00
                    nop                                     ;[3fc7] 00
                    nop                                     ;[3fc8] 00
                    nop                                     ;[3fc9] 00
                    nop                                     ;[3fca] 00
                    nop                                     ;[3fcb] 00
                    nop                                     ;[3fcc] 00
                    nop                                     ;[3fcd] 00
                    nop                                     ;[3fce] 00
                    nop                                     ;[3fcf] 00
                    nop                                     ;[3fd0] 00
                    nop                                     ;[3fd1] 00
                    nop                                     ;[3fd2] 00
                    nop                                     ;[3fd3] 00
                    nop                                     ;[3fd4] 00
                    nop                                     ;[3fd5] 00
                    nop                                     ;[3fd6] 00
                    nop                                     ;[3fd7] 00
                    nop                                     ;[3fd8] 00
                    nop                                     ;[3fd9] 00
                    nop                                     ;[3fda] 00
                    nop                                     ;[3fdb] 00
                    nop                                     ;[3fdc] 00
                    nop                                     ;[3fdd] 00
                    nop                                     ;[3fde] 00
                    nop                                     ;[3fdf] 00
                    nop                                     ;[3fe0] 00
                    nop                                     ;[3fe1] 00
                    nop                                     ;[3fe2] 00
                    nop                                     ;[3fe3] 00
                    nop                                     ;[3fe4] 00
                    nop                                     ;[3fe5] 00
                    nop                                     ;[3fe6] 00
                    nop                                     ;[3fe7] 00
                    nop                                     ;[3fe8] 00
                    nop                                     ;[3fe9] 00
                    nop                                     ;[3fea] 00
                    nop                                     ;[3feb] 00
                    nop                                     ;[3fec] 00
                    nop                                     ;[3fed] 00
                    nop                                     ;[3fee] 00
                    nop                                     ;[3fef] 00
                    jp        $218c                         ;[3ff0] c3 8c 21
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
                    rra                                     ;[3fff] 1f
