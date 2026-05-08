                    ld        d,e                           ;[0000] 53
                    ld        a,c                           ;[0001] 79
                    ld        l,(hl)                        ;[0002] 6e
                    ld        (hl),h                        ;[0003] 74
                    ld        h,c                           ;[0004] 61
                    ld        a,b                           ;[0005] 78
                    nop                                     ;[0006] 00
                    nop                                     ;[0007] 00
                    jp        $2c4c                         ;[0008] c3 4c 2c
                    nop                                     ;[000b] 00
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
                    jp        $00aa                         ;[0034] c3 aa 00
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
                    call      $0176                         ;[004a] cd 76 01
                    call      $0074                         ;[004d] cd 74 00
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

                    ld        bc,$7ffd                      ;[0074] 01 fd 7f
                    ld        a,($5b5c)                     ;[0077] 3a 5c 5b
                    or        $07                           ;[007a] f6 07
                    out       (c),a                         ;[007c] ed 79
                    ld        a,($e600)                     ;[007e] 3a 00 e6
                    or        a                             ;[0081] b7
                    jr        z,$00a1                       ;[0082] 28 1d
                    ld        a,($5c78)                     ;[0084] 3a 78 5c
                    bit       0,a                           ;[0087] cb 47
                    jr        nz,$00a1                      ;[0089] 20 16
                    ld        a,($e600)                     ;[008b] 3a 00 e6
                    dec       a                             ;[008e] 3d
                    ld        ($e600),a                     ;[008f] 32 00 e6
                    jr        nz,$00a1                      ;[0092] 20 0d
                    ld        bc,$1ffd                      ;[0094] 01 fd 1f
                    ld        a,($5b67)                     ;[0097] 3a 67 5b
                    and       $f7                           ;[009a] e6 f7
                    ld        ($5b67),a                     ;[009c] 32 67 5b
                    out       (c),a                         ;[009f] ed 79
                    ld        bc,$7ffd                      ;[00a1] 01 fd 7f
                    ld        a,($5b5c)                     ;[00a4] 3a 5c 5b
                    out       (c),a                         ;[00a7] ed 79
                    ret                                     ;[00a9] c9

                    ld        ($5b58),hl                    ;[00aa] 22 58 5b
                    ld        hl,$5b2a                      ;[00ad] 21 2a 5b
                    ex        (sp),hl                       ;[00b0] e3
                    push      hl                            ;[00b1] e5
                    ld        hl,($5b58)                    ;[00b2] 2a 58 5b
                    ex        (sp),hl                       ;[00b5] e3
                    push      af                            ;[00b6] f5
                    push      bc                            ;[00b7] c5
                    di                                      ;[00b8] f3
                    jp        $5b10                         ;[00b9] c3 10 5b
                    ld        b,d                           ;[00bc] 42
                    ld        c,b                           ;[00bd] 48
                    ld        e,c                           ;[00be] 59
                    ld        (hl),$35                      ;[00bf] 36 35
                    ld        d,h                           ;[00c1] 54
                    ld        b,a                           ;[00c2] 47
                    ld        d,(hl)                        ;[00c3] 56
                    ld        c,(hl)                        ;[00c4] 4e
                    ld        c,d                           ;[00c5] 4a
                    ld        d,l                           ;[00c6] 55
                    scf                                     ;[00c7] 37
                    inc       (hl)                          ;[00c8] 34
                    ld        d,d                           ;[00c9] 52
                    ld        b,(hl)                        ;[00ca] 46
                    ld        b,e                           ;[00cb] 43
                    ld        c,l                           ;[00cc] 4d
                    ld        c,e                           ;[00cd] 4b
                    ld        c,c                           ;[00ce] 49
                    jr        c,$0104                       ;[00cf] 38 33
                    ld        b,l                           ;[00d1] 45
                    ld        b,h                           ;[00d2] 44
                    ld        e,b                           ;[00d3] 58
                    ld        c,$4c                         ;[00d4] 0e 4c
                    ld        c,a                           ;[00d6] 4f
                    add       hl,sp                         ;[00d7] 39
                    ld        ($5357),a                     ;[00d8] 32 57 53
                    ld        e,d                           ;[00db] 5a
                    jr        nz,$00eb                      ;[00dc] 20 0d
                    ld        d,b                           ;[00de] 50
                    jr        nc,$0112                      ;[00df] 30 31
                    ld        d,c                           ;[00e1] 51
                    ld        b,c                           ;[00e2] 41
                    ex        (sp),hl                       ;[00e3] e3
                    call      nz,$e4e0                      ;[00e4] c4 e0 e4
                    or        h                             ;[00e7] b4
                    cp        h                             ;[00e8] bc
                    cp        l                             ;[00e9] bd
                    cp        e                             ;[00ea] bb
                    xor       a                             ;[00eb] af
                    or        b                             ;[00ec] b0
                    or        c                             ;[00ed] b1
                    ret       nz                            ;[00ee] c0
                    and       a                             ;[00ef] a7
                    and       (hl)                          ;[00f0] a6
                    cp        (hl)                          ;[00f1] be
                    xor       l                             ;[00f2] ad
                    or        d                             ;[00f3] b2
                    cp        d                             ;[00f4] ba
                    push      hl                            ;[00f5] e5
                    and       l                             ;[00f6] a5
                    jp        nz,$b3e1                      ;[00f7] c2 e1 b3
                    cp        c                             ;[00fa] b9
                    pop       bc                            ;[00fb] c1
                    cp        b                             ;[00fc] b8
                    ld        a,(hl)                        ;[00fd] 7e
                    call      c,$5cda                       ;[00fe] dc da 5c
                    or        a                             ;[0101] b7
                    ld        a,e                           ;[0102] 7b
                    ld        a,l                           ;[0103] 7d
                    ret       c                             ;[0104] d8
                    cp        a                             ;[0105] bf
                    xor       (hl)                          ;[0106] ae
                    xor       d                             ;[0107] aa
                    xor       e                             ;[0108] ab
                    sbc       $df                           ;[0109] dd de df
                    ld        a,a                           ;[010c] 7f
                    or        l                             ;[010d] b5
                    sub       $7c                           ;[010e] d6 7c
                    push      de                            ;[0110] d5
                    ld        e,l                           ;[0111] 5d
                    in        a,($b6)                       ;[0112] db b6
                    exx                                     ;[0114] d9
                    ld        e,e                           ;[0115] 5b
                    rst       $10                           ;[0116] d7
                    inc       c                             ;[0117] 0c
                    rlca                                    ;[0118] 07
                    ld        b,$04                         ;[0119] 06 04
                    dec       b                             ;[011b] 05
                    ex        af,af'                        ;[011c] 08
                    ld        a,(bc)                        ;[011d] 0a
                    dec       bc                            ;[011e] 0b
                    add       hl,bc                         ;[011f] 09
                    rrca                                    ;[0120] 0f
                    jp        po,$3f2a                      ;[0121] e2 2a 3f
                    call      $ccc8                         ;[0124] cd c8 cc
                    bit       3,(hl)                        ;[0127] cb 5e
                    xor       h                             ;[0129] ac
                    dec       l                             ;[012a] 2d
                    dec       hl                            ;[012b] 2b
                    dec       a                             ;[012c] 3d
                    ld        l,$2c                         ;[012d] 2e 2c
                    dec       sp                            ;[012f] 3b
                    ld        ($3cc7),hl                    ;[0130] 22 c7 3c
                    jp        $c53e                         ;[0133] c3 3e c5
                    cpl                                     ;[0136] 2f
                    ret                                     ;[0137] c9

                    ld        h,b                           ;[0138] 60
                    add       $3a                           ;[0139] c6 3a
                    ret       nc                            ;[013b] d0
                    adc       $a8                           ;[013c] ce a8
                    jp        z,$d4d3                       ;[013e] ca d3 d4
                    pop       de                            ;[0141] d1
                    jp        nc,$cfa9                      ;[0142] d2 a9 cf
                    ld        l,$2f                         ;[0145] 2e 2f
                    ld        de,$ffff                      ;[0147] 11 ff ff
                    ld        bc,$fefe                      ;[014a] 01 fe fe
                    in        a,(c)                         ;[014d] ed 78
                    cpl                                     ;[014f] 2f
                    and       $1f                           ;[0150] e6 1f
                    jr        z,$0162                       ;[0152] 28 0e
                    ld        h,a                           ;[0154] 67
                    ld        a,l                           ;[0155] 7d
                    inc       d                             ;[0156] 14
                    ret       nz                            ;[0157] c0
                    sub       $08                           ;[0158] d6 08
                    srl       h                             ;[015a] cb 3c
                    jr        nc,$0158                      ;[015c] 30 fa
                    ld        d,e                           ;[015e] 53
                    ld        e,a                           ;[015f] 5f
                    jr        nz,$0156                      ;[0160] 20 f4
                    dec       l                             ;[0162] 2d
                    rlc       b                             ;[0163] cb 00
                    jr        c,$014d                       ;[0165] 38 e6
                    ld        a,d                           ;[0167] 7a
                    inc       a                             ;[0168] 3c
                    ret       z                             ;[0169] c8
                    cp        $28                           ;[016a] fe 28
                    ret       z                             ;[016c] c8
                    cp        $19                           ;[016d] fe 19
                    ret       z                             ;[016f] c8
                    ld        a,e                           ;[0170] 7b
                    ld        e,d                           ;[0171] 5a
                    ld        d,a                           ;[0172] 57
                    cp        $18                           ;[0173] fe 18
                    ret                                     ;[0175] c9

                    call      $0145                         ;[0176] cd 45 01
                    ret       nz                            ;[0179] c0
                    ld        hl,$5c00                      ;[017a] 21 00 5c
                    bit       7,(hl)                        ;[017d] cb 7e
                    jr        nz,$0188                      ;[017f] 20 07
                    inc       hl                            ;[0181] 23
                    dec       (hl)                          ;[0182] 35
                    dec       hl                            ;[0183] 2b
                    jr        nz,$0188                      ;[0184] 20 02
                    ld        (hl),$ff                      ;[0186] 36 ff
                    ld        a,l                           ;[0188] 7d
                    ld        hl,$5c04                      ;[0189] 21 04 5c
                    cp        l                             ;[018c] bd
                    jr        nz,$017d                      ;[018d] 20 ee
                    call      $01d5                         ;[018f] cd d5 01
                    ret       nc                            ;[0192] d0
                    ld        hl,$5c00                      ;[0193] 21 00 5c
                    cp        (hl)                          ;[0196] be
                    jr        z,$01c7                       ;[0197] 28 2e
                    ex        de,hl                         ;[0199] eb
                    ld        hl,$5c04                      ;[019a] 21 04 5c
                    cp        (hl)                          ;[019d] be
                    jr        z,$01c7                       ;[019e] 28 27
                    bit       7,(hl)                        ;[01a0] cb 7e
                    jr        nz,$01a8                      ;[01a2] 20 04
                    ex        de,hl                         ;[01a4] eb
                    bit       7,(hl)                        ;[01a5] cb 7e
                    ret       z                             ;[01a7] c8
                    ld        e,a                           ;[01a8] 5f
                    ld        (hl),a                        ;[01a9] 77
                    inc       hl                            ;[01aa] 23
                    ld        (hl),$05                      ;[01ab] 36 05
                    inc       hl                            ;[01ad] 23
                    ld        a,($5c09)                     ;[01ae] 3a 09 5c
                    ld        (hl),a                        ;[01b1] 77
                    inc       hl                            ;[01b2] 23
                    ld        c,(iy+$07)                    ;[01b3] fd 4e 07
                    ld        d,(iy+$01)                    ;[01b6] fd 56 01
                    push      hl                            ;[01b9] e5
                    call      $01ea                         ;[01ba] cd ea 01
                    pop       hl                            ;[01bd] e1
                    ld        (hl),a                        ;[01be] 77
                    ld        ($5c08),a                     ;[01bf] 32 08 5c
                    set       5,(iy+$01)                    ;[01c2] fd cb 01 ee
                    ret                                     ;[01c6] c9

                    inc       hl                            ;[01c7] 23
                    ld        (hl),$05                      ;[01c8] 36 05
                    inc       hl                            ;[01ca] 23
                    dec       (hl)                          ;[01cb] 35
                    ret       nz                            ;[01cc] c0
                    ld        a,($5c0a)                     ;[01cd] 3a 0a 5c
                    ld        (hl),a                        ;[01d0] 77
                    inc       hl                            ;[01d1] 23
                    ld        a,(hl)                        ;[01d2] 7e
                    jr        $01bf                         ;[01d3] 18 ea
                    ld        b,d                           ;[01d5] 42
                    ld        d,$00                         ;[01d6] 16 00
                    ld        a,e                           ;[01d8] 7b
                    cp        $27                           ;[01d9] fe 27
                    ret       nc                            ;[01db] d0
                    cp        $18                           ;[01dc] fe 18
                    jr        nz,$01e3                      ;[01de] 20 03
                    bit       7,b                           ;[01e0] cb 78
                    ret       nz                            ;[01e2] c0
                    ld        hl,$00bc                      ;[01e3] 21 bc 00
                    add       hl,de                         ;[01e6] 19
                    ld        a,(hl)                        ;[01e7] 7e
                    scf                                     ;[01e8] 37
                    ret                                     ;[01e9] c9

                    ld        a,e                           ;[01ea] 7b
                    cp        $3a                           ;[01eb] fe 3a
                    jr        c,$021e                       ;[01ed] 38 2f
                    dec       c                             ;[01ef] 0d
                    jp        m,$0206                       ;[01f0] fa 06 02
                    jr        z,$01f8                       ;[01f3] 28 03
                    add       $4f                           ;[01f5] c6 4f
                    ret                                     ;[01f7] c9

                    ld        hl,$00a2                      ;[01f8] 21 a2 00
                    inc       b                             ;[01fb] 04
                    jr        z,$0201                       ;[01fc] 28 03
                    ld        hl,$00bc                      ;[01fe] 21 bc 00
                    ld        d,$00                         ;[0201] 16 00
                    add       hl,de                         ;[0203] 19
                    ld        a,(hl)                        ;[0204] 7e
                    ret                                     ;[0205] c9

                    ld        hl,$00e0                      ;[0206] 21 e0 00
                    bit       0,b                           ;[0209] cb 40
                    jr        z,$0201                       ;[020b] 28 f4
                    bit       3,d                           ;[020d] cb 5a
                    jr        z,$021b                       ;[020f] 28 0a
                    bit       3,(iy+$30)                    ;[0211] fd cb 30 5e
                    ret       nz                            ;[0215] c0
                    inc       b                             ;[0216] 04
                    ret       nz                            ;[0217] c0
                    add       $20                           ;[0218] c6 20
                    ret                                     ;[021a] c9

                    add       $a5                           ;[021b] c6 a5
                    ret                                     ;[021d] c9

                    cp        $30                           ;[021e] fe 30
                    ret       c                             ;[0220] d8
                    dec       c                             ;[0221] 0d
                    jp        m,$0254                       ;[0222] fa 54 02
                    jr        nz,$0240                      ;[0225] 20 19
                    ld        hl,$010b                      ;[0227] 21 0b 01
                    bit       5,b                           ;[022a] cb 68
                    jr        z,$0201                       ;[022c] 28 d3
                    cp        $38                           ;[022e] fe 38
                    jr        nc,$0239                      ;[0230] 30 07
                    sub       $20                           ;[0232] d6 20
                    inc       b                             ;[0234] 04
                    ret       z                             ;[0235] c8
                    add       $08                           ;[0236] c6 08
                    ret                                     ;[0238] c9

                    sub       $36                           ;[0239] d6 36
                    inc       b                             ;[023b] 04
                    ret       z                             ;[023c] c8
                    add       $fe                           ;[023d] c6 fe
                    ret                                     ;[023f] c9

                    ld        hl,$00e7                      ;[0240] 21 e7 00
                    cp        $39                           ;[0243] fe 39
                    jr        z,$0201                       ;[0245] 28 ba
                    cp        $30                           ;[0247] fe 30
                    jr        z,$0201                       ;[0249] 28 b6
                    and       $07                           ;[024b] e6 07
                    add       $80                           ;[024d] c6 80
                    inc       b                             ;[024f] 04
                    ret       z                             ;[0250] c8
                    xor       $0f                           ;[0251] ee 0f
                    ret                                     ;[0253] c9

                    inc       b                             ;[0254] 04
                    ret       z                             ;[0255] c8
                    bit       5,b                           ;[0256] cb 68
                    ld        hl,$00e7                      ;[0258] 21 e7 00
                    jr        nz,$0201                      ;[025b] 20 a4
                    sub       $10                           ;[025d] d6 10
                    cp        $22                           ;[025f] fe 22
                    jr        z,$0269                       ;[0261] 28 06
                    cp        $20                           ;[0263] fe 20
                    ret       nz                            ;[0265] c0
                    ld        a,$5f                         ;[0266] 3e 5f
                    ret                                     ;[0268] c9

                    ld        a,$40                         ;[0269] 3e 40
                    ret                                     ;[026b] c9

                    rst       $28                           ;[026c] ef
                    jr        $026f                         ;[026d] 18 00
                    cp        $e0                           ;[026f] fe e0
                    jp        z,$03e3                       ;[0271] ca e3 03
                    cp        $ca                           ;[0274] fe ca
                    jr        nz,$027e                      ;[0276] 20 06
                    rst       $28                           ;[0278] ef
                    jr        nz,$027b                      ;[0279] 20 00
                    jp        $1e21                         ;[027b] c3 21 1e
                    rst       $28                           ;[027e] ef
                    adc       h                             ;[027f] 8c
                    inc       e                             ;[0280] 1c
                    call      $10cd                         ;[0281] cd cd 10
                    rst       $28                           ;[0284] ef
                    pop       af                            ;[0285] f1
                    dec       hl                            ;[0286] 2b
                    ld        a,c                           ;[0287] 79
                    dec       a                             ;[0288] 3d
                    dec       a                             ;[0289] 3d
                    or        b                             ;[028a] b0
                    jr        z,$0291                       ;[028b] 28 04
                    call      $2c4c                         ;[028d] cd 4c 2c
                    ld        c,(hl)                        ;[0290] 4e
                    inc       de                            ;[0291] 13
                    ld        a,(de)                        ;[0292] 1a
                    dec       de                            ;[0293] 1b
                    cp        $3a                           ;[0294] fe 3a
                    jr        z,$029c                       ;[0296] 28 04
                    call      $2c4c                         ;[0298] cd 4c 2c
                    ld        c,(hl)                        ;[029b] 4e
                    ld        a,(de)                        ;[029c] 1a
                    and       $df                           ;[029d] e6 df
                    cp        $41                           ;[029f] fe 41
                    jr        z,$02ab                       ;[02a1] 28 08
                    cp        $42                           ;[02a3] fe 42
                    jr        z,$02ab                       ;[02a5] 28 04
                    call      $2c4c                         ;[02a7] cd 4c 2c
                    ld        c,(hl)                        ;[02aa] 4e
                    call      $2cfb                         ;[02ab] cd fb 2c
                    sub       $41                           ;[02ae] d6 41
                    push      af                            ;[02b0] f5
                    ld        hl,$5b66                      ;[02b1] 21 66 5b
                    bit       4,(hl)                        ;[02b4] cb 66
                    jr        nz,$02bf                      ;[02b6] 20 07
                    call      $2cd6                         ;[02b8] cd d6 2c
                    call      $2c4c                         ;[02bb] cd 4c 2c
                    ld        c,h                           ;[02be] 4c
                    pop       af                            ;[02bf] f1
                    or        a                             ;[02c0] b7
                    jr        z,$02d3                       ;[02c1] 28 10
                    push      af                            ;[02c3] f5
                    ld        hl,$5b66                      ;[02c4] 21 66 5b
                    bit       5,(hl)                        ;[02c7] cb 6e
                    jr        nz,$02d2                      ;[02c9] 20 07
                    call      $2cd6                         ;[02cb] cd d6 2c
                    call      $2c4c                         ;[02ce] cd 4c 2c
                    ld        c,e                           ;[02d1] 4b
                    pop       af                            ;[02d2] f1
                    push      af                            ;[02d3] f5
                    ld        c,a                           ;[02d4] 4f
                    push      bc                            ;[02d5] c5
                    add       $41                           ;[02d6] c6 41
                    call      $342d                         ;[02d8] cd 2d 34
                    call      $3f00                         ;[02db] cd 00 3f
                    ld        d,c                           ;[02de] 51
                    ld        bc,$65cd                      ;[02df] 01 cd 65
                    inc       (hl)                          ;[02e2] 34
                    jr        c,$02ec                       ;[02e3] 38 07
                    call      $2cd6                         ;[02e5] cd d6 2c
                    call      $0eb6                         ;[02e8] cd b6 0e
                    rst       $38                           ;[02eb] ff
                    pop       bc                            ;[02ec] c1
                    call      $342d                         ;[02ed] cd 2d 34
                    call      $3f00                         ;[02f0] cd 00 3f
                    ld        (hl),l                        ;[02f3] 75
                    ld        bc,$65cd                      ;[02f4] 01 cd 65
                    inc       (hl)                          ;[02f7] 34
                    jr        nc,$0306                      ;[02f8] 30 0c
                    or        a                             ;[02fa] b7
                    jr        nz,$0315                      ;[02fb] 20 18
                    call      $0381                         ;[02fd] cd 81 03
                    jr        nz,$0315                      ;[0300] 20 13
                    call      $2cd6                         ;[0302] cd d6 2c
                    ret                                     ;[0305] c9

                    cp        $05                           ;[0306] fe 05
                    jr        z,$0315                       ;[0308] 28 0b
                    cp        $09                           ;[030a] fe 09
                    jr        z,$0315                       ;[030c] 28 07
                    call      $2cd6                         ;[030e] cd d6 2c
                    call      $0eb6                         ;[0311] cd b6 0e
                    rst       $38                           ;[0314] ff
                    pop       af                            ;[0315] f1
                    push      af                            ;[0316] f5
                    add       $41                           ;[0317] c6 41
                    call      $342d                         ;[0319] cd 2d 34
                    call      $3f00                         ;[031c] cd 00 3f
                    ld        d,c                           ;[031f] 51
                    ld        bc,$65cd                      ;[0320] 01 cd 65
                    inc       (hl)                          ;[0323] 34
                    jr        c,$032d                       ;[0324] 38 07
                    call      $2cd6                         ;[0326] cd d6 2c
                    call      $0eb6                         ;[0329] cd b6 0e
                    rst       $38                           ;[032c] ff
                    xor       a                             ;[032d] af
                    call      $342d                         ;[032e] cd 2d 34
                    call      $3f00                         ;[0331] cd 00 3f
                    ld        a,b                           ;[0334] 78
                    ld        bc,$65cd                      ;[0335] 01 cd 65
                    inc       (hl)                          ;[0338] 34
                    jr        c,$0342                       ;[0339] 38 07
                    call      $2cd6                         ;[033b] cd d6 2c
                    call      $0eb6                         ;[033e] cd b6 0e
                    rst       $38                           ;[0341] ff
                    pop       af                            ;[0342] f1
                    ld        c,a                           ;[0343] 4f
                    xor       a                             ;[0344] af
                    ld        d,a                           ;[0345] 57
                    call      $036f                         ;[0346] cd 6f 03
                    ld        e,$e5                         ;[0349] 1e e5
                    ld        b,$07                         ;[034b] 06 07
                    ld        hl,$ed11                      ;[034d] 21 11 ed
                    push      af                            ;[0350] f5
                    call      $342d                         ;[0351] cd 2d 34
                    call      $3f00                         ;[0354] cd 00 3f
                    ld        l,h                           ;[0357] 6c
                    ld        bc,$65cd                      ;[0358] 01 cd 65
                    inc       (hl)                          ;[035b] 34
                    jr        c,$0365                       ;[035c] 38 07
                    call      $2cd6                         ;[035e] cd d6 2c
                    call      $0eb6                         ;[0361] cd b6 0e
                    rst       $38                           ;[0364] ff
                    pop       af                            ;[0365] f1
                    inc       a                             ;[0366] 3c
                    cp        $28                           ;[0367] fe 28
                    jr        nz,$0345                      ;[0369] 20 da
                    call      $2cd6                         ;[036b] cd d6 2c
                    ret                                     ;[036e] c9

                    ld        b,$09                         ;[036f] 06 09
                    ld        hl,$ed34                      ;[0371] 21 34 ed
                    ld        (hl),$02                      ;[0374] 36 02
                    dec       hl                            ;[0376] 2b
                    ld        (hl),b                        ;[0377] 70
                    dec       hl                            ;[0378] 2b
                    ld        (hl),$00                      ;[0379] 36 00
                    dec       hl                            ;[037b] 2b
                    ld        (hl),d                        ;[037c] 72
                    dec       hl                            ;[037d] 2b
                    djnz      $0374                         ;[037e] 10 f4
                    ret                                     ;[0380] c9

                    ld        hl,$03a7                      ;[0381] 21 a7 03
                    ld        a,(hl)                        ;[0384] 7e
                    or        a                             ;[0385] b7
                    jr        z,$038e                       ;[0386] 28 06
                    rst       $28                           ;[0388] ef
                    djnz      $038b                         ;[0389] 10 00
                    inc       hl                            ;[038b] 23
                    jr        $0384                         ;[038c] 18 f6
                    res       5,(iy+$01)                    ;[038e] fd cb 01 ae
                    bit       5,(iy+$01)                    ;[0392] fd cb 01 6e
                    jr        z,$0392                       ;[0396] 28 fa
                    ld        a,($5c08)                     ;[0398] 3a 08 5c
                    and       $df                           ;[039b] e6 df
                    cp        $41                           ;[039d] fe 41
                    push      af                            ;[039f] f5
                    push      hl                            ;[03a0] e5
                    rst       $28                           ;[03a1] ef
                    ld        l,(hl)                        ;[03a2] 6e
                    dec       c                             ;[03a3] 0d
                    pop       hl                            ;[03a4] e1
                    pop       af                            ;[03a5] f1
                    ret                                     ;[03a6] c9

                    ld        b,h                           ;[03a7] 44
                    ld        l,c                           ;[03a8] 69
                    ld        (hl),e                        ;[03a9] 73
                    ld        l,e                           ;[03aa] 6b
                    jr        nz,$0416                      ;[03ab] 20 69
                    ld        (hl),e                        ;[03ad] 73
                    jr        nz,$0411                      ;[03ae] 20 61
                    ld        l,h                           ;[03b0] 6c
                    ld        (hl),d                        ;[03b1] 72
                    ld        h,l                           ;[03b2] 65
                    ld        h,c                           ;[03b3] 61
                    ld        h,h                           ;[03b4] 64
                    ld        a,c                           ;[03b5] 79
                    jr        nz,$041e                      ;[03b6] 20 66
                    ld        l,a                           ;[03b8] 6f
                    ld        (hl),d                        ;[03b9] 72
                    ld        l,l                           ;[03ba] 6d
                    ld        h,c                           ;[03bb] 61
                    ld        (hl),h                        ;[03bc] 74
                    ld        (hl),h                        ;[03bd] 74
                    ld        h,l                           ;[03be] 65
                    ld        h,h                           ;[03bf] 64
                    ld        l,$0d                         ;[03c0] 2e 0d
                    ld        b,c                           ;[03c2] 41
                    jr        nz,$0439                      ;[03c3] 20 74
                    ld        l,a                           ;[03c5] 6f
                    jr        nz,$0429                      ;[03c6] 20 61
                    ld        h,d                           ;[03c8] 62
                    ld        h,c                           ;[03c9] 61
                    ld        l,(hl)                        ;[03ca] 6e
                    ld        h,h                           ;[03cb] 64
                    ld        l,a                           ;[03cc] 6f
                    ld        l,(hl)                        ;[03cd] 6e
                    inc       l                             ;[03ce] 2c
                    jr        nz,$0440                      ;[03cf] 20 6f
                    ld        (hl),h                        ;[03d1] 74
                    ld        l,b                           ;[03d2] 68
                    ld        h,l                           ;[03d3] 65
                    ld        (hl),d                        ;[03d4] 72
                    jr        nz,$0442                      ;[03d5] 20 6b
                    ld        h,l                           ;[03d7] 65
                    ld        a,c                           ;[03d8] 79
                    jr        nz,$043e                      ;[03d9] 20 63
                    ld        l,a                           ;[03db] 6f
                    ld        l,(hl)                        ;[03dc] 6e
                    ld        (hl),h                        ;[03dd] 74
                    ld        l,c                           ;[03de] 69
                    ld        l,(hl)                        ;[03df] 6e
                    ld        (hl),l                        ;[03e0] 75
                    ld        h,l                           ;[03e1] 65
                    nop                                     ;[03e2] 00
                    rst       $28                           ;[03e3] ef
                    jr        nz,$03e6                      ;[03e4] 20 00
                    rst       $28                           ;[03e6] ef
                    adc       h                             ;[03e7] 8c
                    inc       e                             ;[03e8] 1c
                    rst       $28                           ;[03e9] ef
                    jr        $03ec                         ;[03ea] 18 00
                    cp        $3b                           ;[03ec] fe 3b
                    call      nz,$10cd                      ;[03ee] c4 cd 10
                    jr        nz,$041c                      ;[03f1] 20 29
                    rst       $28                           ;[03f3] ef
                    jr        nz,$03f6                      ;[03f4] 20 00
                    rst       $28                           ;[03f6] ef
                    adc       h                             ;[03f7] 8c
                    inc       e                             ;[03f8] 1c
                    call      $10cd                         ;[03f9] cd cd 10
                    rst       $28                           ;[03fc] ef
                    pop       af                            ;[03fd] f1
                    dec       hl                            ;[03fe] 2b
                    ld        a,c                           ;[03ff] 79
                    dec       a                             ;[0400] 3d
                    or        b                             ;[0401] b0
                    jr        z,$0407                       ;[0402] 28 03
                    jp        $045b                         ;[0404] c3 5b 04
                    ld        a,(de)                        ;[0407] 1a
                    and       $df                           ;[0408] e6 df
                    ld        hl,$5b66                      ;[040a] 21 66 5b
                    cp        $45                           ;[040d] fe 45
                    jr        nz,$0415                      ;[040f] 20 04
                    set       2,(hl)                        ;[0411] cb d6
                    jr        $041c                         ;[0413] 18 07
                    cp        $55                           ;[0415] fe 55
                    jp        nz,$045b                      ;[0417] c2 5b 04
                    res       2,(hl)                        ;[041a] cb 96
                    rst       $28                           ;[041c] ef
                    pop       af                            ;[041d] f1
                    dec       hl                            ;[041e] 2b
                    ld        a,c                           ;[041f] 79
                    dec       a                             ;[0420] 3d
                    or        b                             ;[0421] b0
                    jr        z,$0427                       ;[0422] 28 03
                    jp        $045b                         ;[0424] c3 5b 04
                    ld        a,(de)                        ;[0427] 1a
                    and       $df                           ;[0428] e6 df
                    ld        hl,$5b66                      ;[042a] 21 66 5b
                    cp        $52                           ;[042d] fe 52
                    jr        nz,$0434                      ;[042f] 20 03
                    set       3,(hl)                        ;[0431] cb de
                    ret                                     ;[0433] c9

                    cp        $43                           ;[0434] fe 43
                    jr        nz,$043b                      ;[0436] 20 03
                    res       3,(hl)                        ;[0438] cb 9e
                    ret                                     ;[043a] c9

                    cp        $45                           ;[043b] fe 45
                    jr        nz,$0442                      ;[043d] 20 03
                    set       2,(hl)                        ;[043f] cb d6
                    ret                                     ;[0441] c9

                    cp        $55                           ;[0442] fe 55
                    jr        nz,$0449                      ;[0444] 20 03
                    res       2,(hl)                        ;[0446] cb 96
                    ret                                     ;[0448] c9

                    ld        hl,$5c6a                      ;[0449] 21 6a 5c
                    cp        $4e                           ;[044c] fe 4e
                    jr        nz,$0453                      ;[044e] 20 03
                    res       6,(hl)                        ;[0450] cb b6
                    ret                                     ;[0452] c9

                    cp        $41                           ;[0453] fe 41
                    jp        nz,$045b                      ;[0455] c2 5b 04
                    set       6,(hl)                        ;[0458] cb f6
                    ret                                     ;[045a] c9

                    call      $2c4c                         ;[045b] cd 4c 2c
                    dec       bc                            ;[045e] 0b
                    rst       $28                           ;[045f] ef
                    pop       af                            ;[0460] f1
                    dec       hl                            ;[0461] 2b
                    ld        a,b                           ;[0462] 78
                    or        c                             ;[0463] b1
                    jr        nz,$046a                      ;[0464] 20 04
                    call      $2c4c                         ;[0466] cd 4c 2c
                    inc       l                             ;[0469] 2c
                    push      bc                            ;[046a] c5
                    push      de                            ;[046b] d5
                    push      de                            ;[046c] d5
                    pop       hl                            ;[046d] e1
                    push      bc                            ;[046e] c5
                    ld        a,$2a                         ;[046f] 3e 2a
                    cpir                                    ;[0471] ed b1
                    pop       bc                            ;[0473] c1
                    jr        z,$0482                       ;[0474] 28 0c
                    push      de                            ;[0476] d5
                    pop       hl                            ;[0477] e1
                    push      bc                            ;[0478] c5
                    ld        a,$3f                         ;[0479] 3e 3f
                    cpir                                    ;[047b] ed b1
                    pop       bc                            ;[047d] c1
                    jr        z,$0482                       ;[047e] 28 02
                    jr        $04ae                         ;[0480] 18 2c
                    ld        hl,$04ea                      ;[0482] 21 ea 04
                    call      $04d6                         ;[0485] cd d6 04
                    call      $04df                         ;[0488] cd df 04
                    ld        hl,$04f1                      ;[048b] 21 f1 04
                    call      $04d6                         ;[048e] cd d6 04
                    ld        hl,$5c3b                      ;[0491] 21 3b 5c
                    res       5,(hl)                        ;[0494] cb ae
                    bit       5,(hl)                        ;[0496] cb 6e
                    jr        z,$0496                       ;[0498] 28 fc
                    res       5,(hl)                        ;[049a] cb ae
                    ld        a,($5c08)                     ;[049c] 3a 08 5c
                    and       $df                           ;[049f] e6 df
                    cp        $4e                           ;[04a1] fe 4e
                    jr        nz,$04a8                      ;[04a3] 20 03
                    pop       de                            ;[04a5] d1
                    pop       bc                            ;[04a6] c1
                    ret                                     ;[04a7] c9

                    cp        $59                           ;[04a8] fe 59
                    jr        z,$04ae                       ;[04aa] 28 02
                    jr        $0491                         ;[04ac] 18 e3
                    rst       $28                           ;[04ae] ef
                    ld        l,(hl)                        ;[04af] 6e
                    dec       c                             ;[04b0] 0d
                    pop       de                            ;[04b1] d1
                    pop       bc                            ;[04b2] c1
                    ld        hl,$ed01                      ;[04b3] 21 01 ed
                    ex        de,hl                         ;[04b6] eb
                    call      $3f63                         ;[04b7] cd 63 3f
                    call      $2cfb                         ;[04ba] cd fb 2c
                    ld        a,$ff                         ;[04bd] 3e ff
                    ld        (de),a                        ;[04bf] 12
                    ld        hl,$ed01                      ;[04c0] 21 01 ed
                    call      $342d                         ;[04c3] cd 2d 34
                    call      $3f00                         ;[04c6] cd 00 3f
                    inc       h                             ;[04c9] 24
                    ld        bc,$65cd                      ;[04ca] 01 cd 65
                    inc       (hl)                          ;[04cd] 34
                    call      $2cd6                         ;[04ce] cd d6 2c
                    ret       c                             ;[04d1] d8
                    call      $0eb6                         ;[04d2] cd b6 0e
                    rst       $38                           ;[04d5] ff
                    ld        a,(hl)                        ;[04d6] 7e
                    or        a                             ;[04d7] b7
                    ret       z                             ;[04d8] c8
                    inc       hl                            ;[04d9] 23
                    rst       $28                           ;[04da] ef
                    djnz      $04dd                         ;[04db] 10 00
                    jr        $04d6                         ;[04dd] 18 f7
                    ld        a,(de)                        ;[04df] 1a
                    rst       $28                           ;[04e0] ef
                    djnz      $04e3                         ;[04e1] 10 00
                    inc       de                            ;[04e3] 13
                    dec       bc                            ;[04e4] 0b
                    ld        a,b                           ;[04e5] 78
                    or        c                             ;[04e6] b1
                    jr        nz,$04df                      ;[04e7] 20 f6
                    ret                                     ;[04e9] c9

                    ld        b,l                           ;[04ea] 45
                    ld        (hl),d                        ;[04eb] 72
                    ld        h,c                           ;[04ec] 61
                    ld        (hl),e                        ;[04ed] 73
                    ld        h,l                           ;[04ee] 65
                    jr        nz,$04f1                      ;[04ef] 20 00
                    jr        nz,$0532                      ;[04f1] 20 3f
                    jr        nz,$051d                      ;[04f3] 20 28
                    ld        e,c                           ;[04f5] 59
                    cpl                                     ;[04f6] 2f
                    ld        c,(hl)                        ;[04f7] 4e
                    add       hl,hl                         ;[04f8] 29
                    nop                                     ;[04f9] 00
                    rst       $28                           ;[04fa] ef
                    pop       af                            ;[04fb] f1
                    dec       hl                            ;[04fc] 2b
                    ld        a,b                           ;[04fd] 78
                    or        c                             ;[04fe] b1
                    jr        nz,$0505                      ;[04ff] 20 04
                    call      $2c4c                         ;[0501] cd 4c 2c
                    inc       l                             ;[0504] 2c
                    ld        a,(de)                        ;[0505] 1a
                    cp        $2b                           ;[0506] fe 2b
                    jp        z,$0556                       ;[0508] ca 56 05
                    cp        $2d                           ;[050b] fe 2d
                    jp        z,$0556                       ;[050d] ca 56 05
                    ld        hl,$ed01                      ;[0510] 21 01 ed
                    ex        de,hl                         ;[0513] eb
                    call      $3f63                         ;[0514] cd 63 3f
                    call      $2cfb                         ;[0517] cd fb 2c
                    ld        a,$ff                         ;[051a] 3e ff
                    ld        (de),a                        ;[051c] 12
                    inc       de                            ;[051d] 13
                    call      $2cd6                         ;[051e] cd d6 2c
                    push      de                            ;[0521] d5
                    rst       $28                           ;[0522] ef
                    pop       af                            ;[0523] f1
                    dec       hl                            ;[0524] 2b
                    ld        a,b                           ;[0525] 78
                    or        c                             ;[0526] b1
                    jr        nz,$052d                      ;[0527] 20 04
                    call      $2c4c                         ;[0529] cd 4c 2c
                    inc       l                             ;[052c] 2c
                    pop       hl                            ;[052d] e1
                    push      hl                            ;[052e] e5
                    ex        de,hl                         ;[052f] eb
                    call      $3f63                         ;[0530] cd 63 3f
                    call      $2cfb                         ;[0533] cd fb 2c
                    ld        a,$ff                         ;[0536] 3e ff
                    ld        (de),a                        ;[0538] 12
                    call      $2cd6                         ;[0539] cd d6 2c
                    pop       hl                            ;[053c] e1
                    ld        de,$ed01                      ;[053d] 11 01 ed
                    call      $2cfb                         ;[0540] cd fb 2c
                    call      $342d                         ;[0543] cd 2d 34
                    call      $3f00                         ;[0546] cd 00 3f
                    daa                                     ;[0549] 27
                    ld        bc,$65cd                      ;[054a] 01 cd 65
                    inc       (hl)                          ;[054d] 34
                    call      $2cd6                         ;[054e] cd d6 2c
                    ret       c                             ;[0551] d8
                    call      $0eb6                         ;[0552] cd b6 0e
                    rst       $38                           ;[0555] ff
                    ld        a,c                           ;[0556] 79
                    dec       a                             ;[0557] 3d
                    dec       a                             ;[0558] 3d
                    or        b                             ;[0559] b0
                    jr        z,$0560                       ;[055a] 28 04
                    call      $2c4c                         ;[055c] cd 4c 2c
                    ld        b,a                           ;[055f] 47
                    ld        a,(de)                        ;[0560] 1a
                    ld        b,a                           ;[0561] 47
                    inc       de                            ;[0562] 13
                    ld        a,(de)                        ;[0563] 1a
                    and       $df                           ;[0564] e6 df
                    cp        $50                           ;[0566] fe 50
                    jr        z,$0576                       ;[0568] 28 0c
                    cp        $53                           ;[056a] fe 53
                    jr        z,$0576                       ;[056c] 28 08
                    cp        $41                           ;[056e] fe 41
                    jr        z,$0576                       ;[0570] 28 04
                    call      $2c4c                         ;[0572] cd 4c 2c
                    ld        b,a                           ;[0575] 47
                    push      bc                            ;[0576] c5
                    push      af                            ;[0577] f5
                    rst       $28                           ;[0578] ef
                    pop       af                            ;[0579] f1
                    dec       hl                            ;[057a] 2b
                    ld        a,b                           ;[057b] 78
                    or        c                             ;[057c] b1
                    jr        nz,$0583                      ;[057d] 20 04
                    call      $2c4c                         ;[057f] cd 4c 2c
                    inc       l                             ;[0582] 2c
                    ld        hl,$ed01                      ;[0583] 21 01 ed
                    ex        de,hl                         ;[0586] eb
                    call      $3f63                         ;[0587] cd 63 3f
                    call      $2cfb                         ;[058a] cd fb 2c
                    ld        a,$ff                         ;[058d] 3e ff
                    ld        (de),a                        ;[058f] 12
                    call      $2cd6                         ;[0590] cd d6 2c
                    ld        de,$0000                      ;[0593] 11 00 00
                    ld        c,$00                         ;[0596] 0e 00
                    pop       af                            ;[0598] f1
                    cp        $50                           ;[0599] fe 50
                    jr        nz,$05a1                      ;[059b] 20 04
                    set       2,c                           ;[059d] cb d1
                    jr        $05ab                         ;[059f] 18 0a
                    cp        $53                           ;[05a1] fe 53
                    jr        nz,$05a9                      ;[05a3] 20 04
                    set       1,c                           ;[05a5] cb c9
                    jr        $05ab                         ;[05a7] 18 02
                    set       0,c                           ;[05a9] cb c1
                    pop       af                            ;[05ab] f1
                    cp        $2b                           ;[05ac] fe 2b
                    jr        nz,$05b3                      ;[05ae] 20 03
                    ld        d,c                           ;[05b0] 51
                    jr        $05b4                         ;[05b1] 18 01
                    ld        e,c                           ;[05b3] 59
                    ld        hl,$ed01                      ;[05b4] 21 01 ed
                    call      $2cfb                         ;[05b7] cd fb 2c
                    call      $342d                         ;[05ba] cd 2d 34
                    call      $3f00                         ;[05bd] cd 00 3f
                    ld        c,b                           ;[05c0] 48
                    ld        bc,$65cd                      ;[05c1] 01 cd 65
                    inc       (hl)                          ;[05c4] 34
                    call      $2cd6                         ;[05c5] cd d6 2c
                    ret       c                             ;[05c8] d8
                    call      $0eb6                         ;[05c9] cd b6 0e
                    rst       $38                           ;[05cc] ff
                    ld        hl,$5b66                      ;[05cd] 21 66 5b
                    res       6,(hl)                        ;[05d0] cb b6
                    rst       $28                           ;[05d2] ef
                    ld        (hl),b                        ;[05d3] 70
                    jr        nz,$060e                      ;[05d4] 20 38
                    dec       de                            ;[05d6] 1b
                    ld        hl,($5c5d)                    ;[05d7] 2a 5d 5c
                    ld        a,(hl)                        ;[05da] 7e
                    cp        $2c                           ;[05db] fe 2c
                    jr        z,$05ef                       ;[05dd] 28 10
                    cp        $0d                           ;[05df] fe 0d
                    jr        z,$0640                       ;[05e1] 28 5d
                    cp        $3a                           ;[05e3] fe 3a
                    jr        z,$0640                       ;[05e5] 28 59
                    cp        $b9                           ;[05e7] fe b9
                    jr        z,$0640                       ;[05e9] 28 55
                    call      $2c4c                         ;[05eb] cd 4c 2c
                    dec       bc                            ;[05ee] 0b
                    rst       $20                           ;[05ef] e7
                    jr        $060d                         ;[05f0] 18 1b
                    ld        a,$02                         ;[05f2] 3e 02
                    bit       7,(iy+$01)                    ;[05f4] fd cb 01 7e
                    jr        z,$05fd                       ;[05f8] 28 03
                    rst       $28                           ;[05fa] ef
                    ld        bc,$2a16                      ;[05fb] 01 16 2a
                    ld        e,l                           ;[05fe] 5d
                    ld        e,h                           ;[05ff] 5c
                    ld        a,(hl)                        ;[0600] 7e
                    cp        $0d                           ;[0601] fe 0d
                    jr        z,$0640                       ;[0603] 28 3b
                    cp        $3a                           ;[0605] fe 3a
                    jr        z,$0640                       ;[0607] 28 37
                    cp        $b9                           ;[0609] fe b9
                    jr        z,$0640                       ;[060b] 28 33
                    rst       $28                           ;[060d] ef
                    adc       h                             ;[060e] 8c
                    inc       e                             ;[060f] 1c
                    rst       $28                           ;[0610] ef
                    jr        $0613                         ;[0611] 18 00
                    cp        $b9                           ;[0613] fe b9
                    jr        nz,$061f                      ;[0615] 20 08
                    ld        hl,$5b66                      ;[0617] 21 66 5b
                    set       6,(hl)                        ;[061a] cb f6
                    rst       $28                           ;[061c] ef
                    jr        nz,$061f                      ;[061d] 20 00
                    call      $10cd                         ;[061f] cd cd 10
                    rst       $28                           ;[0622] ef
                    pop       af                            ;[0623] f1
                    dec       hl                            ;[0624] 2b
                    push      bc                            ;[0625] c5
                    push      de                            ;[0626] d5
                    pop       hl                            ;[0627] e1
                    ld        a,$3a                         ;[0628] 3e 3a
                    cpir                                    ;[062a] ed b1
                    jr        nz,$0638                      ;[062c] 20 0a
                    dec       hl                            ;[062e] 2b
                    dec       hl                            ;[062f] 2b
                    ld        a,(hl)                        ;[0630] 7e
                    and       $df                           ;[0631] e6 df
                    ld        ($5c0b),a                     ;[0633] 32 0b 5c
                    jr        $063d                         ;[0636] 18 05
                    ld        a,$00                         ;[0638] 3e 00
                    ld        ($5c0b),a                     ;[063a] 32 0b 5c
                    pop       bc                            ;[063d] c1
                    jr        $065a                         ;[063e] 18 1a
                    rst       $28                           ;[0640] ef
                    jr        $0643                         ;[0641] 18 00
                    cp        $b9                           ;[0643] fe b9
                    jr        nz,$064f                      ;[0645] 20 08
                    ld        hl,$5b66                      ;[0647] 21 66 5b
                    set       6,(hl)                        ;[064a] cb f6
                    rst       $28                           ;[064c] ef
                    jr        nz,$064f                      ;[064d] 20 00
                    call      $10cd                         ;[064f] cd cd 10
                    ld        bc,$0000                      ;[0652] 01 00 00
                    ld        a,$00                         ;[0655] 3e 00
                    ld        ($5c0b),a                     ;[0657] 32 0b 5c
                    ld        a,c                           ;[065a] 79
                    dec       a                             ;[065b] 3d
                    dec       a                             ;[065c] 3d
                    or        b                             ;[065d] b0
                    jr        nz,$0671                      ;[065e] 20 11
                    inc       de                            ;[0660] 13
                    ld        a,(de)                        ;[0661] 1a
                    dec       de                            ;[0662] 1b
                    cp        $3a                           ;[0663] fe 3a
                    jr        nz,$0671                      ;[0665] 20 0a
                    ld        a,(de)                        ;[0667] 1a
                    and       $df                           ;[0668] e6 df
                    cp        $54                           ;[066a] fe 54
                    jr        nz,$0671                      ;[066c] 20 03
                    jp        $365b                         ;[066e] c3 5b 36
                    ld        hl,$ed01                      ;[0671] 21 01 ed
                    ex        de,hl                         ;[0674] eb
                    push      bc                            ;[0675] c5
                    ld        a,b                           ;[0676] 78
                    or        c                             ;[0677] b1
                    jr        z,$067d                       ;[0678] 28 03
                    call      $3f63                         ;[067a] cd 63 3f
                    pop       bc                            ;[067d] c1
                    ld        hl,$ed01                      ;[067e] 21 01 ed
                    add       hl,bc                         ;[0681] 09
                    call      $2cfb                         ;[0682] cd fb 2c
                    ld        (hl),$ff                      ;[0685] 36 ff
                    ld        hl,$ed11                      ;[0687] 21 11 ed
                    ld        de,$ed12                      ;[068a] 11 12 ed
                    ld        bc,$000b                      ;[068d] 01 0b 00
                    ld        (hl),$00                      ;[0690] 36 00
                    ldir                                    ;[0692] ed b0
                    ld        b,$40                         ;[0694] 06 40
                    ld        c,$00                         ;[0696] 0e 00
                    ld        hl,$5b66                      ;[0698] 21 66 5b
                    bit       6,(hl)                        ;[069b] cb 76
                    jr        z,$06a1                       ;[069d] 28 02
                    ld        c,$01                         ;[069f] 0e 01
                    ld        de,$ed11                      ;[06a1] 11 11 ed
                    ld        hl,$ed01                      ;[06a4] 21 01 ed
                    call      $342d                         ;[06a7] cd 2d 34
                    call      $3f00                         ;[06aa] cd 00 3f
                    ld        e,$01                         ;[06ad] 1e 01
                    call      $3465                         ;[06af] cd 65 34
                    jp        nc,$06c0                      ;[06b2] d2 c0 06
                    ld        hl,$ed1e                      ;[06b5] 21 1e ed
                    dec       b                             ;[06b8] 05
                    ld        a,b                           ;[06b9] 78
                    or        a                             ;[06ba] b7
                    jr        nz,$06cc                      ;[06bb] 20 0f
                    jp        $07cf                         ;[06bd] c3 cf 07
                    cp        $17                           ;[06c0] fe 17
                    jp        z,$07cf                       ;[06c2] ca cf 07
                    call      $2cd6                         ;[06c5] cd d6 2c
                    call      $0eb6                         ;[06c8] cd b6 0e
                    rst       $38                           ;[06cb] ff
                    push      bc                            ;[06cc] c5
                    push      af                            ;[06cd] f5
                    ld        b,$08                         ;[06ce] 06 08
                    ld        a,(hl)                        ;[06d0] 7e
                    and       $7f                           ;[06d1] e6 7f
                    call      $2cd6                         ;[06d3] cd d6 2c
                    rst       $28                           ;[06d6] ef
                    djnz      $06d9                         ;[06d7] 10 00
                    call      $2cfb                         ;[06d9] cd fb 2c
                    inc       hl                            ;[06dc] 23
                    djnz      $06d0                         ;[06dd] 10 f1
                    call      $2cd6                         ;[06df] cd d6 2c
                    ld        a,$2e                         ;[06e2] 3e 2e
                    rst       $28                           ;[06e4] ef
                    djnz      $06e7                         ;[06e5] 10 00
                    xor       a                             ;[06e7] af
                    ld        ($5b5e),a                     ;[06e8] 32 5e 5b
                    ld        b,$03                         ;[06eb] 06 03
                    call      $2cfb                         ;[06ed] cd fb 2c
                    ld        a,(hl)                        ;[06f0] 7e
                    bit       7,a                           ;[06f1] cb 7f
                    jr        z,$0711                       ;[06f3] 28 1c
                    push      af                            ;[06f5] f5
                    push      hl                            ;[06f6] e5
                    ld        hl,$5b5e                      ;[06f7] 21 5e 5b
                    ld        a,b                           ;[06fa] 78
                    cp        $03                           ;[06fb] fe 03
                    jr        nz,$0703                      ;[06fd] 20 04
                    set       3,(hl)                        ;[06ff] cb de
                    jr        $070d                         ;[0701] 18 0a
                    cp        $02                           ;[0703] fe 02
                    jr        nz,$070b                      ;[0705] 20 04
                    set       2,(hl)                        ;[0707] cb d6
                    jr        $070d                         ;[0709] 18 02
                    set       1,(hl)                        ;[070b] cb ce
                    pop       hl                            ;[070d] e1
                    pop       af                            ;[070e] f1
                    and       $7f                           ;[070f] e6 7f
                    call      $2cd6                         ;[0711] cd d6 2c
                    rst       $28                           ;[0714] ef
                    djnz      $0717                         ;[0715] 10 00
                    inc       hl                            ;[0717] 23
                    djnz      $06ed                         ;[0718] 10 d3
                    push      hl                            ;[071a] e5
                    ld        hl,$5b66                      ;[071b] 21 66 5b
                    bit       6,(hl)                        ;[071e] cb 76
                    pop       hl                            ;[0720] e1
                    jr        z,$0753                       ;[0721] 28 30
                    ld        a,($5b5e)                     ;[0723] 3a 5e 5b
                    push      hl                            ;[0726] e5
                    ld        hl,$0827                      ;[0727] 21 27 08
                    bit       3,a                           ;[072a] cb 5f
                    jr        z,$0731                       ;[072c] 28 03
                    ld        hl,$082d                      ;[072e] 21 2d 08
                    push      af                            ;[0731] f5
                    call      $07f7                         ;[0732] cd f7 07
                    pop       af                            ;[0735] f1
                    ld        hl,$0828                      ;[0736] 21 28 08
                    bit       2,a                           ;[0739] cb 57
                    jr        z,$0740                       ;[073b] 28 03
                    ld        hl,$0833                      ;[073d] 21 33 08
                    push      af                            ;[0740] f5
                    call      $07f7                         ;[0741] cd f7 07
                    pop       af                            ;[0744] f1
                    ld        hl,$0828                      ;[0745] 21 28 08
                    bit       1,a                           ;[0748] cb 4f
                    jr        z,$074f                       ;[074a] 28 03
                    ld        hl,$0838                      ;[074c] 21 38 08
                    call      $07f7                         ;[074f] cd f7 07
                    pop       hl                            ;[0752] e1
                    ld        a,$20                         ;[0753] 3e 20
                    rst       $28                           ;[0755] ef
                    djnz      $0758                         ;[0756] 10 00
                    push      hl                            ;[0758] e5
                    call      $2cfb                         ;[0759] cd fb 2c
                    ld        a,(hl)                        ;[075c] 7e
                    inc       hl                            ;[075d] 23
                    ld        h,(hl)                        ;[075e] 66
                    inc       hl                            ;[075f] 23
                    call      $2cd6                         ;[0760] cd d6 2c
                    ld        l,a                           ;[0763] 6f
                    ld        e,$20                         ;[0764] 1e 20
                    call      $0815                         ;[0766] cd 15 08
                    pop       hl                            ;[0769] e1
                    inc       hl                            ;[076a] 23
                    inc       hl                            ;[076b] 23
                    ld        a,$4b                         ;[076c] 3e 4b
                    rst       $28                           ;[076e] ef
                    djnz      $0771                         ;[076f] 10 00
                    call      $0800                         ;[0771] cd 00 08
                    call      $2cfb                         ;[0774] cd fb 2c
                    pop       af                            ;[0777] f1
                    dec       a                             ;[0778] 3d
                    jp        nz,$06cd                      ;[0779] c2 cd 06
                    pop       bc                            ;[077c] c1
                    ld        a,b                           ;[077d] 78
                    sub       $40                           ;[077e] d6 40
                    jr        c,$0790                       ;[0780] 38 0e
                    ld        hl,$f044                      ;[0782] 21 44 f0
                    ld        de,$ed11                      ;[0785] 11 11 ed
                    ld        bc,$000d                      ;[0788] 01 0d 00
                    ldir                                    ;[078b] ed b0
                    jp        $0694                         ;[078d] c3 94 06
                    call      $2cd6                         ;[0790] cd d6 2c
                    call      $0800                         ;[0793] cd 00 08
                    call      $2cfb                         ;[0796] cd fb 2c
                    ld        a,($5c0b)                     ;[0799] 3a 0b 5c
                    or        a                             ;[079c] b7
                    jr        nz,$07af                      ;[079d] 20 10
                    ld        a,$ff                         ;[079f] 3e ff
                    call      $342d                         ;[07a1] cd 2d 34
                    call      $3f00                         ;[07a4] cd 00 3f
                    dec       l                             ;[07a7] 2d
                    ld        bc,$65cd                      ;[07a8] 01 cd 65
                    inc       (hl)                          ;[07ab] 34
                    jp        nc,$06c0                      ;[07ac] d2 c0 06
                    call      $342d                         ;[07af] cd 2d 34
                    call      $3f00                         ;[07b2] cd 00 3f
                    ld        hl,$cd01                      ;[07b5] 21 01 cd
                    ld        h,l                           ;[07b8] 65
                    inc       (hl)                          ;[07b9] 34
                    jp        nc,$06c0                      ;[07ba] d2 c0 06
                    call      $2cd6                         ;[07bd] cd d6 2c
                    ld        e,$20                         ;[07c0] 1e 20
                    call      $0815                         ;[07c2] cd 15 08
                    ld        hl,$07de                      ;[07c5] 21 de 07
                    call      $07f7                         ;[07c8] cd f7 07
                    call      $0800                         ;[07cb] cd 00 08
                    ret                                     ;[07ce] c9

                    call      $2cd6                         ;[07cf] cd d6 2c
                    ld        hl,$07e6                      ;[07d2] 21 e6 07
                    call      $07f7                         ;[07d5] cd f7 07
                    call      $2cfb                         ;[07d8] cd fb 2c
                    jp        $0799                         ;[07db] c3 99 07
                    ld        c,e                           ;[07de] 4b
                    jr        nz,$0847                      ;[07df] 20 66
                    ld        (hl),d                        ;[07e1] 72
                    ld        h,l                           ;[07e2] 65
                    ld        h,l                           ;[07e3] 65
                    dec       c                             ;[07e4] 0d
                    nop                                     ;[07e5] 00
                    ld        c,(hl)                        ;[07e6] 4e
                    ld        l,a                           ;[07e7] 6f
                    jr        nz,$0850                      ;[07e8] 20 66
                    ld        l,c                           ;[07ea] 69
                    ld        l,h                           ;[07eb] 6c
                    ld        h,l                           ;[07ec] 65
                    ld        (hl),e                        ;[07ed] 73
                    jr        nz,$0856                      ;[07ee] 20 66
                    ld        l,a                           ;[07f0] 6f
                    ld        (hl),l                        ;[07f1] 75
                    ld        l,(hl)                        ;[07f2] 6e
                    ld        h,h                           ;[07f3] 64
                    dec       c                             ;[07f4] 0d
                    dec       c                             ;[07f5] 0d
                    nop                                     ;[07f6] 00
                    ld        a,(hl)                        ;[07f7] 7e
                    or        a                             ;[07f8] b7
                    ret       z                             ;[07f9] c8
                    rst       $28                           ;[07fa] ef
                    djnz      $07fd                         ;[07fb] 10 00
                    inc       hl                            ;[07fd] 23
                    jr        $07f7                         ;[07fe] 18 f7
                    ld        a,$0d                         ;[0800] 3e 0d
                    rst       $28                           ;[0802] ef
                    djnz      $0805                         ;[0803] 10 00
                    ret                                     ;[0805] c9

                    push      hl                            ;[0806] e5
                    ld        bc,$d8f0                      ;[0807] 01 f0 d8
                    rst       $28                           ;[080a] ef
                    ld        hl,($0119)                    ;[080b] 2a 19 01
                    jr        $080c                         ;[080e] 18 fc
                    rst       $28                           ;[0810] ef
                    ld        hl,($1819)                    ;[0811] 2a 19 18
                    ld        bc,$01e5                      ;[0814] 01 e5 01
                    sbc       h                             ;[0817] 9c
                    rst       $38                           ;[0818] ff
                    rst       $28                           ;[0819] ef
                    ld        hl,($0e19)                    ;[081a] 2a 19 0e
                    or        $ef                           ;[081d] f6 ef
                    ld        hl,($7d19)                    ;[081f] 2a 19 7d
                    rst       $28                           ;[0822] ef
                    rst       $28                           ;[0823] ef
                    dec       d                             ;[0824] 15
                    pop       hl                            ;[0825] e1
                    ret                                     ;[0826] c9

                    jr        nz,$0849                      ;[0827] 20 20
                    jr        nz,$084b                      ;[0829] 20 20
                    jr        nz,$082d                      ;[082b] 20 00
                    jr        nz,$087f                      ;[082d] 20 50
                    ld        d,d                           ;[082f] 52
                    ld        c,a                           ;[0830] 4f
                    ld        d,h                           ;[0831] 54
                    nop                                     ;[0832] 00
                    jr        nz,$0888                      ;[0833] 20 53
                    ld        e,c                           ;[0835] 59
                    ld        d,e                           ;[0836] 53
                    nop                                     ;[0837] 00
                    jr        nz,$087b                      ;[0838] 20 41
                    ld        d,d                           ;[083a] 52
                    ld        b,e                           ;[083b] 43
                    nop                                     ;[083c] 00
                    ld        hl,$0845                      ;[083d] 21 45 08
                    push      hl                            ;[0840] e5
                    rst       $28                           ;[0841] ef
                    add       $04                           ;[0842] c6 04
                    ret                                     ;[0844] c9

                    rst       $28                           ;[0845] ef
                    ccf                                     ;[0846] 3f
                    dec       b                             ;[0847] 05
                    ret                                     ;[0848] c9

                    push      af                            ;[0849] f5
                    ld        a,($5b5e)                     ;[084a] 3a 5e 5b
                    cp        $54                           ;[084d] fe 54
                    jp        z,$0898                       ;[084f] ca 98 08
                    pop       af                            ;[0852] f1
                    jr        nc,$088f                      ;[0853] 30 3a
                    push      hl                            ;[0855] e5
                    push      de                            ;[0856] d5
                    push      bc                            ;[0857] c5
                    ld        b,$00                         ;[0858] 06 00
                    ld        c,$00                         ;[085a] 0e 00
                    push      ix                            ;[085c] dd e5
                    pop       hl                            ;[085e] e1
                    call      $2cfb                         ;[085f] cd fb 2c
                    call      $342d                         ;[0862] cd 2d 34
                    call      $3f00                         ;[0865] cd 00 3f
                    ld        (de),a                        ;[0868] 12
                    ld        bc,$65cd                      ;[0869] 01 cd 65
                    inc       (hl)                          ;[086c] 34
                    call      $2cd6                         ;[086d] cd d6 2c
                    jr        c,$087a                       ;[0870] 38 08
                    cp        $19                           ;[0872] fe 19
                    jr        nz,$0894                      ;[0874] 20 1e
                    call      $0eb6                         ;[0876] cd b6 0e
                    ld        sp,$0006                      ;[0879] 31 06 00
                    call      $2cfb                         ;[087c] cd fb 2c
                    call      $342d                         ;[087f] cd 2d 34
                    call      $3f00                         ;[0882] cd 00 3f
                    add       hl,bc                         ;[0885] 09
                    ld        bc,$65cd                      ;[0886] 01 cd 65
                    inc       (hl)                          ;[0889] 34
                    call      $2cd6                         ;[088a] cd d6 2c
                    jr        nc,$0894                      ;[088d] 30 05
                    scf                                     ;[088f] 37
                    pop       bc                            ;[0890] c1
                    pop       de                            ;[0891] d1
                    pop       hl                            ;[0892] e1
                    ret                                     ;[0893] c9

                    call      $0eb6                         ;[0894] cd b6 0e
                    rst       $38                           ;[0897] ff
                    pop       af                            ;[0898] f1
                    rst       $28                           ;[0899] ef
                    ld        d,(hl)                        ;[089a] 56
                    dec       b                             ;[089b] 05
                    ret                                     ;[089c] c9

                    pop       af                            ;[089d] f1
                    ld        a,($5c74)                     ;[089e] 3a 74 5c
                    sub       $9f                           ;[08a1] d6 9f
                    ld        ($5c74),a                     ;[08a3] 32 74 5c
                    call      $1145                         ;[08a6] cd 45 11
                    bit       7,(iy+$01)                    ;[08a9] fd cb 01 7e
                    jp        z,$09d6                       ;[08ad] ca d6 09
                    ld        bc,$0011                      ;[08b0] 01 11 00
                    ld        a,($5c74)                     ;[08b3] 3a 74 5c
                    and       a                             ;[08b6] a7
                    jr        z,$08bb                       ;[08b7] 28 02
                    ld        c,$22                         ;[08b9] 0e 22
                    rst       $28                           ;[08bb] ef
                    jr        nc,$08be                      ;[08bc] 30 00
                    push      de                            ;[08be] d5
                    pop       ix                            ;[08bf] dd e1
                    ld        b,$0b                         ;[08c1] 06 0b
                    ld        a,$20                         ;[08c3] 3e 20
                    ld        (de),a                        ;[08c5] 12
                    inc       de                            ;[08c6] 13
                    djnz      $08c5                         ;[08c7] 10 fc
                    ld        (ix+$01),$ff                  ;[08c9] dd 36 01 ff
                    rst       $28                           ;[08cd] ef
                    pop       af                            ;[08ce] f1
                    dec       hl                            ;[08cf] 2b
                    push      de                            ;[08d0] d5
                    push      bc                            ;[08d1] c5
                    ld        a,c                           ;[08d2] 79
                    dec       a                             ;[08d3] 3d
                    or        b                             ;[08d4] b0
                    jr        nz,$0900                      ;[08d5] 20 29
                    ld        a,(de)                        ;[08d7] 1a
                    cp        $2a                           ;[08d8] fe 2a
                    jr        nz,$0900                      ;[08da] 20 24
                    ld        a,($5c74)                     ;[08dc] 3a 74 5c
                    cp        $01                           ;[08df] fe 01
                    jr        nz,$0900                      ;[08e1] 20 1d
                    call      $2cfb                         ;[08e3] cd fb 2c
                    call      $342d                         ;[08e6] cd 2d 34
                    call      $3f00                         ;[08e9] cd 00 3f
                    ld        hl,($cd01)                    ;[08ec] 2a 01 cd
                    ld        h,l                           ;[08ef] 65
                    inc       (hl)                          ;[08f0] 34
                    call      $2cd6                         ;[08f1] cd d6 2c
                    cp        $23                           ;[08f4] fe 23
                    jr        nz,$08fc                      ;[08f6] 20 04
                    call      $0eb6                         ;[08f8] cd b6 0e
                    dec       sp                            ;[08fb] 3b
                    call      $0eb6                         ;[08fc] cd b6 0e
                    rst       $38                           ;[08ff] ff
                    inc       de                            ;[0900] 13
                    ld        a,(de)                        ;[0901] 1a
                    dec       de                            ;[0902] 1b
                    cp        $3a                           ;[0903] fe 3a
                    jr        nz,$091a                      ;[0905] 20 13
                    ld        a,(de)                        ;[0907] 1a
                    and       $df                           ;[0908] e6 df
                    cp        $41                           ;[090a] fe 41
                    jr        z,$092b                       ;[090c] 28 1d
                    cp        $42                           ;[090e] fe 42
                    jr        z,$092b                       ;[0910] 28 19
                    cp        $4d                           ;[0912] fe 4d
                    jr        z,$092b                       ;[0914] 28 15
                    cp        $54                           ;[0916] fe 54
                    jr        z,$092b                       ;[0918] 28 11
                    ld        a,($5c74)                     ;[091a] 3a 74 5c
                    or        a                             ;[091d] b7
                    ld        a,($5b7a)                     ;[091e] 3a 7a 5b
                    jr        z,$0926                       ;[0921] 28 03
                    ld        a,($5b79)                     ;[0923] 3a 79 5b
                    ld        ($5b5e),a                     ;[0926] 32 5e 5b
                    jr        $0988                         ;[0929] 18 5d
                    ld        l,a                           ;[092b] 6f
                    ld        a,c                           ;[092c] 79
                    dec       a                             ;[092d] 3d
                    dec       a                             ;[092e] 3d
                    or        b                             ;[092f] b0
                    jr        nz,$0982                      ;[0930] 20 50
                    ld        a,($5c74)                     ;[0932] 3a 74 5c
                    or        a                             ;[0935] b7
                    jr        z,$093f                       ;[0936] 28 07
                    cp        $01                           ;[0938] fe 01
                    jr        z,$097c                       ;[093a] 28 40
                    ld        a,l                           ;[093c] 7d
                    jr        $0988                         ;[093d] 18 49
                    ld        a,l                           ;[093f] 7d
                    cp        $4d                           ;[0940] fe 4d
                    jr        z,$095b                       ;[0942] 28 17
                    cp        $54                           ;[0944] fe 54
                    jr        z,$095b                       ;[0946] 28 13
                    ld        hl,$5b66                      ;[0948] 21 66 5b
                    bit       4,(hl)                        ;[094b] cb 66
                    jr        z,$0957                       ;[094d] 28 08
                    cp        $41                           ;[094f] fe 41
                    jr        z,$095b                       ;[0951] 28 08
                    bit       5,(hl)                        ;[0953] cb 6e
                    jr        nz,$095b                      ;[0955] 20 04
                    call      $2c4c                         ;[0957] cd 4c 2c
                    ld        c,(hl)                        ;[095a] 4e
                    ld        ($5b7a),a                     ;[095b] 32 7a 5b
                    cp        $54                           ;[095e] fe 54
                    jr        z,$0979                       ;[0960] 28 17
                    call      $2cfb                         ;[0962] cd fb 2c
                    call      $342d                         ;[0965] cd 2d 34
                    call      $3f00                         ;[0968] cd 00 3f
                    dec       l                             ;[096b] 2d
                    ld        bc,$65cd                      ;[096c] 01 cd 65
                    inc       (hl)                          ;[096f] 34
                    call      $2cd6                         ;[0970] cd d6 2c
                    jr        c,$0979                       ;[0973] 38 04
                    call      $0eb6                         ;[0975] cd b6 0e
                    rst       $38                           ;[0978] ff
                    pop       bc                            ;[0979] c1
                    pop       de                            ;[097a] d1
                    ret                                     ;[097b] c9

                    ld        a,l                           ;[097c] 7d
                    ld        ($5b79),a                     ;[097d] 32 79 5b
                    jr        $095e                         ;[0980] 18 dc
                    ld        a,(de)                        ;[0982] 1a
                    and       $df                           ;[0983] e6 df
                    ld        ($5b5e),a                     ;[0985] 32 5e 5b
                    cp        $54                           ;[0988] fe 54
                    jr        z,$09b4                       ;[098a] 28 28
                    ld        a,($5c74)                     ;[098c] 3a 74 5c
                    cp        $02                           ;[098f] fe 02
                    jr        nz,$0996                      ;[0991] 20 03
                    pop       hl                            ;[0993] e1
                    pop       hl                            ;[0994] e1
                    ret                                     ;[0995] c9

                    ld        a,b                           ;[0996] 78
                    or        c                             ;[0997] b1
                    jr        nz,$099e                      ;[0998] 20 04
                    call      $0eb6                         ;[099a] cd b6 0e
                    ld        c,$21                         ;[099d] 0e 21
                    ld        bc,$ebed                      ;[099f] 01 ed eb
                    call      $3f63                         ;[09a2] cd 63 3f
                    pop       bc                            ;[09a5] c1
                    ld        bc,$000a                      ;[09a6] 01 0a 00
                    call      $2cfb                         ;[09a9] cd fb 2c
                    ld        a,$ff                         ;[09ac] 3e ff
                    ld        (de),a                        ;[09ae] 12
                    call      $2cd6                         ;[09af] cd d6 2c
                    jr        $09b5                         ;[09b2] 18 01
                    pop       bc                            ;[09b4] c1
                    pop       de                            ;[09b5] d1
                    ld        hl,$fff6                      ;[09b6] 21 f6 ff
                    dec       bc                            ;[09b9] 0b
                    add       hl,bc                         ;[09ba] 09
                    inc       bc                            ;[09bb] 03
                    jr        nc,$09cf                      ;[09bc] 30 11
                    ld        a,($5c74)                     ;[09be] 3a 74 5c
                    and       a                             ;[09c1] a7
                    jr        nz,$09c8                      ;[09c2] 20 04
                    call      $0eb6                         ;[09c4] cd b6 0e
                    ld        c,$78                         ;[09c7] 0e 78
                    or        c                             ;[09c9] b1
                    jr        z,$09d6                       ;[09ca] 28 0a
                    ld        bc,$000a                      ;[09cc] 01 0a 00
                    push      ix                            ;[09cf] dd e5
                    pop       hl                            ;[09d1] e1
                    inc       hl                            ;[09d2] 23
                    ex        de,hl                         ;[09d3] eb
                    ldir                                    ;[09d4] ed b0
                    rst       $28                           ;[09d6] ef
                    jr        $09d9                         ;[09d7] 18 00
                    cp        $e4                           ;[09d9] fe e4
                    jr        nz,$0a2d                      ;[09db] 20 50
                    ld        a,($5c74)                     ;[09dd] 3a 74 5c
                    cp        $03                           ;[09e0] fe 03
                    jp        z,$1141                       ;[09e2] ca 41 11
                    rst       $28                           ;[09e5] ef
                    jr        nz,$09e8                      ;[09e6] 20 00
                    rst       $28                           ;[09e8] ef
                    or        d                             ;[09e9] b2
                    jr        z,$09b7                       ;[09ea] 28 cb
                    ld        sp,hl                         ;[09ec] f9
                    jr        nc,$09fc                      ;[09ed] 30 0d
                    ld        hl,$0000                      ;[09ef] 21 00 00
                    ld        a,($5c74)                     ;[09f2] 3a 74 5c
                    dec       a                             ;[09f5] 3d
                    jr        z,$0a10                       ;[09f6] 28 18
                    call      $0eb6                         ;[09f8] cd b6 0e
                    ld        bc,$41c2                      ;[09fb] 01 c2 41
                    ld        de,$cbfd                      ;[09fe] 11 fd cb
                    ld        bc,$287e                      ;[0a01] 01 7e 28
                    jr        $0a29                         ;[0a04] 18 23
                    ld        a,(hl)                        ;[0a06] 7e
                    ld        (ix+$0b),a                    ;[0a07] dd 77 0b
                    inc       hl                            ;[0a0a] 23
                    ld        a,(hl)                        ;[0a0b] 7e
                    ld        (ix+$0c),a                    ;[0a0c] dd 77 0c
                    inc       hl                            ;[0a0f] 23
                    ld        (ix+$0e),c                    ;[0a10] dd 71 0e
                    ld        a,$01                         ;[0a13] 3e 01
                    bit       6,c                           ;[0a15] cb 71
                    jr        z,$0a1a                       ;[0a17] 28 01
                    inc       a                             ;[0a19] 3c
                    ld        (ix+$00),a                    ;[0a1a] dd 77 00
                    ex        de,hl                         ;[0a1d] eb
                    rst       $28                           ;[0a1e] ef
                    jr        nz,$0a21                      ;[0a1f] 20 00
                    cp        $29                           ;[0a21] fe 29
                    jr        nz,$09fc                      ;[0a23] 20 d7
                    rst       $20                           ;[0a25] e7
                    call      $10cd                         ;[0a26] cd cd 10
                    ex        de,hl                         ;[0a29] eb
                    jp        $0af1                         ;[0a2a] c3 f1 0a
                    cp        $aa                           ;[0a2d] fe aa
                    jr        nz,$0a52                      ;[0a2f] 20 21
                    ld        a,($5c74)                     ;[0a31] 3a 74 5c
                    cp        $03                           ;[0a34] fe 03
                    jp        z,$1141                       ;[0a36] ca 41 11
                    rst       $28                           ;[0a39] ef
                    jr        nz,$0a3c                      ;[0a3a] 20 00
                    call      $10cd                         ;[0a3c] cd cd 10
                    ld        (ix+$0b),$00                  ;[0a3f] dd 36 0b 00
                    ld        (ix+$0c),$1b                  ;[0a43] dd 36 0c 1b
                    ld        hl,$4000                      ;[0a47] 21 00 40
                    ld        (ix+$0d),l                    ;[0a4a] dd 75 0d
                    ld        (ix+$0e),h                    ;[0a4d] dd 74 0e
                    jr        $0aa5                         ;[0a50] 18 53
                    cp        $af                           ;[0a52] fe af
                    jr        nz,$0aab                      ;[0a54] 20 55
                    ld        a,($5c74)                     ;[0a56] 3a 74 5c
                    cp        $03                           ;[0a59] fe 03
                    jp        z,$1141                       ;[0a5b] ca 41 11
                    rst       $28                           ;[0a5e] ef
                    jr        nz,$0a61                      ;[0a5f] 20 00
                    call      $0eb0                         ;[0a61] cd b0 0e
                    jr        nz,$0a72                      ;[0a64] 20 0c
                    ld        a,($5c74)                     ;[0a66] 3a 74 5c
                    and       a                             ;[0a69] a7
                    jp        z,$1141                       ;[0a6a] ca 41 11
                    rst       $28                           ;[0a6d] ef
                    and       $1c                           ;[0a6e] e6 1c
                    jr        $0a83                         ;[0a70] 18 11
                    call      $113d                         ;[0a72] cd 3d 11
                    rst       $28                           ;[0a75] ef
                    jr        $0a78                         ;[0a76] 18 00
                    cp        $2c                           ;[0a78] fe 2c
                    jr        z,$0a88                       ;[0a7a] 28 0c
                    ld        a,($5c74)                     ;[0a7c] 3a 74 5c
                    and       a                             ;[0a7f] a7
                    jp        z,$1141                       ;[0a80] ca 41 11
                    rst       $28                           ;[0a83] ef
                    and       $1c                           ;[0a84] e6 1c
                    jr        $0a8e                         ;[0a86] 18 06
                    rst       $28                           ;[0a88] ef
                    jr        nz,$0a8b                      ;[0a89] 20 00
                    call      $113d                         ;[0a8b] cd 3d 11
                    call      $10cd                         ;[0a8e] cd cd 10
                    rst       $28                           ;[0a91] ef
                    sbc       c                             ;[0a92] 99
                    ld        e,$dd                         ;[0a93] 1e dd
                    ld        (hl),c                        ;[0a95] 71
                    dec       bc                            ;[0a96] 0b
                    ld        (ix+$0c),b                    ;[0a97] dd 70 0c
                    rst       $28                           ;[0a9a] ef
                    sbc       c                             ;[0a9b] 99
                    ld        e,$dd                         ;[0a9c] 1e dd
                    ld        (hl),c                        ;[0a9e] 71
                    dec       c                             ;[0a9f] 0d
                    ld        (ix+$0e),b                    ;[0aa0] dd 70 0e
                    ld        h,b                           ;[0aa3] 60
                    ld        l,c                           ;[0aa4] 69
                    ld        (ix+$00),$03                  ;[0aa5] dd 36 00 03
                    jr        $0af1                         ;[0aa9] 18 46
                    cp        $ca                           ;[0aab] fe ca
                    jr        z,$0ab8                       ;[0aad] 28 09
                    call      $10cd                         ;[0aaf] cd cd 10
                    ld        (ix+$0e),$80                  ;[0ab2] dd 36 0e 80
                    jr        $0ad1                         ;[0ab6] 18 19
                    ld        a,($5c74)                     ;[0ab8] 3a 74 5c
                    and       a                             ;[0abb] a7
                    jp        nz,$1141                      ;[0abc] c2 41 11
                    rst       $28                           ;[0abf] ef
                    jr        nz,$0ac2                      ;[0ac0] 20 00
                    call      $113d                         ;[0ac2] cd 3d 11
                    call      $10cd                         ;[0ac5] cd cd 10
                    rst       $28                           ;[0ac8] ef
                    sbc       c                             ;[0ac9] 99
                    ld        e,$dd                         ;[0aca] 1e dd
                    ld        (hl),c                        ;[0acc] 71
                    dec       c                             ;[0acd] 0d
                    ld        (ix+$0e),b                    ;[0ace] dd 70 0e
                    ld        (ix+$00),$00                  ;[0ad1] dd 36 00 00
                    ld        hl,($5c59)                    ;[0ad5] 2a 59 5c
                    ld        de,($5c53)                    ;[0ad8] ed 5b 53 5c
                    scf                                     ;[0adc] 37
                    sbc       hl,de                         ;[0add] ed 52
                    ld        (ix+$0b),l                    ;[0adf] dd 75 0b
                    ld        (ix+$0c),h                    ;[0ae2] dd 74 0c
                    ld        hl,($5c4b)                    ;[0ae5] 2a 4b 5c
                    sbc       hl,de                         ;[0ae8] ed 52
                    ld        (ix+$0f),l                    ;[0aea] dd 75 0f
                    ld        (ix+$10),h                    ;[0aed] dd 74 10
                    ex        de,hl                         ;[0af0] eb
                    ld        a,($5c74)                     ;[0af1] 3a 74 5c
                    and       a                             ;[0af4] a7
                    jp        z,$0d8a                       ;[0af5] ca 8a 0d
                    push      hl                            ;[0af8] e5
                    ld        bc,$0011                      ;[0af9] 01 11 00
                    add       ix,bc                         ;[0afc] dd 09
                    ld        a,($5b5e)                     ;[0afe] 3a 5e 5b
                    cp        $54                           ;[0b01] fe 54
                    jr        nz,$0b5d                      ;[0b03] 20 58
                    push      ix                            ;[0b05] dd e5
                    ld        de,$0011                      ;[0b07] 11 11 00
                    xor       a                             ;[0b0a] af
                    scf                                     ;[0b0b] 37
                    call      $0899                         ;[0b0c] cd 99 08
                    pop       ix                            ;[0b0f] dd e1
                    jr        nc,$0b05                      ;[0b11] 30 f2
                    ld        a,$fe                         ;[0b13] 3e fe
                    rst       $28                           ;[0b15] ef
                    ld        bc,$fd16                      ;[0b16] 01 16 fd
                    ld        (hl),$52                      ;[0b19] 36 52
                    inc       bc                            ;[0b1b] 03
                    ld        c,$80                         ;[0b1c] 0e 80
                    ld        a,(ix+$00)                    ;[0b1e] dd 7e 00
                    cp        (ix-$11)                      ;[0b21] dd be ef
                    jr        nz,$0b28                      ;[0b24] 20 02
                    ld        c,$f6                         ;[0b26] 0e f6
                    cp        $04                           ;[0b28] fe 04
                    jr        nc,$0b05                      ;[0b2a] 30 d9
                    ld        de,$09c0                      ;[0b2c] 11 c0 09
                    push      bc                            ;[0b2f] c5
                    rst       $28                           ;[0b30] ef
                    ld        a,(bc)                        ;[0b31] 0a
                    inc       c                             ;[0b32] 0c
                    pop       bc                            ;[0b33] c1
                    push      ix                            ;[0b34] dd e5
                    pop       de                            ;[0b36] d1
                    ld        hl,$fff0                      ;[0b37] 21 f0 ff
                    add       hl,de                         ;[0b3a] 19
                    ld        b,$0a                         ;[0b3b] 06 0a
                    ld        a,(hl)                        ;[0b3d] 7e
                    inc       a                             ;[0b3e] 3c
                    jr        nz,$0b44                      ;[0b3f] 20 03
                    ld        a,c                           ;[0b41] 79
                    add       b                             ;[0b42] 80
                    ld        c,a                           ;[0b43] 4f
                    inc       de                            ;[0b44] 13
                    ld        a,(de)                        ;[0b45] 1a
                    cp        (hl)                          ;[0b46] be
                    inc       hl                            ;[0b47] 23
                    jr        nz,$0b4b                      ;[0b48] 20 01
                    inc       c                             ;[0b4a] 0c
                    rst       $28                           ;[0b4b] ef
                    djnz      $0b4e                         ;[0b4c] 10 00
                    djnz      $0b44                         ;[0b4e] 10 f4
                    bit       7,c                           ;[0b50] cb 79
                    jr        nz,$0b05                      ;[0b52] 20 b1
                    ld        a,$0d                         ;[0b54] 3e 0d
                    rst       $28                           ;[0b56] ef
                    djnz      $0b59                         ;[0b57] 10 00
                    pop       hl                            ;[0b59] e1
                    jp        $0bc2                         ;[0b5a] c3 c2 0b
                    ld        a,($5c74)                     ;[0b5d] 3a 74 5c
                    cp        $02                           ;[0b60] fe 02
                    jr        z,$0bc2                       ;[0b62] 28 5e
                    push      ix                            ;[0b64] dd e5
                    ld        b,$00                         ;[0b66] 06 00
                    ld        c,$01                         ;[0b68] 0e 01
                    ld        d,$00                         ;[0b6a] 16 00
                    ld        e,$01                         ;[0b6c] 1e 01
                    ld        hl,$ed01                      ;[0b6e] 21 01 ed
                    call      $2cfb                         ;[0b71] cd fb 2c
                    call      $342d                         ;[0b74] cd 2d 34
                    call      $3f00                         ;[0b77] cd 00 3f
                    ld        b,$01                         ;[0b7a] 06 01
                    call      $3465                         ;[0b7c] cd 65 34
                    call      $2cd6                         ;[0b7f] cd d6 2c
                    jr        c,$0b88                       ;[0b82] 38 04
                    call      $0eb6                         ;[0b84] cd b6 0e
                    rst       $38                           ;[0b87] ff
                    ld        b,$00                         ;[0b88] 06 00
                    call      $2cfb                         ;[0b8a] cd fb 2c
                    call      $342d                         ;[0b8d] cd 2d 34
                    call      $3f00                         ;[0b90] cd 00 3f
                    rrca                                    ;[0b93] 0f
                    ld        bc,$65cd                      ;[0b94] 01 cd 65
                    inc       (hl)                          ;[0b97] 34
                    call      $2cd6                         ;[0b98] cd d6 2c
                    ex        (sp),ix                       ;[0b9b] dd e3
                    pop       hl                            ;[0b9d] e1
                    call      $2cfb                         ;[0b9e] cd fb 2c
                    ld        a,(hl)                        ;[0ba1] 7e
                    call      $2cd6                         ;[0ba2] cd d6 2c
                    cp        (ix-$11)                      ;[0ba5] dd be ef
                    jr        z,$0bae                       ;[0ba8] 28 04
                    call      $0eb6                         ;[0baa] cd b6 0e
                    dec       e                             ;[0bad] 1d
                    ld        (ix+$00),a                    ;[0bae] dd 77 00
                    push      ix                            ;[0bb1] dd e5
                    pop       de                            ;[0bb3] d1
                    ex        de,hl                         ;[0bb4] eb
                    ld        bc,$000b                      ;[0bb5] 01 0b 00
                    add       hl,bc                         ;[0bb8] 09
                    ex        de,hl                         ;[0bb9] eb
                    inc       hl                            ;[0bba] 23
                    ld        bc,$0006                      ;[0bbb] 01 06 00
                    call      $3f8a                         ;[0bbe] cd 8a 3f
                    pop       hl                            ;[0bc1] e1
                    ld        a,(ix+$00)                    ;[0bc2] dd 7e 00
                    cp        $03                           ;[0bc5] fe 03
                    jr        z,$0bd5                       ;[0bc7] 28 0c
                    ld        a,($5c74)                     ;[0bc9] 3a 74 5c
                    dec       a                             ;[0bcc] 3d
                    jp        z,$0c20                       ;[0bcd] ca 20 0c
                    cp        $02                           ;[0bd0] fe 02
                    jp        z,$0cce                       ;[0bd2] ca ce 0c
                    push      hl                            ;[0bd5] e5
                    ld        l,(ix-$06)                    ;[0bd6] dd 6e fa
                    ld        h,(ix-$05)                    ;[0bd9] dd 66 fb
                    ld        e,(ix+$0b)                    ;[0bdc] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[0bdf] dd 56 0c
                    ld        a,h                           ;[0be2] 7c
                    or        l                             ;[0be3] b5
                    jr        z,$0bf3                       ;[0be4] 28 0d
                    sbc       hl,de                         ;[0be6] ed 52
                    jr        c,$0c1c                       ;[0be8] 38 32
                    jr        z,$0bf3                       ;[0bea] 28 07
                    ld        a,(ix+$00)                    ;[0bec] dd 7e 00
                    cp        $03                           ;[0bef] fe 03
                    jr        nz,$0c18                      ;[0bf1] 20 25
                    pop       hl                            ;[0bf3] e1
                    ld        a,h                           ;[0bf4] 7c
                    or        l                             ;[0bf5] b5
                    jr        nz,$0bfe                      ;[0bf6] 20 06
                    ld        l,(ix+$0d)                    ;[0bf8] dd 6e 0d
                    ld        h,(ix+$0e)                    ;[0bfb] dd 66 0e
                    push      hl                            ;[0bfe] e5
                    pop       ix                            ;[0bff] dd e1
                    ld        a,($5c74)                     ;[0c01] 3a 74 5c
                    cp        $02                           ;[0c04] fe 02
                    scf                                     ;[0c06] 37
                    jr        nz,$0c12                      ;[0c07] 20 09
                    and       a                             ;[0c09] a7
                    ld        a,($5b5e)                     ;[0c0a] 3a 5e 5b
                    cp        $54                           ;[0c0d] fe 54
                    jr        z,$0c12                       ;[0c0f] 28 01
                    ret                                     ;[0c11] c9

                    ld        a,$ff                         ;[0c12] 3e ff
                    call      $0849                         ;[0c14] cd 49 08
                    ret       c                             ;[0c17] d8
                    call      $0eb6                         ;[0c18] cd b6 0e
                    ld        a,(de)                        ;[0c1b] 1a
                    call      $0eb6                         ;[0c1c] cd b6 0e
                    ld        c,a                           ;[0c1f] 4f
                    ld        e,(ix+$0b)                    ;[0c20] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[0c23] dd 56 0c
                    push      hl                            ;[0c26] e5
                    ld        a,h                           ;[0c27] 7c
                    or        l                             ;[0c28] b5
                    jr        nz,$0c31                      ;[0c29] 20 06
                    inc       de                            ;[0c2b] 13
                    inc       de                            ;[0c2c] 13
                    inc       de                            ;[0c2d] 13
                    ex        de,hl                         ;[0c2e] eb
                    jr        $0c3d                         ;[0c2f] 18 0c
                    ld        l,(ix-$06)                    ;[0c31] dd 6e fa
                    ld        h,(ix-$05)                    ;[0c34] dd 66 fb
                    ex        de,hl                         ;[0c37] eb
                    scf                                     ;[0c38] 37
                    sbc       hl,de                         ;[0c39] ed 52
                    jr        c,$0c46                       ;[0c3b] 38 09
                    ld        de,$0005                      ;[0c3d] 11 05 00
                    add       hl,de                         ;[0c40] 19
                    ld        b,h                           ;[0c41] 44
                    ld        c,l                           ;[0c42] 4d
                    rst       $28                           ;[0c43] ef
                    dec       b                             ;[0c44] 05
                    rra                                     ;[0c45] 1f
                    pop       hl                            ;[0c46] e1
                    ld        a,(ix+$00)                    ;[0c47] dd 7e 00
                    and       a                             ;[0c4a] a7
                    jr        z,$0c8b                       ;[0c4b] 28 3e
                    ld        a,h                           ;[0c4d] 7c
                    or        l                             ;[0c4e] b5
                    jr        z,$0c64                       ;[0c4f] 28 13
                    dec       hl                            ;[0c51] 2b
                    ld        b,(hl)                        ;[0c52] 46
                    dec       hl                            ;[0c53] 2b
                    ld        c,(hl)                        ;[0c54] 4e
                    dec       hl                            ;[0c55] 2b
                    inc       bc                            ;[0c56] 03
                    inc       bc                            ;[0c57] 03
                    inc       bc                            ;[0c58] 03
                    ld        ($5c5f),ix                    ;[0c59] dd 22 5f 5c
                    rst       $28                           ;[0c5d] ef
                    ret       pe                            ;[0c5e] e8
                    add       hl,de                         ;[0c5f] 19
                    ld        ix,($5c5f)                    ;[0c60] dd 2a 5f 5c
                    ld        hl,($5c59)                    ;[0c64] 2a 59 5c
                    dec       hl                            ;[0c67] 2b
                    ld        c,(ix+$0b)                    ;[0c68] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[0c6b] dd 46 0c
                    push      bc                            ;[0c6e] c5
                    inc       bc                            ;[0c6f] 03
                    inc       bc                            ;[0c70] 03
                    inc       bc                            ;[0c71] 03
                    ld        a,(ix-$03)                    ;[0c72] dd 7e fd
                    push      af                            ;[0c75] f5
                    rst       $28                           ;[0c76] ef
                    ld        d,l                           ;[0c77] 55
                    ld        d,$23                         ;[0c78] 16 23
                    pop       af                            ;[0c7a] f1
                    ld        (hl),a                        ;[0c7b] 77
                    pop       de                            ;[0c7c] d1
                    inc       hl                            ;[0c7d] 23
                    ld        (hl),e                        ;[0c7e] 73
                    inc       hl                            ;[0c7f] 23
                    ld        (hl),d                        ;[0c80] 72
                    inc       hl                            ;[0c81] 23
                    push      hl                            ;[0c82] e5
                    pop       ix                            ;[0c83] dd e1
                    scf                                     ;[0c85] 37
                    ld        a,$ff                         ;[0c86] 3e ff
                    jp        $0c14                         ;[0c88] c3 14 0c
                    ex        de,hl                         ;[0c8b] eb
                    ld        hl,($5c59)                    ;[0c8c] 2a 59 5c
                    dec       hl                            ;[0c8f] 2b
                    ld        ($5c5f),ix                    ;[0c90] dd 22 5f 5c
                    ld        c,(ix+$0b)                    ;[0c94] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[0c97] dd 46 0c
                    push      bc                            ;[0c9a] c5
                    rst       $28                           ;[0c9b] ef
                    push      hl                            ;[0c9c] e5
                    add       hl,de                         ;[0c9d] 19
                    pop       bc                            ;[0c9e] c1
                    push      hl                            ;[0c9f] e5
                    push      bc                            ;[0ca0] c5
                    rst       $28                           ;[0ca1] ef
                    ld        d,l                           ;[0ca2] 55
                    ld        d,$dd                         ;[0ca3] 16 dd
                    ld        hl,($5c5f)                    ;[0ca5] 2a 5f 5c
                    inc       hl                            ;[0ca8] 23
                    ld        c,(ix+$0f)                    ;[0ca9] dd 4e 0f
                    ld        b,(ix+$10)                    ;[0cac] dd 46 10
                    add       hl,bc                         ;[0caf] 09
                    ld        ($5c4b),hl                    ;[0cb0] 22 4b 5c
                    ld        h,(ix+$0e)                    ;[0cb3] dd 66 0e
                    ld        a,h                           ;[0cb6] 7c
                    and       $c0                           ;[0cb7] e6 c0
                    jr        nz,$0cc5                      ;[0cb9] 20 0a
                    ld        l,(ix+$0d)                    ;[0cbb] dd 6e 0d
                    ld        ($5c42),hl                    ;[0cbe] 22 42 5c
                    ld        (iy+$0a),$00                  ;[0cc1] fd 36 0a 00
                    pop       de                            ;[0cc5] d1
                    pop       ix                            ;[0cc6] dd e1
                    scf                                     ;[0cc8] 37
                    ld        a,$ff                         ;[0cc9] 3e ff
                    jp        $0c14                         ;[0ccb] c3 14 0c
                    ld        c,(ix+$0b)                    ;[0cce] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[0cd1] dd 46 0c
                    push      bc                            ;[0cd4] c5
                    inc       bc                            ;[0cd5] 03
                    rst       $28                           ;[0cd6] ef
                    jr        nc,$0cd9                      ;[0cd7] 30 00
                    ld        (hl),$80                      ;[0cd9] 36 80
                    ex        de,hl                         ;[0cdb] eb
                    pop       de                            ;[0cdc] d1
                    push      hl                            ;[0cdd] e5
                    push      hl                            ;[0cde] e5
                    pop       ix                            ;[0cdf] dd e1
                    scf                                     ;[0ce1] 37
                    ld        a,$ff                         ;[0ce2] 3e ff
                    call      $0c14                         ;[0ce4] cd 14 0c
                    pop       hl                            ;[0ce7] e1
                    ld        de,($5c53)                    ;[0ce8] ed 5b 53 5c
                    ld        a,(hl)                        ;[0cec] 7e
                    and       $c0                           ;[0ced] e6 c0
                    jr        nz,$0d0a                      ;[0cef] 20 19
                    ld        a,(de)                        ;[0cf1] 1a
                    inc       de                            ;[0cf2] 13
                    cp        (hl)                          ;[0cf3] be
                    inc       hl                            ;[0cf4] 23
                    jr        nz,$0cf9                      ;[0cf5] 20 02
                    ld        a,(de)                        ;[0cf7] 1a
                    cp        (hl)                          ;[0cf8] be
                    dec       de                            ;[0cf9] 1b
                    dec       hl                            ;[0cfa] 2b
                    jr        nc,$0d05                      ;[0cfb] 30 08
                    push      hl                            ;[0cfd] e5
                    ex        de,hl                         ;[0cfe] eb
                    rst       $28                           ;[0cff] ef
                    cp        b                             ;[0d00] b8
                    add       hl,de                         ;[0d01] 19
                    pop       hl                            ;[0d02] e1
                    jr        $0cf1                         ;[0d03] 18 ec
                    call      $0d46                         ;[0d05] cd 46 0d
                    jr        $0cec                         ;[0d08] 18 e2
                    ld        a,(hl)                        ;[0d0a] 7e
                    ld        c,a                           ;[0d0b] 4f
                    cp        $80                           ;[0d0c] fe 80
                    ret       z                             ;[0d0e] c8
                    push      hl                            ;[0d0f] e5
                    ld        hl,($5c4b)                    ;[0d10] 2a 4b 5c
                    ld        a,(hl)                        ;[0d13] 7e
                    cp        $80                           ;[0d14] fe 80
                    jr        z,$0d3d                       ;[0d16] 28 25
                    cp        c                             ;[0d18] b9
                    jr        z,$0d23                       ;[0d19] 28 08
                    push      bc                            ;[0d1b] c5
                    rst       $28                           ;[0d1c] ef
                    cp        b                             ;[0d1d] b8
                    add       hl,de                         ;[0d1e] 19
                    pop       bc                            ;[0d1f] c1
                    ex        de,hl                         ;[0d20] eb
                    jr        $0d13                         ;[0d21] 18 f0
                    and       $e0                           ;[0d23] e6 e0
                    cp        $a0                           ;[0d25] fe a0
                    jr        nz,$0d3b                      ;[0d27] 20 12
                    pop       de                            ;[0d29] d1
                    push      de                            ;[0d2a] d5
                    push      hl                            ;[0d2b] e5
                    inc       hl                            ;[0d2c] 23
                    inc       de                            ;[0d2d] 13
                    ld        a,(de)                        ;[0d2e] 1a
                    cp        (hl)                          ;[0d2f] be
                    jr        nz,$0d38                      ;[0d30] 20 06
                    rla                                     ;[0d32] 17
                    jr        nc,$0d2c                      ;[0d33] 30 f7
                    pop       hl                            ;[0d35] e1
                    jr        $0d3b                         ;[0d36] 18 03
                    pop       hl                            ;[0d38] e1
                    jr        $0d1b                         ;[0d39] 18 e0
                    ld        a,$ff                         ;[0d3b] 3e ff
                    pop       de                            ;[0d3d] d1
                    ex        de,hl                         ;[0d3e] eb
                    inc       a                             ;[0d3f] 3c
                    scf                                     ;[0d40] 37
                    call      $0d46                         ;[0d41] cd 46 0d
                    jr        $0d0a                         ;[0d44] 18 c4
                    jr        nz,$0d58                      ;[0d46] 20 10
                    ex        af,af'                        ;[0d48] 08
                    ld        ($5c5f),hl                    ;[0d49] 22 5f 5c
                    ex        de,hl                         ;[0d4c] eb
                    rst       $28                           ;[0d4d] ef
                    cp        b                             ;[0d4e] b8
                    add       hl,de                         ;[0d4f] 19
                    rst       $28                           ;[0d50] ef
                    ret       pe                            ;[0d51] e8
                    add       hl,de                         ;[0d52] 19
                    ex        de,hl                         ;[0d53] eb
                    ld        hl,($5c5f)                    ;[0d54] 2a 5f 5c
                    ex        af,af'                        ;[0d57] 08
                    ex        af,af'                        ;[0d58] 08
                    push      de                            ;[0d59] d5
                    rst       $28                           ;[0d5a] ef
                    cp        b                             ;[0d5b] b8
                    add       hl,de                         ;[0d5c] 19
                    ld        ($5c5f),hl                    ;[0d5d] 22 5f 5c
                    ld        hl,($5c53)                    ;[0d60] 2a 53 5c
                    ex        (sp),hl                       ;[0d63] e3
                    push      bc                            ;[0d64] c5
                    ex        af,af'                        ;[0d65] 08
                    jr        c,$0d6f                       ;[0d66] 38 07
                    dec       hl                            ;[0d68] 2b
                    rst       $28                           ;[0d69] ef
                    ld        d,l                           ;[0d6a] 55
                    ld        d,$23                         ;[0d6b] 16 23
                    jr        $0d72                         ;[0d6d] 18 03
                    rst       $28                           ;[0d6f] ef
                    ld        d,l                           ;[0d70] 55
                    ld        d,$23                         ;[0d71] 16 23
                    pop       bc                            ;[0d73] c1
                    pop       de                            ;[0d74] d1
                    ld        ($5c53),de                    ;[0d75] ed 53 53 5c
                    ld        de,($5c5f)                    ;[0d79] ed 5b 5f 5c
                    push      bc                            ;[0d7d] c5
                    push      de                            ;[0d7e] d5
                    ex        de,hl                         ;[0d7f] eb
                    ldir                                    ;[0d80] ed b0
                    pop       hl                            ;[0d82] e1
                    pop       bc                            ;[0d83] c1
                    push      de                            ;[0d84] d5
                    rst       $28                           ;[0d85] ef
                    ret       pe                            ;[0d86] e8
                    add       hl,de                         ;[0d87] 19
                    pop       de                            ;[0d88] d1
                    ret                                     ;[0d89] c9

                    ld        a,($5b5e)                     ;[0d8a] 3a 5e 5b
                    cp        $54                           ;[0d8d] fe 54
                    jp        z,$0e2c                       ;[0d8f] ca 2c 0e
                    call      $2cfb                         ;[0d92] cd fb 2c
                    push      hl                            ;[0d95] e5
                    ld        b,$00                         ;[0d96] 06 00
                    ld        c,$03                         ;[0d98] 0e 03
                    ld        d,$01                         ;[0d9a] 16 01
                    ld        e,$03                         ;[0d9c] 1e 03
                    ld        hl,$ed01                      ;[0d9e] 21 01 ed
                    push      ix                            ;[0da1] dd e5
                    call      $342d                         ;[0da3] cd 2d 34
                    call      $3f00                         ;[0da6] cd 00 3f
                    ld        b,$01                         ;[0da9] 06 01
                    call      $3465                         ;[0dab] cd 65 34
                    jr        c,$0db7                       ;[0dae] 38 07
                    call      $2cd6                         ;[0db0] cd d6 2c
                    call      $0eb6                         ;[0db3] cd b6 0e
                    rst       $38                           ;[0db6] ff
                    ld        b,$00                         ;[0db7] 06 00
                    call      $342d                         ;[0db9] cd 2d 34
                    call      $3f00                         ;[0dbc] cd 00 3f
                    rrca                                    ;[0dbf] 0f
                    ld        bc,$65cd                      ;[0dc0] 01 cd 65
                    inc       (hl)                          ;[0dc3] 34
                    jr        c,$0dcd                       ;[0dc4] 38 07
                    call      $2cd6                         ;[0dc6] cd d6 2c
                    call      $0eb6                         ;[0dc9] cd b6 0e
                    rst       $38                           ;[0dcc] ff
                    ex        (sp),ix                       ;[0dcd] dd e3
                    pop       hl                            ;[0dcf] e1
                    call      $2cd6                         ;[0dd0] cd d6 2c
                    ld        a,(ix+$00)                    ;[0dd3] dd 7e 00
                    call      $2cfb                         ;[0dd6] cd fb 2c
                    ld        (hl),a                        ;[0dd9] 77
                    inc       hl                            ;[0dda] 23
                    push      ix                            ;[0ddb] dd e5
                    pop       de                            ;[0ddd] d1
                    ex        de,hl                         ;[0dde] eb
                    ld        bc,$000b                      ;[0ddf] 01 0b 00
                    add       hl,bc                         ;[0de2] 09
                    ld        bc,$0006                      ;[0de3] 01 06 00
                    call      $2cd6                         ;[0de6] cd d6 2c
                    call      $3f63                         ;[0de9] cd 63 3f
                    ld        b,$00                         ;[0dec] 06 00
                    ld        c,$00                         ;[0dee] 0e 00
                    ld        e,(ix+$0b)                    ;[0df0] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[0df3] dd 56 0c
                    ld        a,d                           ;[0df6] 7a
                    or        e                             ;[0df7] b3
                    call      $2cfb                         ;[0df8] cd fb 2c
                    jr        z,$0e12                       ;[0dfb] 28 15
                    pop       hl                            ;[0dfd] e1
                    call      $342d                         ;[0dfe] cd 2d 34
                    call      $3f00                         ;[0e01] cd 00 3f
                    dec       d                             ;[0e04] 15
                    ld        bc,$65cd                      ;[0e05] 01 cd 65
                    inc       (hl)                          ;[0e08] 34
                    jr        c,$0e12                       ;[0e09] 38 07
                    call      $2cd6                         ;[0e0b] cd d6 2c
                    call      $0eb6                         ;[0e0e] cd b6 0e
                    rst       $38                           ;[0e11] ff
                    ld        b,$00                         ;[0e12] 06 00
                    call      $342d                         ;[0e14] cd 2d 34
                    call      $3f00                         ;[0e17] cd 00 3f
                    add       hl,bc                         ;[0e1a] 09
                    ld        bc,$65cd                      ;[0e1b] 01 cd 65
                    inc       (hl)                          ;[0e1e] 34
                    jr        c,$0e28                       ;[0e1f] 38 07
                    call      $2cd6                         ;[0e21] cd d6 2c
                    call      $0eb6                         ;[0e24] cd b6 0e
                    rst       $38                           ;[0e27] ff
                    call      $2cd6                         ;[0e28] cd d6 2c
                    ret                                     ;[0e2b] c9

                    push      hl                            ;[0e2c] e5
                    ld        a,$fd                         ;[0e2d] 3e fd
                    rst       $28                           ;[0e2f] ef
                    ld        bc,$af16                      ;[0e30] 01 16 af
                    ld        de,$09a1                      ;[0e33] 11 a1 09
                    rst       $28                           ;[0e36] ef
                    ld        a,(bc)                        ;[0e37] 0a
                    inc       c                             ;[0e38] 0c
                    set       5,(iy+$02)                    ;[0e39] fd cb 02 ee
                    rst       $28                           ;[0e3d] ef
                    call      nc,$dd15                      ;[0e3e] d4 15 dd
                    push      hl                            ;[0e41] e5
                    ld        de,$0011                      ;[0e42] 11 11 00
                    xor       a                             ;[0e45] af
                    call      $083d                         ;[0e46] cd 3d 08
                    pop       ix                            ;[0e49] dd e1
                    ld        b,$32                         ;[0e4b] 06 32
                    halt                                    ;[0e4d] 76
                    djnz      $0e4d                         ;[0e4e] 10 fd
                    ld        e,(ix+$0b)                    ;[0e50] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[0e53] dd 56 0c
                    ld        a,$ff                         ;[0e56] 3e ff
                    pop       ix                            ;[0e58] dd e1
                    jp        $083d                         ;[0e5a] c3 3d 08
                    add       b                             ;[0e5d] 80
                    ld        d,b                           ;[0e5e] 50
                    ld        (hl),d                        ;[0e5f] 72
                    ld        h,l                           ;[0e60] 65
                    ld        (hl),e                        ;[0e61] 73
                    ld        (hl),e                        ;[0e62] 73
                    jr        nz,$0eb7                      ;[0e63] 20 52
                    ld        b,l                           ;[0e65] 45
                    ld        b,e                           ;[0e66] 43
                    jr        nz,$0e8f                      ;[0e67] 20 26
                    jr        nz,$0ebb                      ;[0e69] 20 50
                    ld        c,h                           ;[0e6b] 4c
                    ld        b,c                           ;[0e6c] 41
                    ld        e,c                           ;[0e6d] 59
                    inc       l                             ;[0e6e] 2c
                    jr        nz,$0ee5                      ;[0e6f] 20 74
                    ld        l,b                           ;[0e71] 68
                    ld        h,l                           ;[0e72] 65
                    ld        l,(hl)                        ;[0e73] 6e
                    jr        nz,$0ed7                      ;[0e74] 20 61
                    ld        l,(hl)                        ;[0e76] 6e
                    ld        a,c                           ;[0e77] 79
                    jr        nz,$0ee5                      ;[0e78] 20 6b
                    ld        h,l                           ;[0e7a] 65
                    ld        a,c                           ;[0e7b] 79
                    xor       (hl)                          ;[0e7c] ae
                    dec       c                             ;[0e7d] 0d
                    ld        d,b                           ;[0e7e] 50
                    ld        (hl),d                        ;[0e7f] 72
                    ld        l,a                           ;[0e80] 6f
                    ld        h,a                           ;[0e81] 67
                    ld        (hl),d                        ;[0e82] 72
                    ld        h,c                           ;[0e83] 61
                    ld        l,l                           ;[0e84] 6d
                    ld        a,($0da0)                     ;[0e85] 3a a0 0d
                    ld        c,(hl)                        ;[0e88] 4e
                    ld        (hl),l                        ;[0e89] 75
                    ld        l,l                           ;[0e8a] 6d
                    ld        h,d                           ;[0e8b] 62
                    ld        h,l                           ;[0e8c] 65
                    ld        (hl),d                        ;[0e8d] 72
                    jr        nz,$0ef1                      ;[0e8e] 20 61
                    ld        (hl),d                        ;[0e90] 72
                    ld        (hl),d                        ;[0e91] 72
                    ld        h,c                           ;[0e92] 61
                    ld        a,c                           ;[0e93] 79
                    ld        a,($0da0)                     ;[0e94] 3a a0 0d
                    ld        b,e                           ;[0e97] 43
                    ld        l,b                           ;[0e98] 68
                    ld        h,c                           ;[0e99] 61
                    ld        (hl),d                        ;[0e9a] 72
                    ld        h,c                           ;[0e9b] 61
                    ld        h,e                           ;[0e9c] 63
                    ld        (hl),h                        ;[0e9d] 74
                    ld        h,l                           ;[0e9e] 65
                    ld        (hl),d                        ;[0e9f] 72
                    jr        nz,$0f03                      ;[0ea0] 20 61
                    ld        (hl),d                        ;[0ea2] 72
                    ld        (hl),d                        ;[0ea3] 72
                    ld        h,c                           ;[0ea4] 61
                    ld        a,c                           ;[0ea5] 79
                    ld        a,($0da0)                     ;[0ea6] 3a a0 0d
                    ld        b,d                           ;[0ea9] 42
                    ld        a,c                           ;[0eaa] 79
                    ld        (hl),h                        ;[0eab] 74
                    ld        h,l                           ;[0eac] 65
                    ld        (hl),e                        ;[0ead] 73
                    ld        a,($fea0)                     ;[0eae] 3a a0 fe
                    dec       c                             ;[0eb1] 0d
                    ret       z                             ;[0eb2] c8
                    cp        $3a                           ;[0eb3] fe 3a
                    ret                                     ;[0eb5] c9

                    push      af                            ;[0eb6] f5
                    ld        a,($5b5e)                     ;[0eb7] 3a 5e 5b
                    cp        $54                           ;[0eba] fe 54
                    jr        z,$0ee6                       ;[0ebc] 28 28
                    ld        b,$00                         ;[0ebe] 06 00
                    call      $2cfb                         ;[0ec0] cd fb 2c
                    call      $342d                         ;[0ec3] cd 2d 34
                    call      $3f00                         ;[0ec6] cd 00 3f
                    add       hl,bc                         ;[0ec9] 09
                    ld        bc,$65cd                      ;[0eca] 01 cd 65
                    inc       (hl)                          ;[0ecd] 34
                    call      $2cd6                         ;[0ece] cd d6 2c
                    jr        c,$0ee6                       ;[0ed1] 38 13
                    ld        b,$00                         ;[0ed3] 06 00
                    call      $2cfb                         ;[0ed5] cd fb 2c
                    call      $342d                         ;[0ed8] cd 2d 34
                    call      $3f00                         ;[0edb] cd 00 3f
                    inc       c                             ;[0ede] 0c
                    ld        bc,$65cd                      ;[0edf] 01 cd 65
                    inc       (hl)                          ;[0ee2] 34
                    call      $2cd6                         ;[0ee3] cd d6 2c
                    pop       af                            ;[0ee6] f1
                    pop       hl                            ;[0ee7] e1
                    ld        e,(hl)                        ;[0ee8] 5e
                    bit       7,e                           ;[0ee9] cb 7b
                    jr        z,$0ef8                       ;[0eeb] 28 0b
                    cp        $0a                           ;[0eed] fe 0a
                    jr        nc,$0ef5                      ;[0eef] 30 04
                    add       $3d                           ;[0ef1] c6 3d
                    jr        $0ef7                         ;[0ef3] 18 02
                    add       $18                           ;[0ef5] c6 18
                    ld        e,a                           ;[0ef7] 5f
                    ld        h,e                           ;[0ef8] 63
                    ld        l,$2c                         ;[0ef9] 2e 2c
                    push      hl                            ;[0efb] e5
                    ld        l,$cd                         ;[0efc] 2e cd
                    ld        h,$4c                         ;[0efe] 26 4c
                    push      hl                            ;[0f00] e5
                    xor       a                             ;[0f01] af
                    ld        hl,$0000                      ;[0f02] 21 00 00
                    add       hl,sp                         ;[0f05] 39
                    jp        (hl)                          ;[0f06] e9
                    or        c                             ;[0f07] b1
                    jp        z,$bebc                       ;[0f08] ca bc be
                    jp        $b4af                         ;[0f0b] c3 af b4
                    sub       e                             ;[0f0e] 93
                    sub       c                             ;[0f0f] 91
                    sub       d                             ;[0f10] 92
                    sub       l                             ;[0f11] 95
                    sbc       b                             ;[0f12] 98
                    sbc       b                             ;[0f13] 98
                    sbc       b                             ;[0f14] 98
                    sbc       b                             ;[0f15] 98
                    sbc       b                             ;[0f16] 98
                    sbc       b                             ;[0f17] 98
                    sbc       b                             ;[0f18] 98
                    ld        a,a                           ;[0f19] 7f
                    add       c                             ;[0f1a] 81
                    ld        l,$6c                         ;[0f1b] 2e 6c
                    ld        l,(hl)                        ;[0f1d] 6e
                    ld        (hl),b                        ;[0f1e] 70
                    ld        c,b                           ;[0f1f] 48
                    sub       h                             ;[0f20] 94
                    ld        d,(hl)                        ;[0f21] 56
                    ccf                                     ;[0f22] 3f
                    ld        b,c                           ;[0f23] 41
                    dec       hl                            ;[0f24] 2b
                    rla                                     ;[0f25] 17
                    rra                                     ;[0f26] 1f
                    scf                                     ;[0f27] 37
                    ld        (hl),a                        ;[0f28] 77
                    ld        b,h                           ;[0f29] 44
                    rrca                                    ;[0f2a] 0f
                    ld        e,c                           ;[0f2b] 59
                    dec       hl                            ;[0f2c] 2b
                    ld        b,e                           ;[0f2d] 43
                    dec       l                             ;[0f2e] 2d
                    ld        d,c                           ;[0f2f] 51
                    ld        a,($426d)                     ;[0f30] 3a 6d 42
                    dec       c                             ;[0f33] 0d
                    ld        c,c                           ;[0f34] 49
                    ld        e,h                           ;[0f35] 5c
                    ld        b,h                           ;[0f36] 44
                    dec       d                             ;[0f37] 15
                    ld        e,l                           ;[0f38] 5d
                    ld        bc,$023d                      ;[0f39] 01 3d 02
                    ld        b,$00                         ;[0f3c] 06 00
                    ld        h,a                           ;[0f3e] 67
                    ld        e,$06                         ;[0f3f] 1e 06
                    rrc       (hl)                          ;[0f41] cb 0e
                    ld        a,d                           ;[0f43] 7a
                    ld        de,$0c06                      ;[0f44] 11 06 0c
                    ld        h,(hl)                        ;[0f47] 66
                    ld        (de),a                        ;[0f48] 12
                    nop                                     ;[0f49] 00
                    xor       $1c                           ;[0f4a] ee 1c
                    inc       c                             ;[0f4c] 0c
                    add       d                             ;[0f4d] 82
                    ld        (de),a                        ;[0f4e] 12
                    inc       b                             ;[0f4f] 04
                    dec       a                             ;[0f50] 3d
                    ld        b,$cc                         ;[0f51] 06 cc
                    ld        b,$0e                         ;[0f53] 06 0e
                    sub       h                             ;[0f55] 94
                    ld        de,$0004                      ;[0f56] 11 04 00
                    xor       e                             ;[0f59] ab
                    dec       e                             ;[0f5a] 1d
                    ld        c,$e8                         ;[0f5b] 0e e8
                    ld        ($fc0e),hl                    ;[0f5d] 22 0e fc
                    ld        ($1a0e),hl                    ;[0f60] 22 0e 1a
                    inc       h                             ;[0f63] 24
                    ld        c,$8e                         ;[0f64] 0e 8e
                    djnz      $0f74                         ;[0f66] 10 0c
                    swapnib                                 ;[0f68] ed 23
                    dec       c                             ;[0f6a] 0d
                    dec       d                             ;[0f6b] 15
                    ld        (de),a                        ;[0f6c] 12
                    ld        c,$55                         ;[0f6d] 0e 55
                    dec       d                             ;[0f6f] 15
                    ex        af,af'                        ;[0f70] 08
                    nop                                     ;[0f71] 00
                    add       b                             ;[0f72] 80
                    ld        e,$03                         ;[0f73] 1e 03
                    ld        c,a                           ;[0f75] 4f
                    ld        e,$00                         ;[0f76] 1e 00
                    ld        e,a                           ;[0f78] 5f
                    ld        e,$0d                         ;[0f79] 1e 0d
                    jr        nz,$0f8f                      ;[0f7b] 20 12
                    nop                                     ;[0f7d] 00
                    ld        l,e                           ;[0f7e] 6b
                    dec       c                             ;[0f7f] 0d
                    add       hl,bc                         ;[0f80] 09
                    nop                                     ;[0f81] 00
                    call      c,$0622                       ;[0f82] dc 22 06
                    nop                                     ;[0f85] 00
                    ld        a,($0e1f)                     ;[0f86] 3a 1f 0e
                    cp        (hl)                          ;[0f89] be
                    ld        de,$fe0e                      ;[0f8a] 11 0e fe
                    ld        de,$4203                      ;[0f8d] 11 03 42
                    ld        e,$09                         ;[0f90] 1e 09
                    ld        c,$03                         ;[0f92] 0e 03
                    inc       h                             ;[0f94] 24
                    ld        c,$17                         ;[0f95] 0e 17
                    inc       hl                            ;[0f97] 23
                    ld        c,$e4                         ;[0f98] 0e e4
                    ld        ($510e),hl                    ;[0f9a] 22 0e 51
                    dec       d                             ;[0f9d] 15
                    dec       bc                            ;[0f9e] 0b
                    dec       bc                            ;[0f9f] 0b
                    dec       bc                            ;[0fa0] 0b
                    dec       bc                            ;[0fa1] 0b
                    ex        af,af'                        ;[0fa2] 08
                    nop                                     ;[0fa3] 00
                    ret       m                             ;[0fa4] f8
                    inc       bc                            ;[0fa5] 03
                    add       hl,bc                         ;[0fa6] 09
                    ld        c,$f3                         ;[0fa7] 0e f3
                    inc       hl                            ;[0fa9] 23
                    rlca                                    ;[0faa] 07
                    rlca                                    ;[0fab] 07
                    rlca                                    ;[0fac] 07
                    rlca                                    ;[0fad] 07
                    rlca                                    ;[0fae] 07
                    rlca                                    ;[0faf] 07
                    ex        af,af'                        ;[0fb0] 08
                    nop                                     ;[0fb1] 00
                    ld        a,d                           ;[0fb2] 7a
                    ld        e,$06                         ;[0fb3] 1e 06
                    nop                                     ;[0fb5] 00
                    sub       h                             ;[0fb6] 94
                    ld        ($9f0e),hl                    ;[0fb7] 22 0e 9f
                    ld        (de),a                        ;[0fba] 12
                    ld        b,$2c                         ;[0fbb] 06 2c
                    ld        a,(bc)                        ;[0fbd] 0a
                    nop                                     ;[0fbe] 00
                    ld        (hl),$17                      ;[0fbf] 36 17
                    ld        b,$00                         ;[0fc1] 06 00
                    push      hl                            ;[0fc3] e5
                    ld        d,$0e                         ;[0fc4] 16 0e
                    ld        l,h                           ;[0fc6] 6c
                    ld        (bc),a                        ;[0fc7] 02
                    ld        a,(bc)                        ;[0fc8] 0a
                    call      z,$0c0a                       ;[0fc9] cc 0a 0c
                    jp        m,$0a04                       ;[0fcc] fa 04 0a
                    inc       c                             ;[0fcf] 0c
                    ld        e,a                           ;[0fd0] 5f
                    inc       b                             ;[0fd1] 04
                    ld        c,$cd                         ;[0fd2] 0e cd
                    dec       b                             ;[0fd4] 05
                    inc       c                             ;[0fd5] 0c
                    add       c                             ;[0fd6] 81
                    inc       d                             ;[0fd7] 14
                    ld        c,$5e                         ;[0fd8] 0e 5e
                    dec       h                             ;[0fda] 25
                    res       7,(iy+$01)                    ;[0fdb] fd cb 01 be
                    rst       $28                           ;[0fdf] ef
                    ei                                      ;[0fe0] fb
                    add       hl,de                         ;[0fe1] 19
                    xor       a                             ;[0fe2] af
                    ld        ($5c47),a                     ;[0fe3] 32 47 5c
                    dec       a                             ;[0fe6] 3d
                    ld        ($5c3a),a                     ;[0fe7] 32 3a 5c
                    jr        $0fed                         ;[0fea] 18 01
                    rst       $20                           ;[0fec] e7
                    rst       $28                           ;[0fed] ef
                    cp        a                             ;[0fee] bf
                    ld        d,$fd                         ;[0fef] 16 fd
                    inc       (hl)                          ;[0ff1] 34
                    dec       c                             ;[0ff2] 0d
                    jp        m,$1141                       ;[0ff3] fa 41 11
                    rst       $18                           ;[0ff6] df
                    ld        b,$00                         ;[0ff7] 06 00
                    cp        $0d                           ;[0ff9] fe 0d
                    jp        z,$108f                       ;[0ffb] ca 8f 10
                    cp        $3a                           ;[0ffe] fe 3a
                    jr        z,$0fec                       ;[1000] 28 ea
                    ld        hl,$104d                      ;[1002] 21 4d 10
                    push      hl                            ;[1005] e5
                    ld        c,a                           ;[1006] 4f
                    rst       $20                           ;[1007] e7
                    ld        a,c                           ;[1008] 79
                    sub       $ce                           ;[1009] d6 ce
                    jr        nc,$1020                      ;[100b] 30 13
                    add       $ce                           ;[100d] c6 ce
                    ld        hl,$0fd5                      ;[100f] 21 d5 0f
                    cp        $a3                           ;[1012] fe a3
                    jr        z,$102c                       ;[1014] 28 16
                    ld        hl,$0fd8                      ;[1016] 21 d8 0f
                    cp        $a4                           ;[1019] fe a4
                    jr        z,$102c                       ;[101b] 28 0f
                    jp        $1141                         ;[101d] c3 41 11
                    ld        c,a                           ;[1020] 4f
                    ld        hl,$0f07                      ;[1021] 21 07 0f
                    add       hl,bc                         ;[1024] 09
                    ld        c,(hl)                        ;[1025] 4e
                    add       hl,bc                         ;[1026] 09
                    jr        $102c                         ;[1027] 18 03
                    ld        hl,($5c74)                    ;[1029] 2a 74 5c
                    ld        a,(hl)                        ;[102c] 7e
                    inc       hl                            ;[102d] 23
                    ld        ($5c74),hl                    ;[102e] 22 74 5c
                    ld        bc,$1029                      ;[1031] 01 29 10
                    push      bc                            ;[1034] c5
                    ld        c,a                           ;[1035] 4f
                    cp        $20                           ;[1036] fe 20
                    jr        nc,$1046                      ;[1038] 30 0c
                    ld        hl,$10e1                      ;[103a] 21 e1 10
                    ld        b,$00                         ;[103d] 06 00
                    add       hl,bc                         ;[103f] 09
                    ld        c,(hl)                        ;[1040] 4e
                    add       hl,bc                         ;[1041] 09
                    push      hl                            ;[1042] e5
                    rst       $18                           ;[1043] df
                    dec       b                             ;[1044] 05
                    ret                                     ;[1045] c9

                    rst       $18                           ;[1046] df
                    cp        c                             ;[1047] b9
                    jp        nz,$1141                      ;[1048] c2 41 11
                    rst       $20                           ;[104b] e7
                    ret                                     ;[104c] c9

                    call      $2c6b                         ;[104d] cd 6b 2c
                    jr        c,$1056                       ;[1050] 38 04
                    call      $2c4c                         ;[1052] cd 4c 2c
                    inc       d                             ;[1055] 14
                    bit       7,(iy+$0a)                    ;[1056] fd cb 0a 7e
                    jp        nz,$10d4                      ;[105a] c2 d4 10
                    ld        hl,($5c42)                    ;[105d] 2a 42 5c
                    bit       7,h                           ;[1060] cb 7c
                    jr        z,$1078                       ;[1062] 28 14
                    ld        hl,$fffe                      ;[1064] 21 fe ff
                    ld        ($5c45),hl                    ;[1067] 22 45 5c
                    ld        hl,($5c61)                    ;[106a] 2a 61 5c
                    dec       hl                            ;[106d] 2b
                    ld        de,($5c59)                    ;[106e] ed 5b 59 5c
                    dec       de                            ;[1072] 1b
                    ld        a,($5c44)                     ;[1073] 3a 44 5c
                    jr        $10ae                         ;[1076] 18 36
                    rst       $28                           ;[1078] ef
                    ld        l,(hl)                        ;[1079] 6e
                    add       hl,de                         ;[107a] 19
                    ld        a,($5c44)                     ;[107b] 3a 44 5c
                    jr        z,$109c                       ;[107e] 28 1c
                    and       a                             ;[1080] a7
                    jr        nz,$10c9                      ;[1081] 20 46
                    ld        b,a                           ;[1083] 47
                    ld        a,(hl)                        ;[1084] 7e
                    and       $c0                           ;[1085] e6 c0
                    ld        a,b                           ;[1087] 78
                    jr        z,$109c                       ;[1088] 28 12
                    call      $2c4c                         ;[108a] cd 4c 2c
                    rst       $38                           ;[108d] ff
                    pop       bc                            ;[108e] c1
                    bit       7,(iy+$01)                    ;[108f] fd cb 01 7e
                    ret       z                             ;[1093] c8
                    ld        hl,($5c55)                    ;[1094] 2a 55 5c
                    ld        a,$c0                         ;[1097] 3e c0
                    and       (hl)                          ;[1099] a6
                    ret       nz                            ;[109a] c0
                    xor       a                             ;[109b] af
                    cp        $01                           ;[109c] fe 01
                    adc       $00                           ;[109e] ce 00
                    ld        d,(hl)                        ;[10a0] 56
                    inc       hl                            ;[10a1] 23
                    ld        e,(hl)                        ;[10a2] 5e
                    ld        ($5c45),de                    ;[10a3] ed 53 45 5c
                    inc       hl                            ;[10a7] 23
                    ld        e,(hl)                        ;[10a8] 5e
                    inc       hl                            ;[10a9] 23
                    ld        d,(hl)                        ;[10aa] 56
                    ex        de,hl                         ;[10ab] eb
                    add       hl,de                         ;[10ac] 19
                    inc       hl                            ;[10ad] 23
                    ld        ($5c55),hl                    ;[10ae] 22 55 5c
                    ex        de,hl                         ;[10b1] eb
                    ld        ($5c5d),hl                    ;[10b2] 22 5d 5c
                    ld        d,a                           ;[10b5] 57
                    ld        e,$00                         ;[10b6] 1e 00
                    ld        (iy+$0a),$ff                  ;[10b8] fd 36 0a ff
                    dec       d                             ;[10bc] 15
                    ld        (iy+$0d),d                    ;[10bd] fd 72 0d
                    jp        z,$0fec                       ;[10c0] ca ec 0f
                    inc       d                             ;[10c3] 14
                    rst       $28                           ;[10c4] ef
                    adc       e                             ;[10c5] 8b
                    add       hl,de                         ;[10c6] 19
                    jr        z,$10d4                       ;[10c7] 28 0b
                    call      $2c4c                         ;[10c9] cd 4c 2c
                    ld        d,$fd                         ;[10cc] 16 fd
                    rlc       c                             ;[10ce] cb 01
                    ld        a,(hl)                        ;[10d0] 7e
                    ret       nz                            ;[10d1] c0
                    pop       bc                            ;[10d2] c1
                    pop       bc                            ;[10d3] c1
                    rst       $18                           ;[10d4] df
                    cp        $0d                           ;[10d5] fe 0d
                    jr        z,$108f                       ;[10d7] 28 b6
                    cp        $3a                           ;[10d9] fe 3a
                    jp        z,$0fec                       ;[10db] ca ec 0f
                    jp        $1141                         ;[10de] c3 41 11
                    inc       h                             ;[10e1] 24
                    ld        b,(hl)                        ;[10e2] 46
                    ld        c,c                           ;[10e3] 49
                    ld        e,$4f                         ;[10e4] 1e 4f
                    jr        nz,$113e                      ;[10e6] 20 56
                    ld        h,c                           ;[10e8] 61
                    ld        d,b                           ;[10e9] 50
                    adc       c                             ;[10ea] 89
                    ld        e,d                           ;[10eb] 5a
                    adc       e                             ;[10ec] 8b
                    ld        b,$02                         ;[10ed] 06 02
                    dec       b                             ;[10ef] 05
                    rst       $28                           ;[10f0] ef
                    sbc       $1c                           ;[10f1] de 1c
                    cp        a                             ;[10f3] bf
                    pop       bc                            ;[10f4] c1
                    call      z,$10cd                       ;[10f5] cc cd 10
                    ex        de,hl                         ;[10f8] eb
                    ld        hl,($5c74)                    ;[10f9] 2a 74 5c
                    ld        c,(hl)                        ;[10fc] 4e
                    inc       hl                            ;[10fd] 23
                    ld        b,(hl)                        ;[10fe] 46
                    ex        de,hl                         ;[10ff] eb
                    push      bc                            ;[1100] c5
                    ret                                     ;[1101] c9

                    rst       $28                           ;[1102] ef
                    sbc       $1c                           ;[1103] de 1c
                    cp        a                             ;[1105] bf
                    pop       bc                            ;[1106] c1
                    call      z,$10cd                       ;[1107] cc cd 10
                    ex        de,hl                         ;[110a] eb
                    ld        hl,($5c74)                    ;[110b] 2a 74 5c
                    ld        c,(hl)                        ;[110e] 4e
                    inc       hl                            ;[110f] 23
                    ld        b,(hl)                        ;[1110] 46
                    ex        de,hl                         ;[1111] eb
                    push      hl                            ;[1112] e5
                    ld        hl,$1127                      ;[1113] 21 27 11
                    ld        ($5b5a),hl                    ;[1116] 22 5a 5b
                    ld        hl,$5b2a                      ;[1119] 21 2a 5b
                    ex        (sp),hl                       ;[111c] e3
                    push      hl                            ;[111d] e5
                    ld        h,b                           ;[111e] 60
                    ld        l,c                           ;[111f] 69
                    ex        (sp),hl                       ;[1120] e3
                    push      af                            ;[1121] f5
                    push      bc                            ;[1122] c5
                    di                                      ;[1123] f3
                    jp        $5b10                         ;[1124] c3 10 5b
                    ret                                     ;[1127] c9

                    rst       $28                           ;[1128] ef
                    rra                                     ;[1129] 1f
                    inc       e                             ;[112a] 1c
                    ret                                     ;[112b] c9

                    pop       bc                            ;[112c] c1
                    rst       $28                           ;[112d] ef
                    ld        d,(hl)                        ;[112e] 56
                    inc       e                             ;[112f] 1c
                    call      $10cd                         ;[1130] cd cd 10
                    ret                                     ;[1133] c9

                    rst       $28                           ;[1134] ef
                    ld        l,h                           ;[1135] 6c
                    inc       e                             ;[1136] 1c
                    ret                                     ;[1137] c9

                    rst       $20                           ;[1138] e7
                    rst       $28                           ;[1139] ef
                    ld        a,d                           ;[113a] 7a
                    inc       e                             ;[113b] 1c
                    ret                                     ;[113c] c9

                    rst       $28                           ;[113d] ef
                    add       d                             ;[113e] 82
                    inc       e                             ;[113f] 1c
                    ret                                     ;[1140] c9

                    call      $2c4c                         ;[1141] cd 4c 2c
                    dec       bc                            ;[1144] 0b
                    rst       $28                           ;[1145] ef
                    adc       h                             ;[1146] 8c
                    inc       e                             ;[1147] 1c
                    ret                                     ;[1148] c9

                    bit       7,(iy+$01)                    ;[1149] fd cb 01 7e
                    res       0,(iy+$02)                    ;[114d] fd cb 02 86
                    jr        z,$1156                       ;[1151] 28 03
                    rst       $28                           ;[1153] ef
                    ld        c,l                           ;[1154] 4d
                    dec       c                             ;[1155] 0d
                    pop       af                            ;[1156] f1
                    ld        a,($5c74)                     ;[1157] 3a 74 5c
                    sub       $d2                           ;[115a] d6 d2
                    rst       $28                           ;[115c] ef
                    call      m,$cd21                       ;[115d] fc 21 cd
                    call      $2a10                         ;[1160] cd 10 2a
                    adc       a                             ;[1163] 8f
                    ld        e,h                           ;[1164] 5c
                    ld        ($5c8d),hl                    ;[1165] 22 8d 5c
                    ld        hl,$5c91                      ;[1168] 21 91 5c
                    ld        a,(hl)                        ;[116b] 7e
                    rlca                                    ;[116c] 07
                    xor       (hl)                          ;[116d] ae
                    and       $aa                           ;[116e] e6 aa
                    xor       (hl)                          ;[1170] ae
                    ld        (hl),a                        ;[1171] 77
                    ret                                     ;[1172] c9

                    rst       $28                           ;[1173] ef
                    cp        (hl)                          ;[1174] be
                    inc       e                             ;[1175] 1c
                    ret                                     ;[1176] c9

                    jp        $089d                         ;[1177] c3 9d 08
                    pop       bc                            ;[117a] c1
                    bit       7,(iy+$01)                    ;[117b] fd cb 01 7e
                    jr        z,$1191                       ;[117f] 28 10
                    ld        hl,($5c65)                    ;[1181] 2a 65 5c
                    ld        de,$fffb                      ;[1184] 11 fb ff
                    add       hl,de                         ;[1187] 19
                    ld        ($5c65),hl                    ;[1188] 22 65 5c
                    rst       $28                           ;[118b] ef
                    jp        (hl)                          ;[118c] e9
                    inc       (hl)                          ;[118d] 34
                    jp        c,$108f                       ;[118e] da 8f 10
                    jp        $0fed                         ;[1191] c3 ed 0f
                    cp        $cd                           ;[1194] fe cd
                    jr        nz,$11a1                      ;[1196] 20 09
                    rst       $20                           ;[1198] e7
                    call      $113d                         ;[1199] cd 3d 11
                    call      $10cd                         ;[119c] cd cd 10
                    jr        $11b9                         ;[119f] 18 18
                    call      $10cd                         ;[11a1] cd cd 10
                    ld        hl,($5c65)                    ;[11a4] 2a 65 5c
                    ld        (hl),$00                      ;[11a7] 36 00
                    inc       hl                            ;[11a9] 23
                    ld        (hl),$00                      ;[11aa] 36 00
                    inc       hl                            ;[11ac] 23
                    ld        (hl),$01                      ;[11ad] 36 01
                    inc       hl                            ;[11af] 23
                    ld        (hl),$00                      ;[11b0] 36 00
                    inc       hl                            ;[11b2] 23
                    ld        (hl),$00                      ;[11b3] 36 00
                    inc       hl                            ;[11b5] 23
                    ld        ($5c65),hl                    ;[11b6] 22 65 5c
                    rst       $28                           ;[11b9] ef
                    ld        d,$1d                         ;[11ba] 16 1d
                    ret                                     ;[11bc] c9

                    rst       $20                           ;[11bd] e7
                    call      $1128                         ;[11be] cd 28 11
                    bit       7,(iy+$01)                    ;[11c1] fd cb 01 7e
                    jr        z,$11f5                       ;[11c5] 28 2e
                    rst       $18                           ;[11c7] df
                    ld        ($5c5f),hl                    ;[11c8] 22 5f 5c
                    ld        hl,($5c57)                    ;[11cb] 2a 57 5c
                    ld        a,(hl)                        ;[11ce] 7e
                    cp        $2c                           ;[11cf] fe 2c
                    jr        z,$11de                       ;[11d1] 28 0b
                    ld        e,$e4                         ;[11d3] 1e e4
                    rst       $28                           ;[11d5] ef
                    add       (hl)                          ;[11d6] 86
                    dec       e                             ;[11d7] 1d
                    jr        nc,$11de                      ;[11d8] 30 04
                    call      $2c4c                         ;[11da] cd 4c 2c
                    dec       c                             ;[11dd] 0d
                    inc       hl                            ;[11de] 23
                    ld        ($5c5d),hl                    ;[11df] 22 5d 5c
                    ld        a,(hl)                        ;[11e2] 7e
                    rst       $28                           ;[11e3] ef
                    ld        d,(hl)                        ;[11e4] 56
                    inc       e                             ;[11e5] 1c
                    rst       $18                           ;[11e6] df
                    ld        ($5c57),hl                    ;[11e7] 22 57 5c
                    ld        hl,($5c5f)                    ;[11ea] 2a 5f 5c
                    ld        (iy+$26),$00                  ;[11ed] fd 36 26 00
                    ld        ($5c5d),hl                    ;[11f1] 22 5d 5c
                    ld        a,(hl)                        ;[11f4] 7e
                    rst       $18                           ;[11f5] df
                    cp        $2c                           ;[11f6] fe 2c
                    jr        z,$11bd                       ;[11f8] 28 c3
                    call      $10cd                         ;[11fa] cd cd 10
                    ret                                     ;[11fd] c9

                    bit       7,(iy+$01)                    ;[11fe] fd cb 01 7e
                    jr        nz,$120f                      ;[1202] 20 0b
                    rst       $28                           ;[1204] ef
                    ei                                      ;[1205] fb
                    inc       h                             ;[1206] 24
                    cp        $2c                           ;[1207] fe 2c
                    call      nz,$10cd                      ;[1209] c4 cd 10
                    rst       $20                           ;[120c] e7
                    jr        $1204                         ;[120d] 18 f5
                    ld        a,$e4                         ;[120f] 3e e4
                    rst       $28                           ;[1211] ef
                    add       hl,sp                         ;[1212] 39
                    ld        e,$c9                         ;[1213] 1e c9
                    rst       $28                           ;[1215] ef
                    ld        h,a                           ;[1216] 67
                    ld        e,$01                         ;[1217] 1e 01
                    nop                                     ;[1219] 00
                    nop                                     ;[121a] 00
                    rst       $28                           ;[121b] ef
                    ld        b,l                           ;[121c] 45
                    ld        e,$18                         ;[121d] 1e 18
                    inc       bc                            ;[121f] 03
                    rst       $28                           ;[1220] ef
                    sbc       c                             ;[1221] 99
                    ld        e,$78                         ;[1222] 1e 78
                    or        c                             ;[1224] b1
                    jr        nz,$122b                      ;[1225] 20 04
                    ld        bc,($5cb2)                    ;[1227] ed 4b b2 5c
                    push      bc                            ;[122b] c5
                    ld        de,($5c4b)                    ;[122c] ed 5b 4b 5c
                    ld        hl,($5c59)                    ;[1230] 2a 59 5c
                    dec       hl                            ;[1233] 2b
                    rst       $28                           ;[1234] ef
                    push      hl                            ;[1235] e5
                    add       hl,de                         ;[1236] 19
                    rst       $28                           ;[1237] ef
                    ld        l,e                           ;[1238] 6b
                    dec       c                             ;[1239] 0d
                    ld        hl,($5c65)                    ;[123a] 2a 65 5c
                    ld        de,$0032                      ;[123d] 11 32 00
                    add       hl,de                         ;[1240] 19
                    pop       de                            ;[1241] d1
                    sbc       hl,de                         ;[1242] ed 52
                    jr        nc,$124e                      ;[1244] 30 08
                    ld        hl,($5cb4)                    ;[1246] 2a b4 5c
                    and       a                             ;[1249] a7
                    sbc       hl,de                         ;[124a] ed 52
                    jr        nc,$1252                      ;[124c] 30 04
                    call      $2c4c                         ;[124e] cd 4c 2c
                    dec       d                             ;[1251] 15
                    ld        ($5cb2),de                    ;[1252] ed 53 b2 5c
                    pop       de                            ;[1256] d1
                    pop       hl                            ;[1257] e1
                    pop       bc                            ;[1258] c1
                    ld        sp,($5cb2)                    ;[1259] ed 7b b2 5c
                    inc       sp                            ;[125d] 33
                    push      bc                            ;[125e] c5
                    push      hl                            ;[125f] e5
                    ld        ($5c3d),sp                    ;[1260] ed 73 3d 5c
                    push      de                            ;[1264] d5
                    ret                                     ;[1265] c9

                    pop       de                            ;[1266] d1
                    ld        h,(iy+$0d)                    ;[1267] fd 66 0d
                    inc       h                             ;[126a] 24
                    ex        (sp),hl                       ;[126b] e3
                    inc       sp                            ;[126c] 33
                    ld        bc,($5c45)                    ;[126d] ed 4b 45 5c
                    push      bc                            ;[1271] c5
                    push      hl                            ;[1272] e5
                    ld        ($5c3d),sp                    ;[1273] ed 73 3d 5c
                    push      de                            ;[1277] d5
                    rst       $28                           ;[1278] ef
                    ld        h,a                           ;[1279] 67
                    ld        e,$01                         ;[127a] 1e 01
                    inc       d                             ;[127c] 14
                    nop                                     ;[127d] 00
                    rst       $28                           ;[127e] ef
                    dec       b                             ;[127f] 05
                    rra                                     ;[1280] 1f
                    ret                                     ;[1281] c9

                    pop       bc                            ;[1282] c1
                    pop       hl                            ;[1283] e1
                    pop       de                            ;[1284] d1
                    ld        a,d                           ;[1285] 7a
                    cp        $3e                           ;[1286] fe 3e
                    jr        z,$1299                       ;[1288] 28 0f
                    dec       sp                            ;[128a] 3b
                    ex        (sp),hl                       ;[128b] e3
                    ex        de,hl                         ;[128c] eb
                    ld        ($5c3d),sp                    ;[128d] ed 73 3d 5c
                    push      bc                            ;[1291] c5
                    ld        ($5c42),hl                    ;[1292] 22 42 5c
                    ld        (iy+$0a),d                    ;[1295] fd 72 0a
                    ret                                     ;[1298] c9

                    push      de                            ;[1299] d5
                    push      hl                            ;[129a] e5
                    call      $2c4c                         ;[129b] cd 4c 2c
                    ld        b,$fd                         ;[129e] 06 fd
                    rlc       c                             ;[12a0] cb 01
                    ld        a,(hl)                        ;[12a2] 7e
                    jr        z,$12aa                       ;[12a3] 28 05
                    ld        a,$ce                         ;[12a5] 3e ce
                    jp        $1211                         ;[12a7] c3 11 12
                    set       6,(iy+$01)                    ;[12aa] fd cb 01 f6
                    rst       $28                           ;[12ae] ef
                    adc       l                             ;[12af] 8d
                    inc       l                             ;[12b0] 2c
                    jr        nc,$12c9                      ;[12b1] 30 16
                    rst       $20                           ;[12b3] e7
                    cp        $24                           ;[12b4] fe 24
                    jr        nz,$12bd                      ;[12b6] 20 05
                    res       6,(iy+$01)                    ;[12b8] fd cb 01 b6
                    rst       $20                           ;[12bc] e7
                    cp        $28                           ;[12bd] fe 28
                    jr        nz,$12fd                      ;[12bf] 20 3c
                    rst       $20                           ;[12c1] e7
                    cp        $29                           ;[12c2] fe 29
                    jr        z,$12e6                       ;[12c4] 28 20
                    rst       $28                           ;[12c6] ef
                    adc       l                             ;[12c7] 8d
                    inc       l                             ;[12c8] 2c
                    jp        nc,$1141                      ;[12c9] d2 41 11
                    ex        de,hl                         ;[12cc] eb
                    rst       $20                           ;[12cd] e7
                    cp        $24                           ;[12ce] fe 24
                    jr        nz,$12d4                      ;[12d0] 20 02
                    ex        de,hl                         ;[12d2] eb
                    rst       $20                           ;[12d3] e7
                    ex        de,hl                         ;[12d4] eb
                    ld        bc,$0006                      ;[12d5] 01 06 00
                    rst       $28                           ;[12d8] ef
                    ld        d,l                           ;[12d9] 55
                    ld        d,$23                         ;[12da] 16 23
                    inc       hl                            ;[12dc] 23
                    ld        (hl),$0e                      ;[12dd] 36 0e
                    cp        $2c                           ;[12df] fe 2c
                    jr        nz,$12e6                      ;[12e1] 20 03
                    rst       $20                           ;[12e3] e7
                    jr        $12c6                         ;[12e4] 18 e0
                    cp        $29                           ;[12e6] fe 29
                    jr        nz,$12fd                      ;[12e8] 20 13
                    rst       $20                           ;[12ea] e7
                    cp        $3d                           ;[12eb] fe 3d
                    jr        nz,$12fd                      ;[12ed] 20 0e
                    rst       $20                           ;[12ef] e7
                    ld        a,($5c3b)                     ;[12f0] 3a 3b 5c
                    push      af                            ;[12f3] f5
                    rst       $28                           ;[12f4] ef
                    ei                                      ;[12f5] fb
                    inc       h                             ;[12f6] 24
                    pop       af                            ;[12f7] f1
                    xor       (iy+$01)                      ;[12f8] fd ae 01
                    and       $40                           ;[12fb] e6 40
                    jp        nz,$1141                      ;[12fd] c2 41 11
                    call      $10cd                         ;[1300] cd cd 10
                    ret                                     ;[1303] c9

                    call      $2cfb                         ;[1304] cd fb 2c
                    ld        hl,$ec0e                      ;[1307] 21 0e ec
                    ld        (hl),$ff                      ;[130a] 36 ff
                    ld        hl,$5b66                      ;[130c] 21 66 5b
                    bit       4,(hl)                        ;[130f] cb 66
                    jp        z,$13e2                       ;[1311] ca e2 13
                    xor       a                             ;[1314] af
                    call      $342d                         ;[1315] cd 2d 34
                    call      $3f00                         ;[1318] cd 00 3f
                    ld        c,(hl)                        ;[131b] 4e
                    ld        bc,$65cd                      ;[131c] 01 cd 65
                    inc       (hl)                          ;[131f] 34
                    push      hl                            ;[1320] e5
                    call      $342d                         ;[1321] cd 2d 34
                    call      $3f00                         ;[1324] cd 00 3f
                    ld        hl,($cd01)                    ;[1327] 2a 01 cd
                    ld        h,l                           ;[132a] 65
                    inc       (hl)                          ;[132b] 34
                    call      $2cd6                         ;[132c] cd d6 2c
                    rst       $28                           ;[132f] ef
                    or        b                             ;[1330] b0
                    ld        d,$2a                         ;[1331] 16 2a
                    ld        e,c                           ;[1333] 59
                    ld        e,h                           ;[1334] 5c
                    ld        bc,$0007                      ;[1335] 01 07 00
                    rst       $28                           ;[1338] ef
                    ld        d,l                           ;[1339] 55
                    ld        d,$21                         ;[133a] 16 21
                    ld        c,d                           ;[133c] 4a
                    dec       d                             ;[133d] 15
                    ld        de,($5c59)                    ;[133e] ed 5b 59 5c
                    ld        bc,$0007                      ;[1342] 01 07 00
                    ldir                                    ;[1345] ed b0
                    ld        hl,($5c59)                    ;[1347] 2a 59 5c
                    ld        ($5c5d),hl                    ;[134a] 22 5d 5c
                    call      $2434                         ;[134d] cd 34 24
                    bit       6,(iy+$02)                    ;[1350] fd cb 02 76
                    jr        nz,$1359                      ;[1354] 20 03
                    rst       $28                           ;[1356] ef
                    ld        l,(hl)                        ;[1357] 6e
                    dec       c                             ;[1358] 0d
                    res       6,(iy+$02)                    ;[1359] fd cb 02 b6
                    call      $2cfb                         ;[135d] cd fb 2c
                    ld        hl,$ec0d                      ;[1360] 21 0d ec
                    bit       6,(hl)                        ;[1363] cb 76
                    jr        nz,$1372                      ;[1365] 20 0b
                    inc       hl                            ;[1367] 23
                    ld        a,(hl)                        ;[1368] 7e
                    cp        $00                           ;[1369] fe 00
                    jr        nz,$1372                      ;[136b] 20 05
                    call      $3e80                         ;[136d] cd 80 3e
                    sub       e                             ;[1370] 93
                    ld        a,(de)                        ;[1371] 1a
                    call      $2cd6                         ;[1372] cd d6 2c
                    ld        hl,$5c3c                      ;[1375] 21 3c 5c
                    res       3,(hl)                        ;[1378] cb 9e
                    ld        a,$19                         ;[137a] 3e 19
                    sub       (iy+$4f)                      ;[137c] fd 96 4f
                    ld        ($5c8c),a                     ;[137f] 32 8c 5c
                    set       7,(iy+$01)                    ;[1382] fd cb 01 fe
                    ld        (iy+$0a),$01                  ;[1386] fd 36 0a 01
                    ld        hl,$3e00                      ;[138a] 21 00 3e
                    push      hl                            ;[138d] e5
                    ld        hl,$5b3a                      ;[138e] 21 3a 5b
                    push      hl                            ;[1391] e5
                    ld        ($5c3d),sp                    ;[1392] ed 73 3d 5c
                    ld        hl,$139f                      ;[1396] 21 9f 13
                    ld        ($5b6c),hl                    ;[1399] 22 6c 5b
                    jp        $1064                         ;[139c] c3 64 10
                    call      $2cfb                         ;[139f] cd fb 2c
                    pop       hl                            ;[13a2] e1
                    ld        a,$ff                         ;[13a3] 3e ff
                    call      $342d                         ;[13a5] cd 2d 34
                    call      $3f00                         ;[13a8] cd 00 3f
                    ld        c,(hl)                        ;[13ab] 4e
                    ld        bc,$65cd                      ;[13ac] 01 cd 65
                    inc       (hl)                          ;[13af] 34
                    call      $2cd6                         ;[13b0] cd d6 2c
                    ld        a,($5c3a)                     ;[13b3] 3a 3a 5c
                    bit       7,a                           ;[13b6] cb 7f
                    jp        nz,$2739                      ;[13b8] c2 39 27
                    ld        hl,($5c45)                    ;[13bb] 2a 45 5c
                    ld        de,$fffe                      ;[13be] 11 fe ff
                    xor       a                             ;[13c1] af
                    sbc       hl,de                         ;[13c2] ed 52
                    ld        a,h                           ;[13c4] 7c
                    or        l                             ;[13c5] b5
                    jp        nz,$2739                      ;[13c6] c2 39 27
                    ld        a,$54                         ;[13c9] 3e 54
                    ld        ($5b79),a                     ;[13cb] 32 79 5b
                    ld        hl,$5c3c                      ;[13ce] 21 3c 5c
                    set       0,(hl)                        ;[13d1] cb c6
                    ld        hl,$14fe                      ;[13d3] 21 fe 14
                    call      $1540                         ;[13d6] cd 40 15
                    ld        hl,$5c3c                      ;[13d9] 21 3c 5c
                    res       0,(hl)                        ;[13dc] cb 86
                    set       6,(hl)                        ;[13de] cb f6
                    jr        $13e5                         ;[13e0] 18 03
                    call      $2cd6                         ;[13e2] cd d6 2c
                    rst       $28                           ;[13e5] ef
                    or        b                             ;[13e6] b0
                    ld        d,$2a                         ;[13e7] 16 2a
                    ld        e,c                           ;[13e9] 59
                    ld        e,h                           ;[13ea] 5c
                    ld        bc,$0003                      ;[13eb] 01 03 00
                    rst       $28                           ;[13ee] ef
                    ld        d,l                           ;[13ef] 55
                    ld        d,$21                         ;[13f0] 16 21
                    ei                                      ;[13f2] fb
                    inc       d                             ;[13f3] 14
                    ld        de,($5c59)                    ;[13f4] ed 5b 59 5c
                    ld        bc,$0003                      ;[13f8] 01 03 00
                    ldir                                    ;[13fb] ed b0
                    ld        hl,($5c59)                    ;[13fd] 2a 59 5c
                    ld        ($5c5d),hl                    ;[1400] 22 5d 5c
                    call      $2434                         ;[1403] cd 34 24
                    bit       6,(iy+$02)                    ;[1406] fd cb 02 76
                    jr        nz,$140f                      ;[140a] 20 03
                    rst       $28                           ;[140c] ef
                    ld        l,(hl)                        ;[140d] 6e
                    dec       c                             ;[140e] 0d
                    res       6,(iy+$02)                    ;[140f] fd cb 02 b6
                    call      $2cfb                         ;[1413] cd fb 2c
                    ld        hl,$ec0d                      ;[1416] 21 0d ec
                    bit       6,(hl)                        ;[1419] cb 76
                    jr        nz,$1428                      ;[141b] 20 0b
                    inc       hl                            ;[141d] 23
                    ld        a,(hl)                        ;[141e] 7e
                    cp        $00                           ;[141f] fe 00
                    jr        nz,$1428                      ;[1421] 20 05
                    call      $3e80                         ;[1423] cd 80 3e
                    sub       e                             ;[1426] 93
                    ld        a,(de)                        ;[1427] 1a
                    call      $2cd6                         ;[1428] cd d6 2c
                    ld        hl,$5c3c                      ;[142b] 21 3c 5c
                    res       3,(hl)                        ;[142e] cb 9e
                    ld        a,$19                         ;[1430] 3e 19
                    sub       (iy+$4f)                      ;[1432] fd 96 4f
                    ld        ($5c8c),a                     ;[1435] 32 8c 5c
                    set       7,(iy+$01)                    ;[1438] fd cb 01 fe
                    ld        (iy+$0a),$01                  ;[143c] fd 36 0a 01
                    ld        hl,$3e00                      ;[1440] 21 00 3e
                    push      hl                            ;[1443] e5
                    ld        hl,$5b3a                      ;[1444] 21 3a 5b
                    push      hl                            ;[1447] e5
                    ld        ($5c3d),sp                    ;[1448] ed 73 3d 5c
                    ld        hl,$1455                      ;[144c] 21 55 14
                    ld        ($5b6c),hl                    ;[144f] 22 6c 5b
                    jp        $1064                         ;[1452] c3 64 10
                    ld        a,($5c3a)                     ;[1455] 3a 3a 5c
                    bit       7,a                           ;[1458] cb 7f
                    jp        nz,$2739                      ;[145a] c2 39 27
                    ld        hl,$5b66                      ;[145d] 21 66 5b
                    bit       4,(hl)                        ;[1460] cb 66
                    jp        z,$2739                       ;[1462] ca 39 27
                    ld        a,$41                         ;[1465] 3e 41
                    ld        ($5b79),a                     ;[1467] 32 79 5b
                    jp        $2739                         ;[146a] c3 39 27
                    rst       $28                           ;[146d] ef
                    or        b                             ;[146e] b0
                    ld        d,$2a                         ;[146f] 16 2a
                    ld        e,c                           ;[1471] 59
                    ld        e,h                           ;[1472] 5c
                    ld        bc,$0001                      ;[1473] 01 01 00
                    rst       $28                           ;[1476] ef
                    ld        d,l                           ;[1477] 55
                    ld        d,$2a                         ;[1478] 16 2a
                    ld        e,c                           ;[147a] 59
                    ld        e,h                           ;[147b] 5c
                    ld        (hl),$e1                      ;[147c] 36 e1
                    call      $265d                         ;[147e] cd 5d 26
                    call      $14e0                         ;[1481] cd e0 14
                    ld        sp,($5c3d)                    ;[1484] ed 7b 3d 5c
                    pop       hl                            ;[1488] e1
                    ld        hl,$1303                      ;[1489] 21 03 13
                    push      hl                            ;[148c] e5
                    ld        hl,$0013                      ;[148d] 21 13 00
                    push      hl                            ;[1490] e5
                    ld        hl,$0008                      ;[1491] 21 08 00
                    push      hl                            ;[1494] e5
                    ld        a,$20                         ;[1495] 3e 20
                    ld        ($5b5c),a                     ;[1497] 32 5c 5b
                    push      af                            ;[149a] f5
                    push      bc                            ;[149b] c5
                    di                                      ;[149c] f3
                    res       4,(iy+$01)                    ;[149d] fd cb 01 a6
                    jp        $5b10                         ;[14a1] c3 10 5b
                    ld        hl,$6000                      ;[14a4] 21 00 60
                    push      hl                            ;[14a7] e5
                    ld        de,$6000                      ;[14a8] 11 00 60
                    ld        hl,$14d1                      ;[14ab] 21 d1 14
                    ld        bc,$000f                      ;[14ae] 01 0f 00
                    ldir                                    ;[14b1] ed b0
                    ld        a,($5b67)                     ;[14b3] 3a 67 5b
                    res       3,a                           ;[14b6] cb 9f
                    ld        bc,$1ffd                      ;[14b8] 01 fd 1f
                    di                                      ;[14bb] f3
                    ld        ($5b67),a                     ;[14bc] 32 67 5b
                    out       (c),a                         ;[14bf] ed 79
                    ei                                      ;[14c1] fb
                    ld        a,$30                         ;[14c2] 3e 30
                    di                                      ;[14c4] f3
                    res       4,(iy+$01)                    ;[14c5] fd cb 01 a6
                    ld        ($5b5c),a                     ;[14c9] 32 5c 5b
                    push      af                            ;[14cc] f5
                    push      bc                            ;[14cd] c5
                    jp        $5b10                         ;[14ce] c3 10 5b
                    ld        a,$30                         ;[14d1] 3e 30
                    ld        bc,$7ffd                      ;[14d3] 01 fd 7f
                    di                                      ;[14d6] f3
                    out       (c),a                         ;[14d7] ed 79
                    ei                                      ;[14d9] fb
                    jp        $0000                         ;[14da] c3 00 00
                    ld        b,e                           ;[14dd] 43
                    ld        c,d                           ;[14de] 4a
                    ld        c,h                           ;[14df] 4c
                    ld        hl,($5c4f)                    ;[14e0] 2a 4f 5c
                    ld        de,$0005                      ;[14e3] 11 05 00
                    add       hl,de                         ;[14e6] 19
                    ld        de,$000a                      ;[14e7] 11 0a 00
                    ex        de,hl                         ;[14ea] eb
                    add       hl,de                         ;[14eb] 19
                    ex        de,hl                         ;[14ec] eb
                    ld        bc,$0004                      ;[14ed] 01 04 00
                    ldir                                    ;[14f0] ed b0
                    res       3,(iy+$30)                    ;[14f2] fd cb 30 9e
                    res       4,(iy+$01)                    ;[14f6] fd cb 01 a6
                    ret                                     ;[14fa] c9

                    rst       $28                           ;[14fb] ef
                    ld        ($1622),hl                    ;[14fc] 22 22 16
                    nop                                     ;[14ff] 00
                    nop                                     ;[1500] 00
                    djnz      $1503                         ;[1501] 10 00
                    ld        de,$1307                      ;[1503] 11 07 13
                    nop                                     ;[1506] 00
                    ld        c,c                           ;[1507] 49
                    ld        l,(hl)                        ;[1508] 6e
                    ld        (hl),e                        ;[1509] 73
                    ld        h,l                           ;[150a] 65
                    ld        (hl),d                        ;[150b] 72
                    ld        (hl),h                        ;[150c] 74
                    jr        nz,$1583                      ;[150d] 20 74
                    ld        h,c                           ;[150f] 61
                    ld        (hl),b                        ;[1510] 70
                    ld        h,l                           ;[1511] 65
                    jr        nz,$1575                      ;[1512] 20 61
                    ld        l,(hl)                        ;[1514] 6e
                    ld        h,h                           ;[1515] 64
                    jr        nz,$1588                      ;[1516] 20 70
                    ld        (hl),d                        ;[1518] 72
                    ld        h,l                           ;[1519] 65
                    ld        (hl),e                        ;[151a] 73
                    ld        (hl),e                        ;[151b] 73
                    jr        nz,$156e                      ;[151c] 20 50
                    ld        c,h                           ;[151e] 4c
                    ld        b,c                           ;[151f] 41
                    ld        e,c                           ;[1520] 59
                    dec       c                             ;[1521] 0d
                    ld        d,h                           ;[1522] 54
                    ld        l,a                           ;[1523] 6f
                    jr        nz,$1589                      ;[1524] 20 63
                    ld        h,c                           ;[1526] 61
                    ld        l,(hl)                        ;[1527] 6e
                    ld        h,e                           ;[1528] 63
                    ld        h,l                           ;[1529] 65
                    ld        l,h                           ;[152a] 6c
                    jr        nz,$155a                      ;[152b] 20 2d
                    jr        nz,$159f                      ;[152d] 20 70
                    ld        (hl),d                        ;[152f] 72
                    ld        h,l                           ;[1530] 65
                    ld        (hl),e                        ;[1531] 73
                    ld        (hl),e                        ;[1532] 73
                    jr        nz,$1577                      ;[1533] 20 42
                    ld        d,d                           ;[1535] 52
                    ld        b,l                           ;[1536] 45
                    ld        b,c                           ;[1537] 41
                    ld        c,e                           ;[1538] 4b
                    jr        nz,$15af                      ;[1539] 20 74
                    ld        (hl),a                        ;[153b] 77
                    ld        l,c                           ;[153c] 69
                    ld        h,e                           ;[153d] 63
                    ld        h,l                           ;[153e] 65
                    rst       $38                           ;[153f] ff
                    ld        a,(hl)                        ;[1540] 7e
                    cp        $ff                           ;[1541] fe ff
                    ret       z                             ;[1543] c8
                    rst       $28                           ;[1544] ef
                    djnz      $1547                         ;[1545] 10 00
                    inc       hl                            ;[1547] 23
                    jr        $1540                         ;[1548] 18 f6
                    rst       $28                           ;[154a] ef
                    ld        ($6964),hl                    ;[154b] 22 64 69
                    ld        (hl),e                        ;[154e] 73
                    ld        l,e                           ;[154f] 6b
                    ld        ($033e),hl                    ;[1550] 22 3e 03
                    jr        $1557                         ;[1553] 18 02
                    ld        a,$02                         ;[1555] 3e 02
                    ld        (iy+$02),$00                  ;[1557] fd 36 02 00
                    rst       $28                           ;[155b] ef
                    jr        nc,$1583                      ;[155c] 30 25
                    jr        z,$1563                       ;[155e] 28 03
                    rst       $28                           ;[1560] ef
                    ld        bc,$ef16                      ;[1561] 01 16 ef
                    jr        $1566                         ;[1564] 18 00
                    rst       $28                           ;[1566] ef
                    ld        (hl),b                        ;[1567] 70
                    jr        nz,$15a2                      ;[1568] 20 38
                    jr        $155b                         ;[156a] 18 ef
                    jr        $156e                         ;[156c] 18 00
                    cp        $3b                           ;[156e] fe 3b
                    jr        z,$1576                       ;[1570] 28 04
                    cp        $2c                           ;[1572] fe 2c
                    jr        nz,$157e                      ;[1574] 20 08
                    rst       $28                           ;[1576] ef
                    jr        nz,$1579                      ;[1577] 20 00
                    call      $113d                         ;[1579] cd 3d 11
                    jr        $1586                         ;[157c] 18 08
                    rst       $28                           ;[157e] ef
                    and       $1c                           ;[157f] e6 1c
                    jr        $1586                         ;[1581] 18 03
                    rst       $28                           ;[1583] ef
                    sbc       $1c                           ;[1584] de 1c
                    call      $10cd                         ;[1586] cd cd 10
                    rst       $28                           ;[1589] ef
                    dec       h                             ;[158a] 25
                    jr        $1556                         ;[158b] 18 c9
                    di                                      ;[158d] f3
                    push      bc                            ;[158e] c5
                    ld        de,$0037                      ;[158f] 11 37 00
                    ld        hl,$003c                      ;[1592] 21 3c 00
                    add       hl,de                         ;[1595] 19
                    djnz      $1595                         ;[1596] 10 fd
                    ld        c,l                           ;[1598] 4d
                    ld        b,h                           ;[1599] 44
                    rst       $28                           ;[159a] ef
                    jr        nc,$159d                      ;[159b] 30 00
                    di                                      ;[159d] f3
                    push      de                            ;[159e] d5
                    pop       iy                            ;[159f] fd e1
                    push      hl                            ;[15a1] e5
                    pop       ix                            ;[15a2] dd e1
                    ld        (iy+$10),$ff                  ;[15a4] fd 36 10 ff
                    ld        bc,$ffc9                      ;[15a8] 01 c9 ff
                    add       ix,bc                         ;[15ab] dd 09
                    ld        (ix+$03),$3c                  ;[15ad] dd 36 03 3c
                    ld        (ix+$01),$ff                  ;[15b1] dd 36 01 ff
                    ld        (ix+$04),$0f                  ;[15b5] dd 36 04 0f
                    ld        (ix+$05),$05                  ;[15b9] dd 36 05 05
                    ld        (ix+$21),$00                  ;[15bd] dd 36 21 00
                    ld        (ix+$0a),$00                  ;[15c1] dd 36 0a 00
                    ld        (ix+$0b),$00                  ;[15c5] dd 36 0b 00
                    ld        (ix+$16),$ff                  ;[15c9] dd 36 16 ff
                    ld        (ix+$17),$00                  ;[15cd] dd 36 17 00
                    ld        (ix+$18),$00                  ;[15d1] dd 36 18 00
                    rst       $28                           ;[15d5] ef
                    pop       af                            ;[15d6] f1
                    dec       hl                            ;[15d7] 2b
                    di                                      ;[15d8] f3
                    ld        (ix+$06),e                    ;[15d9] dd 73 06
                    ld        (ix+$07),d                    ;[15dc] dd 72 07
                    ld        (ix+$0c),e                    ;[15df] dd 73 0c
                    ld        (ix+$0d),d                    ;[15e2] dd 72 0d
                    ex        de,hl                         ;[15e5] eb
                    add       hl,bc                         ;[15e6] 09
                    ld        (ix+$08),l                    ;[15e7] dd 75 08
                    ld        (ix+$09),h                    ;[15ea] dd 74 09
                    pop       bc                            ;[15ed] c1
                    push      bc                            ;[15ee] c5
                    dec       b                             ;[15ef] 05
                    ld        c,b                           ;[15f0] 48
                    ld        b,$00                         ;[15f1] 06 00
                    sla       c                             ;[15f3] cb 21
                    push      iy                            ;[15f5] fd e5
                    pop       hl                            ;[15f7] e1
                    add       hl,bc                         ;[15f8] 09
                    push      ix                            ;[15f9] dd e5
                    pop       bc                            ;[15fb] c1
                    ld        (hl),c                        ;[15fc] 71
                    inc       hl                            ;[15fd] 23
                    ld        (hl),b                        ;[15fe] 70
                    or        a                             ;[15ff] b7
                    rl        (iy+$10)                      ;[1600] fd cb 10 16
                    pop       bc                            ;[1604] c1
                    dec       b                             ;[1605] 05
                    push      bc                            ;[1606] c5
                    ld        (ix+$02),b                    ;[1607] dd 70 02
                    jr        nz,$15a8                      ;[160a] 20 9c
                    pop       bc                            ;[160c] c1
                    ld        (iy+$27),$1a                  ;[160d] fd 36 27 1a
                    ld        (iy+$28),$0b                  ;[1611] fd 36 28 0b
                    push      iy                            ;[1615] fd e5
                    pop       hl                            ;[1617] e1
                    ld        bc,$002b                      ;[1618] 01 2b 00
                    add       hl,bc                         ;[161b] 09
                    ex        de,hl                         ;[161c] eb
                    ld        hl,$1639                      ;[161d] 21 39 16
                    ld        bc,$000d                      ;[1620] 01 0d 00
                    ldir                                    ;[1623] ed b0
                    ld        d,$07                         ;[1625] 16 07
                    ld        e,$f8                         ;[1627] 1e f8
                    call      $1a86                         ;[1629] cd 86 1a
                    ld        d,$0b                         ;[162c] 16 0b
                    ld        e,$ff                         ;[162e] 1e ff
                    call      $1a86                         ;[1630] cd 86 1a
                    inc       d                             ;[1633] 14
                    call      $1a86                         ;[1634] cd 86 1a
                    jr        $1685                         ;[1637] 18 4c
                    rst       $28                           ;[1639] ef
                    and       h                             ;[163a] a4
                    ld        bc,$3405                      ;[163b] 01 05 34
                    rst       $18                           ;[163e] df
                    ld        (hl),l                        ;[163f] 75
                    call      p,$7538                       ;[1640] f4 38 75
                    dec       b                             ;[1643] 05
                    jr        c,$160f                       ;[1644] 38 c9
                    ld        a,$7f                         ;[1646] 3e 7f
                    in        a,($fe)                       ;[1648] db fe
                    rra                                     ;[164a] 1f
                    ret       c                             ;[164b] d8
                    ld        a,$fe                         ;[164c] 3e fe
                    in        a,($fe)                       ;[164e] db fe
                    rra                                     ;[1650] 1f
                    ret                                     ;[1651] c9

                    ld        bc,$0011                      ;[1652] 01 11 00
                    jr        $165a                         ;[1655] 18 03
                    ld        bc,$0000                      ;[1657] 01 00 00
                    push      iy                            ;[165a] fd e5
                    pop       hl                            ;[165c] e1
                    add       hl,bc                         ;[165d] 09
                    ld        (iy+$23),l                    ;[165e] fd 75 23
                    ld        (iy+$24),h                    ;[1661] fd 74 24
                    ld        a,(iy+$10)                    ;[1664] fd 7e 10
                    ld        (iy+$22),a                    ;[1667] fd 77 22
                    ld        (iy+$21),$01                  ;[166a] fd 36 21 01
                    ret                                     ;[166e] c9

                    ld        e,(hl)                        ;[166f] 5e
                    inc       hl                            ;[1670] 23
                    ld        d,(hl)                        ;[1671] 56
                    push      de                            ;[1672] d5
                    pop       ix                            ;[1673] dd e1
                    ret                                     ;[1675] c9

                    ld        l,(iy+$23)                    ;[1676] fd 6e 23
                    ld        h,(iy+$24)                    ;[1679] fd 66 24
                    inc       hl                            ;[167c] 23
                    inc       hl                            ;[167d] 23
                    ld        (iy+$23),l                    ;[167e] fd 75 23
                    ld        (iy+$24),h                    ;[1681] fd 74 24
                    ret                                     ;[1684] c9

                    call      $1657                         ;[1685] cd 57 16
                    rr        (iy+$22)                      ;[1688] fd cb 22 1e
                    jr        c,$1694                       ;[168c] 38 06
                    call      $166f                         ;[168e] cd 6f 16
                    call      $1764                         ;[1691] cd 64 17
                    sla       (iy+$21)                      ;[1694] fd cb 21 26
                    jr        c,$169f                       ;[1698] 38 05
                    call      $1676                         ;[169a] cd 76 16
                    jr        $1688                         ;[169d] 18 e9
                    call      $1b9b                         ;[169f] cd 9b 1b
                    push      de                            ;[16a2] d5
                    call      $1b4c                         ;[16a3] cd 4c 1b
                    pop       de                            ;[16a6] d1
                    ld        a,(iy+$10)                    ;[16a7] fd 7e 10
                    cp        $ff                           ;[16aa] fe ff
                    jr        nz,$16b3                      ;[16ac] 20 05
                    call      $1a9d                         ;[16ae] cd 9d 1a
                    ei                                      ;[16b1] fb
                    ret                                     ;[16b2] c9

                    dec       de                            ;[16b3] 1b
                    call      $1b80                         ;[16b4] cd 80 1b
                    call      $1bcb                         ;[16b7] cd cb 1b
                    call      $1b9b                         ;[16ba] cd 9b 1b
                    jr        $16a7                         ;[16bd] 18 e8
                    ld        c,b                           ;[16bf] 48
                    ld        e,d                           ;[16c0] 5a
                    ld        e,c                           ;[16c1] 59
                    ld        e,b                           ;[16c2] 58
                    ld        d,a                           ;[16c3] 57
                    ld        d,l                           ;[16c4] 55
                    ld        d,(hl)                        ;[16c5] 56
                    ld        c,l                           ;[16c6] 4d
                    ld        d,h                           ;[16c7] 54
                    add       hl,hl                         ;[16c8] 29
                    jr        z,$1719                       ;[16c9] 28 4e
                    ld        c,a                           ;[16cb] 4f
                    ld        hl,$edcd                      ;[16cc] 21 cd ed
                    ld        a,(de)                        ;[16cf] 1a
                    ret       c                             ;[16d0] d8
                    inc       (ix+$06)                      ;[16d1] dd 34 06
                    ret       nz                            ;[16d4] c0
                    inc       (ix+$07)                      ;[16d5] dd 34 07
                    ret                                     ;[16d8] c9

                    push      hl                            ;[16d9] e5
                    ld        c,$00                         ;[16da] 0e 00
                    call      $16cd                         ;[16dc] cd cd 16
                    jr        c,$16e9                       ;[16df] 38 08
                    cp        $26                           ;[16e1] fe 26
                    jr        nz,$16f4                      ;[16e3] 20 0f
                    ld        a,$80                         ;[16e5] 3e 80
                    pop       hl                            ;[16e7] e1
                    ret                                     ;[16e8] c9

                    ld        a,(iy+$21)                    ;[16e9] fd 7e 21
                    or        (iy+$10)                      ;[16ec] fd b6 10
                    ld        (iy+$10),a                    ;[16ef] fd 77 10
                    jr        $16e7                         ;[16f2] 18 f3
                    cp        $23                           ;[16f4] fe 23
                    jr        nz,$16fb                      ;[16f6] 20 03
                    inc       c                             ;[16f8] 0c
                    jr        $16dc                         ;[16f9] 18 e1
                    cp        $24                           ;[16fb] fe 24
                    jr        nz,$1702                      ;[16fd] 20 03
                    dec       c                             ;[16ff] 0d
                    jr        $16dc                         ;[1700] 18 da
                    bit       5,a                           ;[1702] cb 6f
                    jr        nz,$170c                      ;[1704] 20 06
                    push      af                            ;[1706] f5
                    ld        a,$0c                         ;[1707] 3e 0c
                    add       c                             ;[1709] 81
                    ld        c,a                           ;[170a] 4f
                    pop       af                            ;[170b] f1
                    and       $df                           ;[170c] e6 df
                    sub       $41                           ;[170e] d6 41
                    jp        c,$1b2c                       ;[1710] da 2c 1b
                    cp        $07                           ;[1713] fe 07
                    jp        nc,$1b2c                      ;[1715] d2 2c 1b
                    push      bc                            ;[1718] c5
                    ld        b,$00                         ;[1719] 06 00
                    ld        c,a                           ;[171b] 4f
                    ld        hl,$1a03                      ;[171c] 21 03 1a
                    add       hl,bc                         ;[171f] 09
                    ld        a,(hl)                        ;[1720] 7e
                    pop       bc                            ;[1721] c1
                    add       c                             ;[1722] 81
                    pop       hl                            ;[1723] e1
                    ret                                     ;[1724] c9

                    push      hl                            ;[1725] e5
                    push      de                            ;[1726] d5
                    ld        l,(ix+$06)                    ;[1727] dd 6e 06
                    ld        h,(ix+$07)                    ;[172a] dd 66 07
                    ld        de,$0000                      ;[172d] 11 00 00
                    ld        a,(hl)                        ;[1730] 7e
                    cp        $30                           ;[1731] fe 30
                    jr        c,$174d                       ;[1733] 38 18
                    cp        $3a                           ;[1735] fe 3a
                    jr        nc,$174d                      ;[1737] 30 14
                    inc       hl                            ;[1739] 23
                    push      hl                            ;[173a] e5
                    call      $1758                         ;[173b] cd 58 17
                    sub       $30                           ;[173e] d6 30
                    ld        h,$00                         ;[1740] 26 00
                    ld        l,a                           ;[1742] 6f
                    add       hl,de                         ;[1743] 19
                    jr        c,$174a                       ;[1744] 38 04
                    ex        de,hl                         ;[1746] eb
                    pop       hl                            ;[1747] e1
                    jr        $1730                         ;[1748] 18 e6
                    jp        $1b24                         ;[174a] c3 24 1b
                    ld        (ix+$06),l                    ;[174d] dd 75 06
                    ld        (ix+$07),h                    ;[1750] dd 74 07
                    push      de                            ;[1753] d5
                    pop       bc                            ;[1754] c1
                    pop       de                            ;[1755] d1
                    pop       hl                            ;[1756] e1
                    ret                                     ;[1757] c9

                    ld        hl,$0000                      ;[1758] 21 00 00
                    ld        b,$0a                         ;[175b] 06 0a
                    add       hl,de                         ;[175d] 19
                    jr        c,$174a                       ;[175e] 38 ea
                    djnz      $175d                         ;[1760] 10 fb
                    ex        de,hl                         ;[1762] eb
                    ret                                     ;[1763] c9

                    call      $1646                         ;[1764] cd 46 16
                    jr        c,$1771                       ;[1767] 38 08
                    call      $1a9d                         ;[1769] cd 9d 1a
                    ei                                      ;[176c] fb
                    call      $2c4c                         ;[176d] cd 4c 2c
                    inc       d                             ;[1770] 14
                    call      $16cd                         ;[1771] cd cd 16
                    jp        c,$19ac                       ;[1774] da ac 19
                    call      $19fa                         ;[1777] cd fa 19
                    ld        b,$00                         ;[177a] 06 00
                    sla       c                             ;[177c] cb 21
                    ld        hl,$19d4                      ;[177e] 21 d4 19
                    add       hl,bc                         ;[1781] 09
                    ld        e,(hl)                        ;[1782] 5e
                    inc       hl                            ;[1783] 23
                    ld        d,(hl)                        ;[1784] 56
                    ex        de,hl                         ;[1785] eb
                    call      $178c                         ;[1786] cd 8c 17
                    jr        $1764                         ;[1789] 18 d9
                    ret                                     ;[178b] c9

                    jp        (hl)                          ;[178c] e9
                    call      $16cd                         ;[178d] cd cd 16
                    jp        c,$19ab                       ;[1790] da ab 19
                    cp        $21                           ;[1793] fe 21
                    ret       z                             ;[1795] c8
                    jr        $178d                         ;[1796] 18 f5
                    call      $1725                         ;[1798] cd 25 17
                    ld        a,c                           ;[179b] 79
                    cp        $09                           ;[179c] fe 09
                    jp        nc,$1b1c                      ;[179e] d2 1c 1b
                    sla       a                             ;[17a1] cb 27
                    sla       a                             ;[17a3] cb 27
                    ld        b,a                           ;[17a5] 47
                    sla       a                             ;[17a6] cb 27
                    add       b                             ;[17a8] 80
                    ld        (ix+$03),a                    ;[17a9] dd 77 03
                    ret                                     ;[17ac] c9

                    ret                                     ;[17ad] c9

                    ld        a,(ix+$0b)                    ;[17ae] dd 7e 0b
                    inc       a                             ;[17b1] 3c
                    cp        $05                           ;[17b2] fe 05
                    jp        z,$1b34                       ;[17b4] ca 34 1b
                    ld        (ix+$0b),a                    ;[17b7] dd 77 0b
                    ld        de,$000c                      ;[17ba] 11 0c 00
                    call      $182f                         ;[17bd] cd 2f 18
                    ld        a,(ix+$06)                    ;[17c0] dd 7e 06
                    ld        (hl),a                        ;[17c3] 77
                    inc       hl                            ;[17c4] 23
                    ld        a,(ix+$07)                    ;[17c5] dd 7e 07
                    ld        (hl),a                        ;[17c8] 77
                    ret                                     ;[17c9] c9

                    ld        a,(ix+$16)                    ;[17ca] dd 7e 16
                    ld        de,$0017                      ;[17cd] 11 17 00
                    or        a                             ;[17d0] b7
                    jp        m,$17f8                       ;[17d1] fa f8 17
                    call      $182f                         ;[17d4] cd 2f 18
                    ld        a,(ix+$06)                    ;[17d7] dd 7e 06
                    cp        (hl)                          ;[17da] be
                    jr        nz,$17f8                      ;[17db] 20 1b
                    inc       hl                            ;[17dd] 23
                    ld        a,(ix+$07)                    ;[17de] dd 7e 07
                    cp        (hl)                          ;[17e1] be
                    jr        nz,$17f8                      ;[17e2] 20 14
                    dec       (ix+$16)                      ;[17e4] dd 35 16
                    ld        a,(ix+$16)                    ;[17e7] dd 7e 16
                    or        a                             ;[17ea] b7
                    ret       p                             ;[17eb] f0
                    bit       0,(ix+$0a)                    ;[17ec] dd cb 0a 46
                    ret       z                             ;[17f0] c8
                    ld        (ix+$16),$00                  ;[17f1] dd 36 16 00
                    xor       a                             ;[17f5] af
                    jr        $1813                         ;[17f6] 18 1b
                    ld        a,(ix+$16)                    ;[17f8] dd 7e 16
                    inc       a                             ;[17fb] 3c
                    cp        $05                           ;[17fc] fe 05
                    jp        z,$1b34                       ;[17fe] ca 34 1b
                    ld        (ix+$16),a                    ;[1801] dd 77 16
                    call      $182f                         ;[1804] cd 2f 18
                    ld        a,(ix+$06)                    ;[1807] dd 7e 06
                    ld        (hl),a                        ;[180a] 77
                    inc       hl                            ;[180b] 23
                    ld        a,(ix+$07)                    ;[180c] dd 7e 07
                    ld        (hl),a                        ;[180f] 77
                    ld        a,(ix+$0b)                    ;[1810] dd 7e 0b
                    ld        de,$000c                      ;[1813] 11 0c 00
                    call      $182f                         ;[1816] cd 2f 18
                    ld        a,(hl)                        ;[1819] 7e
                    ld        (ix+$06),a                    ;[181a] dd 77 06
                    inc       hl                            ;[181d] 23
                    ld        a,(hl)                        ;[181e] 7e
                    ld        (ix+$07),a                    ;[181f] dd 77 07
                    dec       (ix+$0b)                      ;[1822] dd 35 0b
                    ret       p                             ;[1825] f0
                    ld        (ix+$0b),$00                  ;[1826] dd 36 0b 00
                    set       0,(ix+$0a)                    ;[182a] dd cb 0a c6
                    ret                                     ;[182e] c9

                    push      ix                            ;[182f] dd e5
                    pop       hl                            ;[1831] e1
                    add       hl,de                         ;[1832] 19
                    ld        b,$00                         ;[1833] 06 00
                    ld        c,a                           ;[1835] 4f
                    sla       c                             ;[1836] cb 21
                    add       hl,bc                         ;[1838] 09
                    ret                                     ;[1839] c9

                    call      $1725                         ;[183a] cd 25 17
                    ld        a,b                           ;[183d] 78
                    or        a                             ;[183e] b7
                    jp        nz,$1b1c                      ;[183f] c2 1c 1b
                    ld        a,c                           ;[1842] 79
                    cp        $3c                           ;[1843] fe 3c
                    jp        c,$1b1c                       ;[1845] da 1c 1b
                    cp        $f1                           ;[1848] fe f1
                    jp        nc,$1b1c                      ;[184a] d2 1c 1b
                    ld        a,(ix+$02)                    ;[184d] dd 7e 02
                    or        a                             ;[1850] b7
                    ret       nz                            ;[1851] c0
                    ld        b,$00                         ;[1852] 06 00
                    push      bc                            ;[1854] c5
                    pop       hl                            ;[1855] e1
                    add       hl,hl                         ;[1856] 29
                    add       hl,hl                         ;[1857] 29
                    push      hl                            ;[1858] e5
                    pop       bc                            ;[1859] c1
                    push      iy                            ;[185a] fd e5
                    rst       $28                           ;[185c] ef
                    dec       hl                            ;[185d] 2b
                    dec       l                             ;[185e] 2d
                    di                                      ;[185f] f3
                    pop       iy                            ;[1860] fd e1
                    push      iy                            ;[1862] fd e5
                    push      iy                            ;[1864] fd e5
                    pop       hl                            ;[1866] e1
                    ld        bc,$002b                      ;[1867] 01 2b 00
                    add       hl,bc                         ;[186a] 09
                    ld        iy,$5c3a                      ;[186b] fd 21 3a 5c
                    push      hl                            ;[186f] e5
                    ld        hl,$1880                      ;[1870] 21 80 18
                    ld        ($5b5a),hl                    ;[1873] 22 5a 5b
                    ld        hl,$5b2a                      ;[1876] 21 2a 5b
                    ex        (sp),hl                       ;[1879] e3
                    push      hl                            ;[187a] e5
                    push      af                            ;[187b] f5
                    push      hl                            ;[187c] e5
                    jp        $5b10                         ;[187d] c3 10 5b
                    di                                      ;[1880] f3
                    rst       $28                           ;[1881] ef
                    and       d                             ;[1882] a2
                    dec       l                             ;[1883] 2d
                    di                                      ;[1884] f3
                    pop       iy                            ;[1885] fd e1
                    ld        (iy+$27),c                    ;[1887] fd 71 27
                    ld        (iy+$28),b                    ;[188a] fd 70 28
                    ret                                     ;[188d] c9

                    call      $1725                         ;[188e] cd 25 17
                    ld        a,c                           ;[1891] 79
                    cp        $40                           ;[1892] fe 40
                    jp        nc,$1b1c                      ;[1894] d2 1c 1b
                    cpl                                     ;[1897] 2f
                    ld        e,a                           ;[1898] 5f
                    ld        d,$07                         ;[1899] 16 07
                    call      $1a86                         ;[189b] cd 86 1a
                    ret                                     ;[189e] c9

                    call      $1725                         ;[189f] cd 25 17
                    ld        a,c                           ;[18a2] 79
                    cp        $10                           ;[18a3] fe 10
                    jp        nc,$1b1c                      ;[18a5] d2 1c 1b
                    ld        (ix+$04),a                    ;[18a8] dd 77 04
                    ld        e,(ix+$02)                    ;[18ab] dd 5e 02
                    ld        a,$08                         ;[18ae] 3e 08
                    add       e                             ;[18b0] 83
                    ld        d,a                           ;[18b1] 57
                    ld        e,c                           ;[18b2] 59
                    call      $1a86                         ;[18b3] cd 86 1a
                    ret                                     ;[18b6] c9

                    ld        e,(ix+$02)                    ;[18b7] dd 5e 02
                    ld        a,$08                         ;[18ba] 3e 08
                    add       e                             ;[18bc] 83
                    ld        d,a                           ;[18bd] 57
                    ld        e,$1f                         ;[18be] 1e 1f
                    ld        (ix+$04),e                    ;[18c0] dd 73 04
                    ret                                     ;[18c3] c9

                    call      $1725                         ;[18c4] cd 25 17
                    ld        a,c                           ;[18c7] 79
                    cp        $08                           ;[18c8] fe 08
                    jp        nc,$1b1c                      ;[18ca] d2 1c 1b
                    ld        b,$00                         ;[18cd] 06 00
                    ld        hl,$19f2                      ;[18cf] 21 f2 19
                    add       hl,bc                         ;[18d2] 09
                    ld        a,(hl)                        ;[18d3] 7e
                    ld        (iy+$29),a                    ;[18d4] fd 77 29
                    ret                                     ;[18d7] c9

                    call      $1725                         ;[18d8] cd 25 17
                    ld        d,$0b                         ;[18db] 16 0b
                    ld        e,c                           ;[18dd] 59
                    call      $1a86                         ;[18de] cd 86 1a
                    inc       d                             ;[18e1] 14
                    ld        e,b                           ;[18e2] 58
                    call      $1a86                         ;[18e3] cd 86 1a
                    ret                                     ;[18e6] c9

                    call      $1725                         ;[18e7] cd 25 17
                    ld        a,c                           ;[18ea] 79
                    dec       a                             ;[18eb] 3d
                    jp        m,$1b1c                       ;[18ec] fa 1c 1b
                    cp        $10                           ;[18ef] fe 10
                    jp        nc,$1b1c                      ;[18f1] d2 1c 1b
                    ld        (ix+$01),a                    ;[18f4] dd 77 01
                    ret                                     ;[18f7] c9

                    call      $1725                         ;[18f8] cd 25 17
                    ld        a,c                           ;[18fb] 79
                    call      $1dad                         ;[18fc] cd ad 1d
                    ret                                     ;[18ff] c9

                    ld        (iy+$10),$ff                  ;[1900] fd 36 10 ff
                    ret                                     ;[1904] c9

                    call      $1a23                         ;[1905] cd 23 1a
                    jp        c,$198b                       ;[1908] da 8b 19
                    call      $19b6                         ;[190b] cd b6 19
                    call      $19be                         ;[190e] cd be 19
                    xor       a                             ;[1911] af
                    ld        (ix+$21),a                    ;[1912] dd 77 21
                    call      $1ad2                         ;[1915] cd d2 1a
                    call      $1725                         ;[1918] cd 25 17
                    ld        a,c                           ;[191b] 79
                    or        a                             ;[191c] b7
                    jp        z,$1b1c                       ;[191d] ca 1c 1b
                    cp        $0d                           ;[1920] fe 0d
                    jp        nc,$1b1c                      ;[1922] d2 1c 1b
                    cp        $0a                           ;[1925] fe 0a
                    jr        c,$193c                       ;[1927] 38 13
                    call      $1a0a                         ;[1929] cd 0a 1a
                    call      $197e                         ;[192c] cd 7e 19
                    ld        (hl),e                        ;[192f] 73
                    inc       hl                            ;[1930] 23
                    ld        (hl),d                        ;[1931] 72
                    call      $197e                         ;[1932] cd 7e 19
                    inc       hl                            ;[1935] 23
                    ld        (hl),e                        ;[1936] 73
                    inc       hl                            ;[1937] 23
                    ld        (hl),d                        ;[1938] 72
                    inc       hl                            ;[1939] 23
                    jr        $1942                         ;[193a] 18 06
                    ld        (ix+$05),c                    ;[193c] dd 71 05
                    call      $1a0a                         ;[193f] cd 0a 1a
                    call      $197e                         ;[1942] cd 7e 19
                    call      $1aed                         ;[1945] cd ed 1a
                    cp        $5f                           ;[1948] fe 5f
                    jr        nz,$1978                      ;[194a] 20 2c
                    call      $16cd                         ;[194c] cd cd 16
                    call      $1725                         ;[194f] cd 25 17
                    ld        a,c                           ;[1952] 79
                    cp        $0a                           ;[1953] fe 0a
                    jr        c,$1969                       ;[1955] 38 12
                    push      hl                            ;[1957] e5
                    push      de                            ;[1958] d5
                    call      $1a0a                         ;[1959] cd 0a 1a
                    pop       hl                            ;[195c] e1
                    add       hl,de                         ;[195d] 19
                    ld        c,e                           ;[195e] 4b
                    ld        b,d                           ;[195f] 42
                    ex        de,hl                         ;[1960] eb
                    pop       hl                            ;[1961] e1
                    ld        (hl),e                        ;[1962] 73
                    inc       hl                            ;[1963] 23
                    ld        (hl),d                        ;[1964] 72
                    ld        e,c                           ;[1965] 59
                    ld        d,b                           ;[1966] 50
                    jr        $1932                         ;[1967] 18 c9
                    ld        (ix+$05),c                    ;[1969] dd 71 05
                    push      hl                            ;[196c] e5
                    push      de                            ;[196d] d5
                    call      $1a0a                         ;[196e] cd 0a 1a
                    pop       hl                            ;[1971] e1
                    add       hl,de                         ;[1972] 19
                    ex        de,hl                         ;[1973] eb
                    pop       hl                            ;[1974] e1
                    jp        $1945                         ;[1975] c3 45 19
                    ld        (hl),e                        ;[1978] 73
                    inc       hl                            ;[1979] 23
                    ld        (hl),d                        ;[197a] 72
                    jp        $19a6                         ;[197b] c3 a6 19
                    ld        a,(ix+$21)                    ;[197e] dd 7e 21
                    inc       a                             ;[1981] 3c
                    cp        $0b                           ;[1982] fe 0b
                    jp        z,$1b44                       ;[1984] ca 44 1b
                    ld        (ix+$21),a                    ;[1987] dd 77 21
                    ret                                     ;[198a] c9

                    call      $1ad2                         ;[198b] cd d2 1a
                    ld        (ix+$21),$01                  ;[198e] dd 36 21 01
                    call      $19b6                         ;[1992] cd b6 19
                    call      $19be                         ;[1995] cd be 19
                    ld        c,(ix+$05)                    ;[1998] dd 4e 05
                    push      hl                            ;[199b] e5
                    call      $1a0a                         ;[199c] cd 0a 1a
                    pop       hl                            ;[199f] e1
                    ld        (hl),e                        ;[19a0] 73
                    inc       hl                            ;[19a1] 23
                    ld        (hl),d                        ;[19a2] 72
                    jp        $19a6                         ;[19a3] c3 a6 19
                    pop       hl                            ;[19a6] e1
                    inc       hl                            ;[19a7] 23
                    inc       hl                            ;[19a8] 23
                    push      hl                            ;[19a9] e5
                    ret                                     ;[19aa] c9

                    pop       hl                            ;[19ab] e1
                    ld        a,(iy+$21)                    ;[19ac] fd 7e 21
                    or        (iy+$10)                      ;[19af] fd b6 10
                    ld        (iy+$10),a                    ;[19b2] fd 77 10
                    ret                                     ;[19b5] c9

                    push      ix                            ;[19b6] dd e5
                    pop       hl                            ;[19b8] e1
                    ld        bc,$0022                      ;[19b9] 01 22 00
                    add       hl,bc                         ;[19bc] 09
                    ret                                     ;[19bd] c9

                    push      hl                            ;[19be] e5
                    push      iy                            ;[19bf] fd e5
                    pop       hl                            ;[19c1] e1
                    ld        bc,$0011                      ;[19c2] 01 11 00
                    add       hl,bc                         ;[19c5] 09
                    ld        b,$00                         ;[19c6] 06 00
                    ld        c,(ix+$02)                    ;[19c8] dd 4e 02
                    sla       c                             ;[19cb] cb 21
                    add       hl,bc                         ;[19cd] 09
                    pop       de                            ;[19ce] d1
                    ld        (hl),e                        ;[19cf] 73
                    inc       hl                            ;[19d0] 23
                    ld        (hl),d                        ;[19d1] 72
                    ex        de,hl                         ;[19d2] eb
                    ret                                     ;[19d3] c9

                    dec       b                             ;[19d4] 05
                    add       hl,de                         ;[19d5] 19
                    adc       l                             ;[19d6] 8d
                    rla                                     ;[19d7] 17
                    sbc       b                             ;[19d8] 98
                    rla                                     ;[19d9] 17
                    xor       l                             ;[19da] ad
                    rla                                     ;[19db] 17
                    xor       (hl)                          ;[19dc] ae
                    rla                                     ;[19dd] 17
                    jp        z,$3a17                       ;[19de] ca 17 3a
                    jr        $1971                         ;[19e1] 18 8e
                    jr        $1984                         ;[19e3] 18 9f
                    jr        $199e                         ;[19e5] 18 b7
                    jr        $19ad                         ;[19e7] 18 c4
                    jr        $19c3                         ;[19e9] 18 d8
                    jr        $19d4                         ;[19eb] 18 e7
                    jr        $19e7                         ;[19ed] 18 f8
                    jr        $19f1                         ;[19ef] 18 00
                    add       hl,de                         ;[19f1] 19
                    nop                                     ;[19f2] 00
                    inc       b                             ;[19f3] 04
                    dec       bc                            ;[19f4] 0b
                    dec       c                             ;[19f5] 0d
                    ex        af,af'                        ;[19f6] 08
                    inc       c                             ;[19f7] 0c
                    ld        c,$0a                         ;[19f8] 0e 0a
                    ld        bc,$000f                      ;[19fa] 01 0f 00
                    ld        hl,$16bf                      ;[19fd] 21 bf 16
                    cpir                                    ;[1a00] ed b1
                    ret                                     ;[1a02] c9

                    add       hl,bc                         ;[1a03] 09
                    dec       bc                            ;[1a04] 0b
                    nop                                     ;[1a05] 00
                    ld        (bc),a                        ;[1a06] 02
                    inc       b                             ;[1a07] 04
                    dec       b                             ;[1a08] 05
                    rlca                                    ;[1a09] 07
                    push      hl                            ;[1a0a] e5
                    ld        b,$00                         ;[1a0b] 06 00
                    ld        hl,$1a16                      ;[1a0d] 21 16 1a
                    add       hl,bc                         ;[1a10] 09
                    ld        d,$00                         ;[1a11] 16 00
                    ld        e,(hl)                        ;[1a13] 5e
                    pop       hl                            ;[1a14] e1
                    ret                                     ;[1a15] c9

                    add       b                             ;[1a16] 80
                    ld        b,$09                         ;[1a17] 06 09
                    inc       c                             ;[1a19] 0c
                    ld        (de),a                        ;[1a1a] 12
                    jr        $1a41                         ;[1a1b] 18 24
                    jr        nc,$1a67                      ;[1a1d] 30 48
                    ld        h,b                           ;[1a1f] 60
                    inc       b                             ;[1a20] 04
                    ex        af,af'                        ;[1a21] 08
                    djnz      $1a22                         ;[1a22] 10 fe
                    jr        nc,$19fe                      ;[1a24] 30 d8
                    cp        $3a                           ;[1a26] fe 3a
                    ccf                                     ;[1a28] 3f
                    ret                                     ;[1a29] c9

                    ld        c,a                           ;[1a2a] 4f
                    ld        a,(ix+$03)                    ;[1a2b] dd 7e 03
                    add       c                             ;[1a2e] 81
                    cp        $80                           ;[1a2f] fe 80
                    jp        nc,$1b3c                      ;[1a31] d2 3c 1b
                    ld        c,a                           ;[1a34] 4f
                    ld        a,(ix+$02)                    ;[1a35] dd 7e 02
                    or        a                             ;[1a38] b7
                    jr        nz,$1a49                      ;[1a39] 20 0e
                    ld        a,c                           ;[1a3b] 79
                    cpl                                     ;[1a3c] 2f
                    and       $7f                           ;[1a3d] e6 7f
                    srl       a                             ;[1a3f] cb 3f
                    srl       a                             ;[1a41] cb 3f
                    ld        d,$06                         ;[1a43] 16 06
                    ld        e,a                           ;[1a45] 5f
                    call      $1a86                         ;[1a46] cd 86 1a
                    ld        (ix+$00),c                    ;[1a49] dd 71 00
                    ld        a,(ix+$02)                    ;[1a4c] dd 7e 02
                    cp        $03                           ;[1a4f] fe 03
                    ret       nc                            ;[1a51] d0
                    ld        hl,$1ca0                      ;[1a52] 21 a0 1c
                    ld        b,$00                         ;[1a55] 06 00
                    ld        a,c                           ;[1a57] 79
                    sub       $15                           ;[1a58] d6 15
                    jr        nc,$1a61                      ;[1a5a] 30 05
                    ld        de,$0fbf                      ;[1a5c] 11 bf 0f
                    jr        $1a68                         ;[1a5f] 18 07
                    ld        c,a                           ;[1a61] 4f
                    sla       c                             ;[1a62] cb 21
                    add       hl,bc                         ;[1a64] 09
                    ld        e,(hl)                        ;[1a65] 5e
                    inc       hl                            ;[1a66] 23
                    ld        d,(hl)                        ;[1a67] 56
                    ex        de,hl                         ;[1a68] eb
                    ld        d,(ix+$02)                    ;[1a69] dd 56 02
                    sla       d                             ;[1a6c] cb 22
                    ld        e,l                           ;[1a6e] 5d
                    call      $1a86                         ;[1a6f] cd 86 1a
                    inc       d                             ;[1a72] 14
                    ld        e,h                           ;[1a73] 5c
                    call      $1a86                         ;[1a74] cd 86 1a
                    bit       4,(ix+$04)                    ;[1a77] dd cb 04 66
                    ret       z                             ;[1a7b] c8
                    ld        d,$0d                         ;[1a7c] 16 0d
                    ld        a,(iy+$29)                    ;[1a7e] fd 7e 29
                    ld        e,a                           ;[1a81] 5f
                    call      $1a86                         ;[1a82] cd 86 1a
                    ret                                     ;[1a85] c9

                    push      bc                            ;[1a86] c5
                    ld        bc,$fffd                      ;[1a87] 01 fd ff
                    out       (c),d                         ;[1a8a] ed 51
                    ld        bc,$bffd                      ;[1a8c] 01 fd bf
                    out       (c),e                         ;[1a8f] ed 59
                    pop       bc                            ;[1a91] c1
                    ret                                     ;[1a92] c9

                    push      bc                            ;[1a93] c5
                    ld        bc,$fffd                      ;[1a94] 01 fd ff
                    out       (c),a                         ;[1a97] ed 79
                    in        a,(c)                         ;[1a99] ed 78
                    pop       bc                            ;[1a9b] c1
                    ret                                     ;[1a9c] c9

                    ld        d,$07                         ;[1a9d] 16 07
                    ld        e,$ff                         ;[1a9f] 1e ff
                    call      $1a86                         ;[1aa1] cd 86 1a
                    ld        d,$08                         ;[1aa4] 16 08
                    ld        e,$00                         ;[1aa6] 1e 00
                    call      $1a86                         ;[1aa8] cd 86 1a
                    inc       d                             ;[1aab] 14
                    call      $1a86                         ;[1aac] cd 86 1a
                    inc       d                             ;[1aaf] 14
                    call      $1a86                         ;[1ab0] cd 86 1a
                    call      $1657                         ;[1ab3] cd 57 16
                    rr        (iy+$22)                      ;[1ab6] fd cb 22 1e
                    jr        c,$1ac2                       ;[1aba] 38 06
                    call      $166f                         ;[1abc] cd 6f 16
                    call      $1d97                         ;[1abf] cd 97 1d
                    sla       (iy+$21)                      ;[1ac2] fd cb 21 26
                    jr        c,$1acd                       ;[1ac6] 38 05
                    call      $1676                         ;[1ac8] cd 76 16
                    jr        $1ab6                         ;[1acb] 18 e9
                    ld        iy,$5c3a                      ;[1acd] fd 21 3a 5c
                    ret                                     ;[1ad1] c9

                    push      hl                            ;[1ad2] e5
                    push      de                            ;[1ad3] d5
                    ld        l,(ix+$06)                    ;[1ad4] dd 6e 06
                    ld        h,(ix+$07)                    ;[1ad7] dd 66 07
                    dec       hl                            ;[1ada] 2b
                    ld        a,(hl)                        ;[1adb] 7e
                    cp        $20                           ;[1adc] fe 20
                    jr        z,$1ada                       ;[1ade] 28 fa
                    cp        $0d                           ;[1ae0] fe 0d
                    jr        z,$1ada                       ;[1ae2] 28 f6
                    ld        (ix+$06),l                    ;[1ae4] dd 75 06
                    ld        (ix+$07),h                    ;[1ae7] dd 74 07
                    pop       de                            ;[1aea] d1
                    pop       hl                            ;[1aeb] e1
                    ret                                     ;[1aec] c9

                    push      hl                            ;[1aed] e5
                    push      de                            ;[1aee] d5
                    push      bc                            ;[1aef] c5
                    ld        l,(ix+$06)                    ;[1af0] dd 6e 06
                    ld        h,(ix+$07)                    ;[1af3] dd 66 07
                    ld        a,h                           ;[1af6] 7c
                    cp        (ix+$09)                      ;[1af7] dd be 09
                    jr        nz,$1b05                      ;[1afa] 20 09
                    ld        a,l                           ;[1afc] 7d
                    cp        (ix+$08)                      ;[1afd] dd be 08
                    jr        nz,$1b05                      ;[1b00] 20 03
                    scf                                     ;[1b02] 37
                    jr        $1b0f                         ;[1b03] 18 0a
                    ld        a,(hl)                        ;[1b05] 7e
                    cp        $20                           ;[1b06] fe 20
                    jr        z,$1b13                       ;[1b08] 28 09
                    cp        $0d                           ;[1b0a] fe 0d
                    jr        z,$1b13                       ;[1b0c] 28 05
                    or        a                             ;[1b0e] b7
                    pop       bc                            ;[1b0f] c1
                    pop       de                            ;[1b10] d1
                    pop       hl                            ;[1b11] e1
                    ret                                     ;[1b12] c9

                    inc       hl                            ;[1b13] 23
                    ld        (ix+$06),l                    ;[1b14] dd 75 06
                    ld        (ix+$07),h                    ;[1b17] dd 74 07
                    jr        $1af6                         ;[1b1a] 18 da
                    call      $1a9d                         ;[1b1c] cd 9d 1a
                    ei                                      ;[1b1f] fb
                    call      $2c4c                         ;[1b20] cd 4c 2c
                    add       hl,hl                         ;[1b23] 29
                    call      $1a9d                         ;[1b24] cd 9d 1a
                    ei                                      ;[1b27] fb
                    call      $2c4c                         ;[1b28] cd 4c 2c
                    daa                                     ;[1b2b] 27
                    call      $1a9d                         ;[1b2c] cd 9d 1a
                    ei                                      ;[1b2f] fb
                    call      $2c4c                         ;[1b30] cd 4c 2c
                    ld        h,$cd                         ;[1b33] 26 cd
                    sbc       l                             ;[1b35] 9d
                    ld        a,(de)                        ;[1b36] 1a
                    ei                                      ;[1b37] fb
                    call      $2c4c                         ;[1b38] cd 4c 2c
                    rra                                     ;[1b3b] 1f
                    call      $1a9d                         ;[1b3c] cd 9d 1a
                    ei                                      ;[1b3f] fb
                    call      $2c4c                         ;[1b40] cd 4c 2c
                    jr        z,$1b12                       ;[1b43] 28 cd
                    sbc       l                             ;[1b45] 9d
                    ld        a,(de)                        ;[1b46] 1a
                    ei                                      ;[1b47] fb
                    call      $2c4c                         ;[1b48] cd 4c 2c
                    ld        hl,($57cd)                    ;[1b4b] 2a cd 57
                    ld        d,$fd                         ;[1b4e] 16 fd
                    sla       d                             ;[1b50] cb 22
                    ld        e,$38                         ;[1b52] 1e 38
                    ld        hl,$6fcd                      ;[1b54] 21 cd 6f
                    ld        d,$cd                         ;[1b57] 16 cd
                    exx                                     ;[1b59] d9
                    ld        d,$fe                         ;[1b5a] 16 fe
                    add       b                             ;[1b5c] 80
                    jr        z,$1b76                       ;[1b5d] 28 17
                    call      $1a2a                         ;[1b5f] cd 2a 1a
                    ld        a,(ix+$02)                    ;[1b62] dd 7e 02
                    cp        $03                           ;[1b65] fe 03
                    jr        nc,$1b73                      ;[1b67] 30 0a
                    ld        d,$08                         ;[1b69] 16 08
                    add       d                             ;[1b6b] 82
                    ld        d,a                           ;[1b6c] 57
                    ld        e,(ix+$04)                    ;[1b6d] dd 5e 04
                    call      $1a86                         ;[1b70] cd 86 1a
                    call      $1d78                         ;[1b73] cd 78 1d
                    sla       (iy+$21)                      ;[1b76] fd cb 21 26
                    ret       c                             ;[1b7a] d8
                    call      $1676                         ;[1b7b] cd 76 16
                    jr        $1b4f                         ;[1b7e] 18 cf
                    push      hl                            ;[1b80] e5
                    ld        l,(iy+$27)                    ;[1b81] fd 6e 27
                    ld        h,(iy+$28)                    ;[1b84] fd 66 28
                    ld        bc,$0064                      ;[1b87] 01 64 00
                    or        a                             ;[1b8a] b7
                    sbc       hl,bc                         ;[1b8b] ed 42
                    push      hl                            ;[1b8d] e5
                    pop       bc                            ;[1b8e] c1
                    pop       hl                            ;[1b8f] e1
                    dec       bc                            ;[1b90] 0b
                    ld        a,b                           ;[1b91] 78
                    or        c                             ;[1b92] b1
                    jr        nz,$1b90                      ;[1b93] 20 fb
                    dec       de                            ;[1b95] 1b
                    ld        a,d                           ;[1b96] 7a
                    or        e                             ;[1b97] b3
                    jr        nz,$1b80                      ;[1b98] 20 e6
                    ret                                     ;[1b9a] c9

                    ld        de,$ffff                      ;[1b9b] 11 ff ff
                    call      $1652                         ;[1b9e] cd 52 16
                    rr        (iy+$22)                      ;[1ba1] fd cb 22 1e
                    jr        c,$1bb9                       ;[1ba5] 38 12
                    push      de                            ;[1ba7] d5
                    ld        e,(hl)                        ;[1ba8] 5e
                    inc       hl                            ;[1ba9] 23
                    ld        d,(hl)                        ;[1baa] 56
                    ex        de,hl                         ;[1bab] eb
                    ld        e,(hl)                        ;[1bac] 5e
                    inc       hl                            ;[1bad] 23
                    ld        d,(hl)                        ;[1bae] 56
                    push      de                            ;[1baf] d5
                    pop       hl                            ;[1bb0] e1
                    pop       bc                            ;[1bb1] c1
                    or        a                             ;[1bb2] b7
                    sbc       hl,bc                         ;[1bb3] ed 42
                    jr        c,$1bb9                       ;[1bb5] 38 02
                    push      bc                            ;[1bb7] c5
                    pop       de                            ;[1bb8] d1
                    sla       (iy+$21)                      ;[1bb9] fd cb 21 26
                    jr        c,$1bc4                       ;[1bbd] 38 05
                    call      $1676                         ;[1bbf] cd 76 16
                    jr        $1ba1                         ;[1bc2] 18 dd
                    ld        (iy+$25),e                    ;[1bc4] fd 73 25
                    ld        (iy+$26),d                    ;[1bc7] fd 72 26
                    ret                                     ;[1bca] c9

                    xor       a                             ;[1bcb] af
                    ld        (iy+$2a),a                    ;[1bcc] fd 77 2a
                    call      $1657                         ;[1bcf] cd 57 16
                    rr        (iy+$22)                      ;[1bd2] fd cb 22 1e
                    jp        c,$1c64                       ;[1bd6] da 64 1c
                    call      $166f                         ;[1bd9] cd 6f 16
                    push      iy                            ;[1bdc] fd e5
                    pop       hl                            ;[1bde] e1
                    ld        bc,$0011                      ;[1bdf] 01 11 00
                    add       hl,bc                         ;[1be2] 09
                    ld        b,$00                         ;[1be3] 06 00
                    ld        c,(ix+$02)                    ;[1be5] dd 4e 02
                    sla       c                             ;[1be8] cb 21
                    add       hl,bc                         ;[1bea] 09
                    ld        e,(hl)                        ;[1beb] 5e
                    inc       hl                            ;[1bec] 23
                    ld        d,(hl)                        ;[1bed] 56
                    ex        de,hl                         ;[1bee] eb
                    push      hl                            ;[1bef] e5
                    ld        e,(hl)                        ;[1bf0] 5e
                    inc       hl                            ;[1bf1] 23
                    ld        d,(hl)                        ;[1bf2] 56
                    ex        de,hl                         ;[1bf3] eb
                    ld        e,(iy+$25)                    ;[1bf4] fd 5e 25
                    ld        d,(iy+$26)                    ;[1bf7] fd 56 26
                    or        a                             ;[1bfa] b7
                    sbc       hl,de                         ;[1bfb] ed 52
                    ex        de,hl                         ;[1bfd] eb
                    pop       hl                            ;[1bfe] e1
                    jr        z,$1c06                       ;[1bff] 28 05
                    ld        (hl),e                        ;[1c01] 73
                    inc       hl                            ;[1c02] 23
                    ld        (hl),d                        ;[1c03] 72
                    jr        $1c64                         ;[1c04] 18 5e
                    ld        a,(ix+$02)                    ;[1c06] dd 7e 02
                    cp        $03                           ;[1c09] fe 03
                    jr        nc,$1c16                      ;[1c0b] 30 09
                    ld        d,$08                         ;[1c0d] 16 08
                    add       d                             ;[1c0f] 82
                    ld        d,a                           ;[1c10] 57
                    ld        e,$00                         ;[1c11] 1e 00
                    call      $1a86                         ;[1c13] cd 86 1a
                    call      $1d97                         ;[1c16] cd 97 1d
                    push      ix                            ;[1c19] dd e5
                    pop       hl                            ;[1c1b] e1
                    ld        bc,$0021                      ;[1c1c] 01 21 00
                    add       hl,bc                         ;[1c1f] 09
                    dec       (hl)                          ;[1c20] 35
                    jr        nz,$1c30                      ;[1c21] 20 0d
                    call      $1764                         ;[1c23] cd 64 17
                    ld        a,(iy+$21)                    ;[1c26] fd 7e 21
                    and       (iy+$10)                      ;[1c29] fd a6 10
                    jr        nz,$1c64                      ;[1c2c] 20 36
                    jr        $1c47                         ;[1c2e] 18 17
                    push      iy                            ;[1c30] fd e5
                    pop       hl                            ;[1c32] e1
                    ld        bc,$0011                      ;[1c33] 01 11 00
                    add       hl,bc                         ;[1c36] 09
                    ld        b,$00                         ;[1c37] 06 00
                    ld        c,(ix+$02)                    ;[1c39] dd 4e 02
                    sla       c                             ;[1c3c] cb 21
                    add       hl,bc                         ;[1c3e] 09
                    ld        e,(hl)                        ;[1c3f] 5e
                    inc       hl                            ;[1c40] 23
                    ld        d,(hl)                        ;[1c41] 56
                    inc       de                            ;[1c42] 13
                    inc       de                            ;[1c43] 13
                    ld        (hl),d                        ;[1c44] 72
                    dec       hl                            ;[1c45] 2b
                    ld        (hl),e                        ;[1c46] 73
                    call      $16d9                         ;[1c47] cd d9 16
                    ld        c,a                           ;[1c4a] 4f
                    ld        a,(iy+$21)                    ;[1c4b] fd 7e 21
                    and       (iy+$10)                      ;[1c4e] fd a6 10
                    jr        nz,$1c64                      ;[1c51] 20 11
                    ld        a,c                           ;[1c53] 79
                    cp        $80                           ;[1c54] fe 80
                    jr        z,$1c64                       ;[1c56] 28 0c
                    call      $1a2a                         ;[1c58] cd 2a 1a
                    ld        a,(iy+$21)                    ;[1c5b] fd 7e 21
                    or        (iy+$2a)                      ;[1c5e] fd b6 2a
                    ld        (iy+$2a),a                    ;[1c61] fd 77 2a
                    sla       (iy+$21)                      ;[1c64] fd cb 21 26
                    jr        c,$1c70                       ;[1c68] 38 06
                    call      $1676                         ;[1c6a] cd 76 16
                    jp        $1bd2                         ;[1c6d] c3 d2 1b
                    ld        de,$0001                      ;[1c70] 11 01 00
                    call      $1b80                         ;[1c73] cd 80 1b
                    call      $1657                         ;[1c76] cd 57 16
                    rr        (iy+$2a)                      ;[1c79] fd cb 2a 1e
                    jr        nc,$1c96                      ;[1c7d] 30 17
                    call      $166f                         ;[1c7f] cd 6f 16
                    ld        a,(ix+$02)                    ;[1c82] dd 7e 02
                    cp        $03                           ;[1c85] fe 03
                    jr        nc,$1c93                      ;[1c87] 30 0a
                    ld        d,$08                         ;[1c89] 16 08
                    add       d                             ;[1c8b] 82
                    ld        d,a                           ;[1c8c] 57
                    ld        e,(ix+$04)                    ;[1c8d] dd 5e 04
                    call      $1a86                         ;[1c90] cd 86 1a
                    call      $1d78                         ;[1c93] cd 78 1d
                    sla       (iy+$21)                      ;[1c96] fd cb 21 26
                    ret       c                             ;[1c9a] d8
                    call      $1676                         ;[1c9b] cd 76 16
                    jr        $1c79                         ;[1c9e] 18 d9
                    cp        a                             ;[1ca0] bf
                    rrca                                    ;[1ca1] 0f
                    call      c,$070e                       ;[1ca2] dc 0e 07
                    ld        c,$3d                         ;[1ca5] 0e 3d
                    dec       c                             ;[1ca7] 0d
                    ld        a,a                           ;[1ca8] 7f
                    inc       c                             ;[1ca9] 0c
                    call      z,$220b                       ;[1caa] cc 0b 22
                    dec       bc                            ;[1cad] 0b
                    add       d                             ;[1cae] 82
                    ld        a,(bc)                        ;[1caf] 0a
                    ex        de,hl                         ;[1cb0] eb
                    add       hl,bc                         ;[1cb1] 09
                    ld        e,l                           ;[1cb2] 5d
                    add       hl,bc                         ;[1cb3] 09
                    sub       $08                           ;[1cb4] d6 08
                    ld        d,a                           ;[1cb6] 57
                    ex        af,af'                        ;[1cb7] 08
                    rst       $18                           ;[1cb8] df
                    rlca                                    ;[1cb9] 07
                    ld        l,(hl)                        ;[1cba] 6e
                    rlca                                    ;[1cbb] 07
                    inc       bc                            ;[1cbc] 03
                    rlca                                    ;[1cbd] 07
                    sbc       a                             ;[1cbe] 9f
                    ld        b,$40                         ;[1cbf] 06 40
                    ld        b,$e6                         ;[1cc1] 06 e6
                    dec       b                             ;[1cc3] 05
                    sub       c                             ;[1cc4] 91
                    dec       b                             ;[1cc5] 05
                    ld        b,c                           ;[1cc6] 41
                    dec       b                             ;[1cc7] 05
                    or        $04                           ;[1cc8] f6 04
                    xor       (hl)                          ;[1cca] ae
                    inc       b                             ;[1ccb] 04
                    ld        l,e                           ;[1ccc] 6b
                    inc       b                             ;[1ccd] 04
                    inc       l                             ;[1cce] 2c
                    inc       b                             ;[1ccf] 04
                    ret       p                             ;[1cd0] f0
                    inc       bc                            ;[1cd1] 03
                    or        a                             ;[1cd2] b7
                    inc       bc                            ;[1cd3] 03
                    add       d                             ;[1cd4] 82
                    inc       bc                            ;[1cd5] 03
                    ld        c,a                           ;[1cd6] 4f
                    inc       bc                            ;[1cd7] 03
                    jr        nz,$1cdd                      ;[1cd8] 20 03
                    di                                      ;[1cda] f3
                    ld        (bc),a                        ;[1cdb] 02
                    ret       z                             ;[1cdc] c8
                    ld        (bc),a                        ;[1cdd] 02
                    and       c                             ;[1cde] a1
                    ld        (bc),a                        ;[1cdf] 02
                    ld        a,e                           ;[1ce0] 7b
                    ld        (bc),a                        ;[1ce1] 02
                    ld        d,a                           ;[1ce2] 57
                    ld        (bc),a                        ;[1ce3] 02
                    ld        (hl),$02                      ;[1ce4] 36 02
                    ld        d,$02                         ;[1ce6] 16 02
                    ret       m                             ;[1ce8] f8
                    ld        bc,$01dc                      ;[1ce9] 01 dc 01
                    pop       bc                            ;[1cec] c1
                    ld        bc,$01a8                      ;[1ced] 01 a8 01
                    sub       b                             ;[1cf0] 90
                    ld        bc,$0179                      ;[1cf1] 01 79 01
                    ld        h,h                           ;[1cf4] 64
                    ld        bc,$0150                      ;[1cf5] 01 50 01
                    dec       a                             ;[1cf8] 3d
                    ld        bc,$012c                      ;[1cf9] 01 2c 01
                    dec       de                            ;[1cfc] 1b
                    ld        bc,$010b                      ;[1cfd] 01 0b 01
                    call      m,$ee00                       ;[1d00] fc 00 ee
                    nop                                     ;[1d03] 00
                    ret       po                            ;[1d04] e0
                    nop                                     ;[1d05] 00
                    call      nc,$c800                      ;[1d06] d4 00 c8
                    nop                                     ;[1d09] 00
                    cp        l                             ;[1d0a] bd
                    nop                                     ;[1d0b] 00
                    or        d                             ;[1d0c] b2
                    nop                                     ;[1d0d] 00
                    xor       b                             ;[1d0e] a8
                    nop                                     ;[1d0f] 00
                    sbc       a                             ;[1d10] 9f
                    nop                                     ;[1d11] 00
                    sub       (hl)                          ;[1d12] 96
                    nop                                     ;[1d13] 00
                    adc       l                             ;[1d14] 8d
                    nop                                     ;[1d15] 00
                    add       l                             ;[1d16] 85
                    nop                                     ;[1d17] 00
                    ld        a,(hl)                        ;[1d18] 7e
                    nop                                     ;[1d19] 00
                    ld        (hl),a                        ;[1d1a] 77
                    nop                                     ;[1d1b] 00
                    ld        (hl),b                        ;[1d1c] 70
                    nop                                     ;[1d1d] 00
                    ld        l,d                           ;[1d1e] 6a
                    nop                                     ;[1d1f] 00
                    ld        h,h                           ;[1d20] 64
                    nop                                     ;[1d21] 00
                    ld        e,(hl)                        ;[1d22] 5e
                    nop                                     ;[1d23] 00
                    ld        e,c                           ;[1d24] 59
                    nop                                     ;[1d25] 00
                    ld        d,h                           ;[1d26] 54
                    nop                                     ;[1d27] 00
                    ld        c,a                           ;[1d28] 4f
                    nop                                     ;[1d29] 00
                    ld        c,e                           ;[1d2a] 4b
                    nop                                     ;[1d2b] 00
                    ld        b,a                           ;[1d2c] 47
                    nop                                     ;[1d2d] 00
                    ld        b,e                           ;[1d2e] 43
                    nop                                     ;[1d2f] 00
                    ccf                                     ;[1d30] 3f
                    nop                                     ;[1d31] 00
                    dec       sp                            ;[1d32] 3b
                    nop                                     ;[1d33] 00
                    jr        c,$1d36                       ;[1d34] 38 00
                    dec       (hl)                          ;[1d36] 35
                    nop                                     ;[1d37] 00
                    ld        ($2f00),a                     ;[1d38] 32 00 2f
                    nop                                     ;[1d3b] 00
                    dec       l                             ;[1d3c] 2d
                    nop                                     ;[1d3d] 00
                    ld        hl,($2800)                    ;[1d3e] 2a 00 28
                    nop                                     ;[1d41] 00
                    dec       h                             ;[1d42] 25
                    nop                                     ;[1d43] 00
                    inc       hl                            ;[1d44] 23
                    nop                                     ;[1d45] 00
                    ld        hl,$1f00                      ;[1d46] 21 00 1f
                    nop                                     ;[1d49] 00
                    ld        e,$00                         ;[1d4a] 1e 00
                    inc       e                             ;[1d4c] 1c
                    nop                                     ;[1d4d] 00
                    ld        a,(de)                        ;[1d4e] 1a
                    nop                                     ;[1d4f] 00
                    add       hl,de                         ;[1d50] 19
                    nop                                     ;[1d51] 00
                    jr        $1d54                         ;[1d52] 18 00
                    ld        d,$00                         ;[1d54] 16 00
                    dec       d                             ;[1d56] 15
                    nop                                     ;[1d57] 00
                    inc       d                             ;[1d58] 14
                    nop                                     ;[1d59] 00
                    inc       de                            ;[1d5a] 13
                    nop                                     ;[1d5b] 00
                    ld        (de),a                        ;[1d5c] 12
                    nop                                     ;[1d5d] 00
                    ld        de,$1000                      ;[1d5e] 11 00 10
                    nop                                     ;[1d61] 00
                    rrca                                    ;[1d62] 0f
                    nop                                     ;[1d63] 00
                    ld        c,$00                         ;[1d64] 0e 00
                    dec       c                             ;[1d66] 0d
                    nop                                     ;[1d67] 00
                    inc       c                             ;[1d68] 0c
                    nop                                     ;[1d69] 00
                    inc       c                             ;[1d6a] 0c
                    nop                                     ;[1d6b] 00
                    dec       bc                            ;[1d6c] 0b
                    nop                                     ;[1d6d] 00
                    dec       bc                            ;[1d6e] 0b
                    nop                                     ;[1d6f] 00
                    ld        a,(bc)                        ;[1d70] 0a
                    nop                                     ;[1d71] 00
                    add       hl,bc                         ;[1d72] 09
                    nop                                     ;[1d73] 00
                    add       hl,bc                         ;[1d74] 09
                    nop                                     ;[1d75] 00
                    ex        af,af'                        ;[1d76] 08
                    nop                                     ;[1d77] 00
                    ld        a,(ix+$01)                    ;[1d78] dd 7e 01
                    or        a                             ;[1d7b] b7
                    ret       m                             ;[1d7c] f8
                    or        $90                           ;[1d7d] f6 90
                    call      $1dad                         ;[1d7f] cd ad 1d
                    ld        a,(ix+$00)                    ;[1d82] dd 7e 00
                    call      $1dad                         ;[1d85] cd ad 1d
                    ld        a,(ix+$04)                    ;[1d88] dd 7e 04
                    res       4,a                           ;[1d8b] cb a7
                    sla       a                             ;[1d8d] cb 27
                    sla       a                             ;[1d8f] cb 27
                    sla       a                             ;[1d91] cb 27
                    call      $1dad                         ;[1d93] cd ad 1d
                    ret                                     ;[1d96] c9

                    ld        a,(ix+$01)                    ;[1d97] dd 7e 01
                    or        a                             ;[1d9a] b7
                    ret       m                             ;[1d9b] f8
                    or        $80                           ;[1d9c] f6 80
                    call      $1dad                         ;[1d9e] cd ad 1d
                    ld        a,(ix+$00)                    ;[1da1] dd 7e 00
                    call      $1dad                         ;[1da4] cd ad 1d
                    ld        a,$40                         ;[1da7] 3e 40
                    call      $1dad                         ;[1da9] cd ad 1d
                    ret                                     ;[1dac] c9

                    ld        l,a                           ;[1dad] 6f
                    ld        bc,$fffd                      ;[1dae] 01 fd ff
                    ld        a,$0e                         ;[1db1] 3e 0e
                    out       (c),a                         ;[1db3] ed 79
                    ld        bc,$bffd                      ;[1db5] 01 fd bf
                    ld        a,$fa                         ;[1db8] 3e fa
                    out       (c),a                         ;[1dba] ed 79
                    ld        e,$03                         ;[1dbc] 1e 03
                    dec       e                             ;[1dbe] 1d
                    jr        nz,$1dbe                      ;[1dbf] 20 fd
                    nop                                     ;[1dc1] 00
                    nop                                     ;[1dc2] 00
                    nop                                     ;[1dc3] 00
                    nop                                     ;[1dc4] 00
                    ld        a,l                           ;[1dc5] 7d
                    ld        d,$08                         ;[1dc6] 16 08
                    rra                                     ;[1dc8] 1f
                    ld        l,a                           ;[1dc9] 6f
                    jp        nc,$1dd3                      ;[1dca] d2 d3 1d
                    ld        a,$fe                         ;[1dcd] 3e fe
                    out       (c),a                         ;[1dcf] ed 79
                    jr        $1dd9                         ;[1dd1] 18 06
                    ld        a,$fa                         ;[1dd3] 3e fa
                    out       (c),a                         ;[1dd5] ed 79
                    jr        $1dd9                         ;[1dd7] 18 00
                    ld        e,$02                         ;[1dd9] 1e 02
                    dec       e                             ;[1ddb] 1d
                    jr        nz,$1ddb                      ;[1ddc] 20 fd
                    nop                                     ;[1dde] 00
                    add       $00                           ;[1ddf] c6 00
                    ld        a,l                           ;[1de1] 7d
                    dec       d                             ;[1de2] 15
                    jr        nz,$1dc8                      ;[1de3] 20 e3
                    nop                                     ;[1de5] 00
                    nop                                     ;[1de6] 00
                    add       $00                           ;[1de7] c6 00
                    nop                                     ;[1de9] 00
                    nop                                     ;[1dea] 00
                    ld        a,$fe                         ;[1deb] 3e fe
                    out       (c),a                         ;[1ded] ed 79
                    ld        e,$06                         ;[1def] 1e 06
                    dec       e                             ;[1df1] 1d
                    jr        nz,$1df1                      ;[1df2] 20 fd
                    ret                                     ;[1df4] c9

                    rst       $28                           ;[1df5] ef
                    jr        $1df8                         ;[1df6] 18 00
                    rst       $28                           ;[1df8] ef
                    adc       h                             ;[1df9] 8c
                    inc       e                             ;[1dfa] 1c
                    bit       7,(iy+$01)                    ;[1dfb] fd cb 01 7e
                    jr        z,$1e15                       ;[1dff] 28 14
                    rst       $28                           ;[1e01] ef
                    pop       af                            ;[1e02] f1
                    dec       hl                            ;[1e03] 2b
                    ld        a,c                           ;[1e04] 79
                    dec       a                             ;[1e05] 3d
                    or        b                             ;[1e06] b0
                    jr        z,$1e0d                       ;[1e07] 28 04
                    call      $2c4c                         ;[1e09] cd 4c 2c
                    inc       h                             ;[1e0c] 24
                    ld        a,(de)                        ;[1e0d] 1a
                    and       $df                           ;[1e0e] e6 df
                    cp        $50                           ;[1e10] fe 50
                    jp        nz,$1141                      ;[1e12] c2 41 11
                    ld        hl,($5c5d)                    ;[1e15] 2a 5d 5c
                    ld        a,(hl)                        ;[1e18] 7e
                    cp        $3b                           ;[1e19] fe 3b
                    jp        nz,$1141                      ;[1e1b] c2 41 11
                    rst       $28                           ;[1e1e] ef
                    jr        nz,$1e21                      ;[1e1f] 20 00
                    rst       $28                           ;[1e21] ef
                    add       d                             ;[1e22] 82
                    inc       e                             ;[1e23] 1c
                    bit       7,(iy+$01)                    ;[1e24] fd cb 01 7e
                    jr        z,$1e31                       ;[1e28] 28 07
                    rst       $28                           ;[1e2a] ef
                    sbc       c                             ;[1e2b] 99
                    ld        e,$ed                         ;[1e2c] 1e ed
                    ld        b,e                           ;[1e2e] 43
                    ld        e,a                           ;[1e2f] 5f
                    ld        e,e                           ;[1e30] 5b
                    rst       $28                           ;[1e31] ef
                    jr        $1e34                         ;[1e32] 18 00
                    cp        $0d                           ;[1e34] fe 0d
                    jr        z,$1e3d                       ;[1e36] 28 05
                    cp        $3a                           ;[1e38] fe 3a
                    jp        nz,$1141                      ;[1e3a] c2 41 11
                    call      $10cd                         ;[1e3d] cd cd 10
                    ld        bc,($5b5f)                    ;[1e40] ed 4b 5f 5b
                    ld        a,b                           ;[1e44] 78
                    or        c                             ;[1e45] b1
                    jr        nz,$1e4c                      ;[1e46] 20 04
                    call      $2c4c                         ;[1e48] cd 4c 2c
                    dec       h                             ;[1e4b] 25
                    ld        hl,$1e6c                      ;[1e4c] 21 6c 1e
                    ld        e,(hl)                        ;[1e4f] 5e
                    inc       hl                            ;[1e50] 23
                    ld        d,(hl)                        ;[1e51] 56
                    inc       hl                            ;[1e52] 23
                    ex        de,hl                         ;[1e53] eb
                    ld        a,h                           ;[1e54] 7c
                    cp        $25                           ;[1e55] fe 25
                    jr        nc,$1e63                      ;[1e57] 30 0a
                    and       a                             ;[1e59] a7
                    sbc       hl,bc                         ;[1e5a] ed 42
                    jr        nc,$1e63                      ;[1e5c] 30 05
                    ex        de,hl                         ;[1e5e] eb
                    inc       hl                            ;[1e5f] 23
                    inc       hl                            ;[1e60] 23
                    jr        $1e4f                         ;[1e61] 18 ec
                    ex        de,hl                         ;[1e63] eb
                    ld        e,(hl)                        ;[1e64] 5e
                    inc       hl                            ;[1e65] 23
                    ld        d,(hl)                        ;[1e66] 56
                    ld        ($5b5f),de                    ;[1e67] ed 53 5f 5b
                    ret                                     ;[1e6b] c9

                    ld        ($a500),a                     ;[1e6c] 32 00 a5
                    ld        a,(bc)                        ;[1e6f] 0a
                    ld        l,(hl)                        ;[1e70] 6e
                    nop                                     ;[1e71] 00
                    call      nc,$2c04                      ;[1e72] d4 04 2c
                    ld        bc,$01c3                      ;[1e75] 01 c3 01
                    ld        e,b                           ;[1e78] 58
                    ld        (bc),a                        ;[1e79] 02
                    ret       po                            ;[1e7a] e0
                    nop                                     ;[1e7b] 00
                    or        b                             ;[1e7c] b0
                    inc       b                             ;[1e7d] 04
                    ld        l,(hl)                        ;[1e7e] 6e
                    nop                                     ;[1e7f] 00
                    ld        h,b                           ;[1e80] 60
                    add       hl,bc                         ;[1e81] 09
                    ld        (hl),$00                      ;[1e82] 36 00
                    ret       nz                            ;[1e84] c0
                    ld        (de),a                        ;[1e85] 12
                    add       hl,de                         ;[1e86] 19
                    nop                                     ;[1e87] 00
                    add       b                             ;[1e88] 80
                    dec       h                             ;[1e89] 25
                    dec       bc                            ;[1e8a] 0b
                    nop                                     ;[1e8b] 00
                    ld        hl,$5b66                      ;[1e8c] 21 66 5b
                    bit       3,(hl)                        ;[1e8f] cb 5e
                    jp        z,$1ea1                       ;[1e91] ca a1 1e
                    ld        hl,$5b61                      ;[1e94] 21 61 5b
                    ld        a,(hl)                        ;[1e97] 7e
                    and       a                             ;[1e98] a7
                    jr        z,$1ea5                       ;[1e99] 28 0a
                    ld        (hl),$00                      ;[1e9b] 36 00
                    inc       hl                            ;[1e9d] 23
                    ld        a,(hl)                        ;[1e9e] 7e
                    scf                                     ;[1e9f] 37
                    ret                                     ;[1ea0] c9

                    rst       $28                           ;[1ea1] ef
                    call      nz,$c915                      ;[1ea2] c4 15 c9
                    call      $2c6b                         ;[1ea5] cd 6b 2c
                    ld        hl,$5c6a                      ;[1ea8] 21 6a 5c
                    bit       6,(hl)                        ;[1eab] cb 76
                    jr        z,$1eb2                       ;[1ead] 28 03
                    jp        $1f94                         ;[1eaf] c3 94 1f
                    di                                      ;[1eb2] f3
                    exx                                     ;[1eb3] d9
                    ld        de,($5b5f)                    ;[1eb4] ed 5b 5f 5b
                    ld        hl,($5b5f)                    ;[1eb8] 2a 5f 5b
                    srl       h                             ;[1ebb] cb 3c
                    rr        l                             ;[1ebd] cb 1d
                    or        a                             ;[1ebf] b7
                    ld        b,$fa                         ;[1ec0] 06 fa
                    exx                                     ;[1ec2] d9
                    ld        c,$fd                         ;[1ec3] 0e fd
                    ld        d,$ff                         ;[1ec5] 16 ff
                    ld        e,$bf                         ;[1ec7] 1e bf
                    ld        b,d                           ;[1ec9] 42
                    ld        a,$0e                         ;[1eca] 3e 0e
                    out       (c),a                         ;[1ecc] ed 79
                    in        a,(c)                         ;[1ece] ed 78
                    or        $f0                           ;[1ed0] f6 f0
                    and       $fb                           ;[1ed2] e6 fb
                    ld        b,e                           ;[1ed4] 43
                    out       (c),a                         ;[1ed5] ed 79
                    ld        h,a                           ;[1ed7] 67
                    ld        b,d                           ;[1ed8] 42
                    in        a,(c)                         ;[1ed9] ed 78
                    and       $80                           ;[1edb] e6 80
                    jr        z,$1ee8                       ;[1edd] 28 09
                    exx                                     ;[1edf] d9
                    dec       b                             ;[1ee0] 05
                    exx                                     ;[1ee1] d9
                    jr        nz,$1ed8                      ;[1ee2] 20 f4
                    xor       a                             ;[1ee4] af
                    push      af                            ;[1ee5] f5
                    jr        $1f21                         ;[1ee6] 18 39
                    in        a,(c)                         ;[1ee8] ed 78
                    and       $80                           ;[1eea] e6 80
                    jr        nz,$1edf                      ;[1eec] 20 f1
                    in        a,(c)                         ;[1eee] ed 78
                    and       $80                           ;[1ef0] e6 80
                    jr        nz,$1edf                      ;[1ef2] 20 eb
                    exx                                     ;[1ef4] d9
                    ld        bc,$fffd                      ;[1ef5] 01 fd ff
                    ld        a,$80                         ;[1ef8] 3e 80
                    ex        af,af'                        ;[1efa] 08
                    add       hl,de                         ;[1efb] 19
                    nop                                     ;[1efc] 00
                    nop                                     ;[1efd] 00
                    nop                                     ;[1efe] 00
                    nop                                     ;[1eff] 00
                    dec       hl                            ;[1f00] 2b
                    ld        a,h                           ;[1f01] 7c
                    or        l                             ;[1f02] b5
                    jr        nz,$1f00                      ;[1f03] 20 fb
                    in        a,(c)                         ;[1f05] ed 78
                    and       $80                           ;[1f07] e6 80
                    jp        z,$1f15                       ;[1f09] ca 15 1f
                    ex        af,af'                        ;[1f0c] 08
                    scf                                     ;[1f0d] 37
                    rra                                     ;[1f0e] 1f
                    jr        c,$1f1e                       ;[1f0f] 38 0d
                    ex        af,af'                        ;[1f11] 08
                    jp        $1efb                         ;[1f12] c3 fb 1e
                    ex        af,af'                        ;[1f15] 08
                    or        a                             ;[1f16] b7
                    rra                                     ;[1f17] 1f
                    jr        c,$1f1e                       ;[1f18] 38 04
                    ex        af,af'                        ;[1f1a] 08
                    jp        $1efb                         ;[1f1b] c3 fb 1e
                    scf                                     ;[1f1e] 37
                    push      af                            ;[1f1f] f5
                    exx                                     ;[1f20] d9
                    ld        a,h                           ;[1f21] 7c
                    or        $04                           ;[1f22] f6 04
                    ld        b,e                           ;[1f24] 43
                    out       (c),a                         ;[1f25] ed 79
                    exx                                     ;[1f27] d9
                    ld        h,d                           ;[1f28] 62
                    ld        l,e                           ;[1f29] 6b
                    ld        bc,$0007                      ;[1f2a] 01 07 00
                    or        a                             ;[1f2d] b7
                    sbc       hl,bc                         ;[1f2e] ed 42
                    dec       hl                            ;[1f30] 2b
                    ld        a,h                           ;[1f31] 7c
                    or        l                             ;[1f32] b5
                    jr        nz,$1f30                      ;[1f33] 20 fb
                    ld        bc,$fffd                      ;[1f35] 01 fd ff
                    add       hl,de                         ;[1f38] 19
                    add       hl,de                         ;[1f39] 19
                    add       hl,de                         ;[1f3a] 19
                    in        a,(c)                         ;[1f3b] ed 78
                    and       $80                           ;[1f3d] e6 80
                    jr        z,$1f49                       ;[1f3f] 28 08
                    dec       hl                            ;[1f41] 2b
                    ld        a,h                           ;[1f42] 7c
                    or        l                             ;[1f43] b5
                    jr        nz,$1f3b                      ;[1f44] 20 f5
                    pop       af                            ;[1f46] f1
                    ei                                      ;[1f47] fb
                    ret                                     ;[1f48] c9

                    in        a,(c)                         ;[1f49] ed 78
                    and       $80                           ;[1f4b] e6 80
                    jr        nz,$1f3b                      ;[1f4d] 20 ec
                    in        a,(c)                         ;[1f4f] ed 78
                    and       $80                           ;[1f51] e6 80
                    jr        nz,$1f3b                      ;[1f53] 20 e6
                    ld        h,d                           ;[1f55] 62
                    ld        l,e                           ;[1f56] 6b
                    ld        bc,$0002                      ;[1f57] 01 02 00
                    srl       h                             ;[1f5a] cb 3c
                    rr        l                             ;[1f5c] cb 1d
                    or        a                             ;[1f5e] b7
                    sbc       hl,bc                         ;[1f5f] ed 42
                    ld        bc,$fffd                      ;[1f61] 01 fd ff
                    ld        a,$80                         ;[1f64] 3e 80
                    ex        af,af'                        ;[1f66] 08
                    nop                                     ;[1f67] 00
                    nop                                     ;[1f68] 00
                    nop                                     ;[1f69] 00
                    nop                                     ;[1f6a] 00
                    add       hl,de                         ;[1f6b] 19
                    dec       hl                            ;[1f6c] 2b
                    ld        a,h                           ;[1f6d] 7c
                    or        l                             ;[1f6e] b5
                    jr        nz,$1f6c                      ;[1f6f] 20 fb
                    in        a,(c)                         ;[1f71] ed 78
                    and       $80                           ;[1f73] e6 80
                    jp        z,$1f81                       ;[1f75] ca 81 1f
                    ex        af,af'                        ;[1f78] 08
                    scf                                     ;[1f79] 37
                    rra                                     ;[1f7a] 1f
                    jr        c,$1f8a                       ;[1f7b] 38 0d
                    ex        af,af'                        ;[1f7d] 08
                    jp        $1f67                         ;[1f7e] c3 67 1f
                    ex        af,af'                        ;[1f81] 08
                    or        a                             ;[1f82] b7
                    rra                                     ;[1f83] 1f
                    jr        c,$1f8a                       ;[1f84] 38 04
                    ex        af,af'                        ;[1f86] 08
                    jp        $1f67                         ;[1f87] c3 67 1f
                    ld        hl,$5b61                      ;[1f8a] 21 61 5b
                    ld        (hl),$01                      ;[1f8d] 36 01
                    inc       hl                            ;[1f8f] 23
                    ld        (hl),a                        ;[1f90] 77
                    pop       af                            ;[1f91] f1
                    ei                                      ;[1f92] fb
                    ret                                     ;[1f93] c9

                    di                                      ;[1f94] f3
                    exx                                     ;[1f95] d9
                    ld        de,($5b5f)                    ;[1f96] ed 5b 5f 5b
                    ld        hl,($5b5f)                    ;[1f9a] 2a 5f 5b
                    srl       h                             ;[1f9d] cb 3c
                    rr        l                             ;[1f9f] cb 1d
                    or        a                             ;[1fa1] b7
                    ld        b,$fa                         ;[1fa2] 06 fa
                    exx                                     ;[1fa4] d9
                    ld        c,$fd                         ;[1fa5] 0e fd
                    ld        d,$ff                         ;[1fa7] 16 ff
                    ld        e,$bf                         ;[1fa9] 1e bf
                    ld        b,d                           ;[1fab] 42
                    ld        a,$0e                         ;[1fac] 3e 0e
                    out       (c),a                         ;[1fae] ed 79
                    in        a,(c)                         ;[1fb0] ed 78
                    or        $f0                           ;[1fb2] f6 f0
                    and       $fd                           ;[1fb4] e6 fd
                    ld        b,e                           ;[1fb6] 43
                    out       (c),a                         ;[1fb7] ed 79
                    ld        h,a                           ;[1fb9] 67
                    ld        b,d                           ;[1fba] 42
                    in        a,(c)                         ;[1fbb] ed 78
                    and       $10                           ;[1fbd] e6 10
                    jr        z,$1fca                       ;[1fbf] 28 09
                    exx                                     ;[1fc1] d9
                    dec       b                             ;[1fc2] 05
                    exx                                     ;[1fc3] d9
                    jr        nz,$1fba                      ;[1fc4] 20 f4
                    xor       a                             ;[1fc6] af
                    push      af                            ;[1fc7] f5
                    jr        $2003                         ;[1fc8] 18 39
                    in        a,(c)                         ;[1fca] ed 78
                    and       $10                           ;[1fcc] e6 10
                    jr        nz,$1fc1                      ;[1fce] 20 f1
                    in        a,(c)                         ;[1fd0] ed 78
                    and       $10                           ;[1fd2] e6 10
                    jr        nz,$1fc1                      ;[1fd4] 20 eb
                    exx                                     ;[1fd6] d9
                    ld        bc,$fffd                      ;[1fd7] 01 fd ff
                    ld        a,$80                         ;[1fda] 3e 80
                    ex        af,af'                        ;[1fdc] 08
                    add       hl,de                         ;[1fdd] 19
                    nop                                     ;[1fde] 00
                    nop                                     ;[1fdf] 00
                    nop                                     ;[1fe0] 00
                    nop                                     ;[1fe1] 00
                    dec       hl                            ;[1fe2] 2b
                    ld        a,h                           ;[1fe3] 7c
                    or        l                             ;[1fe4] b5
                    jr        nz,$1fe2                      ;[1fe5] 20 fb
                    in        a,(c)                         ;[1fe7] ed 78
                    and       $10                           ;[1fe9] e6 10
                    jp        z,$1ff7                       ;[1feb] ca f7 1f
                    ex        af,af'                        ;[1fee] 08
                    scf                                     ;[1fef] 37
                    rra                                     ;[1ff0] 1f
                    jr        c,$2000                       ;[1ff1] 38 0d
                    ex        af,af'                        ;[1ff3] 08
                    jp        $1fdd                         ;[1ff4] c3 dd 1f
                    ex        af,af'                        ;[1ff7] 08
                    or        a                             ;[1ff8] b7
                    rra                                     ;[1ff9] 1f
                    jr        c,$2000                       ;[1ffa] 38 04
                    ex        af,af'                        ;[1ffc] 08
                    jp        $1fdd                         ;[1ffd] c3 dd 1f
                    scf                                     ;[2000] 37
                    push      af                            ;[2001] f5
                    exx                                     ;[2002] d9
                    ld        a,h                           ;[2003] 7c
                    or        $02                           ;[2004] f6 02
                    ld        b,e                           ;[2006] 43
                    out       (c),a                         ;[2007] ed 79
                    exx                                     ;[2009] d9
                    ld        h,d                           ;[200a] 62
                    ld        l,e                           ;[200b] 6b
                    ld        bc,$0007                      ;[200c] 01 07 00
                    or        a                             ;[200f] b7
                    sbc       hl,bc                         ;[2010] ed 42
                    dec       hl                            ;[2012] 2b
                    ld        a,h                           ;[2013] 7c
                    or        l                             ;[2014] b5
                    jr        nz,$2012                      ;[2015] 20 fb
                    ld        bc,$fffd                      ;[2017] 01 fd ff
                    add       hl,de                         ;[201a] 19
                    add       hl,de                         ;[201b] 19
                    add       hl,de                         ;[201c] 19
                    in        a,(c)                         ;[201d] ed 78
                    and       $10                           ;[201f] e6 10
                    jr        z,$202b                       ;[2021] 28 08
                    dec       hl                            ;[2023] 2b
                    ld        a,h                           ;[2024] 7c
                    or        l                             ;[2025] b5
                    jr        nz,$201d                      ;[2026] 20 f5
                    pop       af                            ;[2028] f1
                    ei                                      ;[2029] fb
                    ret                                     ;[202a] c9

                    in        a,(c)                         ;[202b] ed 78
                    and       $10                           ;[202d] e6 10
                    jr        nz,$201d                      ;[202f] 20 ec
                    in        a,(c)                         ;[2031] ed 78
                    and       $10                           ;[2033] e6 10
                    jr        nz,$201d                      ;[2035] 20 e6
                    ld        h,d                           ;[2037] 62
                    ld        l,e                           ;[2038] 6b
                    ld        bc,$0002                      ;[2039] 01 02 00
                    srl       h                             ;[203c] cb 3c
                    rr        l                             ;[203e] cb 1d
                    or        a                             ;[2040] b7
                    sbc       hl,bc                         ;[2041] ed 42
                    ld        bc,$fffd                      ;[2043] 01 fd ff
                    ld        a,$80                         ;[2046] 3e 80
                    ex        af,af'                        ;[2048] 08
                    nop                                     ;[2049] 00
                    nop                                     ;[204a] 00
                    nop                                     ;[204b] 00
                    nop                                     ;[204c] 00
                    add       hl,de                         ;[204d] 19
                    dec       hl                            ;[204e] 2b
                    ld        a,h                           ;[204f] 7c
                    or        l                             ;[2050] b5
                    jr        nz,$204e                      ;[2051] 20 fb
                    in        a,(c)                         ;[2053] ed 78
                    and       $10                           ;[2055] e6 10
                    jp        z,$2063                       ;[2057] ca 63 20
                    ex        af,af'                        ;[205a] 08
                    scf                                     ;[205b] 37
                    rra                                     ;[205c] 1f
                    jr        c,$206c                       ;[205d] 38 0d
                    ex        af,af'                        ;[205f] 08
                    jp        $2049                         ;[2060] c3 49 20
                    ex        af,af'                        ;[2063] 08
                    or        a                             ;[2064] b7
                    rra                                     ;[2065] 1f
                    jr        c,$206c                       ;[2066] 38 04
                    ex        af,af'                        ;[2068] 08
                    jp        $2049                         ;[2069] c3 49 20
                    ld        hl,$5b61                      ;[206c] 21 61 5b
                    ld        (hl),$01                      ;[206f] 36 01
                    inc       hl                            ;[2071] 23
                    ld        (hl),a                        ;[2072] 77
                    pop       af                            ;[2073] f1
                    ei                                      ;[2074] fb
                    ret                                     ;[2075] c9

                    push      hl                            ;[2076] e5
                    ld        hl,$5b66                      ;[2077] 21 66 5b
                    bit       2,(hl)                        ;[207a] cb 56
                    pop       hl                            ;[207c] e1
                    jp        z,$2159                       ;[207d] ca 59 21
                    push      af                            ;[2080] f5
                    ld        a,($5b65)                     ;[2081] 3a 65 5b
                    or        a                             ;[2084] b7
                    jr        z,$2096                       ;[2085] 28 0f
                    dec       a                             ;[2087] 3d
                    ld        ($5b65),a                     ;[2088] 32 65 5b
                    jr        nz,$2091                      ;[208b] 20 04
                    pop       af                            ;[208d] f1
                    jp        $2128                         ;[208e] c3 28 21
                    pop       af                            ;[2091] f1
                    ld        ($5c0f),a                     ;[2092] 32 0f 5c
                    ret                                     ;[2095] c9

                    pop       af                            ;[2096] f1
                    cp        $a3                           ;[2097] fe a3
                    jr        c,$20a8                       ;[2099] 38 0d
                    ld        hl,($5b5a)                    ;[209b] 2a 5a 5b
                    push      hl                            ;[209e] e5
                    rst       $28                           ;[209f] ef
                    ld        d,d                           ;[20a0] 52
                    dec       bc                            ;[20a1] 0b
                    pop       hl                            ;[20a2] e1
                    ld        ($5b5a),hl                    ;[20a3] 22 5a 5b
                    scf                                     ;[20a6] 37
                    ret                                     ;[20a7] c9

                    ld        hl,$5c3b                      ;[20a8] 21 3b 5c
                    res       0,(hl)                        ;[20ab] cb 86
                    cp        $20                           ;[20ad] fe 20
                    jr        nz,$20b3                      ;[20af] 20 02
                    set       0,(hl)                        ;[20b1] cb c6
                    cp        $7f                           ;[20b3] fe 7f
                    jr        c,$20b9                       ;[20b5] 38 02
                    ld        a,$3f                         ;[20b7] 3e 3f
                    cp        $20                           ;[20b9] fe 20
                    jr        c,$20d4                       ;[20bb] 38 17
                    push      af                            ;[20bd] f5
                    ld        hl,$5b63                      ;[20be] 21 63 5b
                    inc       (hl)                          ;[20c1] 34
                    ld        a,($5b64)                     ;[20c2] 3a 64 5b
                    cp        (hl)                          ;[20c5] be
                    jr        nc,$20d0                      ;[20c6] 30 08
                    call      $20d8                         ;[20c8] cd d8 20
                    ld        a,$01                         ;[20cb] 3e 01
                    ld        ($5b63),a                     ;[20cd] 32 63 5b
                    pop       af                            ;[20d0] f1
                    jp        $2159                         ;[20d1] c3 59 21
                    cp        $0d                           ;[20d4] fe 0d
                    jr        nz,$20e6                      ;[20d6] 20 0e
                    xor       a                             ;[20d8] af
                    ld        ($5b63),a                     ;[20d9] 32 63 5b
                    ld        a,$0d                         ;[20dc] 3e 0d
                    call      $2159                         ;[20de] cd 59 21
                    ld        a,$0a                         ;[20e1] 3e 0a
                    jp        $2159                         ;[20e3] c3 59 21
                    cp        $06                           ;[20e6] fe 06
                    jr        nz,$2109                      ;[20e8] 20 1f
                    ld        bc,($5b63)                    ;[20ea] ed 4b 63 5b
                    ld        e,$00                         ;[20ee] 1e 00
                    inc       e                             ;[20f0] 1c
                    inc       c                             ;[20f1] 0c
                    ld        a,c                           ;[20f2] 79
                    cp        b                             ;[20f3] b8
                    jr        z,$20fe                       ;[20f4] 28 08
                    sub       $08                           ;[20f6] d6 08
                    jr        z,$20fe                       ;[20f8] 28 04
                    jr        nc,$20f6                      ;[20fa] 30 fa
                    jr        $20f0                         ;[20fc] 18 f2
                    push      de                            ;[20fe] d5
                    ld        a,$20                         ;[20ff] 3e 20
                    call      $2076                         ;[2101] cd 76 20
                    pop       de                            ;[2104] d1
                    dec       e                             ;[2105] 1d
                    ret       z                             ;[2106] c8
                    jr        $20fe                         ;[2107] 18 f5
                    cp        $16                           ;[2109] fe 16
                    jr        z,$2116                       ;[210b] 28 09
                    cp        $17                           ;[210d] fe 17
                    jr        z,$2116                       ;[210f] 28 05
                    cp        $10                           ;[2111] fe 10
                    ret       c                             ;[2113] d8
                    jr        $211f                         ;[2114] 18 09
                    ld        ($5c0e),a                     ;[2116] 32 0e 5c
                    ld        a,$02                         ;[2119] 3e 02
                    ld        ($5b65),a                     ;[211b] 32 65 5b
                    ret                                     ;[211e] c9

                    ld        ($5c0e),a                     ;[211f] 32 0e 5c
                    ld        a,$02                         ;[2122] 3e 02
                    ld        ($5b65),a                     ;[2124] 32 65 5b
                    ret                                     ;[2127] c9

                    ld        d,a                           ;[2128] 57
                    ld        a,($5c0e)                     ;[2129] 3a 0e 5c
                    cp        $16                           ;[212c] fe 16
                    jr        z,$2138                       ;[212e] 28 08
                    cp        $17                           ;[2130] fe 17
                    ccf                                     ;[2132] 3f
                    ret       nz                            ;[2133] c0
                    ld        a,($5c0f)                     ;[2134] 3a 0f 5c
                    ld        d,a                           ;[2137] 57
                    ld        a,($5b64)                     ;[2138] 3a 64 5b
                    cp        d                             ;[213b] ba
                    jr        z,$2140                       ;[213c] 28 02
                    jr        nc,$2146                      ;[213e] 30 06
                    ld        b,a                           ;[2140] 47
                    ld        a,d                           ;[2141] 7a
                    sub       b                             ;[2142] 90
                    ld        d,a                           ;[2143] 57
                    jr        $2138                         ;[2144] 18 f2
                    ld        a,d                           ;[2146] 7a
                    or        a                             ;[2147] b7
                    jp        z,$20d8                       ;[2148] ca d8 20
                    ld        a,($5b63)                     ;[214b] 3a 63 5b
                    cp        d                             ;[214e] ba
                    ret       z                             ;[214f] c8
                    push      de                            ;[2150] d5
                    ld        a,$20                         ;[2151] 3e 20
                    call      $2076                         ;[2153] cd 76 20
                    pop       de                            ;[2156] d1
                    jr        $214b                         ;[2157] 18 f2
                    push      hl                            ;[2159] e5
                    ld        hl,$5b66                      ;[215a] 21 66 5b
                    bit       3,(hl)                        ;[215d] cb 5e
                    pop       hl                            ;[215f] e1
                    jp        z,$2207                       ;[2160] ca 07 22
                    ld        hl,$5c6a                      ;[2163] 21 6a 5c
                    bit       6,(hl)                        ;[2166] cb 76
                    jr        z,$216d                       ;[2168] 28 03
                    jp        $21ba                         ;[216a] c3 ba 21
                    push      af                            ;[216d] f5
                    ld        c,$fd                         ;[216e] 0e fd
                    ld        d,$ff                         ;[2170] 16 ff
                    ld        e,$bf                         ;[2172] 1e bf
                    ld        b,d                           ;[2174] 42
                    ld        a,$0e                         ;[2175] 3e 0e
                    out       (c),a                         ;[2177] ed 79
                    call      $2c6b                         ;[2179] cd 6b 2c
                    in        a,(c)                         ;[217c] ed 78
                    and       $40                           ;[217e] e6 40
                    jr        nz,$2179                      ;[2180] 20 f7
                    ld        hl,($5b5f)                    ;[2182] 2a 5f 5b
                    ld        de,$0002                      ;[2185] 11 02 00
                    or        a                             ;[2188] b7
                    sbc       hl,de                         ;[2189] ed 52
                    ex        de,hl                         ;[218b] eb
                    pop       af                            ;[218c] f1
                    cpl                                     ;[218d] 2f
                    scf                                     ;[218e] 37
                    ld        b,$0b                         ;[218f] 06 0b
                    di                                      ;[2191] f3
                    push      bc                            ;[2192] c5
                    push      af                            ;[2193] f5
                    ld        a,$fe                         ;[2194] 3e fe
                    ld        h,d                           ;[2196] 62
                    ld        l,e                           ;[2197] 6b
                    ld        bc,$bffd                      ;[2198] 01 fd bf
                    jp        nc,$21a4                      ;[219b] d2 a4 21
                    and       $f7                           ;[219e] e6 f7
                    out       (c),a                         ;[21a0] ed 79
                    jr        $21aa                         ;[21a2] 18 06
                    or        $08                           ;[21a4] f6 08
                    out       (c),a                         ;[21a6] ed 79
                    jr        $21aa                         ;[21a8] 18 00
                    dec       hl                            ;[21aa] 2b
                    ld        a,h                           ;[21ab] 7c
                    or        l                             ;[21ac] b5
                    jr        nz,$21aa                      ;[21ad] 20 fb
                    nop                                     ;[21af] 00
                    nop                                     ;[21b0] 00
                    nop                                     ;[21b1] 00
                    pop       af                            ;[21b2] f1
                    pop       bc                            ;[21b3] c1
                    or        a                             ;[21b4] b7
                    rra                                     ;[21b5] 1f
                    djnz      $2192                         ;[21b6] 10 da
                    ei                                      ;[21b8] fb
                    ret                                     ;[21b9] c9

                    push      af                            ;[21ba] f5
                    ld        c,$fd                         ;[21bb] 0e fd
                    ld        d,$ff                         ;[21bd] 16 ff
                    ld        e,$bf                         ;[21bf] 1e bf
                    ld        b,d                           ;[21c1] 42
                    ld        a,$0e                         ;[21c2] 3e 0e
                    out       (c),a                         ;[21c4] ed 79
                    call      $2c6b                         ;[21c6] cd 6b 2c
                    in        a,(c)                         ;[21c9] ed 78
                    and       $20                           ;[21cb] e6 20
                    jr        nz,$21c6                      ;[21cd] 20 f7
                    ld        hl,($5b5f)                    ;[21cf] 2a 5f 5b
                    ld        de,$0002                      ;[21d2] 11 02 00
                    or        a                             ;[21d5] b7
                    sbc       hl,de                         ;[21d6] ed 52
                    ex        de,hl                         ;[21d8] eb
                    pop       af                            ;[21d9] f1
                    cpl                                     ;[21da] 2f
                    scf                                     ;[21db] 37
                    ld        b,$0b                         ;[21dc] 06 0b
                    di                                      ;[21de] f3
                    push      bc                            ;[21df] c5
                    push      af                            ;[21e0] f5
                    ld        a,$fe                         ;[21e1] 3e fe
                    ld        h,d                           ;[21e3] 62
                    ld        l,e                           ;[21e4] 6b
                    ld        bc,$bffd                      ;[21e5] 01 fd bf
                    jp        nc,$21f1                      ;[21e8] d2 f1 21
                    and       $fe                           ;[21eb] e6 fe
                    out       (c),a                         ;[21ed] ed 79
                    jr        $21f7                         ;[21ef] 18 06
                    or        $01                           ;[21f1] f6 01
                    out       (c),a                         ;[21f3] ed 79
                    jr        $21f7                         ;[21f5] 18 00
                    dec       hl                            ;[21f7] 2b
                    ld        a,h                           ;[21f8] 7c
                    or        l                             ;[21f9] b5
                    jr        nz,$21f7                      ;[21fa] 20 fb
                    nop                                     ;[21fc] 00
                    nop                                     ;[21fd] 00
                    nop                                     ;[21fe] 00
                    pop       af                            ;[21ff] f1
                    pop       bc                            ;[2200] c1
                    or        a                             ;[2201] b7
                    rra                                     ;[2202] 1f
                    djnz      $21df                         ;[2203] 10 da
                    ei                                      ;[2205] fb
                    ret                                     ;[2206] c9

                    push      af                            ;[2207] f5
                    ld        bc,$1ffd                      ;[2208] 01 fd 1f
                    ld        a,($5b67)                     ;[220b] 3a 67 5b
                    set       4,a                           ;[220e] cb e7
                    di                                      ;[2210] f3
                    out       (c),a                         ;[2211] ed 79
                    ld        ($5b67),a                     ;[2213] 32 67 5b
                    ei                                      ;[2216] fb
                    call      $2c6b                         ;[2217] cd 6b 2c
                    ld        bc,$0ffd                      ;[221a] 01 fd 0f
                    in        a,(c)                         ;[221d] ed 78
                    bit       0,a                           ;[221f] cb 47
                    jr        nz,$2217                      ;[2221] 20 f4
                    pop       af                            ;[2223] f1
                    out       (c),a                         ;[2224] ed 79
                    di                                      ;[2226] f3
                    ld        bc,$1ffd                      ;[2227] 01 fd 1f
                    ld        a,($5b67)                     ;[222a] 3a 67 5b
                    res       4,a                           ;[222d] cb a7
                    out       (c),a                         ;[222f] ed 79
                    set       4,a                           ;[2231] cb e7
                    out       (c),a                         ;[2233] ed 79
                    ld        ($5b67),a                     ;[2235] 32 67 5b
                    ei                                      ;[2238] fb
                    scf                                     ;[2239] 37
                    ret                                     ;[223a] c9

                    bit       7,(iy+$01)                    ;[223b] fd cb 01 7e
                    ret       z                             ;[223f] c8
                    ld        a,($5b67)                     ;[2240] 3a 67 5b
                    ld        bc,$1ffd                      ;[2243] 01 fd 1f
                    set       4,a                           ;[2246] cb e7
                    di                                      ;[2248] f3
                    ld        ($5b67),a                     ;[2249] 32 67 5b
                    out       (c),a                         ;[224c] ed 79
                    ei                                      ;[224e] fb
                    ld        hl,$5b69                      ;[224f] 21 69 5b
                    ld        (hl),$2b                      ;[2252] 36 2b
                    ld        hl,$22d8                      ;[2254] 21 d8 22
                    call      $22be                         ;[2257] cd be 22
                    call      $2274                         ;[225a] cd 74 22
                    ld        hl,$22df                      ;[225d] 21 df 22
                    call      $22be                         ;[2260] cd be 22
                    ld        hl,$5b69                      ;[2263] 21 69 5b
                    xor       a                             ;[2266] af
                    cp        (hl)                          ;[2267] be
                    jr        z,$226d                       ;[2268] 28 03
                    dec       (hl)                          ;[226a] 35
                    jr        $2254                         ;[226b] 18 e7
                    ld        hl,$22e1                      ;[226d] 21 e1 22
                    call      $22be                         ;[2270] cd be 22
                    ret                                     ;[2273] c9

                    ld        hl,$5b68                      ;[2274] 21 68 5b
                    ld        (hl),$ff                      ;[2277] 36 ff
                    call      $2285                         ;[2279] cd 85 22
                    ld        hl,$5b68                      ;[227c] 21 68 5b
                    xor       a                             ;[227f] af
                    cp        (hl)                          ;[2280] be
                    ret       z                             ;[2281] c8
                    dec       (hl)                          ;[2282] 35
                    jr        $2279                         ;[2283] 18 f4
                    ld        de,$c000                      ;[2285] 11 00 c0
                    ld        bc,($5b68)                    ;[2288] ed 4b 68 5b
                    scf                                     ;[228c] 37
                    rl        b                             ;[228d] cb 10
                    scf                                     ;[228f] 37
                    rl        b                             ;[2290] cb 10
                    ld        a,c                           ;[2292] 79
                    cpl                                     ;[2293] 2f
                    ld        c,a                           ;[2294] 4f
                    xor       a                             ;[2295] af
                    push      af                            ;[2296] f5
                    push      de                            ;[2297] d5
                    push      bc                            ;[2298] c5
                    call      $22cc                         ;[2299] cd cc 22
                    pop       bc                            ;[229c] c1
                    pop       de                            ;[229d] d1
                    ld        e,$00                         ;[229e] 1e 00
                    jr        z,$22a3                       ;[22a0] 28 01
                    ld        e,d                           ;[22a2] 5a
                    pop       af                            ;[22a3] f1
                    or        e                             ;[22a4] b3
                    push      af                            ;[22a5] f5
                    dec       b                             ;[22a6] 05
                    srl       d                             ;[22a7] cb 3a
                    srl       d                             ;[22a9] cb 3a
                    push      de                            ;[22ab] d5
                    push      bc                            ;[22ac] c5
                    jr        nc,$2299                      ;[22ad] 30 ea
                    pop       bc                            ;[22af] c1
                    pop       de                            ;[22b0] d1
                    pop       af                            ;[22b1] f1
                    ld        b,$03                         ;[22b2] 06 03
                    push      bc                            ;[22b4] c5
                    push      af                            ;[22b5] f5
                    call      $2159                         ;[22b6] cd 59 21
                    pop       af                            ;[22b9] f1
                    pop       bc                            ;[22ba] c1
                    djnz      $22b4                         ;[22bb] 10 f7
                    ret                                     ;[22bd] c9

                    ld        b,(hl)                        ;[22be] 46
                    inc       hl                            ;[22bf] 23
                    ld        a,(hl)                        ;[22c0] 7e
                    push      hl                            ;[22c1] e5
                    push      bc                            ;[22c2] c5
                    call      $2159                         ;[22c3] cd 59 21
                    pop       bc                            ;[22c6] c1
                    pop       hl                            ;[22c7] e1
                    inc       hl                            ;[22c8] 23
                    djnz      $22c0                         ;[22c9] 10 f5
                    ret                                     ;[22cb] c9

                    rst       $28                           ;[22cc] ef
                    xor       d                             ;[22cd] aa
                    ld        ($0447),hl                    ;[22ce] 22 47 04
                    xor       a                             ;[22d1] af
                    scf                                     ;[22d2] 37
                    rra                                     ;[22d3] 1f
                    djnz      $22d3                         ;[22d4] 10 fd
                    and       (hl)                          ;[22d6] a6
                    ret                                     ;[22d7] c9

                    ld        b,$1b                         ;[22d8] 06 1b
                    ld        sp,$4c1b                      ;[22da] 31 1b 4c
                    nop                                     ;[22dd] 00
                    inc       bc                            ;[22de] 03
                    ld        bc,$020a                      ;[22df] 01 0a 02
                    dec       de                            ;[22e2] 1b
                    ld        ($033e),a                     ;[22e3] 32 3e 03
                    jr        $22ea                         ;[22e6] 18 02
                    ld        a,$02                         ;[22e8] 3e 02
                    rst       $28                           ;[22ea] ef
                    jr        nc,$2312                      ;[22eb] 30 25
                    jr        z,$22f2                       ;[22ed] 28 03
                    rst       $28                           ;[22ef] ef
                    ld        bc,$ef16                      ;[22f0] 01 16 ef
                    ld        c,l                           ;[22f3] 4d
                    dec       c                             ;[22f4] 0d
                    rst       $28                           ;[22f5] ef
                    rst       $18                           ;[22f6] df
                    rra                                     ;[22f7] 1f
                    call      $10cd                         ;[22f8] cd cd 10
                    ret                                     ;[22fb] c9

                    rst       $28                           ;[22fc] ef
                    jr        nc,$2324                      ;[22fd] 30 25
                    jr        z,$2309                       ;[22ff] 28 08
                    ld        a,$01                         ;[2301] 3e 01
                    rst       $28                           ;[2303] ef
                    ld        bc,$ef16                      ;[2304] 01 16 ef
                    ld        l,(hl)                        ;[2307] 6e
                    dec       c                             ;[2308] 0d
                    ld        (iy+$02),$01                  ;[2309] fd 36 02 01
                    rst       $28                           ;[230d] ef
                    pop       bc                            ;[230e] c1
                    jr        nz,$22de                      ;[230f] 20 cd
                    call      $ef10                         ;[2311] cd 10 ef
                    and       b                             ;[2314] a0
                    jr        nz,$22e0                      ;[2315] 20 c9
                    rst       $18                           ;[2317] df
                    cp        $0d                           ;[2318] fe 0d
                    jp        z,$223b                       ;[231a] ca 3b 22
                    cp        $3a                           ;[231d] fe 3a
                    jp        z,$223b                       ;[231f] ca 3b 22
                    cp        $b9                           ;[2322] fe b9
                    jp        z,$349f                       ;[2324] ca 9f 34
                    cp        $f9                           ;[2327] fe f9
                    jp        z,$3759                       ;[2329] ca 59 37
                    rst       $28                           ;[232c] ef
                    adc       h                             ;[232d] 8c
                    inc       e                             ;[232e] 1c
                    rst       $28                           ;[232f] ef
                    jr        $2332                         ;[2330] 18 00
                    cp        $cc                           ;[2332] fe cc
                    jr        z,$233a                       ;[2334] 28 04
                    call      $2c4c                         ;[2336] cd 4c 2c
                    dec       bc                            ;[2339] 0b
                    rst       $28                           ;[233a] ef
                    jr        nz,$233d                      ;[233b] 20 00
                    cp        $aa                           ;[233d] fe aa
                    jp        z,$23a4                       ;[233f] ca a4 23
                    cp        $a3                           ;[2342] fe a3
                    jp        z,$23c4                       ;[2344] ca c4 23
                    cp        $e0                           ;[2347] fe e0
                    jp        z,$23a4                       ;[2349] ca a4 23
                    rst       $28                           ;[234c] ef
                    adc       h                             ;[234d] 8c
                    inc       e                             ;[234e] 1c
                    call      $10cd                         ;[234f] cd cd 10
                    rst       $28                           ;[2352] ef
                    pop       af                            ;[2353] f1
                    dec       hl                            ;[2354] 2b
                    ld        a,b                           ;[2355] 78
                    or        c                             ;[2356] b1
                    jr        nz,$235d                      ;[2357] 20 04
                    call      $2c4c                         ;[2359] cd 4c 2c
                    inc       l                             ;[235c] 2c
                    inc       de                            ;[235d] 13
                    ld        a,(de)                        ;[235e] 1a
                    dec       de                            ;[235f] 1b
                    cp        $3a                           ;[2360] fe 3a
                    jr        nz,$2368                      ;[2362] 20 04
                    ld        a,(de)                        ;[2364] 1a
                    and       $df                           ;[2365] e6 df
                    ld        (de),a                        ;[2367] 12
                    ld        hl,$ed01                      ;[2368] 21 01 ed
                    ex        de,hl                         ;[236b] eb
                    call      $3f63                         ;[236c] cd 63 3f
                    call      $2cfb                         ;[236f] cd fb 2c
                    ld        a,$ff                         ;[2372] 3e ff
                    ld        (de),a                        ;[2374] 12
                    inc       de                            ;[2375] 13
                    call      $2cd6                         ;[2376] cd d6 2c
                    push      de                            ;[2379] d5
                    rst       $28                           ;[237a] ef
                    pop       af                            ;[237b] f1
                    dec       hl                            ;[237c] 2b
                    ld        a,b                           ;[237d] 78
                    or        c                             ;[237e] b1
                    jr        nz,$2385                      ;[237f] 20 04
                    call      $2c4c                         ;[2381] cd 4c 2c
                    inc       l                             ;[2384] 2c
                    inc       de                            ;[2385] 13
                    ld        a,(de)                        ;[2386] 1a
                    dec       de                            ;[2387] 1b
                    cp        $3a                           ;[2388] fe 3a
                    jr        nz,$2390                      ;[238a] 20 04
                    ld        a,(de)                        ;[238c] 1a
                    and       $df                           ;[238d] e6 df
                    ld        (de),a                        ;[238f] 12
                    pop       hl                            ;[2390] e1
                    ex        de,hl                         ;[2391] eb
                    call      $3f63                         ;[2392] cd 63 3f
                    call      $2cfb                         ;[2395] cd fb 2c
                    ld        a,$ff                         ;[2398] 3e ff
                    ld        (de),a                        ;[239a] 12
                    call      $2cd6                         ;[239b] cd d6 2c
                    xor       a                             ;[239e] af
                    scf                                     ;[239f] 37
                    call      $2d15                         ;[23a0] cd 15 2d
                    ret                                     ;[23a3] c9

                    push      af                            ;[23a4] f5
                    rst       $28                           ;[23a5] ef
                    jr        nz,$23a8                      ;[23a6] 20 00
                    call      $10cd                         ;[23a8] cd cd 10
                    rst       $28                           ;[23ab] ef
                    pop       af                            ;[23ac] f1
                    dec       hl                            ;[23ad] 2b
                    ld        hl,$ed01                      ;[23ae] 21 01 ed
                    ex        de,hl                         ;[23b1] eb
                    call      $3f63                         ;[23b2] cd 63 3f
                    call      $2cfb                         ;[23b5] cd fb 2c
                    ld        a,$ff                         ;[23b8] 3e ff
                    ld        (de),a                        ;[23ba] 12
                    call      $2cd6                         ;[23bb] cd d6 2c
                    pop       af                            ;[23be] f1
                    and       a                             ;[23bf] a7
                    call      $2d15                         ;[23c0] cd 15 2d
                    ret                                     ;[23c3] c9

                    rst       $28                           ;[23c4] ef
                    jr        nz,$23c7                      ;[23c5] 20 00
                    cp        $d0                           ;[23c7] fe d0
                    jr        z,$23cf                       ;[23c9] 28 04
                    call      $2c4c                         ;[23cb] cd 4c 2c
                    dec       bc                            ;[23ce] 0b
                    rst       $28                           ;[23cf] ef
                    jr        nz,$23d2                      ;[23d0] 20 00
                    call      $10cd                         ;[23d2] cd cd 10
                    rst       $28                           ;[23d5] ef
                    pop       af                            ;[23d6] f1
                    dec       hl                            ;[23d7] 2b
                    ld        hl,$ed01                      ;[23d8] 21 01 ed
                    ex        de,hl                         ;[23db] eb
                    call      $3f63                         ;[23dc] cd 63 3f
                    call      $2cfb                         ;[23df] cd fb 2c
                    ld        a,$ff                         ;[23e2] 3e ff
                    ld        (de),a                        ;[23e4] 12
                    call      $2cd6                         ;[23e5] cd d6 2c
                    xor       a                             ;[23e8] af
                    call      $2d15                         ;[23e9] cd 15 2d
                    ret                                     ;[23ec] c9

                    di                                      ;[23ed] f3
                    call      $3e80                         ;[23ee] cd 80 3e
                    or        b                             ;[23f1] b0
                    ld        bc,$fedf                      ;[23f2] 01 df fe
                    inc       l                             ;[23f5] 2c
                    jr        nz,$2430                      ;[23f6] 20 38
                    rst       $20                           ;[23f8] e7
                    rst       $28                           ;[23f9] ef
                    add       d                             ;[23fa] 82
                    inc       e                             ;[23fb] 1c
                    call      $10cd                         ;[23fc] cd cd 10
                    rst       $28                           ;[23ff] ef
                    dec       l                             ;[2400] 2d
                    inc       hl                            ;[2401] 23
                    ret                                     ;[2402] c9

                    rst       $18                           ;[2403] df
                    cp        $2c                           ;[2404] fe 2c
                    jr        z,$240f                       ;[2406] 28 07
                    call      $10cd                         ;[2408] cd cd 10
                    rst       $28                           ;[240b] ef
                    ld        (hl),a                        ;[240c] 77
                    inc       h                             ;[240d] 24
                    ret                                     ;[240e] c9

                    rst       $20                           ;[240f] e7
                    rst       $28                           ;[2410] ef
                    add       d                             ;[2411] 82
                    inc       e                             ;[2412] 1c
                    call      $10cd                         ;[2413] cd cd 10
                    rst       $28                           ;[2416] ef
                    sub       h                             ;[2417] 94
                    inc       hl                            ;[2418] 23
                    ret                                     ;[2419] c9

                    rst       $28                           ;[241a] ef
                    or        d                             ;[241b] b2
                    jr        z,$243e                       ;[241c] 28 20
                    ld        de,$30ef                      ;[241e] 11 ef 30
                    dec       h                             ;[2421] 25
                    jr        nz,$242c                      ;[2422] 20 08
                    res       6,c                           ;[2424] cb b1
                    rst       $28                           ;[2426] ef
                    sub       (hl)                          ;[2427] 96
                    add       hl,hl                         ;[2428] 29
                    call      $10cd                         ;[2429] cd cd 10
                    rst       $28                           ;[242c] ef
                    dec       d                             ;[242d] 15
                    inc       l                             ;[242e] 2c
                    ret                                     ;[242f] c9

                    call      $2c4c                         ;[2430] cd 4c 2c
                    dec       bc                            ;[2433] 0b
                    bit       0,(iy+$30)                    ;[2434] fd cb 30 46
                    ret       z                             ;[2438] c8
                    rst       $28                           ;[2439] ef
                    xor       a                             ;[243a] af
                    dec       c                             ;[243b] 0d
                    ret                                     ;[243c] c9

                    ld        hl,$fffe                      ;[243d] 21 fe ff
                    ld        ($5c45),hl                    ;[2440] 22 45 5c
                    res       7,(iy+$01)                    ;[2443] fd cb 01 be
                    call      $24d5                         ;[2447] cd d5 24
                    rst       $28                           ;[244a] ef
                    ei                                      ;[244b] fb
                    inc       h                             ;[244c] 24
                    bit       6,(iy+$01)                    ;[244d] fd cb 01 76
                    jr        z,$247f                       ;[2451] 28 2c
                    rst       $18                           ;[2453] df
                    cp        $0d                           ;[2454] fe 0d
                    jr        nz,$247f                      ;[2456] 20 27
                    set       7,(iy+$01)                    ;[2458] fd cb 01 fe
                    call      $24d5                         ;[245c] cd d5 24
                    ld        hl,$2739                      ;[245f] 21 39 27
                    ld        ($5b6c),hl                    ;[2462] 22 6c 5b
                    rst       $28                           ;[2465] ef
                    ei                                      ;[2466] fb
                    inc       h                             ;[2467] 24
                    bit       6,(iy+$01)                    ;[2468] fd cb 01 76
                    jr        z,$247f                       ;[246c] 28 11
                    ld        de,$5b6e                      ;[246e] 11 6e 5b
                    ld        hl,($5c65)                    ;[2471] 2a 65 5c
                    ld        bc,$0005                      ;[2474] 01 05 00
                    or        a                             ;[2477] b7
                    sbc       hl,bc                         ;[2478] ed 42
                    ldir                                    ;[247a] ed b0
                    jp        $2483                         ;[247c] c3 83 24
                    call      $2c4c                         ;[247f] cd 4c 2c
                    add       hl,de                         ;[2482] 19
                    ld        a,$0d                         ;[2483] 3e 0d
                    call      $24b4                         ;[2485] cd b4 24
                    ld        bc,$0001                      ;[2488] 01 01 00
                    rst       $28                           ;[248b] ef
                    jr        nc,$248e                      ;[248c] 30 00
                    ld        ($5c5b),hl                    ;[248e] 22 5b 5c
                    push      hl                            ;[2491] e5
                    ld        hl,($5c51)                    ;[2492] 2a 51 5c
                    push      hl                            ;[2495] e5
                    ld        a,$ff                         ;[2496] 3e ff
                    rst       $28                           ;[2498] ef
                    ld        bc,$ef16                      ;[2499] 01 16 ef
                    ex        (sp),hl                       ;[249c] e3
                    dec       l                             ;[249d] 2d
                    pop       hl                            ;[249e] e1
                    rst       $28                           ;[249f] ef
                    dec       d                             ;[24a0] 15
                    ld        d,$d1                         ;[24a1] 16 d1
                    ld        hl,($5c5b)                    ;[24a3] 2a 5b 5c
                    and       a                             ;[24a6] a7
                    sbc       hl,de                         ;[24a7] ed 52
                    ld        a,(de)                        ;[24a9] 1a
                    call      $24b4                         ;[24aa] cd b4 24
                    inc       de                            ;[24ad] 13
                    dec       hl                            ;[24ae] 2b
                    ld        a,h                           ;[24af] 7c
                    or        l                             ;[24b0] b5
                    jr        nz,$24a9                      ;[24b1] 20 f6
                    ret                                     ;[24b3] c9

                    push      hl                            ;[24b4] e5
                    push      de                            ;[24b5] d5
                    call      $2cfb                         ;[24b6] cd fb 2c
                    ld        hl,$ec0d                      ;[24b9] 21 0d ec
                    res       3,(hl)                        ;[24bc] cb 9e
                    push      af                            ;[24be] f5
                    ld        a,$02                         ;[24bf] 3e 02
                    rst       $28                           ;[24c1] ef
                    ld        bc,$f116                      ;[24c2] 01 16 f1
                    call      $3e80                         ;[24c5] cd 80 3e
                    dec       de                            ;[24c8] 1b
                    rlca                                    ;[24c9] 07
                    ld        hl,$ec0d                      ;[24ca] 21 0d ec
                    res       3,(hl)                        ;[24cd] cb 9e
                    call      $2cd6                         ;[24cf] cd d6 2c
                    pop       de                            ;[24d2] d1
                    pop       hl                            ;[24d3] e1
                    ret                                     ;[24d4] c9

                    ld        hl,($5c59)                    ;[24d5] 2a 59 5c
                    dec       hl                            ;[24d8] 2b
                    ld        ($5c5d),hl                    ;[24d9] 22 5d 5c
                    rst       $20                           ;[24dc] e7
                    ret                                     ;[24dd] c9

                    call      $24d5                         ;[24de] cd d5 24
                    cp        $f1                           ;[24e1] fe f1
                    ret       nz                            ;[24e3] c0
                    ld        hl,($5c5d)                    ;[24e4] 2a 5d 5c
                    ld        a,(hl)                        ;[24e7] 7e
                    inc       hl                            ;[24e8] 23
                    cp        $0d                           ;[24e9] fe 0d
                    ret       z                             ;[24eb] c8
                    cp        $3a                           ;[24ec] fe 3a
                    jr        nz,$24e7                      ;[24ee] 20 f7
                    or        a                             ;[24f0] b7
                    ret                                     ;[24f1] c9

                    ld        b,a                           ;[24f2] 47
                    ld        hl,$2504                      ;[24f3] 21 04 25
                    ld        a,(hl)                        ;[24f6] 7e
                    inc       hl                            ;[24f7] 23
                    or        a                             ;[24f8] b7
                    jr        z,$2500                       ;[24f9] 28 05
                    cp        b                             ;[24fb] b8
                    jr        nz,$24f6                      ;[24fc] 20 f8
                    ld        a,b                           ;[24fe] 78
                    ret                                     ;[24ff] c9

                    or        $ff                           ;[2500] f6 ff
                    ld        a,b                           ;[2502] 78
                    ret                                     ;[2503] c9

                    dec       hl                            ;[2504] 2b
                    dec       l                             ;[2505] 2d
                    ld        hl,($5e2f)                    ;[2506] 2a 2f 5e
                    dec       a                             ;[2509] 3d
                    ld        a,$3c                         ;[250a] 3e 3c
                    rst       $00                           ;[250c] c7
                    ret       z                             ;[250d] c8
                    ret                                     ;[250e] c9

                    push      bc                            ;[250f] c5
                    add       $00                           ;[2510] c6 00
                    cp        $a5                           ;[2512] fe a5
                    jr        c,$2524                       ;[2514] 38 0e
                    cp        $c4                           ;[2516] fe c4
                    jr        nc,$2524                      ;[2518] 30 0a
                    cp        $ac                           ;[251a] fe ac
                    jr        z,$2524                       ;[251c] 28 06
                    cp        $ad                           ;[251e] fe ad
                    jr        z,$2524                       ;[2520] 28 02
                    cp        a                             ;[2522] bf
                    ret                                     ;[2523] c9

                    cp        $a5                           ;[2524] fe a5
                    ret                                     ;[2526] c9

                    ld        b,a                           ;[2527] 47
                    or        $20                           ;[2528] f6 20
                    cp        $61                           ;[252a] fe 61
                    jr        c,$2534                       ;[252c] 38 06
                    cp        $7b                           ;[252e] fe 7b
                    jr        nc,$2534                      ;[2530] 30 02
                    cp        a                             ;[2532] bf
                    ret                                     ;[2533] c9

                    ld        a,b                           ;[2534] 78
                    cp        $2e                           ;[2535] fe 2e
                    ret       z                             ;[2537] c8
                    call      $2551                         ;[2538] cd 51 25
                    jr        nz,$254e                      ;[253b] 20 11
                    rst       $20                           ;[253d] e7
                    call      $2551                         ;[253e] cd 51 25
                    jr        z,$253d                       ;[2541] 28 fa
                    cp        $2e                           ;[2543] fe 2e
                    ret       z                             ;[2545] c8
                    cp        $45                           ;[2546] fe 45
                    ret       z                             ;[2548] c8
                    cp        $65                           ;[2549] fe 65
                    ret       z                             ;[254b] c8
                    jr        $24f2                         ;[254c] 18 a4
                    or        $ff                           ;[254e] f6 ff
                    ret                                     ;[2550] c9

                    cp        $30                           ;[2551] fe 30
                    jr        c,$255b                       ;[2553] 38 06
                    cp        $3a                           ;[2555] fe 3a
                    jr        nc,$255b                      ;[2557] 30 02
                    cp        a                             ;[2559] bf
                    ret                                     ;[255a] c9

                    cp        $30                           ;[255b] fe 30
                    ret                                     ;[255d] c9

                    ld        b,$00                         ;[255e] 06 00
                    rst       $18                           ;[2560] df
                    push      bc                            ;[2561] c5
                    rst       $28                           ;[2562] ef
                    adc       h                             ;[2563] 8c
                    inc       e                             ;[2564] 1c
                    pop       bc                            ;[2565] c1
                    inc       b                             ;[2566] 04
                    cp        $2c                           ;[2567] fe 2c
                    jr        nz,$256e                      ;[2569] 20 03
                    rst       $20                           ;[256b] e7
                    jr        $2561                         ;[256c] 18 f3
                    ld        a,b                           ;[256e] 78
                    cp        $09                           ;[256f] fe 09
                    jr        c,$2577                       ;[2571] 38 04
                    call      $2c4c                         ;[2573] cd 4c 2c
                    dec       hl                            ;[2576] 2b
                    call      $10cd                         ;[2577] cd cd 10
                    jp        $158d                         ;[257a] c3 8d 15
                    call      $2cfb                         ;[257d] cd fb 2c
                    ld        hl,$5b66                      ;[2580] 21 66 5b
                    bit       7,(hl)                        ;[2583] cb 7e
                    jr        nz,$2597                      ;[2585] 20 10
                    call      $342d                         ;[2587] cd 2d 34
                    call      $3f00                         ;[258a] cd 00 3f
                    nop                                     ;[258d] 00
                    ld        bc,$65cd                      ;[258e] 01 cd 65
                    inc       (hl)                          ;[2591] 34
                    ld        hl,$5b66                      ;[2592] 21 66 5b
                    set       7,(hl)                        ;[2595] cb fe
                    ld        a,$ff                         ;[2597] 3e ff
                    ld        hl,$244e                      ;[2599] 21 4e 24
                    call      $342d                         ;[259c] cd 2d 34
                    call      $3f00                         ;[259f] cd 00 3f
                    ld        c,(hl)                        ;[25a2] 4e
                    ld        bc,$65cd                      ;[25a3] 01 cd 65
                    inc       (hl)                          ;[25a6] 34
                    call      $2cd6                         ;[25a7] cd d6 2c
                    ld        hl,$262b                      ;[25aa] 21 2b 26
                    call      $2622                         ;[25ad] cd 22 26
                    ld        hl,$5b66                      ;[25b0] 21 66 5b
                    res       4,(hl)                        ;[25b3] cb a6
                    call      $2cfb                         ;[25b5] cd fb 2c
                    call      $342d                         ;[25b8] cd 2d 34
                    call      $3f00                         ;[25bb] cd 00 3f
                    ld        d,a                           ;[25be] 57
                    ld        bc,$65cd                      ;[25bf] 01 cd 65
                    inc       (hl)                          ;[25c2] 34
                    call      $2cd6                         ;[25c3] cd d6 2c
                    jr        c,$25d0                       ;[25c6] 38 08
                    ld        hl,$2631                      ;[25c8] 21 31 26
                    call      $2622                         ;[25cb] cd 22 26
                    jr        $261b                         ;[25ce] 18 4b
                    ld        a,$41                         ;[25d0] 3e 41
                    ld        ($5b79),a                     ;[25d2] 32 79 5b
                    ld        ($5b7a),a                     ;[25d5] 32 7a 5b
                    ld        hl,$5b66                      ;[25d8] 21 66 5b
                    set       4,(hl)                        ;[25db] cb e6
                    res       5,(hl)                        ;[25dd] cb ae
                    call      $2cfb                         ;[25df] cd fb 2c
                    call      $342d                         ;[25e2] cd 2d 34
                    call      $3f00                         ;[25e5] cd 00 3f
                    ld        a,e                           ;[25e8] 7b
                    ld        bc,$65cd                      ;[25e9] 01 cd 65
                    inc       (hl)                          ;[25ec] 34
                    call      $2cd6                         ;[25ed] cd d6 2c
                    jr        c,$2610                       ;[25f0] 38 1e
                    ld        c,$00                         ;[25f2] 0e 00
                    ld        hl,$2470                      ;[25f4] 21 70 24
                    call      $2cfb                         ;[25f7] cd fb 2c
                    call      $342d                         ;[25fa] cd 2d 34
                    call      $3f00                         ;[25fd] cd 00 3f
                    ld        d,h                           ;[2600] 54
                    ld        bc,$65cd                      ;[2601] 01 cd 65
                    inc       (hl)                          ;[2604] 34
                    call      $2cd6                         ;[2605] cd d6 2c
                    ld        hl,$2635                      ;[2608] 21 35 26
                    call      $2622                         ;[260b] cd 22 26
                    jr        $261b                         ;[260e] 18 0b
                    ld        hl,$5b66                      ;[2610] 21 66 5b
                    set       5,(hl)                        ;[2613] cb ee
                    ld        hl,$2641                      ;[2615] 21 41 26
                    call      $2622                         ;[2618] cd 22 26
                    ld        hl,$2651                      ;[261b] 21 51 26
                    call      $2622                         ;[261e] cd 22 26
                    ret                                     ;[2621] c9

                    ld        a,(hl)                        ;[2622] 7e
                    or        a                             ;[2623] b7
                    ret       z                             ;[2624] c8
                    rst       $28                           ;[2625] ef
                    djnz      $2628                         ;[2626] 10 00
                    inc       hl                            ;[2628] 23
                    jr        $2622                         ;[2629] 18 f7
                    ld        b,h                           ;[262b] 44
                    ld        (hl),d                        ;[262c] 72
                    ld        l,c                           ;[262d] 69
                    halt                                    ;[262e] 76
                    ld        h,l                           ;[262f] 65
                    nop                                     ;[2630] 00
                    jr        nz,$2680                      ;[2631] 20 4d
                    ld        a,($7300)                     ;[2633] 3a 00 73
                    jr        nz,$2679                      ;[2636] 20 41
                    ld        a,($6120)                     ;[2638] 3a 20 61
                    ld        l,(hl)                        ;[263b] 6e
                    ld        h,h                           ;[263c] 64
                    jr        nz,$268c                      ;[263d] 20 4d
                    ld        a,($7300)                     ;[263f] 3a 00 73
                    jr        nz,$2685                      ;[2642] 20 41
                    ld        a,($202c)                     ;[2644] 3a 2c 20
                    ld        b,d                           ;[2647] 42
                    ld        a,($6120)                     ;[2648] 3a 20 61
                    ld        l,(hl)                        ;[264b] 6e
                    ld        h,h                           ;[264c] 64
                    jr        nz,$269c                      ;[264d] 20 4d
                    ld        a,($2000)                     ;[264f] 3a 00 20
                    ld        h,c                           ;[2652] 61
                    halt                                    ;[2653] 76
                    ld        h,c                           ;[2654] 61
                    ld        l,c                           ;[2655] 69
                    ld        l,h                           ;[2656] 6c
                    ld        h,c                           ;[2657] 61
                    ld        h,d                           ;[2658] 62
                    ld        l,h                           ;[2659] 6c
                    ld        h,l                           ;[265a] 65
                    ld        l,$00                         ;[265b] 2e 00
                    ld        (iy+$00),$ff                  ;[265d] fd 36 00 ff
                    ld        (iy+$31),$02                  ;[2661] fd 36 31 02
                    ld        hl,$5b3a                      ;[2665] 21 3a 5b
                    push      hl                            ;[2668] e5
                    ld        ($5c3d),sp                    ;[2669] ed 73 3d 5c
                    ld        hl,$26ce                      ;[266d] 21 ce 26
                    ld        ($5b6c),hl                    ;[2670] 22 6c 5b
                    call      $24d5                         ;[2673] cd d5 24
                    call      $2512                         ;[2676] cd 12 25
                    jp        z,$243d                       ;[2679] ca 3d 24
                    cp        $28                           ;[267c] fe 28
                    jp        z,$243d                       ;[267e] ca 3d 24
                    cp        $2d                           ;[2681] fe 2d
                    jp        z,$243d                       ;[2683] ca 3d 24
                    cp        $2b                           ;[2686] fe 2b
                    jp        z,$243d                       ;[2688] ca 3d 24
                    call      $2527                         ;[268b] cd 27 25
                    jp        z,$243d                       ;[268e] ca 3d 24
                    call      $2cfb                         ;[2691] cd fb 2c
                    ld        a,($ec0e)                     ;[2694] 3a 0e ec
                    call      $2cd6                         ;[2697] cd d6 2c
                    cp        $04                           ;[269a] fe 04
                    jp        nz,$0fdb                      ;[269c] c2 db 0f
                    call      $24de                         ;[269f] cd de 24
                    jp        z,$0fdb                       ;[26a2] ca db 0f
                    pop       hl                            ;[26a5] e1
                    ret                                     ;[26a6] c9

                    ei                                      ;[26a7] fb
                    call      $2cfb                         ;[26a8] cd fb 2c
                    ld        b,$00                         ;[26ab] 06 00
                    call      $342d                         ;[26ad] cd 2d 34
                    call      $3f00                         ;[26b0] cd 00 3f
                    add       hl,bc                         ;[26b3] 09
                    ld        bc,$65cd                      ;[26b4] 01 cd 65
                    inc       (hl)                          ;[26b7] 34
                    jr        c,$26c7                       ;[26b8] 38 0d
                    ld        b,$00                         ;[26ba] 06 00
                    call      $342d                         ;[26bc] cd 2d 34
                    call      $3f00                         ;[26bf] cd 00 3f
                    inc       c                             ;[26c2] 0c
                    ld        bc,$65cd                      ;[26c3] 01 cd 65
                    inc       (hl)                          ;[26c6] 34
                    call      $2cd6                         ;[26c7] cd d6 2c
                    ld        hl,($5b6c)                    ;[26ca] 2a 6c 5b
                    jp        (hl)                          ;[26cd] e9
                    bit       7,(iy+$00)                    ;[26ce] fd cb 00 7e
                    jr        nz,$26d5                      ;[26d2] 20 01
                    ret                                     ;[26d4] c9

                    ld        hl,($5c59)                    ;[26d5] 2a 59 5c
                    ld        ($5c5d),hl                    ;[26d8] 22 5d 5c
                    rst       $28                           ;[26db] ef
                    ei                                      ;[26dc] fb
                    add       hl,de                         ;[26dd] 19
                    ld        a,b                           ;[26de] 78
                    or        c                             ;[26df] b1
                    jp        nz,$27fc                      ;[26e0] c2 fc 27
                    rst       $18                           ;[26e3] df
                    cp        $0d                           ;[26e4] fe 0d
                    ret       z                             ;[26e6] c8
                    call      $2434                         ;[26e7] cd 34 24
                    bit       6,(iy+$02)                    ;[26ea] fd cb 02 76
                    jr        nz,$26f3                      ;[26ee] 20 03
                    rst       $28                           ;[26f0] ef
                    ld        l,(hl)                        ;[26f1] 6e
                    dec       c                             ;[26f2] 0d
                    res       6,(iy+$02)                    ;[26f3] fd cb 02 b6
                    call      $2cfb                         ;[26f7] cd fb 2c
                    ld        hl,$ec0d                      ;[26fa] 21 0d ec
                    bit       6,(hl)                        ;[26fd] cb 76
                    jr        nz,$270c                      ;[26ff] 20 0b
                    inc       hl                            ;[2701] 23
                    ld        a,(hl)                        ;[2702] 7e
                    cp        $00                           ;[2703] fe 00
                    jr        nz,$270c                      ;[2705] 20 05
                    call      $3e80                         ;[2707] cd 80 3e
                    sub       e                             ;[270a] 93
                    ld        a,(de)                        ;[270b] 1a
                    call      $2cd6                         ;[270c] cd d6 2c
                    ld        hl,$5c3c                      ;[270f] 21 3c 5c
                    res       3,(hl)                        ;[2712] cb 9e
                    ld        a,$19                         ;[2714] 3e 19
                    sub       (iy+$4f)                      ;[2716] fd 96 4f
                    ld        ($5c8c),a                     ;[2719] 32 8c 5c
                    set       7,(iy+$01)                    ;[271c] fd cb 01 fe
                    ld        (iy+$0a),$01                  ;[2720] fd 36 0a 01
                    ld        hl,$3e00                      ;[2724] 21 00 3e
                    push      hl                            ;[2727] e5
                    ld        hl,$5b3a                      ;[2728] 21 3a 5b
                    push      hl                            ;[272b] e5
                    ld        ($5c3d),sp                    ;[272c] ed 73 3d 5c
                    ld        hl,$2739                      ;[2730] 21 39 27
                    ld        ($5b6c),hl                    ;[2733] 22 6c 5b
                    jp        $1064                         ;[2736] c3 64 10
                    ld        sp,($5cb2)                    ;[2739] ed 7b b2 5c
                    inc       sp                            ;[273d] 33
                    ld        hl,$5bff                      ;[273e] 21 ff 5b
                    ld        ($5b6a),hl                    ;[2741] 22 6a 5b
                    halt                                    ;[2744] 76
                    res       5,(iy+$01)                    ;[2745] fd cb 01 ae
                    ld        a,($5c3a)                     ;[2749] 3a 3a 5c
                    inc       a                             ;[274c] 3c
                    push      af                            ;[274d] f5
                    ld        hl,$0000                      ;[274e] 21 00 00
                    ld        (iy+$37),h                    ;[2751] fd 74 37
                    ld        (iy+$26),h                    ;[2754] fd 74 26
                    ld        ($5c0b),hl                    ;[2757] 22 0b 5c
                    ld        hl,$0001                      ;[275a] 21 01 00
                    ld        ($5c16),hl                    ;[275d] 22 16 5c
                    rst       $28                           ;[2760] ef
                    or        b                             ;[2761] b0
                    ld        d,$fd                         ;[2762] 16 fd
                    sll       a                             ;[2764] cb 37
                    xor       (hl)                          ;[2766] ae
                    rst       $28                           ;[2767] ef
                    ld        l,(hl)                        ;[2768] 6e
                    dec       c                             ;[2769] 0d
                    set       5,(iy+$02)                    ;[276a] fd cb 02 ee
                    pop       af                            ;[276e] f1
                    ld        b,a                           ;[276f] 47
                    cp        $0a                           ;[2770] fe 0a
                    jr        c,$2782                       ;[2772] 38 0e
                    cp        $1d                           ;[2774] fe 1d
                    jr        c,$2780                       ;[2776] 38 08
                    cp        $2c                           ;[2778] fe 2c
                    jr        nc,$2788                      ;[277a] 30 0c
                    add       $14                           ;[277c] c6 14
                    jr        $2782                         ;[277e] 18 02
                    add       $07                           ;[2780] c6 07
                    rst       $28                           ;[2782] ef
                    rst       $28                           ;[2783] ef
                    dec       d                             ;[2784] 15
                    ld        a,$20                         ;[2785] 3e 20
                    rst       $10                           ;[2787] d7
                    ld        a,b                           ;[2788] 78
                    cp        $1d                           ;[2789] fe 1d
                    jr        c,$279f                       ;[278b] 38 12
                    sub       $1d                           ;[278d] d6 1d
                    ld        b,$00                         ;[278f] 06 00
                    ld        c,a                           ;[2791] 4f
                    ld        hl,$2873                      ;[2792] 21 73 28
                    add       hl,bc                         ;[2795] 09
                    add       hl,bc                         ;[2796] 09
                    ld        e,(hl)                        ;[2797] 5e
                    inc       hl                            ;[2798] 23
                    ld        d,(hl)                        ;[2799] 56
                    call      $2c40                         ;[279a] cd 40 2c
                    jr        $27a5                         ;[279d] 18 06
                    ld        de,$1391                      ;[279f] 11 91 13
                    rst       $28                           ;[27a2] ef
                    ld        a,(bc)                        ;[27a3] 0a
                    inc       c                             ;[27a4] 0c
                    xor       a                             ;[27a5] af
                    ld        de,$1536                      ;[27a6] 11 36 15
                    rst       $28                           ;[27a9] ef
                    ld        a,(bc)                        ;[27aa] 0a
                    inc       c                             ;[27ab] 0c
                    ld        bc,($5c45)                    ;[27ac] ed 4b 45 5c
                    rst       $28                           ;[27b0] ef
                    dec       de                            ;[27b1] 1b
                    ld        a,(de)                        ;[27b2] 1a
                    ld        a,$3a                         ;[27b3] 3e 3a
                    rst       $10                           ;[27b5] d7
                    ld        c,(iy+$0d)                    ;[27b6] fd 4e 0d
                    ld        b,$00                         ;[27b9] 06 00
                    rst       $28                           ;[27bb] ef
                    dec       de                            ;[27bc] 1b
                    ld        a,(de)                        ;[27bd] 1a
                    rst       $28                           ;[27be] ef
                    sub       a                             ;[27bf] 97
                    djnz      $27fc                         ;[27c0] 10 3a
                    ld        a,($3c5c)                     ;[27c2] 3a 5c 3c
                    jr        z,$27e2                       ;[27c5] 28 1b
                    cp        $09                           ;[27c7] fe 09
                    jr        z,$27cf                       ;[27c9] 28 04
                    cp        $15                           ;[27cb] fe 15
                    jr        nz,$27d2                      ;[27cd] 20 03
                    inc       (iy+$0d)                      ;[27cf] fd 34 0d
                    ld        bc,$0003                      ;[27d2] 01 03 00
                    ld        de,$5c70                      ;[27d5] 11 70 5c
                    ld        hl,$5c44                      ;[27d8] 21 44 5c
                    bit       7,(hl)                        ;[27db] cb 7e
                    jr        z,$27e0                       ;[27dd] 28 01
                    add       hl,bc                         ;[27df] 09
                    lddr                                    ;[27e0] ed b8
                    ld        (iy+$0a),$ff                  ;[27e2] fd 36 0a ff
                    res       3,(iy+$01)                    ;[27e6] fd cb 01 9e
                    ld        hl,$5b66                      ;[27ea] 21 66 5b
                    res       0,(hl)                        ;[27ed] cb 86
                    call      $3e80                         ;[27ef] cd 80 3e
                    add       b                             ;[27f2] 80
                    ld        b,$3e                         ;[27f3] 06 3e
                    djnz      $27f8                         ;[27f5] 10 01
                    nop                                     ;[27f7] 00
                    nop                                     ;[27f8] 00
                    jp        $274d                         ;[27f9] c3 4d 27
                    ld        ($5c49),bc                    ;[27fc] ed 43 49 5c
                    call      $2cfb                         ;[2800] cd fb 2c
                    ld        a,b                           ;[2803] 78
                    or        c                             ;[2804] b1
                    jr        z,$280f                       ;[2805] 28 08
                    ld        ($5c49),bc                    ;[2807] ed 43 49 5c
                    ld        ($ec08),bc                    ;[280b] ed 43 08 ec
                    call      $2cd6                         ;[280f] cd d6 2c
                    ld        hl,($5c5d)                    ;[2812] 2a 5d 5c
                    ex        de,hl                         ;[2815] eb
                    ld        hl,$27f4                      ;[2816] 21 f4 27
                    push      hl                            ;[2819] e5
                    ld        hl,($5c61)                    ;[281a] 2a 61 5c
                    scf                                     ;[281d] 37
                    sbc       hl,de                         ;[281e] ed 52
                    push      hl                            ;[2820] e5
                    ld        h,b                           ;[2821] 60
                    ld        l,c                           ;[2822] 69
                    rst       $28                           ;[2823] ef
                    ld        l,(hl)                        ;[2824] 6e
                    add       hl,de                         ;[2825] 19
                    jr        nz,$282e                      ;[2826] 20 06
                    rst       $28                           ;[2828] ef
                    cp        b                             ;[2829] b8
                    add       hl,de                         ;[282a] 19
                    rst       $28                           ;[282b] ef
                    ret       pe                            ;[282c] e8
                    add       hl,de                         ;[282d] 19
                    pop       bc                            ;[282e] c1
                    ld        a,c                           ;[282f] 79
                    dec       a                             ;[2830] 3d
                    or        b                             ;[2831] b0
                    jr        nz,$2849                      ;[2832] 20 15
                    call      $2cfb                         ;[2834] cd fb 2c
                    push      hl                            ;[2837] e5
                    ld        hl,($5c49)                    ;[2838] 2a 49 5c
                    call      $3e80                         ;[283b] cd 80 3e
                    dec       e                             ;[283e] 1d
                    inc       d                             ;[283f] 14
                    ld        ($5c49),hl                    ;[2840] 22 49 5c
                    pop       hl                            ;[2843] e1
                    call      $2cd6                         ;[2844] cd d6 2c
                    jr        $2871                         ;[2847] 18 28
                    push      bc                            ;[2849] c5
                    inc       bc                            ;[284a] 03
                    inc       bc                            ;[284b] 03
                    inc       bc                            ;[284c] 03
                    inc       bc                            ;[284d] 03
                    dec       hl                            ;[284e] 2b
                    ld        de,($5c53)                    ;[284f] ed 5b 53 5c
                    push      de                            ;[2853] d5
                    rst       $28                           ;[2854] ef
                    ld        d,l                           ;[2855] 55
                    ld        d,$e1                         ;[2856] 16 e1
                    ld        ($5c53),hl                    ;[2858] 22 53 5c
                    pop       bc                            ;[285b] c1
                    push      bc                            ;[285c] c5
                    inc       de                            ;[285d] 13
                    ld        hl,($5c61)                    ;[285e] 2a 61 5c
                    dec       hl                            ;[2861] 2b
                    dec       hl                            ;[2862] 2b
                    lddr                                    ;[2863] ed b8
                    ld        hl,($5c49)                    ;[2865] 2a 49 5c
                    ex        de,hl                         ;[2868] eb
                    pop       bc                            ;[2869] c1
                    ld        (hl),b                        ;[286a] 70
                    dec       hl                            ;[286b] 2b
                    ld        (hl),c                        ;[286c] 71
                    dec       hl                            ;[286d] 2b
                    ld        (hl),e                        ;[286e] 73
                    dec       hl                            ;[286f] 2b
                    ld        (hl),d                        ;[2870] 72
                    pop       af                            ;[2871] f1
                    ret                                     ;[2872] c9

                    rst       $18                           ;[2873] df
                    jr        z,$2860                       ;[2874] 28 ea
                    jr        z,$2871                       ;[2876] 28 f9
                    jr        z,$287d                       ;[2878] 28 03
                    add       hl,hl                         ;[287a] 29
                    inc       d                             ;[287b] 14
                    add       hl,hl                         ;[287c] 29
                    daa                                     ;[287d] 27
                    add       hl,hl                         ;[287e] 29
                    inc       sp                            ;[287f] 33
                    add       hl,hl                         ;[2880] 29
                    inc       sp                            ;[2881] 33
                    add       hl,hl                         ;[2882] 29
                    ld        b,(hl)                        ;[2883] 46
                    add       hl,hl                         ;[2884] 29
                    ld        d,h                           ;[2885] 54
                    add       hl,hl                         ;[2886] 29
                    ld        h,l                           ;[2887] 65
                    add       hl,hl                         ;[2888] 29
                    halt                                    ;[2889] 76
                    add       hl,hl                         ;[288a] 29
                    add       h                             ;[288b] 84
                    add       hl,hl                         ;[288c] 29
                    sub       l                             ;[288d] 95
                    add       hl,hl                         ;[288e] 29
                    and       c                             ;[288f] a1
                    add       hl,hl                         ;[2890] 29
                    or        h                             ;[2891] b4
                    add       hl,hl                         ;[2892] 29
                    or        h                             ;[2893] b4
                    add       hl,hl                         ;[2894] 29
                    ret       nz                            ;[2895] c0
                    add       hl,hl                         ;[2896] 29
                    adc       $29                           ;[2897] ce 29
                    add       ix,ix                         ;[2899] dd 29
                    ex        de,hl                         ;[289b] eb
                    add       hl,hl                         ;[289c] 29
                    cp        $29                           ;[289d] fe 29
                    rrca                                    ;[289f] 0f
                    ld        hl,($2a18)                    ;[28a0] 2a 18 2a
                    ld        h,$2a                         ;[28a3] 26 2a
                    scf                                     ;[28a5] 37
                    ld        hl,($2a44)                    ;[28a6] 2a 44 2a
                    ld        d,a                           ;[28a9] 57
                    ld        hl,($2a6f)                    ;[28aa] 2a 6f 2a
                    ld        a,l                           ;[28ad] 7d
                    ld        hl,($2a85)                    ;[28ae] 2a 85 2a
                    sub       c                             ;[28b1] 91
                    ld        hl,($2aa5)                    ;[28b2] 2a a5 2a
                    or        c                             ;[28b5] b1
                    ld        hl,($2ac0)                    ;[28b6] 2a c0 2a
                    rst       $10                           ;[28b9] d7
                    ld        hl,($2ae0)                    ;[28ba] 2a e0 2a
                    xor       $2a                           ;[28bd] ee 2a
                    push      af                            ;[28bf] f5
                    ld        hl,($2b09)                    ;[28c0] 2a 09 2b
                    ld        hl,$332b                      ;[28c3] 21 2b 33
                    dec       hl                            ;[28c6] 2b
                    ld        c,b                           ;[28c7] 48
                    dec       hl                            ;[28c8] 2b
                    ld        e,b                           ;[28c9] 58
                    dec       hl                            ;[28ca] 2b
                    ld        l,c                           ;[28cb] 69
                    dec       hl                            ;[28cc] 2b
                    add       c                             ;[28cd] 81
                    dec       hl                            ;[28ce] 2b
                    sbc       e                             ;[28cf] 9b
                    dec       hl                            ;[28d0] 2b
                    or        h                             ;[28d1] b4
                    dec       hl                            ;[28d2] 2b
                    sra       e                             ;[28d3] cb 2b
                    and       $2b                           ;[28d5] e6 2b
                    call      m,$092b                       ;[28d7] fc 2b 09
                    inc       l                             ;[28da] 2c
                    ld        a,(de)                        ;[28db] 1a
                    inc       l                             ;[28dc] 2c
                    inc       sp                            ;[28dd] 33
                    inc       l                             ;[28de] 2c
                    ld        c,l                           ;[28df] 4d
                    ld        b,l                           ;[28e0] 45
                    ld        d,d                           ;[28e1] 52
                    ld        b,a                           ;[28e2] 47
                    ld        b,l                           ;[28e3] 45
                    jr        nz,$294b                      ;[28e4] 20 65
                    ld        (hl),d                        ;[28e6] 72
                    ld        (hl),d                        ;[28e7] 72
                    ld        l,a                           ;[28e8] 6f
                    jp        p,$7257                       ;[28e9] f2 57 72
                    ld        l,a                           ;[28ec] 6f
                    ld        l,(hl)                        ;[28ed] 6e
                    ld        h,a                           ;[28ee] 67
                    jr        nz,$2957                      ;[28ef] 20 66
                    ld        l,c                           ;[28f1] 69
                    ld        l,h                           ;[28f2] 6c
                    ld        h,l                           ;[28f3] 65
                    jr        nz,$296a                      ;[28f4] 20 74
                    ld        a,c                           ;[28f6] 79
                    ld        (hl),b                        ;[28f7] 70
                    push      hl                            ;[28f8] e5
                    ld        b,e                           ;[28f9] 43
                    ld        c,a                           ;[28fa] 4f
                    ld        b,h                           ;[28fb] 44
                    ld        b,l                           ;[28fc] 45
                    jr        nz,$2964                      ;[28fd] 20 65
                    ld        (hl),d                        ;[28ff] 72
                    ld        (hl),d                        ;[2900] 72
                    ld        l,a                           ;[2901] 6f
                    jp        p,$6f54                       ;[2902] f2 54 6f
                    ld        l,a                           ;[2905] 6f
                    jr        nz,$2975                      ;[2906] 20 6d
                    ld        h,c                           ;[2908] 61
                    ld        l,(hl)                        ;[2909] 6e
                    ld        a,c                           ;[290a] 79
                    jr        nz,$296f                      ;[290b] 20 62
                    ld        (hl),d                        ;[290d] 72
                    ld        h,c                           ;[290e] 61
                    ld        h,e                           ;[290f] 63
                    ld        l,e                           ;[2910] 6b
                    ld        h,l                           ;[2911] 65
                    ld        (hl),h                        ;[2912] 74
                    di                                      ;[2913] f3
                    ld        b,(hl)                        ;[2914] 46
                    ld        l,c                           ;[2915] 69
                    ld        l,h                           ;[2916] 6c
                    ld        h,l                           ;[2917] 65
                    jr        nz,$297b                      ;[2918] 20 61
                    ld        l,h                           ;[291a] 6c
                    ld        (hl),d                        ;[291b] 72
                    ld        h,l                           ;[291c] 65
                    ld        h,c                           ;[291d] 61
                    ld        h,h                           ;[291e] 64
                    ld        a,c                           ;[291f] 79
                    jr        nz,$2987                      ;[2920] 20 65
                    ld        a,b                           ;[2922] 78
                    ld        l,c                           ;[2923] 69
                    ld        (hl),e                        ;[2924] 73
                    ld        (hl),h                        ;[2925] 74
                    di                                      ;[2926] f3
                    ld        c,c                           ;[2927] 49
                    ld        l,(hl)                        ;[2928] 6e
                    halt                                    ;[2929] 76
                    ld        h,c                           ;[292a] 61
                    ld        l,h                           ;[292b] 6c
                    ld        l,c                           ;[292c] 69
                    ld        h,h                           ;[292d] 64
                    jr        nz,$299e                      ;[292e] 20 6e
                    ld        h,c                           ;[2930] 61
                    ld        l,l                           ;[2931] 6d
                    push      hl                            ;[2932] e5
                    ld        b,(hl)                        ;[2933] 46
                    ld        l,c                           ;[2934] 69
                    ld        l,h                           ;[2935] 6c
                    ld        h,l                           ;[2936] 65
                    jr        nz,$299d                      ;[2937] 20 64
                    ld        l,a                           ;[2939] 6f
                    ld        h,l                           ;[293a] 65
                    ld        (hl),e                        ;[293b] 73
                    jr        nz,$29ac                      ;[293c] 20 6e
                    ld        l,a                           ;[293e] 6f
                    ld        (hl),h                        ;[293f] 74
                    jr        nz,$29a7                      ;[2940] 20 65
                    ld        a,b                           ;[2942] 78
                    ld        l,c                           ;[2943] 69
                    ld        (hl),e                        ;[2944] 73
                    call      p,$6e49                       ;[2945] f4 49 6e
                    halt                                    ;[2948] 76
                    ld        h,c                           ;[2949] 61
                    ld        l,h                           ;[294a] 6c
                    ld        l,c                           ;[294b] 69
                    ld        h,h                           ;[294c] 64
                    jr        nz,$29b3                      ;[294d] 20 64
                    ld        h,l                           ;[294f] 65
                    halt                                    ;[2950] 76
                    ld        l,c                           ;[2951] 69
                    ld        h,e                           ;[2952] 63
                    push      hl                            ;[2953] e5
                    ld        c,c                           ;[2954] 49
                    ld        l,(hl)                        ;[2955] 6e
                    halt                                    ;[2956] 76
                    ld        h,c                           ;[2957] 61
                    ld        l,h                           ;[2958] 6c
                    ld        l,c                           ;[2959] 69
                    ld        h,h                           ;[295a] 64
                    jr        nz,$29bf                      ;[295b] 20 62
                    ld        h,c                           ;[295d] 61
                    ld        (hl),l                        ;[295e] 75
                    ld        h,h                           ;[295f] 64
                    jr        nz,$29d4                      ;[2960] 20 72
                    ld        h,c                           ;[2962] 61
                    ld        (hl),h                        ;[2963] 74
                    push      hl                            ;[2964] e5
                    ld        c,c                           ;[2965] 49
                    ld        l,(hl)                        ;[2966] 6e
                    halt                                    ;[2967] 76
                    ld        h,c                           ;[2968] 61
                    ld        l,h                           ;[2969] 6c
                    ld        l,c                           ;[296a] 69
                    ld        h,h                           ;[296b] 64
                    jr        nz,$29dc                      ;[296c] 20 6e
                    ld        l,a                           ;[296e] 6f
                    ld        (hl),h                        ;[296f] 74
                    ld        h,l                           ;[2970] 65
                    jr        nz,$29e1                      ;[2971] 20 6e
                    ld        h,c                           ;[2973] 61
                    ld        l,l                           ;[2974] 6d
                    push      hl                            ;[2975] e5
                    ld        c,(hl)                        ;[2976] 4e
                    ld        (hl),l                        ;[2977] 75
                    ld        l,l                           ;[2978] 6d
                    ld        h,d                           ;[2979] 62
                    ld        h,l                           ;[297a] 65
                    ld        (hl),d                        ;[297b] 72
                    jr        nz,$29f2                      ;[297c] 20 74
                    ld        l,a                           ;[297e] 6f
                    ld        l,a                           ;[297f] 6f
                    jr        nz,$29e4                      ;[2980] 20 62
                    ld        l,c                           ;[2982] 69
                    rst       $20                           ;[2983] e7
                    ld        c,(hl)                        ;[2984] 4e
                    ld        l,a                           ;[2985] 6f
                    ld        (hl),h                        ;[2986] 74
                    ld        h,l                           ;[2987] 65
                    jr        nz,$29f9                      ;[2988] 20 6f
                    ld        (hl),l                        ;[298a] 75
                    ld        (hl),h                        ;[298b] 74
                    jr        nz,$29fd                      ;[298c] 20 6f
                    ld        h,(hl)                        ;[298e] 66
                    jr        nz,$2a03                      ;[298f] 20 72
                    ld        h,c                           ;[2991] 61
                    ld        l,(hl)                        ;[2992] 6e
                    ld        h,a                           ;[2993] 67
                    push      hl                            ;[2994] e5
                    ld        c,a                           ;[2995] 4f
                    ld        (hl),l                        ;[2996] 75
                    ld        (hl),h                        ;[2997] 74
                    jr        nz,$2a09                      ;[2998] 20 6f
                    ld        h,(hl)                        ;[299a] 66
                    jr        nz,$2a0f                      ;[299b] 20 72
                    ld        h,c                           ;[299d] 61
                    ld        l,(hl)                        ;[299e] 6e
                    ld        h,a                           ;[299f] 67
                    push      hl                            ;[29a0] e5
                    ld        d,h                           ;[29a1] 54
                    ld        l,a                           ;[29a2] 6f
                    ld        l,a                           ;[29a3] 6f
                    jr        nz,$2a13                      ;[29a4] 20 6d
                    ld        h,c                           ;[29a6] 61
                    ld        l,(hl)                        ;[29a7] 6e
                    ld        a,c                           ;[29a8] 79
                    jr        nz,$2a1f                      ;[29a9] 20 74
                    ld        l,c                           ;[29ab] 69
                    ld        h,l                           ;[29ac] 65
                    ld        h,h                           ;[29ad] 64
                    jr        nz,$2a1e                      ;[29ae] 20 6e
                    ld        l,a                           ;[29b0] 6f
                    ld        (hl),h                        ;[29b1] 74
                    ld        h,l                           ;[29b2] 65
                    di                                      ;[29b3] f3
                    ld        b,d                           ;[29b4] 42
                    ld        h,c                           ;[29b5] 61
                    ld        h,h                           ;[29b6] 64
                    jr        nz,$2a1f                      ;[29b7] 20 66
                    ld        l,c                           ;[29b9] 69
                    ld        l,h                           ;[29ba] 6c
                    ld        h,l                           ;[29bb] 65
                    ld        l,(hl)                        ;[29bc] 6e
                    ld        h,c                           ;[29bd] 61
                    ld        l,l                           ;[29be] 6d
                    push      hl                            ;[29bf] e5
                    ld        b,d                           ;[29c0] 42
                    ld        h,c                           ;[29c1] 61
                    ld        h,h                           ;[29c2] 64
                    jr        nz,$2a35                      ;[29c3] 20 70
                    ld        h,c                           ;[29c5] 61
                    ld        (hl),d                        ;[29c6] 72
                    ld        h,c                           ;[29c7] 61
                    ld        l,l                           ;[29c8] 6d
                    ld        h,l                           ;[29c9] 65
                    ld        (hl),h                        ;[29ca] 74
                    ld        h,l                           ;[29cb] 65
                    ld        (hl),d                        ;[29cc] 72
                    di                                      ;[29cd] f3
                    ld        b,h                           ;[29ce] 44
                    ld        (hl),d                        ;[29cf] 72
                    ld        l,c                           ;[29d0] 69
                    halt                                    ;[29d1] 76
                    ld        h,l                           ;[29d2] 65
                    jr        nz,$2a43                      ;[29d3] 20 6e
                    ld        l,a                           ;[29d5] 6f
                    ld        (hl),h                        ;[29d6] 74
                    jr        nz,$2a3f                      ;[29d7] 20 66
                    ld        l,a                           ;[29d9] 6f
                    ld        (hl),l                        ;[29da] 75
                    ld        l,(hl)                        ;[29db] 6e
                    call      po,$6946                      ;[29dc] e4 46 69
                    ld        l,h                           ;[29df] 6c
                    ld        h,l                           ;[29e0] 65
                    jr        nz,$2a51                      ;[29e1] 20 6e
                    ld        l,a                           ;[29e3] 6f
                    ld        (hl),h                        ;[29e4] 74
                    jr        nz,$2a4d                      ;[29e5] 20 66
                    ld        l,a                           ;[29e7] 6f
                    ld        (hl),l                        ;[29e8] 75
                    ld        l,(hl)                        ;[29e9] 6e
                    call      po,$6946                      ;[29ea] e4 46 69
                    ld        l,h                           ;[29ed] 6c
                    ld        h,l                           ;[29ee] 65
                    jr        nz,$2a52                      ;[29ef] 20 61
                    ld        l,h                           ;[29f1] 6c
                    ld        (hl),d                        ;[29f2] 72
                    ld        h,l                           ;[29f3] 65
                    ld        h,c                           ;[29f4] 61
                    ld        h,h                           ;[29f5] 64
                    ld        a,c                           ;[29f6] 79
                    jr        nz,$2a5e                      ;[29f7] 20 65
                    ld        a,b                           ;[29f9] 78
                    ld        l,c                           ;[29fa] 69
                    ld        (hl),e                        ;[29fb] 73
                    ld        (hl),h                        ;[29fc] 74
                    di                                      ;[29fd] f3
                    ld        b,l                           ;[29fe] 45
                    ld        l,(hl)                        ;[29ff] 6e
                    ld        h,h                           ;[2a00] 64
                    jr        nz,$2a72                      ;[2a01] 20 6f
                    ld        h,(hl)                        ;[2a03] 66
                    jr        nz,$2a6c                      ;[2a04] 20 66
                    ld        l,c                           ;[2a06] 69
                    ld        l,h                           ;[2a07] 6c
                    ld        h,l                           ;[2a08] 65
                    jr        nz,$2a71                      ;[2a09] 20 66
                    ld        l,a                           ;[2a0b] 6f
                    ld        (hl),l                        ;[2a0c] 75
                    ld        l,(hl)                        ;[2a0d] 6e
                    call      po,$6944                      ;[2a0e] e4 44 69
                    ld        (hl),e                        ;[2a11] 73
                    ld        l,e                           ;[2a12] 6b
                    jr        nz,$2a7b                      ;[2a13] 20 66
                    ld        (hl),l                        ;[2a15] 75
                    ld        l,h                           ;[2a16] 6c
                    call      pe,$6944                      ;[2a17] ec 44 69
                    ld        (hl),d                        ;[2a1a] 72
                    ld        h,l                           ;[2a1b] 65
                    ld        h,e                           ;[2a1c] 63
                    ld        (hl),h                        ;[2a1d] 74
                    ld        l,a                           ;[2a1e] 6f
                    ld        (hl),d                        ;[2a1f] 72
                    ld        a,c                           ;[2a20] 79
                    jr        nz,$2a89                      ;[2a21] 20 66
                    ld        (hl),l                        ;[2a23] 75
                    ld        l,h                           ;[2a24] 6c
                    call      pe,$6946                      ;[2a25] ec 46 69
                    ld        l,h                           ;[2a28] 6c
                    ld        h,l                           ;[2a29] 65
                    jr        nz,$2a95                      ;[2a2a] 20 69
                    ld        (hl),e                        ;[2a2c] 73
                    jr        nz,$2aa1                      ;[2a2d] 20 72
                    ld        h,l                           ;[2a2f] 65
                    ld        h,c                           ;[2a30] 61
                    ld        h,h                           ;[2a31] 64
                    jr        nz,$2aa3                      ;[2a32] 20 6f
                    ld        l,(hl)                        ;[2a34] 6e
                    ld        l,h                           ;[2a35] 6c
                    ld        sp,hl                         ;[2a36] f9
                    ld        b,(hl)                        ;[2a37] 46
                    ld        l,c                           ;[2a38] 69
                    ld        l,h                           ;[2a39] 6c
                    ld        h,l                           ;[2a3a] 65
                    jr        nz,$2aab                      ;[2a3b] 20 6e
                    ld        l,a                           ;[2a3d] 6f
                    ld        (hl),h                        ;[2a3e] 74
                    jr        nz,$2ab0                      ;[2a3f] 20 6f
                    ld        (hl),b                        ;[2a41] 70
                    ld        h,l                           ;[2a42] 65
                    xor       $46                           ;[2a43] ee 46
                    ld        l,c                           ;[2a45] 69
                    ld        l,h                           ;[2a46] 6c
                    ld        h,l                           ;[2a47] 65
                    jr        nz,$2aab                      ;[2a48] 20 61
                    ld        l,h                           ;[2a4a] 6c
                    ld        (hl),d                        ;[2a4b] 72
                    ld        h,l                           ;[2a4c] 65
                    ld        h,c                           ;[2a4d] 61
                    ld        h,h                           ;[2a4e] 64
                    ld        a,c                           ;[2a4f] 79
                    jr        nz,$2abb                      ;[2a50] 20 69
                    ld        l,(hl)                        ;[2a52] 6e
                    jr        nz,$2aca                      ;[2a53] 20 75
                    ld        (hl),e                        ;[2a55] 73
                    push      hl                            ;[2a56] e5
                    ld        c,(hl)                        ;[2a57] 4e
                    ld        l,a                           ;[2a58] 6f
                    jr        nz,$2acd                      ;[2a59] 20 72
                    ld        h,l                           ;[2a5b] 65
                    ld        l,(hl)                        ;[2a5c] 6e
                    ld        h,c                           ;[2a5d] 61
                    ld        l,l                           ;[2a5e] 6d
                    ld        h,l                           ;[2a5f] 65
                    jr        nz,$2ac4                      ;[2a60] 20 62
                    ld        h,l                           ;[2a62] 65
                    ld        (hl),h                        ;[2a63] 74
                    ld        (hl),a                        ;[2a64] 77
                    ld        h,l                           ;[2a65] 65
                    ld        h,l                           ;[2a66] 65
                    ld        l,(hl)                        ;[2a67] 6e
                    jr        nz,$2ace                      ;[2a68] 20 64
                    ld        (hl),d                        ;[2a6a] 72
                    ld        l,c                           ;[2a6b] 69
                    halt                                    ;[2a6c] 76
                    ld        h,l                           ;[2a6d] 65
                    di                                      ;[2a6e] f3
                    ld        c,l                           ;[2a6f] 4d
                    ld        l,c                           ;[2a70] 69
                    ld        (hl),e                        ;[2a71] 73
                    ld        (hl),e                        ;[2a72] 73
                    ld        l,c                           ;[2a73] 69
                    ld        l,(hl)                        ;[2a74] 6e
                    ld        h,a                           ;[2a75] 67
                    jr        nz,$2add                      ;[2a76] 20 65
                    ld        a,b                           ;[2a78] 78
                    ld        (hl),h                        ;[2a79] 74
                    ld        h,l                           ;[2a7a] 65
                    ld        l,(hl)                        ;[2a7b] 6e
                    call      p,$6e55                       ;[2a7c] f4 55 6e
                    ld        h,e                           ;[2a7f] 63
                    ld        h,c                           ;[2a80] 61
                    ld        h,e                           ;[2a81] 63
                    ld        l,b                           ;[2a82] 68
                    ld        h,l                           ;[2a83] 65
                    call      po,$6946                      ;[2a84] e4 46 69
                    ld        l,h                           ;[2a87] 6c
                    ld        h,l                           ;[2a88] 65
                    jr        nz,$2aff                      ;[2a89] 20 74
                    ld        l,a                           ;[2a8b] 6f
                    ld        l,a                           ;[2a8c] 6f
                    jr        nz,$2af1                      ;[2a8d] 20 62
                    ld        l,c                           ;[2a8f] 69
                    rst       $20                           ;[2a90] e7
                    ld        b,h                           ;[2a91] 44
                    ld        l,c                           ;[2a92] 69
                    ld        (hl),e                        ;[2a93] 73
                    ld        l,e                           ;[2a94] 6b
                    jr        nz,$2b00                      ;[2a95] 20 69
                    ld        (hl),e                        ;[2a97] 73
                    jr        nz,$2b08                      ;[2a98] 20 6e
                    ld        l,a                           ;[2a9a] 6f
                    ld        (hl),h                        ;[2a9b] 74
                    jr        nz,$2b00                      ;[2a9c] 20 62
                    ld        l,a                           ;[2a9e] 6f
                    ld        l,a                           ;[2a9f] 6f
                    ld        (hl),h                        ;[2aa0] 74
                    ld        h,c                           ;[2aa1] 61
                    ld        h,d                           ;[2aa2] 62
                    ld        l,h                           ;[2aa3] 6c
                    push      hl                            ;[2aa4] e5
                    ld        b,h                           ;[2aa5] 44
                    ld        (hl),d                        ;[2aa6] 72
                    ld        l,c                           ;[2aa7] 69
                    halt                                    ;[2aa8] 76
                    ld        h,l                           ;[2aa9] 65
                    jr        nz,$2b15                      ;[2aaa] 20 69
                    ld        l,(hl)                        ;[2aac] 6e
                    jr        nz,$2b24                      ;[2aad] 20 75
                    ld        (hl),e                        ;[2aaf] 73
                    push      hl                            ;[2ab0] e5
                    ld        b,h                           ;[2ab1] 44
                    ld        (hl),d                        ;[2ab2] 72
                    ld        l,c                           ;[2ab3] 69
                    halt                                    ;[2ab4] 76
                    ld        h,l                           ;[2ab5] 65
                    jr        nz,$2b26                      ;[2ab6] 20 6e
                    ld        l,a                           ;[2ab8] 6f
                    ld        (hl),h                        ;[2ab9] 74
                    jr        nz,$2b2e                      ;[2aba] 20 72
                    ld        h,l                           ;[2abc] 65
                    ld        h,c                           ;[2abd] 61
                    ld        h,h                           ;[2abe] 64
                    ld        sp,hl                         ;[2abf] f9
                    ld        b,h                           ;[2ac0] 44
                    ld        l,c                           ;[2ac1] 69
                    ld        (hl),e                        ;[2ac2] 73
                    ld        l,e                           ;[2ac3] 6b
                    jr        nz,$2b2f                      ;[2ac4] 20 69
                    ld        (hl),e                        ;[2ac6] 73
                    jr        nz,$2b40                      ;[2ac7] 20 77
                    ld        (hl),d                        ;[2ac9] 72
                    ld        l,c                           ;[2aca] 69
                    ld        (hl),h                        ;[2acb] 74
                    ld        h,l                           ;[2acc] 65
                    jr        nz,$2b3f                      ;[2acd] 20 70
                    ld        (hl),d                        ;[2acf] 72
                    ld        l,a                           ;[2ad0] 6f
                    ld        (hl),h                        ;[2ad1] 74
                    ld        h,l                           ;[2ad2] 65
                    ld        h,e                           ;[2ad3] 63
                    ld        (hl),h                        ;[2ad4] 74
                    ld        h,l                           ;[2ad5] 65
                    call      po,$6553                      ;[2ad6] e4 53 65
                    ld        h,l                           ;[2ad9] 65
                    ld        l,e                           ;[2ada] 6b
                    jr        nz,$2b43                      ;[2adb] 20 66
                    ld        h,c                           ;[2add] 61
                    ld        l,c                           ;[2ade] 69
                    call      pe,$5243                      ;[2adf] ec 43 52
                    ld        b,e                           ;[2ae2] 43
                    jr        nz,$2b49                      ;[2ae3] 20 64
                    ld        h,c                           ;[2ae5] 61
                    ld        (hl),h                        ;[2ae6] 74
                    ld        h,c                           ;[2ae7] 61
                    jr        nz,$2b4f                      ;[2ae8] 20 65
                    ld        (hl),d                        ;[2aea] 72
                    ld        (hl),d                        ;[2aeb] 72
                    ld        l,a                           ;[2aec] 6f
                    jp        p,$6f4e                       ;[2aed] f2 4e 6f
                    jr        nz,$2b56                      ;[2af0] 20 64
                    ld        h,c                           ;[2af2] 61
                    ld        (hl),h                        ;[2af3] 74
                    pop       hl                            ;[2af4] e1
                    ld        c,l                           ;[2af5] 4d
                    ld        l,c                           ;[2af6] 69
                    ld        (hl),e                        ;[2af7] 73
                    ld        (hl),e                        ;[2af8] 73
                    ld        l,c                           ;[2af9] 69
                    ld        l,(hl)                        ;[2afa] 6e
                    ld        h,a                           ;[2afb] 67
                    jr        nz,$2b5f                      ;[2afc] 20 61
                    ld        h,h                           ;[2afe] 64
                    ld        h,h                           ;[2aff] 64
                    ld        (hl),d                        ;[2b00] 72
                    ld        h,l                           ;[2b01] 65
                    ld        (hl),e                        ;[2b02] 73
                    ld        (hl),e                        ;[2b03] 73
                    jr        nz,$2b73                      ;[2b04] 20 6d
                    ld        h,c                           ;[2b06] 61
                    ld        (hl),d                        ;[2b07] 72
                    ex        de,hl                         ;[2b08] eb
                    ld        d,l                           ;[2b09] 55
                    ld        l,(hl)                        ;[2b0a] 6e
                    ld        (hl),d                        ;[2b0b] 72
                    ld        h,l                           ;[2b0c] 65
                    ld        h,e                           ;[2b0d] 63
                    ld        l,a                           ;[2b0e] 6f
                    ld        h,a                           ;[2b0f] 67
                    ld        l,(hl)                        ;[2b10] 6e
                    ld        l,c                           ;[2b11] 69
                    ld        (hl),e                        ;[2b12] 73
                    ld        h,l                           ;[2b13] 65
                    ld        h,h                           ;[2b14] 64
                    jr        nz,$2b7b                      ;[2b15] 20 64
                    ld        l,c                           ;[2b17] 69
                    ld        (hl),e                        ;[2b18] 73
                    ld        l,e                           ;[2b19] 6b
                    jr        nz,$2b82                      ;[2b1a] 20 66
                    ld        l,a                           ;[2b1c] 6f
                    ld        (hl),d                        ;[2b1d] 72
                    ld        l,l                           ;[2b1e] 6d
                    ld        h,c                           ;[2b1f] 61
                    call      p,$6e55                       ;[2b20] f4 55 6e
                    ld        l,e                           ;[2b23] 6b
                    ld        l,(hl)                        ;[2b24] 6e
                    ld        l,a                           ;[2b25] 6f
                    ld        (hl),a                        ;[2b26] 77
                    ld        l,(hl)                        ;[2b27] 6e
                    jr        nz,$2b8e                      ;[2b28] 20 64
                    ld        l,c                           ;[2b2a] 69
                    ld        (hl),e                        ;[2b2b] 73
                    ld        l,e                           ;[2b2c] 6b
                    jr        nz,$2b94                      ;[2b2d] 20 65
                    ld        (hl),d                        ;[2b2f] 72
                    ld        (hl),d                        ;[2b30] 72
                    ld        l,a                           ;[2b31] 6f
                    jp        p,$6944                       ;[2b32] f2 44 69
                    ld        (hl),e                        ;[2b35] 73
                    ld        l,e                           ;[2b36] 6b
                    jr        nz,$2ba1                      ;[2b37] 20 68
                    ld        h,c                           ;[2b39] 61
                    ld        (hl),e                        ;[2b3a] 73
                    jr        nz,$2b9f                      ;[2b3b] 20 62
                    ld        h,l                           ;[2b3d] 65
                    ld        h,l                           ;[2b3e] 65
                    ld        l,(hl)                        ;[2b3f] 6e
                    jr        nz,$2ba5                      ;[2b40] 20 63
                    ld        l,b                           ;[2b42] 68
                    ld        h,c                           ;[2b43] 61
                    ld        l,(hl)                        ;[2b44] 6e
                    ld        h,a                           ;[2b45] 67
                    ld        h,l                           ;[2b46] 65
                    call      po,$6e55                      ;[2b47] e4 55 6e
                    ld        (hl),e                        ;[2b4a] 73
                    ld        (hl),l                        ;[2b4b] 75
                    ld        l,c                           ;[2b4c] 69
                    ld        (hl),h                        ;[2b4d] 74
                    ld        h,c                           ;[2b4e] 61
                    ld        h,d                           ;[2b4f] 62
                    ld        l,h                           ;[2b50] 6c
                    ld        h,l                           ;[2b51] 65
                    jr        nz,$2bc1                      ;[2b52] 20 6d
                    ld        h,l                           ;[2b54] 65
                    ld        h,h                           ;[2b55] 64
                    ld        l,c                           ;[2b56] 69
                    pop       hl                            ;[2b57] e1
                    ld        c,c                           ;[2b58] 49
                    ld        l,(hl)                        ;[2b59] 6e
                    halt                                    ;[2b5a] 76
                    ld        h,c                           ;[2b5b] 61
                    ld        l,h                           ;[2b5c] 6c
                    ld        l,c                           ;[2b5d] 69
                    ld        h,h                           ;[2b5e] 64
                    jr        nz,$2bc2                      ;[2b5f] 20 61
                    ld        (hl),h                        ;[2b61] 74
                    ld        (hl),h                        ;[2b62] 74
                    ld        (hl),d                        ;[2b63] 72
                    ld        l,c                           ;[2b64] 69
                    ld        h,d                           ;[2b65] 62
                    ld        (hl),l                        ;[2b66] 75
                    ld        (hl),h                        ;[2b67] 74
                    push      hl                            ;[2b68] e5
                    ld        b,e                           ;[2b69] 43
                    ld        h,c                           ;[2b6a] 61
                    ld        l,(hl)                        ;[2b6b] 6e
                    ld        l,(hl)                        ;[2b6c] 6e
                    ld        l,a                           ;[2b6d] 6f
                    ld        (hl),h                        ;[2b6e] 74
                    jr        nz,$2bd4                      ;[2b6f] 20 63
                    ld        l,a                           ;[2b71] 6f
                    ld        (hl),b                        ;[2b72] 70
                    ld        a,c                           ;[2b73] 79
                    jr        nz,$2bea                      ;[2b74] 20 74
                    ld        l,a                           ;[2b76] 6f
                    cpl                                     ;[2b77] 2f
                    ld        h,(hl)                        ;[2b78] 66
                    ld        (hl),d                        ;[2b79] 72
                    ld        l,a                           ;[2b7a] 6f
                    ld        l,l                           ;[2b7b] 6d
                    jr        nz,$2bf2                      ;[2b7c] 20 74
                    ld        h,c                           ;[2b7e] 61
                    ld        (hl),b                        ;[2b7f] 70
                    push      hl                            ;[2b80] e5
                    ld        b,h                           ;[2b81] 44
                    ld        h,l                           ;[2b82] 65
                    ld        (hl),e                        ;[2b83] 73
                    ld        (hl),h                        ;[2b84] 74
                    ld        l,c                           ;[2b85] 69
                    ld        l,(hl)                        ;[2b86] 6e
                    ld        h,c                           ;[2b87] 61
                    ld        (hl),h                        ;[2b88] 74
                    ld        l,c                           ;[2b89] 69
                    ld        l,a                           ;[2b8a] 6f
                    ld        l,(hl)                        ;[2b8b] 6e
                    jr        nz,$2bf1                      ;[2b8c] 20 63
                    ld        h,c                           ;[2b8e] 61
                    ld        l,(hl)                        ;[2b8f] 6e
                    ld        l,(hl)                        ;[2b90] 6e
                    ld        l,a                           ;[2b91] 6f
                    ld        (hl),h                        ;[2b92] 74
                    jr        nz,$2bf7                      ;[2b93] 20 62
                    ld        h,l                           ;[2b95] 65
                    jr        nz,$2c0f                      ;[2b96] 20 77
                    ld        l,c                           ;[2b98] 69
                    ld        l,h                           ;[2b99] 6c
                    call      po,$6544                      ;[2b9a] e4 44 65
                    ld        (hl),e                        ;[2b9d] 73
                    ld        (hl),h                        ;[2b9e] 74
                    ld        l,c                           ;[2b9f] 69
                    ld        l,(hl)                        ;[2ba0] 6e
                    ld        h,c                           ;[2ba1] 61
                    ld        (hl),h                        ;[2ba2] 74
                    ld        l,c                           ;[2ba3] 69
                    ld        l,a                           ;[2ba4] 6f
                    ld        l,(hl)                        ;[2ba5] 6e
                    jr        nz,$2c15                      ;[2ba6] 20 6d
                    ld        (hl),l                        ;[2ba8] 75
                    ld        (hl),e                        ;[2ba9] 73
                    ld        (hl),h                        ;[2baa] 74
                    jr        nz,$2c0f                      ;[2bab] 20 62
                    ld        h,l                           ;[2bad] 65
                    jr        nz,$2c14                      ;[2bae] 20 64
                    ld        (hl),d                        ;[2bb0] 72
                    ld        l,c                           ;[2bb1] 69
                    halt                                    ;[2bb2] 76
                    push      hl                            ;[2bb3] e5
                    ld        b,h                           ;[2bb4] 44
                    ld        (hl),d                        ;[2bb5] 72
                    ld        l,c                           ;[2bb6] 69
                    halt                                    ;[2bb7] 76
                    ld        h,l                           ;[2bb8] 65
                    jr        nz,$2bfd                      ;[2bb9] 20 42
                    ld        a,($6920)                     ;[2bbb] 3a 20 69
                    ld        (hl),e                        ;[2bbe] 73
                    jr        nz,$2c2f                      ;[2bbf] 20 6e
                    ld        l,a                           ;[2bc1] 6f
                    ld        (hl),h                        ;[2bc2] 74
                    jr        nz,$2c35                      ;[2bc3] 20 70
                    ld        (hl),d                        ;[2bc5] 72
                    ld        h,l                           ;[2bc6] 65
                    ld        (hl),e                        ;[2bc7] 73
                    ld        h,l                           ;[2bc8] 65
                    ld        l,(hl)                        ;[2bc9] 6e
                    call      p,$322b                       ;[2bca] f4 2b 32
                    ld        b,c                           ;[2bcd] 41
                    jr        nz,$2c34                      ;[2bce] 20 64
                    ld        l,a                           ;[2bd0] 6f
                    ld        h,l                           ;[2bd1] 65
                    ld        (hl),e                        ;[2bd2] 73
                    jr        nz,$2c43                      ;[2bd3] 20 6e
                    ld        l,a                           ;[2bd5] 6f
                    ld        (hl),h                        ;[2bd6] 74
                    jr        nz,$2c4c                      ;[2bd7] 20 73
                    ld        (hl),l                        ;[2bd9] 75
                    ld        (hl),b                        ;[2bda] 70
                    ld        (hl),b                        ;[2bdb] 70
                    ld        l,a                           ;[2bdc] 6f
                    ld        (hl),d                        ;[2bdd] 72
                    ld        (hl),h                        ;[2bde] 74
                    jr        nz,$2c47                      ;[2bdf] 20 66
                    ld        l,a                           ;[2be1] 6f
                    ld        (hl),d                        ;[2be2] 72
                    ld        l,l                           ;[2be3] 6d
                    ld        h,c                           ;[2be4] 61
                    call      p,$7244                       ;[2be5] f4 44 72
                    ld        l,c                           ;[2be8] 69
                    halt                                    ;[2be9] 76
                    ld        h,l                           ;[2bea] 65
                    jr        nz,$2c5a                      ;[2beb] 20 6d
                    ld        (hl),l                        ;[2bed] 75
                    ld        (hl),e                        ;[2bee] 73
                    ld        (hl),h                        ;[2bef] 74
                    jr        nz,$2c54                      ;[2bf0] 20 62
                    ld        h,l                           ;[2bf2] 65
                    jr        nz,$2c36                      ;[2bf3] 20 41
                    ld        a,($6f20)                     ;[2bf5] 3a 20 6f
                    ld        (hl),d                        ;[2bf8] 72
                    jr        nz,$2c3d                      ;[2bf9] 20 42
                    cp        d                             ;[2bfb] ba
                    ld        c,c                           ;[2bfc] 49
                    ld        l,(hl)                        ;[2bfd] 6e
                    halt                                    ;[2bfe] 76
                    ld        h,c                           ;[2bff] 61
                    ld        l,h                           ;[2c00] 6c
                    ld        l,c                           ;[2c01] 69
                    ld        h,h                           ;[2c02] 64
                    jr        nz,$2c69                      ;[2c03] 20 64
                    ld        (hl),d                        ;[2c05] 72
                    ld        l,c                           ;[2c06] 69
                    halt                                    ;[2c07] 76
                    push      hl                            ;[2c08] e5
                    ld        b,e                           ;[2c09] 43
                    ld        l,a                           ;[2c0a] 6f
                    ld        h,h                           ;[2c0b] 64
                    ld        h,l                           ;[2c0c] 65
                    jr        nz,$2c7b                      ;[2c0d] 20 6c
                    ld        h,l                           ;[2c0f] 65
                    ld        l,(hl)                        ;[2c10] 6e
                    ld        h,a                           ;[2c11] 67
                    ld        (hl),h                        ;[2c12] 74
                    ld        l,b                           ;[2c13] 68
                    jr        nz,$2c7b                      ;[2c14] 20 65
                    ld        (hl),d                        ;[2c16] 72
                    ld        (hl),d                        ;[2c17] 72
                    ld        l,a                           ;[2c18] 6f
                    jp        p,$6f59                       ;[2c19] f2 59 6f
                    ld        (hl),l                        ;[2c1c] 75
                    jr        nz,$2c92                      ;[2c1d] 20 73
                    ld        l,b                           ;[2c1f] 68
                    ld        l,a                           ;[2c20] 6f
                    ld        (hl),l                        ;[2c21] 75
                    ld        l,h                           ;[2c22] 6c
                    ld        h,h                           ;[2c23] 64
                    jr        nz,$2c94                      ;[2c24] 20 6e
                    ld        h,l                           ;[2c26] 65
                    halt                                    ;[2c27] 76
                    ld        h,l                           ;[2c28] 65
                    ld        (hl),d                        ;[2c29] 72
                    jr        nz,$2c9f                      ;[2c2a] 20 73
                    ld        h,l                           ;[2c2c] 65
                    ld        h,l                           ;[2c2d] 65
                    jr        nz,$2ca4                      ;[2c2e] 20 74
                    ld        l,b                           ;[2c30] 68
                    ld        l,c                           ;[2c31] 69
                    di                                      ;[2c32] f3
                    ld        c,b                           ;[2c33] 48
                    ld        h,l                           ;[2c34] 65
                    ld        l,h                           ;[2c35] 6c
                    ld        l,h                           ;[2c36] 6c
                    ld        l,a                           ;[2c37] 6f
                    jr        nz,$2cae                      ;[2c38] 20 74
                    ld        l,b                           ;[2c3a] 68
                    ld        h,l                           ;[2c3b] 65
                    ld        (hl),d                        ;[2c3c] 72
                    ld        h,l                           ;[2c3d] 65
                    jr        nz,$2c61                      ;[2c3e] 20 21
                    ld        a,(de)                        ;[2c40] 1a
                    and       $7f                           ;[2c41] e6 7f
                    push      de                            ;[2c43] d5
                    rst       $10                           ;[2c44] d7
                    pop       de                            ;[2c45] d1
                    ld        a,(de)                        ;[2c46] 1a
                    inc       de                            ;[2c47] 13
                    add       a                             ;[2c48] 87
                    jr        nc,$2c40                      ;[2c49] 30 f5
                    ret                                     ;[2c4b] c9

                    pop       hl                            ;[2c4c] e1
                    ld        sp,($5c3d)                    ;[2c4d] ed 7b 3d 5c
                    ld        a,(hl)                        ;[2c51] 7e
                    ld        ($5b5e),a                     ;[2c52] 32 5e 5b
                    inc       a                             ;[2c55] 3c
                    cp        $1e                           ;[2c56] fe 1e
                    jr        nc,$2c5d                      ;[2c58] 30 03
                    rst       $28                           ;[2c5a] ef
                    ld        e,l                           ;[2c5b] 5d
                    ld        e,e                           ;[2c5c] 5b
                    dec       a                             ;[2c5d] 3d
                    ld        (iy+$00),a                    ;[2c5e] fd 77 00
                    ld        hl,($5c5d)                    ;[2c61] 2a 5d 5c
                    ld        ($5c5f),hl                    ;[2c64] 22 5f 5c
                    rst       $28                           ;[2c67] ef
                    push      bc                            ;[2c68] c5
                    ld        d,$c9                         ;[2c69] 16 c9
                    ld        a,$7f                         ;[2c6b] 3e 7f
                    in        a,($fe)                       ;[2c6d] db fe
                    rra                                     ;[2c6f] 1f
                    ret       c                             ;[2c70] d8
                    ld        a,$fe                         ;[2c71] 3e fe
                    in        a,($fe)                       ;[2c73] db fe
                    rra                                     ;[2c75] 1f
                    ret       c                             ;[2c76] d8
                    call      $2c4c                         ;[2c77] cd 4c 2c
                    inc       d                             ;[2c7a] 14
                    ei                                      ;[2c7b] fb
                    ex        af,af'                        ;[2c7c] 08
                    ld        de,$3a1b                      ;[2c7d] 11 1b 3a
                    push      de                            ;[2c80] d5
                    res       3,(iy+$02)                    ;[2c81] fd cb 02 9e
                    push      hl                            ;[2c85] e5
                    ld        hl,($5c3d)                    ;[2c86] 2a 3d 5c
                    ld        e,(hl)                        ;[2c89] 5e
                    inc       hl                            ;[2c8a] 23
                    ld        d,(hl)                        ;[2c8b] 56
                    and       a                             ;[2c8c] a7
                    ld        hl,$107f                      ;[2c8d] 21 7f 10
                    sbc       hl,de                         ;[2c90] ed 52
                    jr        nz,$2ccc                      ;[2c92] 20 38
                    pop       hl                            ;[2c94] e1
                    ld        sp,($5c3d)                    ;[2c95] ed 7b 3d 5c
                    pop       de                            ;[2c99] d1
                    pop       de                            ;[2c9a] d1
                    ld        ($5c3d),de                    ;[2c9b] ed 53 3d 5c
                    push      hl                            ;[2c9f] e5
                    ld        de,$2ca5                      ;[2ca0] 11 a5 2c
                    push      de                            ;[2ca3] d5
                    jp        (hl)                          ;[2ca4] e9
                    jr        c,$2cb0                       ;[2ca5] 38 09
                    jr        z,$2cad                       ;[2ca7] 28 04
                    call      $2c4c                         ;[2ca9] cd 4c 2c
                    rlca                                    ;[2cac] 07
                    pop       hl                            ;[2cad] e1
                    jr        $2c9f                         ;[2cae] 18 ef
                    cp        $0d                           ;[2cb0] fe 0d
                    jr        z,$2cc2                       ;[2cb2] 28 0e
                    ld        hl,($5b5a)                    ;[2cb4] 2a 5a 5b
                    push      hl                            ;[2cb7] e5
                    rst       $28                           ;[2cb8] ef
                    add       l                             ;[2cb9] 85
                    rrca                                    ;[2cba] 0f
                    pop       hl                            ;[2cbb] e1
                    ld        ($5b5a),hl                    ;[2cbc] 22 5a 5b
                    pop       hl                            ;[2cbf] e1
                    jr        $2c9f                         ;[2cc0] 18 dd
                    pop       hl                            ;[2cc2] e1
                    ld        a,($5b5c)                     ;[2cc3] 3a 5c 5b
                    or        $10                           ;[2cc6] f6 10
                    push      af                            ;[2cc8] f5
                    jp        $3a1b                         ;[2cc9] c3 1b 3a
                    pop       hl                            ;[2ccc] e1
                    ld        de,$2cd2                      ;[2ccd] 11 d2 2c
                    push      de                            ;[2cd0] d5
                    jp        (hl)                          ;[2cd1] e9
                    ret       c                             ;[2cd2] d8
                    ret       z                             ;[2cd3] c8
                    jr        $2ca9                         ;[2cd4] 18 d3
                    ex        af,af'                        ;[2cd6] 08
                    ld        a,$10                         ;[2cd7] 3e 10
                    di                                      ;[2cd9] f3
                    call      $2cf0                         ;[2cda] cd f0 2c
                    pop       af                            ;[2cdd] f1
                    ld        ($5b58),hl                    ;[2cde] 22 58 5b
                    ld        hl,($5b6a)                    ;[2ce1] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[2ce4] ed 73 6a 5b
                    ld        sp,hl                         ;[2ce8] f9
                    ei                                      ;[2ce9] fb
                    ld        hl,($5b58)                    ;[2cea] 2a 58 5b
                    push      af                            ;[2ced] f5
                    ex        af,af'                        ;[2cee] 08
                    ret                                     ;[2cef] c9

                    push      bc                            ;[2cf0] c5
                    ld        bc,$7ffd                      ;[2cf1] 01 fd 7f
                    out       (c),a                         ;[2cf4] ed 79
                    ld        ($5b5c),a                     ;[2cf6] 32 5c 5b
                    pop       bc                            ;[2cf9] c1
                    ret                                     ;[2cfa] c9

                    ex        af,af'                        ;[2cfb] 08
                    di                                      ;[2cfc] f3
                    pop       af                            ;[2cfd] f1
                    ld        ($5b58),hl                    ;[2cfe] 22 58 5b
                    ld        hl,($5b6a)                    ;[2d01] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[2d04] ed 73 6a 5b
                    ld        sp,hl                         ;[2d08] f9
                    ld        hl,($5b58)                    ;[2d09] 2a 58 5b
                    push      af                            ;[2d0c] f5
                    ld        a,$17                         ;[2d0d] 3e 17
                    call      $2cf0                         ;[2d0f] cd f0 2c
                    ei                                      ;[2d12] fb
                    ex        af,af'                        ;[2d13] 08
                    ret                                     ;[2d14] c9

                    call      $2cfb                         ;[2d15] cd fb 2c
                    ld        ($c00c),a                     ;[2d18] 32 0c c0
                    push      af                            ;[2d1b] f5
                    jr        z,$2d24                       ;[2d1c] 28 06
                    cp        $e0                           ;[2d1e] fe e0
                    ld        a,$03                         ;[2d20] 3e 03
                    jr        z,$2d26                       ;[2d22] 28 02
                    ld        a,$02                         ;[2d24] 3e 02
                    call      $2cd6                         ;[2d26] cd d6 2c
                    rst       $28                           ;[2d29] ef
                    ld        bc,$cd16                      ;[2d2a] 01 16 cd
                    ei                                      ;[2d2d] fb
                    inc       l                             ;[2d2e] 2c
                    pop       af                            ;[2d2f] f1
                    jr        z,$2d90                       ;[2d30] 28 5e
                    ld        hl,$ed01                      ;[2d32] 21 01 ed
                    ld        bc,$0001                      ;[2d35] 01 01 00
                    ld        de,$0001                      ;[2d38] 11 01 00
                    call      $342d                         ;[2d3b] cd 2d 34
                    call      $3f00                         ;[2d3e] cd 00 3f
                    ld        b,$01                         ;[2d41] 06 01
                    call      $3465                         ;[2d43] cd 65 34
                    jp        nc,$338b                      ;[2d46] d2 8b 33
                    ld        b,$00                         ;[2d49] 06 00
                    call      $342d                         ;[2d4b] cd 2d 34
                    call      $3f00                         ;[2d4e] cd 00 3f
                    jr        $2d54                         ;[2d51] 18 01
                    call      $3465                         ;[2d53] cd 65 34
                    jr        c,$2d5f                       ;[2d56] 38 07
                    cp        $19                           ;[2d58] fe 19
                    jp        nz,$338b                      ;[2d5a] c2 8b 33
                    jr        $2d7c                         ;[2d5d] 18 1d
                    ld        a,($c00c)                     ;[2d5f] 3a 0c c0
                    cp        $aa                           ;[2d62] fe aa
                    ld        a,c                           ;[2d64] 79
                    jr        nz,$2d71                      ;[2d65] 20 0a
                    cp        $0d                           ;[2d67] fe 0d
                    jr        z,$2d71                       ;[2d69] 28 06
                    cp        $20                           ;[2d6b] fe 20
                    jr        nc,$2d71                      ;[2d6d] 30 02
                    ld        a,$20                         ;[2d6f] 3e 20
                    call      $2cd6                         ;[2d71] cd d6 2c
                    rst       $28                           ;[2d74] ef
                    djnz      $2d77                         ;[2d75] 10 00
                    call      $2cfb                         ;[2d77] cd fb 2c
                    jr        $2d49                         ;[2d7a] 18 cd
                    ld        b,$00                         ;[2d7c] 06 00
                    call      $342d                         ;[2d7e] cd 2d 34
                    call      $3f00                         ;[2d81] cd 00 3f
                    add       hl,bc                         ;[2d84] 09
                    ld        bc,$65cd                      ;[2d85] 01 cd 65
                    inc       (hl)                          ;[2d88] 34
                    jp        nc,$338b                      ;[2d89] d2 8b 33
                    call      $2cd6                         ;[2d8c] cd d6 2c
                    ret                                     ;[2d8f] c9

                    push      af                            ;[2d90] f5
                    ld        hl,$ed01                      ;[2d91] 21 01 ed
                    ld        ($c002),hl                    ;[2d94] 22 02 c0
                    ld        de,$c010                      ;[2d97] 11 10 c0
                    call      $327b                         ;[2d9a] cd 7b 32
                    push      hl                            ;[2d9d] e5
                    ld        a,$ff                         ;[2d9e] 3e ff
                    call      $342d                         ;[2da0] cd 2d 34
                    call      $3f00                         ;[2da3] cd 00 3f
                    dec       l                             ;[2da6] 2d
                    ld        bc,$65cd                      ;[2da7] 01 cd 65
                    inc       (hl)                          ;[2daa] 34
                    ld        ($c005),a                     ;[2dab] 32 05 c0
                    ld        ($c004),a                     ;[2dae] 32 04 c0
                    ld        hl,$c010                      ;[2db1] 21 10 c0
                    call      $3262                         ;[2db4] cd 62 32
                    jr        nz,$2dbf                      ;[2db7] 20 06
                    ld        de,$c005                      ;[2db9] 11 05 c0
                    call      $3255                         ;[2dbc] cd 55 32
                    pop       hl                            ;[2dbf] e1
                    pop       af                            ;[2dc0] f1
                    jp        nc,$3295                      ;[2dc1] d2 95 32
                    ld        ($c000),hl                    ;[2dc4] 22 00 c0
                    ld        de,$c022                      ;[2dc7] 11 22 c0
                    call      $327b                         ;[2dca] cd 7b 32
                    ld        hl,$c022                      ;[2dcd] 21 22 c0
                    call      $3262                         ;[2dd0] cd 62 32
                    jr        nz,$2ddb                      ;[2dd3] 20 06
                    ld        de,$c004                      ;[2dd5] 11 04 c0
                    call      $3255                         ;[2dd8] cd 55 32
                    ld        ($5c8c),a                     ;[2ddb] 32 8c 5c
                    ld        a,$0d                         ;[2dde] 3e 0d
                    rst       $10                           ;[2de0] d7
                    xor       a                             ;[2de1] af
                    ld        ($c00b),a                     ;[2de2] 32 0b c0
                    ld        ($c00f),a                     ;[2de5] 32 0f c0
                    ld        ($c00c),a                     ;[2de8] 32 0c c0
                    ld        hl,$c010                      ;[2deb] 21 10 c0
                    call      $3228                         ;[2dee] cd 28 32
                    ld        a,($c00b)                     ;[2df1] 3a 0b c0
                    or        a                             ;[2df4] b7
                    jr        z,$2dfe                       ;[2df5] 28 07
                    call      $2cd6                         ;[2df7] cd d6 2c
                    call      $2c4c                         ;[2dfa] cd 4c 2c
                    ld        c,c                           ;[2dfd] 49
                    ld        hl,$33fa                      ;[2dfe] 21 fa 33
                    ld        de,$c04e                      ;[2e01] 11 4e c0
                    ld        bc,$000e                      ;[2e04] 01 0e 00
                    ldir                                    ;[2e07] ed b0
                    ld        hl,$c022                      ;[2e09] 21 22 c0
                    call      $3228                         ;[2e0c] cd 28 32
                    ld        a,($c00b)                     ;[2e0f] 3a 0b c0
                    or        a                             ;[2e12] b7
                    jr        nz,$2e1b                      ;[2e13] 20 06
                    call      $2eca                         ;[2e15] cd ca 2e
                    jp        $2e98                         ;[2e18] c3 98 2e
                    ld        hl,($c002)                    ;[2e1b] 2a 02 c0
                    call      $3262                         ;[2e1e] cd 62 32
                    ld        a,$ff                         ;[2e21] 3e ff
                    cp        (hl)                          ;[2e23] be
                    jr        z,$2e2d                       ;[2e24] 28 07
                    call      $2cd6                         ;[2e26] cd d6 2c
                    call      $2c4c                         ;[2e29] cd 4c 2c
                    ld        c,d                           ;[2e2c] 4a
                    ld        hl,$c041                      ;[2e2d] 21 41 c0
                    xor       a                             ;[2e30] af
                    ld        b,$0d                         ;[2e31] 06 0d
                    ld        (hl),a                        ;[2e33] 77
                    inc       hl                            ;[2e34] 23
                    djnz      $2e33                         ;[2e35] 10 fc
                    ld        hl,$c041                      ;[2e37] 21 41 c0
                    ld        de,$c034                      ;[2e3a] 11 34 c0
                    ld        bc,$000d                      ;[2e3d] 01 0d 00
                    ldir                                    ;[2e40] ed b0
                    ld        hl,($c000)                    ;[2e42] 2a 00 c0
                    ld        bc,$0200                      ;[2e45] 01 00 02
                    ld        de,$c034                      ;[2e48] 11 34 c0
                    call      $342d                         ;[2e4b] cd 2d 34
                    call      $3f00                         ;[2e4e] cd 00 3f
                    ld        e,$01                         ;[2e51] 1e 01
                    call      $3465                         ;[2e53] cd 65 34
                    jp        nc,$338b                      ;[2e56] d2 8b 33
                    ld        hl,$c00c                      ;[2e59] 21 0c c0
                    ld        a,(hl)                        ;[2e5c] 7e
                    or        a                             ;[2e5d] b7
                    jr        nz,$2e69                      ;[2e5e] 20 09
                    inc       a                             ;[2e60] 3c
                    ld        (hl),a                        ;[2e61] 77
                    ld        a,$17                         ;[2e62] 3e 17
                    dec       b                             ;[2e64] 05
                    jp        z,$338b                       ;[2e65] ca 8b 33
                    inc       b                             ;[2e68] 04
                    dec       b                             ;[2e69] 05
                    jr        z,$2e98                       ;[2e6a] 28 2c
                    ld        hl,$c022                      ;[2e6c] 21 22 c0
                    call      $3262                         ;[2e6f] cd 62 32
                    ex        de,hl                         ;[2e72] eb
                    ld        hl,$c041                      ;[2e73] 21 41 c0
                    ld        b,$08                         ;[2e76] 06 08
                    call      $324b                         ;[2e78] cd 4b 32
                    ld        hl,$c049                      ;[2e7b] 21 49 c0
                    ld        a,$2e                         ;[2e7e] 3e 2e
                    ld        (de),a                        ;[2e80] 12
                    inc       de                            ;[2e81] 13
                    ld        b,$03                         ;[2e82] 06 03
                    call      $324b                         ;[2e84] cd 4b 32
                    ld        a,$ff                         ;[2e87] 3e ff
                    ld        (de),a                        ;[2e89] 12
                    ld        hl,$c010                      ;[2e8a] 21 10 c0
                    call      $3262                         ;[2e8d] cd 62 32
                    ld        (hl),$ff                      ;[2e90] 36 ff
                    call      $2eca                         ;[2e92] cd ca 2e
                    jp        $2e37                         ;[2e95] c3 37 2e
                    ld        hl,$c04e                      ;[2e98] 21 4e c0
                    call      $342d                         ;[2e9b] cd 2d 34
                    call      $3f00                         ;[2e9e] cd 00 3f
                    inc       h                             ;[2ea1] 24
                    ld        bc,$65cd                      ;[2ea2] 01 cd 65
                    inc       (hl)                          ;[2ea5] 34
                    ld        a,($c00f)                     ;[2ea6] 3a 0f c0
                    dec       a                             ;[2ea9] 3d
                    ld        hl,$3408                      ;[2eaa] 21 08 34
                    jr        z,$2ebe                       ;[2ead] 28 0f
                    inc       a                             ;[2eaf] 3c
                    ld        l,a                           ;[2eb0] 6f
                    ld        h,$00                         ;[2eb1] 26 00
                    ld        a,$0d                         ;[2eb3] 3e 0d
                    rst       $10                           ;[2eb5] d7
                    ld        e,$20                         ;[2eb6] 1e 20
                    call      $0815                         ;[2eb8] cd 15 08
                    ld        hl,$341c                      ;[2ebb] 21 1c 34
                    call      $33df                         ;[2ebe] cd df 33
                    ld        a,$17                         ;[2ec1] 3e 17
                    ld        ($5c8c),a                     ;[2ec3] 32 8c 5c
                    call      $2cd6                         ;[2ec6] cd d6 2c
                    ret                                     ;[2ec9] c9

                    ld        hl,$c010                      ;[2eca] 21 10 c0
                    ld        de,$c022                      ;[2ecd] 11 22 c0
                    ld        a,(de)                        ;[2ed0] 1a
                    cp        (hl)                          ;[2ed1] be
                    jr        nz,$2ee4                      ;[2ed2] 20 10
                    ld        a,$ff                         ;[2ed4] 3e ff
                    cp        (hl)                          ;[2ed6] be
                    jr        nz,$2ee0                      ;[2ed7] 20 07
                    call      $2cd6                         ;[2ed9] cd d6 2c
                    call      $2c4c                         ;[2edc] cd 4c 2c
                    jr        nc,$2f04                      ;[2edf] 30 23
                    inc       de                            ;[2ee1] 13
                    jr        $2ed0                         ;[2ee2] 18 ec
                    ld        hl,$c010                      ;[2ee4] 21 10 c0
                    call      $3262                         ;[2ee7] cd 62 32
                    ld        a,(hl)                        ;[2eea] 7e
                    cp        $ff                           ;[2eeb] fe ff
                    jr        nz,$2f06                      ;[2eed] 20 17
                    ld        hl,$c010                      ;[2eef] 21 10 c0
                    call      $3262                         ;[2ef2] cd 62 32
                    push      hl                            ;[2ef5] e5
                    ld        hl,$c022                      ;[2ef6] 21 22 c0
                    call      $3262                         ;[2ef9] cd 62 32
                    pop       de                            ;[2efc] d1
                    ld        a,(hl)                        ;[2efd] 7e
                    ld        (de),a                        ;[2efe] 12
                    inc       a                             ;[2eff] 3c
                    jr        z,$2f06                       ;[2f00] 28 04
                    inc       de                            ;[2f02] 13
                    inc       hl                            ;[2f03] 23
                    jr        $2efd                         ;[2f04] 18 f7
                    xor       a                             ;[2f06] af
                    ld        ($c009),a                     ;[2f07] 32 09 c0
                    ld        a,$4d                         ;[2f0a] 3e 4d
                    call      $342d                         ;[2f0c] cd 2d 34
                    call      $3f00                         ;[2f0f] cd 00 3f
                    ld        hl,$cd01                      ;[2f12] 21 01 cd
                    ld        h,l                           ;[2f15] 65
                    inc       (hl)                          ;[2f16] 34
                    jp        nc,$338b                      ;[2f17] d2 8b 33
                    ld        a,h                           ;[2f1a] 7c
                    or        a                             ;[2f1b] b7
                    jr        z,$2f21                       ;[2f1c] 28 03
                    ld        hl,$003f                      ;[2f1e] 21 3f 00
                    ld        a,l                           ;[2f21] 7d
                    cp        $40                           ;[2f22] fe 40
                    jr        c,$2f29                       ;[2f24] 38 03
                    ld        hl,$003f                      ;[2f26] 21 3f 00
                    ld        h,l                           ;[2f29] 65
                    ld        l,$00                         ;[2f2a] 2e 00
                    add       hl,hl                         ;[2f2c] 29
                    add       hl,hl                         ;[2f2d] 29
                    ld        ($c007),hl                    ;[2f2e] 22 07 c0
                    ld        de,$0800                      ;[2f31] 11 00 08
                    or        a                             ;[2f34] b7
                    sbc       hl,de                         ;[2f35] ed 52
                    jr        nc,$2f43                      ;[2f37] 30 0a
                    ld        a,$ff                         ;[2f39] 3e ff
                    ld        ($5c8c),a                     ;[2f3b] 32 8c 5c
                    ld        a,$01                         ;[2f3e] 3e 01
                    ld        ($c009),a                     ;[2f40] 32 09 c0
                    xor       a                             ;[2f43] af
                    ld        ($c00a),a                     ;[2f44] 32 0a c0
                    ld        ($c006),a                     ;[2f47] 32 06 c0
                    ld        hl,$c010                      ;[2f4a] 21 10 c0
                    call      $3262                         ;[2f4d] cd 62 32
                    ld        a,(hl)                        ;[2f50] 7e
                    cp        $ff                           ;[2f51] fe ff
                    jp        nz,$2fcf                      ;[2f53] c2 cf 2f
                    ld        hl,$c022                      ;[2f56] 21 22 c0
                    call      $3262                         ;[2f59] cd 62 32
                    ld        a,(hl)                        ;[2f5c] 7e
                    cp        $ff                           ;[2f5d] fe ff
                    jp        nz,$2fcf                      ;[2f5f] c2 cf 2f
                    ld        a,($c005)                     ;[2f62] 3a 05 c0
                    cp        $4d                           ;[2f65] fe 4d
                    jp        z,$2fcf                       ;[2f67] ca cf 2f
                    ld        a,($c004)                     ;[2f6a] 3a 04 c0
                    cp        $4d                           ;[2f6d] fe 4d
                    jp        z,$2fcf                       ;[2f6f] ca cf 2f
                    ld        a,$41                         ;[2f72] 3e 41
                    call      $342d                         ;[2f74] cd 2d 34
                    call      $3f00                         ;[2f77] cd 00 3f
                    ld        d,c                           ;[2f7a] 51
                    ld        bc,$65cd                      ;[2f7b] 01 cd 65
                    inc       (hl)                          ;[2f7e] 34
                    jp        nc,$338b                      ;[2f7f] d2 8b 33
                    ld        c,$00                         ;[2f82] 0e 00
                    call      $342d                         ;[2f84] cd 2d 34
                    call      $3f00                         ;[2f87] cd 00 3f
                    ld        (hl),l                        ;[2f8a] 75
                    ld        bc,$65cd                      ;[2f8b] 01 cd 65
                    inc       (hl)                          ;[2f8e] 34
                    jp        nc,$338b                      ;[2f8f] d2 8b 33
                    or        a                             ;[2f92] b7
                    ld        a,$06                         ;[2f93] 3e 06
                    jp        nz,$338b                      ;[2f95] c2 8b 33
                    ld        a,($c004)                     ;[2f98] 3a 04 c0
                    ld        bc,$0001                      ;[2f9b] 01 01 00
                    call      $342d                         ;[2f9e] cd 2d 34
                    call      $3f00                         ;[2fa1] cd 00 3f
                    ld        c,e                           ;[2fa4] 4b
                    ld        bc,$65cd                      ;[2fa5] 01 cd 65
                    inc       (hl)                          ;[2fa8] 34
                    jp        nc,$338b                      ;[2fa9] d2 8b 33
                    ld        a,($c005)                     ;[2fac] 3a 05 c0
                    ld        bc,$0102                      ;[2faf] 01 02 01
                    call      $342d                         ;[2fb2] cd 2d 34
                    call      $3f00                         ;[2fb5] cd 00 3f
                    ld        c,e                           ;[2fb8] 4b
                    ld        bc,$65cd                      ;[2fb9] 01 cd 65
                    inc       (hl)                          ;[2fbc] 34
                    jp        nc,$338b                      ;[2fbd] d2 8b 33
                    ld        a,$01                         ;[2fc0] 3e 01
                    ld        ($c00a),a                     ;[2fc2] 32 0a c0
                    ld        a,($c009)                     ;[2fc5] 3a 09 c0
                    or        a                             ;[2fc8] b7
                    jp        z,$30b6                       ;[2fc9] ca b6 30
                    jp        $303f                         ;[2fcc] c3 3f 30
                    ld        hl,$c022                      ;[2fcf] 21 22 c0
                    ld        a,$ff                         ;[2fd2] 3e ff
                    ld        ($5c8c),a                     ;[2fd4] 32 8c 5c
                    push      hl                            ;[2fd7] e5
                    push      hl                            ;[2fd8] e5
                    call      $33df                         ;[2fd9] cd df 33
                    pop       de                            ;[2fdc] d1
                    ex        de,hl                         ;[2fdd] eb
                    or        a                             ;[2fde] b7
                    sbc       hl,de                         ;[2fdf] ed 52
                    ld        de,$0011                      ;[2fe1] 11 11 00
                    add       hl,de                         ;[2fe4] 19
                    ld        b,l                           ;[2fe5] 45
                    push      bc                            ;[2fe6] c5
                    ld        a,$20                         ;[2fe7] 3e 20
                    rst       $10                           ;[2fe9] d7
                    pop       bc                            ;[2fea] c1
                    djnz      $2fe6                         ;[2feb] 10 f9
                    pop       hl                            ;[2fed] e1
                    ld        a,($c005)                     ;[2fee] 3a 05 c0
                    or        $20                           ;[2ff1] f6 20
                    cp        $6d                           ;[2ff3] fe 6d
                    jr        z,$3007                       ;[2ff5] 28 10
                    ld        a,($c009)                     ;[2ff7] 3a 09 c0
                    or        a                             ;[2ffa] b7
                    jr        nz,$3007                      ;[2ffb] 20 0a
                    ld        a,($c004)                     ;[2ffd] 3a 04 c0
                    or        $20                           ;[3000] f6 20
                    cp        $6d                           ;[3002] fe 6d
                    jp        nz,$309f                      ;[3004] c2 9f 30
                    ld        hl,$c022                      ;[3007] 21 22 c0
                    ld        bc,$0001                      ;[300a] 01 01 00
                    ld        de,$0002                      ;[300d] 11 02 00
                    call      $342d                         ;[3010] cd 2d 34
                    call      $3f00                         ;[3013] cd 00 3f
                    ld        b,$01                         ;[3016] 06 01
                    call      $3465                         ;[3018] cd 65 34
                    jp        nc,$338b                      ;[301b] d2 8b 33
                    ld        hl,$c010                      ;[301e] 21 10 c0
                    push      hl                            ;[3021] e5
                    call      $33df                         ;[3022] cd df 33
                    pop       hl                            ;[3025] e1
                    ld        bc,$0102                      ;[3026] 01 02 01
                    ld        de,$0204                      ;[3029] 11 04 02
                    call      $342d                         ;[302c] cd 2d 34
                    call      $3f00                         ;[302f] cd 00 3f
                    ld        b,$01                         ;[3032] 06 01
                    call      $3465                         ;[3034] cd 65 34
                    jp        nc,$338b                      ;[3037] d2 8b 33
                    ld        a,$01                         ;[303a] 3e 01
                    ld        ($c00a),a                     ;[303c] 32 0a c0
                    ld        bc,$0007                      ;[303f] 01 07 00
                    ld        de,$0800                      ;[3042] 11 00 08
                    ld        hl,$ed11                      ;[3045] 21 11 ed
                    call      $342d                         ;[3048] cd 2d 34
                    call      $3f00                         ;[304b] cd 00 3f
                    ld        (de),a                        ;[304e] 12
                    ld        bc,$65cd                      ;[304f] 01 cd 65
                    inc       (hl)                          ;[3052] 34
                    jr        c,$307a                       ;[3053] 38 25
                    cp        $19                           ;[3055] fe 19
                    jp        nz,$338b                      ;[3057] c2 8b 33
                    ld        a,$01                         ;[305a] 3e 01
                    ld        ($c006),a                     ;[305c] 32 06 c0
                    push      de                            ;[305f] d5
                    ld        b,$00                         ;[3060] 06 00
                    call      $342d                         ;[3062] cd 2d 34
                    call      $3f00                         ;[3065] cd 00 3f
                    add       hl,bc                         ;[3068] 09
                    ld        bc,$65cd                      ;[3069] 01 cd 65
                    inc       (hl)                          ;[306c] 34
                    pop       de                            ;[306d] d1
                    jp        nc,$338b                      ;[306e] d2 8b 33
                    ld        hl,$0800                      ;[3071] 21 00 08
                    or        a                             ;[3074] b7
                    sbc       hl,de                         ;[3075] ed 52
                    ex        de,hl                         ;[3077] eb
                    jr        $307d                         ;[3078] 18 03
                    ld        de,$0800                      ;[307a] 11 00 08
                    ld        a,e                           ;[307d] 7b
                    or        d                             ;[307e] b2
                    jr        z,$3095                       ;[307f] 28 14
                    ld        hl,$ed11                      ;[3081] 21 11 ed
                    ld        bc,$0107                      ;[3084] 01 07 01
                    call      $342d                         ;[3087] cd 2d 34
                    call      $3f00                         ;[308a] cd 00 3f
                    dec       d                             ;[308d] 15
                    ld        bc,$65cd                      ;[308e] 01 cd 65
                    inc       (hl)                          ;[3091] 34
                    jp        nc,$338b                      ;[3092] d2 8b 33
                    ld        a,($c006)                     ;[3095] 3a 06 c0
                    or        a                             ;[3098] b7
                    jp        z,$303f                       ;[3099] ca 3f 30
                    jp        $3210                         ;[309c] c3 10 32
                    ld        hl,$c022                      ;[309f] 21 22 c0
                    ld        bc,$0001                      ;[30a2] 01 01 00
                    ld        de,$0002                      ;[30a5] 11 02 00
                    call      $342d                         ;[30a8] cd 2d 34
                    call      $3f00                         ;[30ab] cd 00 3f
                    ld        b,$01                         ;[30ae] 06 01
                    call      $3465                         ;[30b0] cd 65 34
                    jp        nc,$338b                      ;[30b3] d2 8b 33
                    ld        hl,$c04e                      ;[30b6] 21 4e c0
                    ld        bc,$0203                      ;[30b9] 01 03 02
                    ld        de,$0204                      ;[30bc] 11 04 02
                    call      $342d                         ;[30bf] cd 2d 34
                    call      $3f00                         ;[30c2] cd 00 3f
                    ld        b,$01                         ;[30c5] 06 01
                    call      $3465                         ;[30c7] cd 65 34
                    jp        nc,$338b                      ;[30ca] d2 8b 33
                    ld        hl,$0000                      ;[30cd] 21 00 00
                    ld        ($c00d),hl                    ;[30d0] 22 0d c0
                    ld        bc,$0007                      ;[30d3] 01 07 00
                    ld        de,$0800                      ;[30d6] 11 00 08
                    ld        hl,$ed11                      ;[30d9] 21 11 ed
                    call      $342d                         ;[30dc] cd 2d 34
                    call      $3f00                         ;[30df] cd 00 3f
                    ld        (de),a                        ;[30e2] 12
                    ld        bc,$65cd                      ;[30e3] 01 cd 65
                    inc       (hl)                          ;[30e6] 34
                    jr        c,$310e                       ;[30e7] 38 25
                    cp        $19                           ;[30e9] fe 19
                    jp        nz,$338b                      ;[30eb] c2 8b 33
                    ld        a,$01                         ;[30ee] 3e 01
                    ld        ($c006),a                     ;[30f0] 32 06 c0
                    push      de                            ;[30f3] d5
                    ld        b,$00                         ;[30f4] 06 00
                    call      $342d                         ;[30f6] cd 2d 34
                    call      $3f00                         ;[30f9] cd 00 3f
                    add       hl,bc                         ;[30fc] 09
                    ld        bc,$65cd                      ;[30fd] 01 cd 65
                    inc       (hl)                          ;[3100] 34
                    pop       de                            ;[3101] d1
                    jp        nc,$338b                      ;[3102] d2 8b 33
                    ld        hl,$0800                      ;[3105] 21 00 08
                    or        a                             ;[3108] b7
                    sbc       hl,de                         ;[3109] ed 52
                    ex        de,hl                         ;[310b] eb
                    jr        $3111                         ;[310c] 18 03
                    ld        de,$0800                      ;[310e] 11 00 08
                    ld        a,e                           ;[3111] 7b
                    or        d                             ;[3112] b2
                    jr        z,$312b                       ;[3113] 28 16
                    push      de                            ;[3115] d5
                    ld        hl,$ed11                      ;[3116] 21 11 ed
                    ld        bc,$0207                      ;[3119] 01 07 02
                    call      $342d                         ;[311c] cd 2d 34
                    call      $3f00                         ;[311f] cd 00 3f
                    dec       d                             ;[3122] 15
                    ld        bc,$65cd                      ;[3123] 01 cd 65
                    inc       (hl)                          ;[3126] 34
                    pop       de                            ;[3127] d1
                    jp        nc,$338b                      ;[3128] d2 8b 33
                    ld        hl,($c00d)                    ;[312b] 2a 0d c0
                    add       hl,de                         ;[312e] 19
                    ld        ($c00d),hl                    ;[312f] 22 0d c0
                    ld        de,$0800                      ;[3132] 11 00 08
                    add       hl,de                         ;[3135] 19
                    ex        de,hl                         ;[3136] eb
                    ld        hl,($c007)                    ;[3137] 2a 07 c0
                    ld        a,($c006)                     ;[313a] 3a 06 c0
                    or        a                             ;[313d] b7
                    jr        nz,$3144                      ;[313e] 20 04
                    sbc       hl,de                         ;[3140] ed 52
                    jr        nc,$30d3                      ;[3142] 30 8f
                    ld        a,($c004)                     ;[3144] 3a 04 c0
                    and       $df                           ;[3147] e6 df
                    call      $342d                         ;[3149] cd 2d 34
                    call      $3f00                         ;[314c] cd 00 3f
                    ld        b,d                           ;[314f] 42
                    ld        bc,$65cd                      ;[3150] 01 cd 65
                    inc       (hl)                          ;[3153] 34
                    jp        nc,$338b                      ;[3154] d2 8b 33
                    ld        b,$02                         ;[3157] 06 02
                    ld        hl,$0000                      ;[3159] 21 00 00
                    ld        e,$00                         ;[315c] 1e 00
                    call      $342d                         ;[315e] cd 2d 34
                    call      $3f00                         ;[3161] cd 00 3f
                    ld        (hl),$01                      ;[3164] 36 01
                    call      $3465                         ;[3166] cd 65 34
                    ld        a,($c00a)                     ;[3169] 3a 0a c0
                    or        a                             ;[316c] b7
                    jr        nz,$3190                      ;[316d] 20 21
                    ld        hl,$c010                      ;[316f] 21 10 c0
                    push      hl                            ;[3172] e5
                    call      $33df                         ;[3173] cd df 33
                    pop       hl                            ;[3176] e1
                    ld        bc,$0102                      ;[3177] 01 02 01
                    ld        de,$0204                      ;[317a] 11 04 02
                    call      $342d                         ;[317d] cd 2d 34
                    call      $3f00                         ;[3180] cd 00 3f
                    ld        b,$01                         ;[3183] 06 01
                    call      $3465                         ;[3185] cd 65 34
                    jp        nc,$338b                      ;[3188] d2 8b 33
                    ld        a,$01                         ;[318b] 3e 01
                    ld        ($c00a),a                     ;[318d] 32 0a c0
                    ld        hl,$ed11                      ;[3190] 21 11 ed
                    ld        de,$0800                      ;[3193] 11 00 08
                    ld        bc,$0207                      ;[3196] 01 07 02
                    call      $342d                         ;[3199] cd 2d 34
                    call      $3f00                         ;[319c] cd 00 3f
                    ld        (de),a                        ;[319f] 12
                    ld        bc,$65cd                      ;[31a0] 01 cd 65
                    inc       (hl)                          ;[31a3] 34
                    ld        hl,$0800                      ;[31a4] 21 00 08
                    jr        c,$31b4                       ;[31a7] 38 0b
                    cp        $19                           ;[31a9] fe 19
                    jp        nz,$338b                      ;[31ab] c2 8b 33
                    ld        hl,$0800                      ;[31ae] 21 00 08
                    or        a                             ;[31b1] b7
                    sbc       hl,de                         ;[31b2] ed 52
                    ex        de,hl                         ;[31b4] eb
                    ld        bc,$0107                      ;[31b5] 01 07 01
                    ld        hl,$ed11                      ;[31b8] 21 11 ed
                    call      $342d                         ;[31bb] cd 2d 34
                    call      $3f00                         ;[31be] cd 00 3f
                    dec       d                             ;[31c1] 15
                    ld        bc,$65cd                      ;[31c2] 01 cd 65
                    inc       (hl)                          ;[31c5] 34
                    jp        nc,$338b                      ;[31c6] d2 8b 33
                    ld        hl,($c00d)                    ;[31c9] 2a 0d c0
                    ld        de,$0800                      ;[31cc] 11 00 08
                    or        a                             ;[31cf] b7
                    sbc       hl,de                         ;[31d0] ed 52
                    jr        c,$31db                       ;[31d2] 38 07
                    ld        a,h                           ;[31d4] 7c
                    or        l                             ;[31d5] b5
                    ld        ($c00d),hl                    ;[31d6] 22 0d c0
                    jr        nz,$3190                      ;[31d9] 20 b5
                    ld        b,$02                         ;[31db] 06 02
                    call      $342d                         ;[31dd] cd 2d 34
                    call      $3f00                         ;[31e0] cd 00 3f
                    add       hl,bc                         ;[31e3] 09
                    ld        bc,$65cd                      ;[31e4] 01 cd 65
                    inc       (hl)                          ;[31e7] 34
                    ld        a,($c005)                     ;[31e8] 3a 05 c0
                    and       $df                           ;[31eb] e6 df
                    call      $342d                         ;[31ed] cd 2d 34
                    call      $3f00                         ;[31f0] cd 00 3f
                    ld        b,d                           ;[31f3] 42
                    ld        bc,$65cd                      ;[31f4] 01 cd 65
                    inc       (hl)                          ;[31f7] 34
                    jp        nc,$338b                      ;[31f8] d2 8b 33
                    ld        a,($c006)                     ;[31fb] 3a 06 c0
                    or        a                             ;[31fe] b7
                    jp        z,$30b6                       ;[31ff] ca b6 30
                    ld        hl,$c04e                      ;[3202] 21 4e c0
                    call      $342d                         ;[3205] cd 2d 34
                    call      $3f00                         ;[3208] cd 00 3f
                    inc       h                             ;[320b] 24
                    ld        bc,$65cd                      ;[320c] 01 cd 65
                    inc       (hl)                          ;[320f] 34
                    ld        b,$01                         ;[3210] 06 01
                    call      $342d                         ;[3212] cd 2d 34
                    call      $3f00                         ;[3215] cd 00 3f
                    add       hl,bc                         ;[3218] 09
                    ld        bc,$65cd                      ;[3219] 01 cd 65
                    inc       (hl)                          ;[321c] 34
                    jp        nc,$338b                      ;[321d] d2 8b 33
                    ld        a,$0d                         ;[3220] 3e 0d
                    rst       $10                           ;[3222] d7
                    ld        hl,$c00f                      ;[3223] 21 0f c0
                    inc       (hl)                          ;[3226] 34
                    ret                                     ;[3227] c9

                    ld        b,$11                         ;[3228] 06 11
                    ld        a,(hl)                        ;[322a] 7e
                    cp        $3f                           ;[322b] fe 3f
                    jr        nz,$3236                      ;[322d] 20 07
                    push      af                            ;[322f] f5
                    ld        a,$01                         ;[3230] 3e 01
                    ld        ($c00b),a                     ;[3232] 32 0b c0
                    pop       af                            ;[3235] f1
                    cp        $2a                           ;[3236] fe 2a
                    jr        nz,$3241                      ;[3238] 20 07
                    push      af                            ;[323a] f5
                    ld        a,$01                         ;[323b] 3e 01
                    ld        ($c00b),a                     ;[323d] 32 0b c0
                    pop       af                            ;[3240] f1
                    inc       hl                            ;[3241] 23
                    inc       a                             ;[3242] 3c
                    ret       z                             ;[3243] c8
                    djnz      $3228                         ;[3244] 10 e2
                    ld        a,$14                         ;[3246] 3e 14
                    jp        $338b                         ;[3248] c3 8b 33
                    ld        a,(hl)                        ;[324b] 7e
                    cp        $20                           ;[324c] fe 20
                    ret       z                             ;[324e] c8
                    ld        (de),a                        ;[324f] 12
                    inc       hl                            ;[3250] 23
                    inc       de                            ;[3251] 13
                    djnz      $324b                         ;[3252] 10 f7
                    ret                                     ;[3254] c9

                    dec       hl                            ;[3255] 2b
                    dec       hl                            ;[3256] 2b
                    ld        a,(hl)                        ;[3257] 7e
                    or        $20                           ;[3258] f6 20
                    cp        $61                           ;[325a] fe 61
                    ret       c                             ;[325c] d8
                    cp        $7b                           ;[325d] fe 7b
                    ret       nc                            ;[325f] d0
                    ld        (de),a                        ;[3260] 12
                    ret                                     ;[3261] c9

                    push      hl                            ;[3262] e5
                    pop       de                            ;[3263] d1
                    ld        a,(hl)                        ;[3264] 7e
                    inc       a                             ;[3265] 3c
                    jr        z,$3275                       ;[3266] 28 0d
                    ld        b,$03                         ;[3268] 06 03
                    ld        a,(hl)                        ;[326a] 7e
                    cp        $3a                           ;[326b] fe 3a
                    jr        z,$3279                       ;[326d] 28 0a
                    inc       a                             ;[326f] 3c
                    jr        z,$3275                       ;[3270] 28 03
                    inc       hl                            ;[3272] 23
                    djnz      $326a                         ;[3273] 10 f5
                    or        $ff                           ;[3275] f6 ff
                    ex        de,hl                         ;[3277] eb
                    ret                                     ;[3278] c9

                    inc       hl                            ;[3279] 23
                    ret                                     ;[327a] c9

                    ld        b,$11                         ;[327b] 06 11
                    ld        a,(hl)                        ;[327d] 7e
                    ld        (de),a                        ;[327e] 12
                    inc       hl                            ;[327f] 23
                    inc       de                            ;[3280] 13
                    inc       a                             ;[3281] 3c
                    jr        z,$328b                       ;[3282] 28 07
                    djnz      $327b                         ;[3284] 10 f5
                    ld        a,$14                         ;[3286] 3e 14
                    jp        $338b                         ;[3288] c3 8b 33
                    ret                                     ;[328b] c9

                    rst       $28                           ;[328c] ef
                    ld        l,e                           ;[328d] 6b
                    dec       c                             ;[328e] 0d
                    ld        a,$02                         ;[328f] 3e 02
                    rst       $28                           ;[3291] ef
                    ld        bc,$c916                      ;[3292] 01 16 c9
                    xor       a                             ;[3295] af
                    ld        ($c00b),a                     ;[3296] 32 0b c0
                    ld        ($c00a),a                     ;[3299] 32 0a c0
                    ld        hl,$c010                      ;[329c] 21 10 c0
                    call      $3228                         ;[329f] cd 28 32
                    ld        a,($c00b)                     ;[32a2] 3a 0b c0
                    or        a                             ;[32a5] b7
                    jr        z,$32af                       ;[32a6] 28 07
                    call      $2cd6                         ;[32a8] cd d6 2c
                    call      $2c4c                         ;[32ab] cd 4c 2c
                    ld        c,c                           ;[32ae] 49
                    ld        hl,$c010                      ;[32af] 21 10 c0
                    ld        b,$12                         ;[32b2] 06 12
                    ld        a,(hl)                        ;[32b4] 7e
                    cp        $2e                           ;[32b5] fe 2e
                    inc       hl                            ;[32b7] 23
                    jr        z,$32bd                       ;[32b8] 28 03
                    inc       a                             ;[32ba] 3c
                    jr        nz,$32b4                      ;[32bb] 20 f7
                    dec       hl                            ;[32bd] 2b
                    ex        de,hl                         ;[32be] eb
                    ld        hl,$3386                      ;[32bf] 21 86 33
                    ld        bc,$0004                      ;[32c2] 01 04 00
                    ldir                                    ;[32c5] ed b0
                    ld        hl,($c002)                    ;[32c7] 2a 02 c0
                    ld        bc,$0001                      ;[32ca] 01 01 00
                    ld        de,$0001                      ;[32cd] 11 01 00
                    call      $342d                         ;[32d0] cd 2d 34
                    call      $3f00                         ;[32d3] cd 00 3f
                    ld        b,$01                         ;[32d6] 06 01
                    call      $3465                         ;[32d8] cd 65 34
                    jp        nc,$338b                      ;[32db] d2 8b 33
                    ld        hl,$c010                      ;[32de] 21 10 c0
                    ld        bc,$0102                      ;[32e1] 01 02 01
                    ld        de,$0104                      ;[32e4] 11 04 01
                    call      $342d                         ;[32e7] cd 2d 34
                    call      $3f00                         ;[32ea] cd 00 3f
                    ld        b,$01                         ;[32ed] 06 01
                    call      $3465                         ;[32ef] cd 65 34
                    jp        nc,$338b                      ;[32f2] d2 8b 33
                    ld        a,$01                         ;[32f5] 3e 01
                    ld        ($c00a),a                     ;[32f7] 32 0a c0
                    ld        hl,$0000                      ;[32fa] 21 00 00
                    ld        ($c00d),hl                    ;[32fd] 22 0d c0
                    ld        b,$00                         ;[3300] 06 00
                    call      $342d                         ;[3302] cd 2d 34
                    call      $3f00                         ;[3305] cd 00 3f
                    jr        $330b                         ;[3308] 18 01
                    call      $3465                         ;[330a] cd 65 34
                    jr        c,$3316                       ;[330d] 38 07
                    cp        $19                           ;[330f] fe 19
                    jp        nz,$338b                      ;[3311] c2 8b 33
                    jr        z,$332f                       ;[3314] 28 19
                    ld        b,$01                         ;[3316] 06 01
                    call      $342d                         ;[3318] cd 2d 34
                    call      $3f00                         ;[331b] cd 00 3f
                    dec       de                            ;[331e] 1b
                    ld        bc,$65cd                      ;[331f] 01 cd 65
                    inc       (hl)                          ;[3322] 34
                    ld        hl,($c00d)                    ;[3323] 2a 0d c0
                    inc       hl                            ;[3326] 23
                    ld        ($c00d),hl                    ;[3327] 22 0d c0
                    jr        c,$3300                       ;[332a] 38 d4
                    jp        $338b                         ;[332c] c3 8b 33
                    ld        b,$00                         ;[332f] 06 00
                    call      $342d                         ;[3331] cd 2d 34
                    call      $3f00                         ;[3334] cd 00 3f
                    add       hl,bc                         ;[3337] 09
                    ld        bc,$65cd                      ;[3338] 01 cd 65
                    inc       (hl)                          ;[333b] 34
                    jp        nc,$338b                      ;[333c] d2 8b 33
                    ld        a,($c005)                     ;[333f] 3a 05 c0
                    call      $342d                         ;[3342] cd 2d 34
                    call      $3f00                         ;[3345] cd 00 3f
                    ld        b,d                           ;[3348] 42
                    ld        bc,$65cd                      ;[3349] 01 cd 65
                    inc       (hl)                          ;[334c] 34
                    ld        b,$01                         ;[334d] 06 01
                    call      $342d                         ;[334f] cd 2d 34
                    call      $3f00                         ;[3352] cd 00 3f
                    rrca                                    ;[3355] 0f
                    ld        bc,$65cd                      ;[3356] 01 cd 65
                    inc       (hl)                          ;[3359] 34
                    jp        nc,$338b                      ;[335a] d2 8b 33
                    ld        a,$03                         ;[335d] 3e 03
                    ld        (ix+$00),a                    ;[335f] dd 77 00
                    ld        hl,($c00d)                    ;[3362] 2a 0d c0
                    ld        (ix+$01),l                    ;[3365] dd 75 01
                    ld        (ix+$02),h                    ;[3368] dd 74 02
                    xor       a                             ;[336b] af
                    ld        (ix+$03),a                    ;[336c] dd 77 03
                    ld        (ix+$04),a                    ;[336f] dd 77 04
                    ld        b,$01                         ;[3372] 06 01
                    call      $342d                         ;[3374] cd 2d 34
                    call      $3f00                         ;[3377] cd 00 3f
                    add       hl,bc                         ;[337a] 09
                    ld        bc,$65cd                      ;[337b] 01 cd 65
                    inc       (hl)                          ;[337e] 34
                    jp        nc,$338b                      ;[337f] d2 8b 33
                    call      $2cd6                         ;[3382] cd d6 2c
                    ret                                     ;[3385] c9

                    ld        l,$48                         ;[3386] 2e 48
                    ld        b,l                           ;[3388] 45
                    ld        b,h                           ;[3389] 44
                    rst       $38                           ;[338a] ff
                    push      af                            ;[338b] f5
                    ld        b,$03                         ;[338c] 06 03
                    push      bc                            ;[338e] c5
                    dec       b                             ;[338f] 05
                    push      bc                            ;[3390] c5
                    call      $342d                         ;[3391] cd 2d 34
                    call      $3f00                         ;[3394] cd 00 3f
                    add       hl,bc                         ;[3397] 09
                    ld        bc,$65cd                      ;[3398] 01 cd 65
                    inc       (hl)                          ;[339b] 34
                    pop       bc                            ;[339c] c1
                    jr        c,$33aa                       ;[339d] 38 0b
                    call      $342d                         ;[339f] cd 2d 34
                    call      $3f00                         ;[33a2] cd 00 3f
                    inc       c                             ;[33a5] 0c
                    ld        bc,$65cd                      ;[33a6] 01 cd 65
                    inc       (hl)                          ;[33a9] 34
                    pop       bc                            ;[33aa] c1
                    djnz      $338e                         ;[33ab] 10 e1
                    ld        a,$0d                         ;[33ad] 3e 0d
                    rst       $10                           ;[33af] d7
                    ld        a,($c00a)                     ;[33b0] 3a 0a c0
                    or        a                             ;[33b3] b7
                    jr        z,$33c4                       ;[33b4] 28 0e
                    ld        hl,$c010                      ;[33b6] 21 10 c0
                    call      $342d                         ;[33b9] cd 2d 34
                    call      $3f00                         ;[33bc] cd 00 3f
                    inc       h                             ;[33bf] 24
                    ld        bc,$65cd                      ;[33c0] 01 cd 65
                    inc       (hl)                          ;[33c3] 34
                    ld        hl,$c04e                      ;[33c4] 21 4e c0
                    call      $342d                         ;[33c7] cd 2d 34
                    call      $3f00                         ;[33ca] cd 00 3f
                    inc       h                             ;[33cd] 24
                    ld        bc,$65cd                      ;[33ce] 01 cd 65
                    inc       (hl)                          ;[33d1] 34
                    pop       af                            ;[33d2] f1
                    call      $2cd6                         ;[33d3] cd d6 2c
                    ld        a,$02                         ;[33d6] 3e 02
                    rst       $28                           ;[33d8] ef
                    ld        bc,$cd16                      ;[33d9] 01 16 cd
                    or        (hl)                          ;[33dc] b6
                    ld        c,$ff                         ;[33dd] 0e ff
                    ld        a,(hl)                        ;[33df] 7e
                    inc       hl                            ;[33e0] 23
                    or        a                             ;[33e1] b7
                    ret       z                             ;[33e2] c8
                    cp        $ff                           ;[33e3] fe ff
                    ret       z                             ;[33e5] c8
                    and       $7f                           ;[33e6] e6 7f
                    rst       $10                           ;[33e8] d7
                    jr        $33df                         ;[33e9] 18 f4
                    ld        hl,$5c3b                      ;[33eb] 21 3b 5c
                    res       5,(hl)                        ;[33ee] cb ae
                    bit       5,(hl)                        ;[33f0] cb 6e
                    jr        z,$33f0                       ;[33f2] 28 fc
                    res       5,(hl)                        ;[33f4] cb ae
                    ld        a,($5c08)                     ;[33f6] 3a 08 5c
                    ret                                     ;[33f9] c9

                    ld        c,l                           ;[33fa] 4d
                    ld        a,($4156)                     ;[33fb] 3a 56 41
                    ld        e,b                           ;[33fe] 58
                    ld        c,(hl)                        ;[33ff] 4e
                    ld        d,e                           ;[3400] 53
                    ld        d,l                           ;[3401] 55
                    ld        e,d                           ;[3402] 5a
                    ld        l,$24                         ;[3403] 2e 24
                    inc       h                             ;[3405] 24
                    inc       h                             ;[3406] 24
                    rst       $38                           ;[3407] ff
                    dec       c                             ;[3408] 0d
                    jr        nz,$342b                      ;[3409] 20 20
                    ld        sp,$6620                      ;[340b] 31 20 66
                    ld        l,c                           ;[340e] 69
                    ld        l,h                           ;[340f] 6c
                    ld        h,l                           ;[3410] 65
                    jr        nz,$3476                      ;[3411] 20 63
                    ld        l,a                           ;[3413] 6f
                    ld        (hl),b                        ;[3414] 70
                    ld        l,c                           ;[3415] 69
                    ld        h,l                           ;[3416] 65
                    ld        h,h                           ;[3417] 64
                    ld        l,$0d                         ;[3418] 2e 0d
                    dec       c                             ;[341a] 0d
                    nop                                     ;[341b] 00
                    jr        nz,$3484                      ;[341c] 20 66
                    ld        l,c                           ;[341e] 69
                    ld        l,h                           ;[341f] 6c
                    ld        h,l                           ;[3420] 65
                    ld        (hl),e                        ;[3421] 73
                    jr        nz,$3487                      ;[3422] 20 63
                    ld        l,a                           ;[3424] 6f
                    ld        (hl),b                        ;[3425] 70
                    ld        l,c                           ;[3426] 69
                    ld        h,l                           ;[3427] 65
                    ld        h,h                           ;[3428] 64
                    ld        l,$0d                         ;[3429] 2e 0d
                    dec       c                             ;[342b] 0d
                    nop                                     ;[342c] 00
                    di                                      ;[342d] f3
                    ld        ($e608),hl                    ;[342e] 22 08 e6
                    push      af                            ;[3431] f5
                    pop       hl                            ;[3432] e1
                    ld        ($e606),hl                    ;[3433] 22 06 e6
                    ld        ($e60a),de                    ;[3436] ed 53 0a e6
                    ld        ($e60c),bc                    ;[343a] ed 43 0c e6
                    ld        hl,$5bff                      ;[343e] 21 ff 5b
                    ld        de,$e7ff                      ;[3441] 11 ff e7
                    ld        bc,$0083                      ;[3444] 01 83 00
                    lddr                                    ;[3447] ed b8
                    pop       bc                            ;[3449] c1
                    ld        ($e602),sp                    ;[344a] ed 73 02 e6
                    ld        hl,$5bff                      ;[344e] 21 ff 5b
                    ld        sp,hl                         ;[3451] f9
                    push      bc                            ;[3452] c5
                    ld        bc,($e60c)                    ;[3453] ed 4b 0c e6
                    ld        de,($e60a)                    ;[3457] ed 5b 0a e6
                    ld        hl,($e606)                    ;[345b] 2a 06 e6
                    push      hl                            ;[345e] e5
                    pop       af                            ;[345f] f1
                    ld        hl,($e608)                    ;[3460] 2a 08 e6
                    ei                                      ;[3463] fb
                    ret                                     ;[3464] c9

                    di                                      ;[3465] f3
                    ld        ($e608),hl                    ;[3466] 22 08 e6
                    push      af                            ;[3469] f5
                    pop       hl                            ;[346a] e1
                    ld        ($e606),hl                    ;[346b] 22 06 e6
                    ld        ($e60a),de                    ;[346e] ed 53 0a e6
                    ld        ($e60c),bc                    ;[3472] ed 43 0c e6
                    pop       hl                            ;[3476] e1
                    ld        ($e604),hl                    ;[3477] 22 04 e6
                    ld        hl,$e7ff                      ;[347a] 21 ff e7
                    ld        de,$5bff                      ;[347d] 11 ff 5b
                    ld        bc,$0083                      ;[3480] 01 83 00
                    lddr                                    ;[3483] ed b8
                    ld        hl,($e602)                    ;[3485] 2a 02 e6
                    ld        sp,hl                         ;[3488] f9
                    ld        hl,($e604)                    ;[3489] 2a 04 e6
                    push      hl                            ;[348c] e5
                    ld        bc,($e60c)                    ;[348d] ed 4b 0c e6
                    ld        de,($e60a)                    ;[3491] ed 5b 0a e6
                    ld        hl,($e606)                    ;[3495] 2a 06 e6
                    push      hl                            ;[3498] e5
                    pop       af                            ;[3499] f1
                    ld        hl,($e608)                    ;[349a] 2a 08 e6
                    ei                                      ;[349d] fb
                    ret                                     ;[349e] c9

                    xor       a                             ;[349f] af
                    call      $2cfb                         ;[34a0] cd fb 2c
                    ld        ($ed16),a                     ;[34a3] 32 16 ed
                    call      $2cd6                         ;[34a6] cd d6 2c
                    rst       $28                           ;[34a9] ef
                    jr        nz,$34ac                      ;[34aa] 20 00
                    cp        $dd                           ;[34ac] fe dd
                    jr        nz,$34be                      ;[34ae] 20 0e
                    ld        a,$fc                         ;[34b0] 3e fc
                    call      $2cfb                         ;[34b2] cd fb 2c
                    ld        ($ed16),a                     ;[34b5] 32 16 ed
                    call      $2cd6                         ;[34b8] cd d6 2c
                    rst       $28                           ;[34bb] ef
                    jr        nz,$34be                      ;[34bc] 20 00
                    cp        $dc                           ;[34be] fe dc
                    jr        nz,$34dc                      ;[34c0] 20 1a
                    ld        hl,$5c3b                      ;[34c2] 21 3b 5c
                    bit       7,(hl)                        ;[34c5] cb 7e
                    jr        z,$34d9                       ;[34c7] 28 10
                    ld        hl,$5800                      ;[34c9] 21 00 58
                    ld        bc,$0300                      ;[34cc] 01 00 03
                    ld        a,(hl)                        ;[34cf] 7e
                    or        $40                           ;[34d0] f6 40
                    ld        (hl),a                        ;[34d2] 77
                    inc       hl                            ;[34d3] 23
                    dec       bc                            ;[34d4] 0b
                    ld        a,b                           ;[34d5] 78
                    or        c                             ;[34d6] b1
                    jr        nz,$34cf                      ;[34d7] 20 f6
                    rst       $28                           ;[34d9] ef
                    jr        nz,$34dc                      ;[34da] 20 00
                    call      $10cd                         ;[34dc] cd cd 10
                    ld        a,($5b67)                     ;[34df] 3a 67 5b
                    ld        bc,$1ffd                      ;[34e2] 01 fd 1f
                    set       4,a                           ;[34e5] cb e7
                    di                                      ;[34e7] f3
                    ld        ($5b67),a                     ;[34e8] 32 67 5b
                    out       (c),a                         ;[34eb] ed 79
                    ei                                      ;[34ed] fb
                    call      $2cfb                         ;[34ee] cd fb 2c
                    di                                      ;[34f1] f3
                    ld        a,$1b                         ;[34f2] 3e 1b
                    call      $354e                         ;[34f4] cd 4e 35
                    ld        a,$33                         ;[34f7] 3e 33
                    call      $354e                         ;[34f9] cd 4e 35
                    ld        hl,$5b7b                      ;[34fc] 21 7b 5b
                    ld        a,(hl)                        ;[34ff] 7e
                    call      $354e                         ;[3500] cd 4e 35
                    ld        hl,$401f                      ;[3503] 21 1f 40
                    ld        e,$20                         ;[3506] 1e 20
                    push      hl                            ;[3508] e5
                    ld        d,$01                         ;[3509] 16 01
                    push      de                            ;[350b] d5
                    push      hl                            ;[350c] e5
                    ld        hl,$3654                      ;[350d] 21 54 36
                    call      $355a                         ;[3510] cd 5a 35
                    pop       hl                            ;[3513] e1
                    pop       de                            ;[3514] d1
                    push      hl                            ;[3515] e5
                    call      $3566                         ;[3516] cd 66 35
                    ld        a,h                           ;[3519] 7c
                    and       $07                           ;[351a] e6 07
                    inc       h                             ;[351c] 24
                    cp        $07                           ;[351d] fe 07
                    jr        nz,$3516                      ;[351f] 20 f5
                    ld        a,h                           ;[3521] 7c
                    sub       $08                           ;[3522] d6 08
                    ld        h,a                           ;[3524] 67
                    ld        a,l                           ;[3525] 7d
                    add       $20                           ;[3526] c6 20
                    ld        l,a                           ;[3528] 6f
                    jr        nc,$3516                      ;[3529] 30 eb
                    ld        a,h                           ;[352b] 7c
                    add       $08                           ;[352c] c6 08
                    ld        h,a                           ;[352e] 67
                    cp        $58                           ;[352f] fe 58
                    jr        nz,$3516                      ;[3531] 20 e3
                    pop       hl                            ;[3533] e1
                    sla       d                             ;[3534] cb 22
                    sla       d                             ;[3536] cb 22
                    jr        nc,$350b                      ;[3538] 30 d1
                    pop       hl                            ;[353a] e1
                    dec       hl                            ;[353b] 2b
                    dec       e                             ;[353c] 1d
                    jr        nz,$3508                      ;[353d] 20 c9
                    ld        a,$1b                         ;[353f] 3e 1b
                    call      $354e                         ;[3541] cd 4e 35
                    ld        a,$40                         ;[3544] 3e 40
                    call      $354e                         ;[3546] cd 4e 35
                    ei                                      ;[3549] fb
                    call      $2cd6                         ;[354a] cd d6 2c
                    ret                                     ;[354d] c9

                    ei                                      ;[354e] fb
                    call      $2cd6                         ;[354f] cd d6 2c
                    call      $2159                         ;[3552] cd 59 21
                    call      $2cfb                         ;[3555] cd fb 2c
                    di                                      ;[3558] f3
                    ret                                     ;[3559] c9

                    ld        a,(hl)                        ;[355a] 7e
                    cp        $ff                           ;[355b] fe ff
                    ret       z                             ;[355d] c8
                    push      hl                            ;[355e] e5
                    call      $354e                         ;[355f] cd 4e 35
                    pop       hl                            ;[3562] e1
                    inc       hl                            ;[3563] 23
                    jr        $355a                         ;[3564] 18 f4
                    push      af                            ;[3566] f5
                    push      hl                            ;[3567] e5
                    push      de                            ;[3568] d5
                    push      hl                            ;[3569] e5
                    call      $3597                         ;[356a] cd 97 35
                    pop       hl                            ;[356d] e1
                    call      $35a7                         ;[356e] cd a7 35
                    call      $35d4                         ;[3571] cd d4 35
                    call      $35b4                         ;[3574] cd b4 35
                    sla       d                             ;[3577] cb 22
                    call      $35d4                         ;[3579] cd d4 35
                    call      $35c7                         ;[357c] cd c7 35
                    ld        b,$04                         ;[357f] 06 04
                    ld        hl,$ed11                      ;[3581] 21 11 ed
                    ld        a,(hl)                        ;[3584] 7e
                    push      bc                            ;[3585] c5
                    push      hl                            ;[3586] e5
                    ld        hl,$ed16                      ;[3587] 21 16 ed
                    xor       (hl)                          ;[358a] ae
                    call      $354e                         ;[358b] cd 4e 35
                    pop       hl                            ;[358e] e1
                    pop       bc                            ;[358f] c1
                    inc       hl                            ;[3590] 23
                    djnz      $3584                         ;[3591] 10 f1
                    pop       de                            ;[3593] d1
                    pop       hl                            ;[3594] e1
                    pop       af                            ;[3595] f1
                    ret                                     ;[3596] c9

                    push      af                            ;[3597] f5
                    ld        a,h                           ;[3598] 7c
                    and       $18                           ;[3599] e6 18
                    srl       a                             ;[359b] cb 3f
                    srl       a                             ;[359d] cb 3f
                    srl       a                             ;[359f] cb 3f
                    or        $58                           ;[35a1] f6 58
                    ld        h,a                           ;[35a3] 67
                    ld        e,(hl)                        ;[35a4] 5e
                    pop       af                            ;[35a5] f1
                    ret                                     ;[35a6] c9

                    push      hl                            ;[35a7] e5
                    ld        hl,$ed11                      ;[35a8] 21 11 ed
                    ld        b,$04                         ;[35ab] 06 04
                    ld        (hl),$00                      ;[35ad] 36 00
                    inc       hl                            ;[35af] 23
                    djnz      $35ad                         ;[35b0] 10 fb
                    pop       hl                            ;[35b2] e1
                    ret                                     ;[35b3] c9

                    push      hl                            ;[35b4] e5
                    push      bc                            ;[35b5] c5
                    ld        hl,$ed11                      ;[35b6] 21 11 ed
                    ld        b,$04                         ;[35b9] 06 04
                    sla       (hl)                          ;[35bb] cb 26
                    sla       (hl)                          ;[35bd] cb 26
                    sla       (hl)                          ;[35bf] cb 26
                    inc       hl                            ;[35c1] 23
                    djnz      $35bb                         ;[35c2] 10 f7
                    pop       bc                            ;[35c4] c1
                    pop       hl                            ;[35c5] e1
                    ret                                     ;[35c6] c9

                    ld        hl,$ed11                      ;[35c7] 21 11 ed
                    ld        b,$04                         ;[35ca] 06 04
                    sla       (hl)                          ;[35cc] cb 26
                    sla       (hl)                          ;[35ce] cb 26
                    inc       hl                            ;[35d0] 23
                    djnz      $35cc                         ;[35d1] 10 f9
                    ret                                     ;[35d3] c9

                    push      de                            ;[35d4] d5
                    push      hl                            ;[35d5] e5
                    ld        a,d                           ;[35d6] 7a
                    and       (hl)                          ;[35d7] a6
                    ld        a,e                           ;[35d8] 7b
                    jr        nz,$35e1                      ;[35d9] 20 06
                    srl       a                             ;[35db] cb 3f
                    srl       a                             ;[35dd] cb 3f
                    srl       a                             ;[35df] cb 3f
                    and       $07                           ;[35e1] e6 07
                    bit       6,e                           ;[35e3] cb 73
                    jr        z,$35e9                       ;[35e5] 28 02
                    or        $08                           ;[35e7] f6 08
                    ld        hl,$3604                      ;[35e9] 21 04 36
                    ld        d,$00                         ;[35ec] 16 00
                    ld        e,a                           ;[35ee] 5f
                    add       hl,de                         ;[35ef] 19
                    ld        e,(hl)                        ;[35f0] 5e
                    ld        hl,$3614                      ;[35f1] 21 14 36
                    add       hl,de                         ;[35f4] 19
                    ld        b,$04                         ;[35f5] 06 04
                    ld        de,$ed11                      ;[35f7] 11 11 ed
                    ld        a,(de)                        ;[35fa] 1a
                    or        (hl)                          ;[35fb] b6
                    ld        (de),a                        ;[35fc] 12
                    inc       hl                            ;[35fd] 23
                    inc       de                            ;[35fe] 13
                    djnz      $35fa                         ;[35ff] 10 f9
                    pop       hl                            ;[3601] e1
                    pop       de                            ;[3602] d1
                    ret                                     ;[3603] c9

                    nop                                     ;[3604] 00
                    inc       b                             ;[3605] 04
                    ex        af,af'                        ;[3606] 08
                    inc       c                             ;[3607] 0c
                    djnz      $361e                         ;[3608] 10 14
                    jr        $3628                         ;[360a] 18 1c
                    jr        nz,$3632                      ;[360c] 20 24
                    jr        z,$363c                       ;[360e] 28 2c
                    jr        nc,$3646                      ;[3610] 30 34
                    jr        c,$3650                       ;[3612] 38 3c
                    rlca                                    ;[3614] 07
                    rlca                                    ;[3615] 07
                    rlca                                    ;[3616] 07
                    rlca                                    ;[3617] 07
                    rlca                                    ;[3618] 07
                    dec       b                             ;[3619] 05
                    rlca                                    ;[361a] 07
                    rlca                                    ;[361b] 07
                    inc       bc                            ;[361c] 03
                    rlca                                    ;[361d] 07
                    ld        b,$07                         ;[361e] 06 07
                    rlca                                    ;[3620] 07
                    inc       bc                            ;[3621] 03
                    ld        b,$03                         ;[3622] 06 03
                    ld        b,$03                         ;[3624] 06 03
                    ld        b,$03                         ;[3626] 06 03
                    ld        b,$05                         ;[3628] 06 05
                    ld        (bc),a                        ;[362a] 02
                    dec       b                             ;[362b] 05
                    ld        (bc),a                        ;[362c] 02
                    dec       b                             ;[362d] 05
                    ld        (bc),a                        ;[362e] 02
                    dec       b                             ;[362f] 05
                    ld        bc,$0306                      ;[3630] 01 06 03
                    inc       b                             ;[3633] 04
                    rlca                                    ;[3634] 07
                    rlca                                    ;[3635] 07
                    rlca                                    ;[3636] 07
                    rlca                                    ;[3637] 07
                    dec       b                             ;[3638] 05
                    ld        (bc),a                        ;[3639] 02
                    inc       bc                            ;[363a] 03
                    inc       b                             ;[363b] 04
                    ld        b,$01                         ;[363c] 06 01
                    ld        (bc),a                        ;[363e] 02
                    ld        bc,$0401                      ;[363f] 01 01 04
                    ld        (bc),a                        ;[3642] 02
                    inc       b                             ;[3643] 04
                    inc       b                             ;[3644] 04
                    nop                                     ;[3645] 00
                    inc       b                             ;[3646] 04
                    ld        bc,$0001                      ;[3647] 01 01 00
                    inc       b                             ;[364a] 04
                    nop                                     ;[364b] 00
                    nop                                     ;[364c] 00
                    ld        (bc),a                        ;[364d] 02
                    nop                                     ;[364e] 00
                    nop                                     ;[364f] 00
                    nop                                     ;[3650] 00
                    nop                                     ;[3651] 00
                    nop                                     ;[3652] 00
                    nop                                     ;[3653] 00
                    dec       c                             ;[3654] 0d
                    ld        a,(bc)                        ;[3655] 0a
                    dec       de                            ;[3656] 1b
                    ld        c,h                           ;[3657] 4c
                    nop                                     ;[3658] 00
                    inc       bc                            ;[3659] 03
                    rst       $38                           ;[365a] ff
                    ld        bc,$0011                      ;[365b] 01 11 00
                    rst       $28                           ;[365e] ef
                    jr        nc,$3661                      ;[365f] 30 00
                    push      de                            ;[3661] d5
                    pop       ix                            ;[3662] dd e1
                    ld        a,$0d                         ;[3664] 3e 0d
                    rst       $28                           ;[3666] ef
                    djnz      $3669                         ;[3667] 10 00
                    ld        a,$7f                         ;[3669] 3e 7f
                    in        a,($fe)                       ;[366b] db fe
                    rra                                     ;[366d] 1f
                    jr        c,$3678                       ;[366e] 38 08
                    ld        a,$fe                         ;[3670] 3e fe
                    in        a,($fe)                       ;[3672] db fe
                    rra                                     ;[3674] 1f
                    jr        c,$3678                       ;[3675] 38 01
                    ret                                     ;[3677] c9

                    ld        a,$00                         ;[3678] 3e 00
                    ld        de,$0011                      ;[367a] 11 11 00
                    scf                                     ;[367d] 37
                    push      ix                            ;[367e] dd e5
                    rst       $28                           ;[3680] ef
                    ld        d,(hl)                        ;[3681] 56
                    dec       b                             ;[3682] 05
                    pop       ix                            ;[3683] dd e1
                    jr        nc,$3669                      ;[3685] 30 e2
                    push      ix                            ;[3687] dd e5
                    ld        a,$22                         ;[3689] 3e 22
                    rst       $28                           ;[368b] ef
                    djnz      $368e                         ;[368c] 10 00
                    ld        b,$0a                         ;[368e] 06 0a
                    ld        a,(ix+$01)                    ;[3690] dd 7e 01
                    rst       $28                           ;[3693] ef
                    djnz      $3696                         ;[3694] 10 00
                    inc       ix                            ;[3696] dd 23
                    djnz      $3690                         ;[3698] 10 f6
                    pop       ix                            ;[369a] dd e1
                    ld        hl,$3736                      ;[369c] 21 36 37
                    call      $3726                         ;[369f] cd 26 37
                    ld        a,(ix+$00)                    ;[36a2] dd 7e 00
                    cp        $00                           ;[36a5] fe 00
                    jr        nz,$36cc                      ;[36a7] 20 23
                    ld        a,(ix+$0e)                    ;[36a9] dd 7e 0e
                    cp        $80                           ;[36ac] fe 80
                    jr        z,$36c4                       ;[36ae] 28 14
                    ld        hl,$3753                      ;[36b0] 21 53 37
                    call      $3726                         ;[36b3] cd 26 37
                    ld        c,(ix+$0d)                    ;[36b6] dd 4e 0d
                    ld        b,(ix+$0e)                    ;[36b9] dd 46 0e
                    call      $372f                         ;[36bc] cd 2f 37
                    ld        a,$20                         ;[36bf] 3e 20
                    rst       $28                           ;[36c1] ef
                    djnz      $36c4                         ;[36c2] 10 00
                    ld        hl,$3739                      ;[36c4] 21 39 37
                    call      $3726                         ;[36c7] cd 26 37
                    jr        $3664                         ;[36ca] 18 98
                    cp        $01                           ;[36cc] fe 01
                    jr        nz,$36e9                      ;[36ce] 20 19
                    ld        hl,$3742                      ;[36d0] 21 42 37
                    call      $3726                         ;[36d3] cd 26 37
                    ld        a,(ix+$0e)                    ;[36d6] dd 7e 0e
                    and       $7f                           ;[36d9] e6 7f
                    or        $40                           ;[36db] f6 40
                    rst       $28                           ;[36dd] ef
                    djnz      $36e0                         ;[36de] 10 00
                    ld        hl,$374f                      ;[36e0] 21 4f 37
                    call      $3726                         ;[36e3] cd 26 37
                    jp        $3664                         ;[36e6] c3 64 36
                    cp        $02                           ;[36e9] fe 02
                    jr        nz,$3706                      ;[36eb] 20 19
                    ld        hl,$3742                      ;[36ed] 21 42 37
                    call      $3726                         ;[36f0] cd 26 37
                    ld        a,(ix+$0e)                    ;[36f3] dd 7e 0e
                    and       $7f                           ;[36f6] e6 7f
                    or        $40                           ;[36f8] f6 40
                    rst       $28                           ;[36fa] ef
                    djnz      $36fd                         ;[36fb] 10 00
                    ld        hl,$374e                      ;[36fd] 21 4e 37
                    call      $3726                         ;[3700] cd 26 37
                    jp        $3664                         ;[3703] c3 64 36
                    ld        hl,$3748                      ;[3706] 21 48 37
                    call      $3726                         ;[3709] cd 26 37
                    ld        c,(ix+$0d)                    ;[370c] dd 4e 0d
                    ld        b,(ix+$0e)                    ;[370f] dd 46 0e
                    call      $372f                         ;[3712] cd 2f 37
                    ld        a,$2c                         ;[3715] 3e 2c
                    rst       $28                           ;[3717] ef
                    djnz      $371a                         ;[3718] 10 00
                    ld        c,(ix+$0b)                    ;[371a] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[371d] dd 46 0c
                    call      $372f                         ;[3720] cd 2f 37
                    jp        $3664                         ;[3723] c3 64 36
                    ld        a,(hl)                        ;[3726] 7e
                    or        a                             ;[3727] b7
                    ret       z                             ;[3728] c8
                    rst       $28                           ;[3729] ef
                    djnz      $372c                         ;[372a] 10 00
                    inc       hl                            ;[372c] 23
                    jr        $3726                         ;[372d] 18 f7
                    rst       $28                           ;[372f] ef
                    dec       hl                            ;[3730] 2b
                    dec       l                             ;[3731] 2d
                    rst       $28                           ;[3732] ef
                    ex        (sp),hl                       ;[3733] e3
                    dec       l                             ;[3734] 2d
                    ret                                     ;[3735] c9

                    ld        ($0020),hl                    ;[3736] 22 20 00
                    jr        z,$377d                       ;[3739] 28 42
                    ld        b,c                           ;[373b] 41
                    ld        d,e                           ;[373c] 53
                    ld        c,c                           ;[373d] 49
                    ld        b,e                           ;[373e] 43
                    add       hl,hl                         ;[373f] 29
                    jr        nz,$3742                      ;[3740] 20 00
                    ld        b,h                           ;[3742] 44
                    ld        b,c                           ;[3743] 41
                    ld        d,h                           ;[3744] 54
                    ld        b,c                           ;[3745] 41
                    jr        nz,$3748                      ;[3746] 20 00
                    ld        b,e                           ;[3748] 43
                    ld        c,a                           ;[3749] 4f
                    ld        b,h                           ;[374a] 44
                    ld        b,l                           ;[374b] 45
                    jr        nz,$374e                      ;[374c] 20 00
                    inc       h                             ;[374e] 24
                    jr        z,$377a                       ;[374f] 28 29
                    jr        nz,$3753                      ;[3751] 20 00
                    ld        c,h                           ;[3753] 4c
                    ld        c,c                           ;[3754] 49
                    ld        c,(hl)                        ;[3755] 4e
                    ld        b,l                           ;[3756] 45
                    jr        nz,$3759                      ;[3757] 20 00
                    ld        c,$40                         ;[3759] 0e 40
                    ld        b,$00                         ;[375b] 06 00
                    ld        a,$fe                         ;[375d] 3e fe
                    in        a,($fe)                       ;[375f] db fe
                    bit       3,a                           ;[3761] cb 5f
                    jr        nz,$3771                      ;[3763] 20 0c
                    ld        a,$bf                         ;[3765] 3e bf
                    in        a,($fe)                       ;[3767] db fe
                    bit       3,a                           ;[3769] cb 5f
                    jr        nz,$3771                      ;[376b] 20 04
                    bit       1,a                           ;[376d] cb 4f
                    jr        z,$377a                       ;[376f] 28 09
                    djnz      $375d                         ;[3771] 10 ea
                    dec       c                             ;[3773] 0d
                    jr        nz,$375b                      ;[3774] 20 e5
                    call      $2c4c                         ;[3776] cd 4c 2c
                    dec       bc                            ;[3779] 0b
                    ld        ix,$2000                      ;[377a] dd 21 00 20
                    xor       a                             ;[377e] af
                    out       ($fe),a                       ;[377f] d3 fe
                    ld        hl,$5800                      ;[3781] 21 00 58
                    ld        de,$5801                      ;[3784] 11 01 58
                    ld        bc,$02ff                      ;[3787] 01 ff 02
                    ld        (hl),$00                      ;[378a] 36 00
                    ldir                                    ;[378c] ed b0
                    ld        hl,$4000                      ;[378e] 21 00 40
                    ld        de,$4001                      ;[3791] 11 01 40
                    ld        bc,$17ff                      ;[3794] 01 ff 17
                    ld        (hl),$ff                      ;[3797] 36 ff
                    ldir                                    ;[3799] ed b0
                    ld        c,$07                         ;[379b] 0e 07
                    ld        a,c                           ;[379d] 79
                    and       $07                           ;[379e] e6 07
                    jr        nz,$37a5                      ;[37a0] 20 03
                    inc       c                             ;[37a2] 0c
                    jr        $379d                         ;[37a3] 18 f8
                    ld        d,$58                         ;[37a5] 16 58
                    ld        hl,$3801                      ;[37a7] 21 01 38
                    ld        e,(hl)                        ;[37aa] 5e
                    push      af                            ;[37ab] f5
                    ld        a,$fb                         ;[37ac] 3e fb
                    in        a,($fe)                       ;[37ae] db fe
                    rra                                     ;[37b0] 1f
                    jr        c,$37c5                       ;[37b1] 38 12
                    pop       af                            ;[37b3] f1
                    ld        hl,$5800                      ;[37b4] 21 00 58
                    ld        de,$5801                      ;[37b7] 11 01 58
                    ld        bc,$02ff                      ;[37ba] 01 ff 02
                    ld        (hl),$38                      ;[37bd] 36 38
                    ldir                                    ;[37bf] ed b0
                    call      $2c4c                         ;[37c1] cd 4c 2c
                    rst       $38                           ;[37c4] ff
                    ld        a,$ef                         ;[37c5] 3e ef
                    in        a,($fe)                       ;[37c7] db fe
                    rra                                     ;[37c9] 1f
                    jr        c,$37ce                       ;[37ca] 38 02
                    inc       ix                            ;[37cc] dd 23
                    ld        a,$df                         ;[37ce] 3e df
                    in        a,($fe)                       ;[37d0] db fe
                    rra                                     ;[37d2] 1f
                    jr        c,$37d7                       ;[37d3] 38 02
                    dec       ix                            ;[37d5] dd 2b
                    ld        a,e                           ;[37d7] 7b
                    or        a                             ;[37d8] b7
                    jr        nz,$37f4                      ;[37d9] 20 19
                    inc       d                             ;[37db] 14
                    ld        a,d                           ;[37dc] 7a
                    cp        $5b                           ;[37dd] fe 5b
                    jr        nz,$37f1                      ;[37df] 20 10
                    pop       af                            ;[37e1] f1
                    inc       c                             ;[37e2] 0c
                    push      af                            ;[37e3] f5
                    push      bc                            ;[37e4] c5
                    push      ix                            ;[37e5] dd e5
                    pop       bc                            ;[37e7] c1
                    dec       bc                            ;[37e8] 0b
                    ld        a,b                           ;[37e9] 78
                    or        c                             ;[37ea] b1
                    jr        nz,$37e8                      ;[37eb] 20 fb
                    pop       bc                            ;[37ed] c1
                    pop       af                            ;[37ee] f1
                    jr        $379d                         ;[37ef] 18 ac
                    pop       af                            ;[37f1] f1
                    jr        $37fe                         ;[37f2] 18 0a
                    pop       af                            ;[37f4] f1
                    and       $07                           ;[37f5] e6 07
                    or        a                             ;[37f7] b7
                    jr        nz,$37fc                      ;[37f8] 20 02
                    ld        a,$07                         ;[37fa] 3e 07
                    ld        (de),a                        ;[37fc] 12
                    dec       a                             ;[37fd] 3d
                    inc       hl                            ;[37fe] 23
                    jr        $37aa                         ;[37ff] 18 a9
                    ld        hl,$6141                      ;[3801] 21 41 61
                    add       c                             ;[3804] 81
                    and       c                             ;[3805] a1
                    pop       bc                            ;[3806] c1
                    pop       hl                            ;[3807] e1
                    add       d                             ;[3808] 82
                    add       e                             ;[3809] 83
                    add       h                             ;[380a] 84
                    dec       h                             ;[380b] 25
                    ld        b,l                           ;[380c] 45
                    ld        h,l                           ;[380d] 65
                    add       l                             ;[380e] 85
                    and       l                             ;[380f] a5
                    push      bc                            ;[3810] c5
                    push      hl                            ;[3811] e5
                    daa                                     ;[3812] 27
                    ld        b,a                           ;[3813] 47
                    ld        h,a                           ;[3814] 67
                    add       a                             ;[3815] 87
                    and       a                             ;[3816] a7
                    rst       $00                           ;[3817] c7
                    rst       $20                           ;[3818] e7
                    jr        z,$3844                       ;[3819] 28 29
                    ld        hl,($882b)                    ;[381b] 2a 2b 88
                    adc       c                             ;[381e] 89
                    adc       d                             ;[381f] 8a
                    ret       pe                            ;[3820] e8
                    jp        (hl)                          ;[3821] e9
                    jp        pe,$2deb                      ;[3822] ea eb 2d
                    ld        c,l                           ;[3825] 4d
                    ld        l,l                           ;[3826] 6d
                    adc       l                             ;[3827] 8d
                    xor       l                             ;[3828] ad
                    call      $eeed                         ;[3829] cd ed ee
                    rst       $28                           ;[382c] ef
                    ret       p                             ;[382d] f0
                    pop       af                            ;[382e] f1
                    inc       sp                            ;[382f] 33
                    ld        d,e                           ;[3830] 53
                    ld        (hl),e                        ;[3831] 73
                    sub       e                             ;[3832] 93
                    or        e                             ;[3833] b3
                    out       ($f3),a                       ;[3834] d3 f3
                    call      p,$f6f5                       ;[3836] f4 f5 f6
                    rst       $30                           ;[3839] f7
                    ld        e,c                           ;[383a] 59
                    ld        a,c                           ;[383b] 79
                    sbc       c                             ;[383c] 99
                    cp        c                             ;[383d] b9
                    exx                                     ;[383e] d9
                    ld        a,($3c3b)                     ;[383f] 3a 3b 3c
                    dec       a                             ;[3842] 3d
                    jp        m,$fcfb                       ;[3843] fa fb fc
                    ld        e,(iy+$7e)                    ;[3846] fd 5e 7e
                    sbc       (hl)                          ;[3849] 9e
                    cp        (hl)                          ;[384a] be
                    sbc       $00                           ;[384b] de 00
                    ld        hl,$2322                      ;[384d] 21 22 23
                    inc       h                             ;[3850] 24
                    dec       h                             ;[3851] 25
                    ld        b,e                           ;[3852] 43
                    ld        h,e                           ;[3853] 63
                    add       e                             ;[3854] 83
                    and       e                             ;[3855] a3
                    jp        $27e3                         ;[3856] c3 e3 27
                    ld        b,a                           ;[3859] 47
                    ld        h,a                           ;[385a] 67
                    add       a                             ;[385b] 87
                    and       a                             ;[385c] a7
                    rst       $00                           ;[385d] c7
                    rst       $20                           ;[385e] e7
                    adc       b                             ;[385f] 88
                    adc       c                             ;[3860] 89
                    adc       d                             ;[3861] 8a
                    dec       hl                            ;[3862] 2b
                    ld        c,e                           ;[3863] 4b
                    ld        l,e                           ;[3864] 6b
                    adc       e                             ;[3865] 8b
                    xor       e                             ;[3866] ab
                    set       5,e                           ;[3867] cb eb
                    dec       l                             ;[3869] 2d
                    ld        c,l                           ;[386a] 4d
                    ld        l,l                           ;[386b] 6d
                    adc       l                             ;[386c] 8d
                    xor       l                             ;[386d] ad
                    call      $2eed                         ;[386e] cd ed 2e
                    cpl                                     ;[3871] 2f
                    jr        nc,$38a5                      ;[3872] 30 31
                    adc       (hl)                          ;[3874] 8e
                    adc       a                             ;[3875] 8f
                    sub       b                             ;[3876] 90
                    xor       $ef                           ;[3877] ee ef
                    ret       p                             ;[3879] f0
                    pop       af                            ;[387a] f1
                    inc       sp                            ;[387b] 33
                    ld        d,e                           ;[387c] 53
                    ld        (hl),e                        ;[387d] 73
                    sub       e                             ;[387e] 93
                    or        e                             ;[387f] b3
                    out       ($f3),a                       ;[3880] d3 f3
                    inc       (hl)                          ;[3882] 34
                    dec       (hl)                          ;[3883] 35
                    ld        (hl),$94                      ;[3884] 36 94
                    sub       l                             ;[3886] 95
                    sub       (hl)                          ;[3887] 96
                    ld        d,a                           ;[3888] 57
                    ld        (hl),a                        ;[3889] 77
                    or        l                             ;[388a] b5
                    sub       $f7                           ;[388b] d6 f7
                    add       hl,sp                         ;[388d] 39
                    ld        e,c                           ;[388e] 59
                    ld        a,c                           ;[388f] 79
                    sbc       c                             ;[3890] 99
                    cp        c                             ;[3891] b9
                    exx                                     ;[3892] d9
                    ld        sp,hl                         ;[3893] f9
                    ld        a,($3c3b)                     ;[3894] 3a 3b 3c
                    dec       a                             ;[3897] 3d
                    sbc       d                             ;[3898] 9a
                    sbc       e                             ;[3899] 9b
                    sbc       h                             ;[389a] 9c
                    jp        m,$fcfb                       ;[389b] fa fb fc
                    nop                                     ;[389e] fd 00
                    jr        nz,$38e2                      ;[38a0] 20 40
                    ld        h,b                           ;[38a2] 60
                    add       b                             ;[38a3] 80
                    and       b                             ;[38a4] a0
                    ret       nz                            ;[38a5] c0
                    ret       po                            ;[38a6] e0
                    ld        ($4424),hl                    ;[38a7] 22 24 44
                    ld        h,h                           ;[38aa] 64
                    add       h                             ;[38ab] 84
                    and       h                             ;[38ac] a4
                    call      nz,$45e4                      ;[38ad] c4 e4 45
                    ld        h,(hl)                        ;[38b0] 66
                    ld        b,a                           ;[38b1] 47
                    jr        z,$38fc                       ;[38b2] 28 48
                    ld        l,b                           ;[38b4] 68
                    adc       b                             ;[38b5] 88
                    xor       b                             ;[38b6] a8
                    ret       z                             ;[38b7] c8
                    ret       pe                            ;[38b8] e8
                    ld        c,h                           ;[38b9] 4c
                    ld        l,h                           ;[38ba] 6c
                    adc       h                             ;[38bb] 8c
                    xor       h                             ;[38bc] ac
                    call      z,$2dec                       ;[38bd] cc ec 2d
                    ld        l,$2f                         ;[38c0] 2e 2f
                    adc       l                             ;[38c2] 8d
                    adc       (hl)                          ;[38c3] 8e
                    adc       a                             ;[38c4] 8f
                    ld        d,b                           ;[38c5] 50
                    ld        (hl),b                        ;[38c6] 70
                    sub       b                             ;[38c7] 90
                    or        b                             ;[38c8] b0
                    ret       nc                            ;[38c9] d0
                    ret       p                             ;[38ca] f0
                    sub       h                             ;[38cb] 94
                    sub       l                             ;[38cc] 95
                    sub       (hl)                          ;[38cd] 96
                    sub       a                             ;[38ce] 97
                    sbc       b                             ;[38cf] 98
                    ld        d,(hl)                        ;[38d0] 56
                    halt                                    ;[38d1] 76
                    or        (hl)                          ;[38d2] b6
                    sub       $5a                           ;[38d3] d6 5a
                    dec       sp                            ;[38d5] 3b
                    inc       a                             ;[38d6] 3c
                    dec       a                             ;[38d7] 3d
                    ld        e,(hl)                        ;[38d8] 5e
                    ld        a,(hl)                        ;[38d9] 7e
                    sbc       l                             ;[38da] 9d
                    sbc       h                             ;[38db] 9c
                    cp        (hl)                          ;[38dc] be
                    sbc       $fd                           ;[38dd] de fd
                    call      m,$dafb                       ;[38df] fc fb da
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
                    ld        hl,$3d03                      ;[3a00] 21 03 3d
                    jr        $3a08                         ;[3a03] 18 03
                    ld        hl,$3d06                      ;[3a05] 21 06 3d
                    ex        af,af'                        ;[3a08] 08
                    ld        bc,$1ffd                      ;[3a09] 01 fd 1f
                    ld        a,($5b67)                     ;[3a0c] 3a 67 5b
                    push      af                            ;[3a0f] f5
                    and       $fb                           ;[3a10] e6 fb
                    di                                      ;[3a12] f3
                    ld        ($5b67),a                     ;[3a13] 32 67 5b
                    out       (c),a                         ;[3a16] ed 79
                    jp        $3d00                         ;[3a18] c3 00 3d
                    ex        af,af'                        ;[3a1b] 08
                    pop       af                            ;[3a1c] f1
                    ld        bc,$1ffd                      ;[3a1d] 01 fd 1f
                    di                                      ;[3a20] f3
                    ld        ($5b67),a                     ;[3a21] 32 67 5b
                    out       (c),a                         ;[3a24] ed 79
                    ei                                      ;[3a26] fb
                    ex        af,af'                        ;[3a27] 08
                    ret                                     ;[3a28] c9

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
                    jp        $2c7b                         ;[3d00] c3 7b 2c
                    jp        $1e8c                         ;[3d03] c3 8c 1e
                    jp        $2076                         ;[3d06] c3 76 20
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
                    nop                                     ;[3e00] 00
                    nop                                     ;[3e01] 00
                    nop                                     ;[3e02] 00
                    nop                                     ;[3e03] 00
                    nop                                     ;[3e04] 00
                    nop                                     ;[3e05] 00
                    nop                                     ;[3e06] 00
                    nop                                     ;[3e07] 00
                    nop                                     ;[3e08] 00
                    nop                                     ;[3e09] 00
                    nop                                     ;[3e0a] 00
                    nop                                     ;[3e0b] 00
                    nop                                     ;[3e0c] 00
                    nop                                     ;[3e0d] 00
                    nop                                     ;[3e0e] 00
                    nop                                     ;[3e0f] 00
                    nop                                     ;[3e10] 00
                    nop                                     ;[3e11] 00
                    nop                                     ;[3e12] 00
                    nop                                     ;[3e13] 00
                    nop                                     ;[3e14] 00
                    nop                                     ;[3e15] 00
                    nop                                     ;[3e16] 00
                    nop                                     ;[3e17] 00
                    nop                                     ;[3e18] 00
                    nop                                     ;[3e19] 00
                    nop                                     ;[3e1a] 00
                    nop                                     ;[3e1b] 00
                    nop                                     ;[3e1c] 00
                    nop                                     ;[3e1d] 00
                    nop                                     ;[3e1e] 00
                    nop                                     ;[3e1f] 00
                    nop                                     ;[3e20] 00
                    nop                                     ;[3e21] 00
                    nop                                     ;[3e22] 00
                    nop                                     ;[3e23] 00
                    nop                                     ;[3e24] 00
                    nop                                     ;[3e25] 00
                    nop                                     ;[3e26] 00
                    nop                                     ;[3e27] 00
                    nop                                     ;[3e28] 00
                    nop                                     ;[3e29] 00
                    nop                                     ;[3e2a] 00
                    nop                                     ;[3e2b] 00
                    nop                                     ;[3e2c] 00
                    nop                                     ;[3e2d] 00
                    nop                                     ;[3e2e] 00
                    nop                                     ;[3e2f] 00
                    nop                                     ;[3e30] 00
                    nop                                     ;[3e31] 00
                    nop                                     ;[3e32] 00
                    nop                                     ;[3e33] 00
                    nop                                     ;[3e34] 00
                    nop                                     ;[3e35] 00
                    nop                                     ;[3e36] 00
                    nop                                     ;[3e37] 00
                    nop                                     ;[3e38] 00
                    nop                                     ;[3e39] 00
                    nop                                     ;[3e3a] 00
                    nop                                     ;[3e3b] 00
                    nop                                     ;[3e3c] 00
                    nop                                     ;[3e3d] 00
                    nop                                     ;[3e3e] 00
                    nop                                     ;[3e3f] 00
                    nop                                     ;[3e40] 00
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
                    and       $ef                           ;[3e97] e6 ef
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
                    and       $ef                           ;[3eba] e6 ef
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

                    di                                      ;[3f63] f3
                    exx                                     ;[3f64] d9
                    ld        bc,$7ffd                      ;[3f65] 01 fd 7f
                    exx                                     ;[3f68] d9
                    exx                                     ;[3f69] d9
                    ld        a,$10                         ;[3f6a] 3e 10
                    out       (c),a                         ;[3f6c] ed 79
                    exx                                     ;[3f6e] d9
                    ld        a,(hl)                        ;[3f6f] 7e
                    ex        af,af'                        ;[3f70] 08
                    exx                                     ;[3f71] d9
                    ld        a,$17                         ;[3f72] 3e 17
                    out       (c),a                         ;[3f74] ed 79
                    exx                                     ;[3f76] d9
                    ex        af,af'                        ;[3f77] 08
                    ld        (de),a                        ;[3f78] 12
                    inc       hl                            ;[3f79] 23
                    inc       de                            ;[3f7a] 13
                    dec       bc                            ;[3f7b] 0b
                    ld        a,b                           ;[3f7c] 78
                    or        c                             ;[3f7d] b1
                    jr        nz,$3f69                      ;[3f7e] 20 e9
                    ld        a,($5b5c)                     ;[3f80] 3a 5c 5b
                    ld        bc,$7ffd                      ;[3f83] 01 fd 7f
                    out       (c),a                         ;[3f86] ed 79
                    ei                                      ;[3f88] fb
                    ret                                     ;[3f89] c9

                    di                                      ;[3f8a] f3
                    exx                                     ;[3f8b] d9
                    ld        bc,$7ffd                      ;[3f8c] 01 fd 7f
                    exx                                     ;[3f8f] d9
                    exx                                     ;[3f90] d9
                    ld        a,$17                         ;[3f91] 3e 17
                    out       (c),a                         ;[3f93] ed 79
                    exx                                     ;[3f95] d9
                    ld        a,(hl)                        ;[3f96] 7e
                    ex        af,af'                        ;[3f97] 08
                    exx                                     ;[3f98] d9
                    ld        a,$10                         ;[3f99] 3e 10
                    out       (c),a                         ;[3f9b] ed 79
                    exx                                     ;[3f9d] d9
                    ex        af,af'                        ;[3f9e] 08
                    ld        (de),a                        ;[3f9f] 12
                    inc       hl                            ;[3fa0] 23
                    inc       de                            ;[3fa1] 13
                    dec       bc                            ;[3fa2] 0b
                    ld        a,b                           ;[3fa3] 78
                    or        c                             ;[3fa4] b1
                    jr        nz,$3f90                      ;[3fa5] 20 e9
                    ld        a,($5b5c)                     ;[3fa7] 3a 5c 5b
                    ld        bc,$7ffd                      ;[3faa] 01 fd 7f
                    out       (c),a                         ;[3fad] ed 79
                    ei                                      ;[3faf] fb
                    ret                                     ;[3fb0] c9

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
                    ld        (de),a                        ;[3fff] 12
