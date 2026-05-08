                    di                                      ;[0000] f3
                    jp        $006a                         ;[0001] c3 6a 00
                    ld        b,h                           ;[0004] 44
                    ld        d,(hl)                        ;[0005] 56
                    add       hl,bc                         ;[0006] 09
                    ld        (bc),a                        ;[0007] 02
                    jp        $0512                         ;[0008] c3 12 05
                    pop       hl                            ;[000b] e1
                    push      af                            ;[000c] f5
                    jp        $3364                         ;[000d] c3 64 33
                    rst       $18                           ;[0010] df
                    djnz      $0013                         ;[0011] 10 00
                    ret                                     ;[0013] c9

                    ld        a,$3a                         ;[0014] 3e 3a
                    and       a                             ;[0016] a7
                    ret                                     ;[0017] c9

                    nop                                     ;[0018] 00
                    nop                                     ;[0019] 00
                    jp        $03c6                         ;[001a] c3 c6 03
                    ld        a,(hl)                        ;[001d] 7e
                    ret                                     ;[001e] c9

                    rst       $38                           ;[001f] ff
                    inc       sp                            ;[0020] 33
                    inc       sp                            ;[0021] 33
                    call      $00ce                         ;[0022] cd ce 00
                    jp        $0071                         ;[0025] c3 71 00
                    ex        (sp),hl                       ;[0028] e3
                    ld        c,(hl)                        ;[0029] 4e
                    inc       hl                            ;[002a] 23
                    ex        (sp),hl                       ;[002b] e3
                    jp        $04e9                         ;[002c] c3 e9 04
                    nop                                     ;[002f] 00
                    exx                                     ;[0030] d9
                    ex        (sp),hl                       ;[0031] e3
                    push      de                            ;[0032] d5
                    ld        d,a                           ;[0033] 57
                    push      hl                            ;[0034] e5
                    jp        $018d                         ;[0035] c3 8d 01
                    jp        $00e5                         ;[0038] c3 e5 00
                    call      $0045                         ;[003b] cd 45 00
                    ld        hl,$0050                      ;[003e] 21 50 00
                    ex        (sp),hl                       ;[0041] e3
                    jp        $1ff9                         ;[0042] c3 f9 1f
                    in        a,($e3)                       ;[0045] db e3
                    ld        h,a                           ;[0047] 67
                    and       $c0                           ;[0048] e6 c0
                    or        $01                           ;[004a] f6 01
                    out       ($e3),a                       ;[004c] d3 e3
                    ld        ($25b8),sp                    ;[004e] ed 73 b8 25
                    ld        sp,$26ed                      ;[0052] 31 ed 26
                    push      hl                            ;[0055] e5
                    call      $2009                         ;[0056] cd 09 20
                    pop       af                            ;[0059] f1
                    ld        sp,($25b8)                    ;[005a] ed 7b b8 25
                    out       ($e3),a                       ;[005e] d3 e3
                    ret                                     ;[0060] c9

                    jp        (ix)                          ;[0061] dd e9
                    nop                                     ;[0063] 00
                    nop                                     ;[0064] 00
                    nop                                     ;[0065] 00
                    push      af                            ;[0066] f5
                    pop       af                            ;[0067] f1
                    retn                                    ;[0068] ed 45

                    push    $0001                           ;[006a] ed 8a 00 01
                    jp        $1ea0                         ;[006e] c3 a0 1e
                    push      hl                            ;[0071] e5
                    xor       a                             ;[0072] af
                    ld        ($32ff),a                     ;[0073] 32 ff 32
                    jp        $1ff9                         ;[0076] c3 f9 1f
                    ld        bc,$243b                      ;[0079] 01 3b 24
                    out       (c),a                         ;[007c] ed 79
                    inc       b                             ;[007e] 04
                    in        a,(c)                         ;[007f] ed 78
                    ret                                     ;[0081] c9

                    push      af                            ;[0082] f5
                    ld        a,$07                         ;[0083] 3e 07
                    jr        $0089                         ;[0085] 18 02
                    push      af                            ;[0087] f5
                    ld        a,c                           ;[0088] 79
                    add       a                             ;[0089] 87
                    nextreg $56,a                           ;[008a] ed 92 56
                    inc       a                             ;[008d] 3c
                    nextreg $57,a                           ;[008e] ed 92 57
                    pop       af                            ;[0091] f1
                    ret                                     ;[0092] c9

                    ld        b,$ff                         ;[0093] 06 ff
                    ld        c,(hl)                        ;[0095] 4e
                    inc       hl                            ;[0096] 23
                    push      hl                            ;[0097] e5
                    in        a,($e3)                       ;[0098] db e3
                    ld        l,a                           ;[009a] 6f
                    ld        a,$02                         ;[009b] 3e 02
                    out       ($e3),a                       ;[009d] d3 e3
                    ld        a,c                           ;[009f] 79
                    inc       a                             ;[00a0] 3c
                    jr        z,$00a4                       ;[00a1] 28 01
                    ld        a,c                           ;[00a3] 79
                    ld        (de),a                        ;[00a4] 12
                    inc       de                            ;[00a5] 13
                    ld        a,l                           ;[00a6] 7d
                    out       ($e3),a                       ;[00a7] d3 e3
                    pop       hl                            ;[00a9] e1
                    ret       z                             ;[00aa] c8
                    djnz      $0095                         ;[00ab] 10 e8
                    ld        c,$ff                         ;[00ad] 0e ff
                    jr        $0097                         ;[00af] 18 e6
                    ld        a,$ff                         ;[00b1] 3e ff
                    ld        b,a                           ;[00b3] 47
                    push      de                            ;[00b4] d5
                    in        a,($e3)                       ;[00b5] db e3
                    ld        c,a                           ;[00b7] 4f
                    ld        a,$02                         ;[00b8] 3e 02
                    out       ($e3),a                       ;[00ba] d3 e3
                    ld        a,c                           ;[00bc] 79
                    ld        c,(hl)                        ;[00bd] 4e
                    inc       hl                            ;[00be] 23
                    out       ($e3),a                       ;[00bf] d3 e3
                    ld        a,c                           ;[00c1] 79
                    and       a                             ;[00c2] a7
                    jr        z,$00c9                       ;[00c3] 28 04
                    ld        (de),a                        ;[00c5] 12
                    inc       de                            ;[00c6] 13
                    djnz      $00b5                         ;[00c7] 10 ec
                    ld        a,$ff                         ;[00c9] 3e ff
                    ld        (de),a                        ;[00cb] 12
                    pop       hl                            ;[00cc] e1
                    ret                                     ;[00cd] c9

                    ld        a,$80                         ;[00ce] 3e 80
                    out       ($e3),a                       ;[00d0] d3 e3
                    jp        $3cfd                         ;[00d2] c3 fd 3c
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
                    push      af                            ;[00e5] f5
                    push      hl                            ;[00e6] e5
                    call      $0045                         ;[00e7] cd 45 00
                    add       a                             ;[00ea] 87
                    jp        c,$1ffc                       ;[00eb] da fc 1f
                    pop       hl                            ;[00ee] e1
                    pop       af                            ;[00ef] f1
                    ei                                      ;[00f0] fb
                    ret                                     ;[00f1] c9

                    ld        bc,$0000                      ;[00f2] 01 00 00
                    call      $041f                         ;[00f5] cd 1f 04
                    ld        bc,$204f                      ;[00f8] 01 4f 20
                    ret                                     ;[00fb] c9

                    ld        b,(hl)                        ;[00fc] 46
                    ld        b,c                           ;[00fd] 41
                    call      nc,$f700                      ;[00fe] d4 00 f7
                    xor       a                             ;[0101] af
                    add       hl,bc                         ;[0102] 09
                    rst       $30                           ;[0103] f7
                    di                                      ;[0104] f3
                    ld        c,$f7                         ;[0105] 0e f7
                    ret       m                             ;[0107] f8
                    ld        c,$f7                         ;[0108] 0e f7
                    ld        l,c                           ;[010a] 69
                    dec       c                             ;[010b] 0d
                    rst       $30                           ;[010c] f7
                    ld        a,(de)                        ;[010d] 1a
                    dec       c                             ;[010e] 0d
                    rst       $30                           ;[010f] f7
                    cp        (hl)                          ;[0110] be
                    dec       c                             ;[0111] 0d
                    jp        $0014                         ;[0112] c3 14 00
                    jp        $0014                         ;[0115] c3 14 00
                    rst       $30                           ;[0118] f7
                    ret                                     ;[0119] c9

                    add       hl,bc                         ;[011a] 09
                    rst       $30                           ;[011b] f7
                    xor       b                             ;[011c] a8
                    ld        c,$f7                         ;[011d] 0e f7
                    ld        (hl),c                        ;[011f] 71
                    ld        c,$f7                         ;[0120] 0e f7
                    daa                                     ;[0122] 27
                    ld        c,$f7                         ;[0123] 0e f7
                    inc       d                             ;[0125] 14
                    nop                                     ;[0126] 00
                    rst       $30                           ;[0127] f7
                    cp        c                             ;[0128] b9
                    ld        (de),a                        ;[0129] 12
                    jp        $0014                         ;[012a] c3 14 00
                    rst       $30                           ;[012d] f7
                    pop       af                            ;[012e] f1
                    dec       a                             ;[012f] 3d
                    rst       $30                           ;[0130] f7
                    rra                                     ;[0131] 1f
                    ld        a,$f7                         ;[0132] 3e f7
                    add       c                             ;[0134] 81
                    ld        a,$f7                         ;[0135] 3e f7
                    ld        l,h                           ;[0137] 6c
                    rla                                     ;[0138] 17
                    jp        $1ca1                         ;[0139] c3 a1 1c
                    jp        $1d30                         ;[013c] c3 30 1d
                    rst       $30                           ;[013f] f7
                    ld        h,a                           ;[0140] 67
                    inc       b                             ;[0141] 04
                    rst       $30                           ;[0142] f7
                    ld        a,d                           ;[0143] 7a
                    dec       d                             ;[0144] 15
                    rst       $30                           ;[0145] f7
                    ld        (hl),$16                      ;[0146] 36 16
                    rst       $30                           ;[0148] f7
                    jr        c,$0160                       ;[0149] 38 15
                    rst       $30                           ;[014b] f7
                    ld        e,(hl)                        ;[014c] 5e
                    dec       d                             ;[014d] 15
                    rst       $30                           ;[014e] f7
                    adc       d                             ;[014f] 8a
                    ld        d,$f7                         ;[0150] 16 f7
                    sub       e                             ;[0152] 93
                    ld        d,$f7                         ;[0153] 16 f7
                    and       d                             ;[0155] a2
                    ld        d,$f7                         ;[0156] 16 f7
                    ld        a,(de)                        ;[0158] 1a
                    rla                                     ;[0159] 17
                    rst       $30                           ;[015a] f7
                    call      nz,$f716                      ;[015b] c4 16 f7
                    rrca                                    ;[015e] 0f
                    rla                                     ;[015f] 17
                    rst       $30                           ;[0160] f7
                    call      z,$f716                       ;[0161] cc 16 f7
                    rrca                                    ;[0164] 0f
                    inc       d                             ;[0165] 14
                    jp        $0333                         ;[0166] c3 33 03
                    jp        $02d4                         ;[0169] c3 d4 02
                    jp        $02e2                         ;[016c] c3 e2 02
                    jp        $02ed                         ;[016f] c3 ed 02
                    jp        $0223                         ;[0172] c3 23 02
                    jp        $02bd                         ;[0175] c3 bd 02
                    jp        $02cd                         ;[0178] c3 cd 02
                    jp        $0310                         ;[017b] c3 10 03
                    jp        $032c                         ;[017e] c3 2c 03
                    jp        $1db8                         ;[0181] c3 b8 1d
                    jp        $0317                         ;[0184] c3 17 03
                    jp        $031e                         ;[0187] c3 1e 03
                    jp        $0325                         ;[018a] c3 25 03
                    in        a,($e3)                       ;[018d] db e3
                    ld        e,a                           ;[018f] 5f
                    ld        a,l                           ;[0190] 7d
                    ld        hl,$2320                      ;[0191] 21 20 23
                    cp        $3f                           ;[0194] fe 3f
                    jr        nc,$01a5                      ;[0196] 30 0d
                    ld        l,$00                         ;[0198] 2e 00
                    cp        $18                           ;[019a] fe 18
                    jr        c,$01a5                       ;[019c] 38 07
                    ld        l,$10                         ;[019e] 2e 10
                    ld        a,d                           ;[01a0] 7a
                    and       $0f                           ;[01a1] e6 0f
                    jr        $01b7                         ;[01a3] 18 12
                    ld        a,d                           ;[01a5] 7a
                    cp        $10                           ;[01a6] fe 10
                    jr        c,$01b7                       ;[01a8] 38 0d
                    pop       hl                            ;[01aa] e1
                    ld        a,l                           ;[01ab] 7d
                    cp        $3f                           ;[01ac] fe 3f
                    ld        a,$1d                         ;[01ae] 3e 1d
                    jr        nc,$01dc                      ;[01b0] 30 2a
                    ld        a,$16                         ;[01b2] 3e 16
                    and       a                             ;[01b4] a7
                    jr        $01dc                         ;[01b5] 18 25
                    add       hl,a                          ;[01b7] ed 31
                    ld        a,(hl)                        ;[01b9] 7e
                    pop       hl                            ;[01ba] e1
                    ld        ($2006),a                     ;[01bb] 32 06 20
                    bit       6,a                           ;[01be] cb 77
                    jr        nz,$01c8                      ;[01c0] 20 06
                    push      af                            ;[01c2] f5
                    ld        a,d                           ;[01c3] 7a
                    and       $0f                           ;[01c4] e6 0f
                    ld        d,a                           ;[01c6] 57
                    pop       af                            ;[01c7] f1
                    and       $8f                           ;[01c8] e6 8f
                    jp        m,$01ab                       ;[01ca] fa ab 01
                    jr        nz,$01e4                      ;[01cd] 20 15
                    ld        a,(hl)                        ;[01cf] 7e
                    inc       l                             ;[01d0] 2c
                    ld        h,(hl)                        ;[01d1] 66
                    ld        l,a                           ;[01d2] 6f
                    call      $01e0                         ;[01d3] cd e0 01
                    exx                                     ;[01d6] d9
                    ld        d,a                           ;[01d7] 57
                    ld        a,e                           ;[01d8] 7b
                    out       ($e3),a                       ;[01d9] d3 e3
                    ld        a,d                           ;[01db] 7a
                    pop       de                            ;[01dc] d1
                    pop       hl                            ;[01dd] e1
                    exx                                     ;[01de] d9
                    ret                                     ;[01df] c9

                    push      hl                            ;[01e0] e5
                    ld        a,d                           ;[01e1] 7a
                    exx                                     ;[01e2] d9
                    ret                                     ;[01e3] c9

                    ld        h,a                           ;[01e4] 67
                    ld        a,e                           ;[01e5] 7b
                    and       $80                           ;[01e6] e6 80
                    or        h                             ;[01e8] b4
                    out       ($e3),a                       ;[01e9] d3 e3
                    dec       l                             ;[01eb] 2d
                    ld        h,$20                         ;[01ec] 26 20
                    jr        $01d3                         ;[01ee] 18 e3
                    push      af                            ;[01f0] f5
                    in        a,($e3)                       ;[01f1] db e3
                    and       $0f                           ;[01f3] e6 0f
                    jr        z,$01fa                       ;[01f5] 28 03
                    ld        ixh,$20                       ;[01f7] dd 26 20
                    pop       af                            ;[01fa] f1
                    push      hl                            ;[01fb] e5
                    ld        l,(ix+$01)                    ;[01fc] dd 6e 01
                    ld        h,(ix+$02)                    ;[01ff] dd 66 02
                    ex        (sp),hl                       ;[0202] e3
                    ret                                     ;[0203] c9

                    push      hl                            ;[0204] e5
                    and       $0f                           ;[0205] e6 0f
                    ld        hl,$2310                      ;[0207] 21 10 23
                    add       hl,a                          ;[020a] ed 31
                    ld        a,(hl)                        ;[020c] 7e
                    pop       hl                            ;[020d] e1
                    ret                                     ;[020e] c9

                    call      $0218                         ;[020f] cd 18 02
                    ret       nc                            ;[0212] d0
                    ret       m                             ;[0213] f8
                    ld        a,$14                         ;[0214] 3e 14
                    and       a                             ;[0216] a7
                    ret                                     ;[0217] c9

                    ld        iyh,a                         ;[0218] fd 67
                    call      $0204                         ;[021a] cd 04 02
                    call      $040d                         ;[021d] cd 0d 04
                    cp        b                             ;[0220] b8
                    dec       de                            ;[0221] 1b
                    ret                                     ;[0222] c9

                    ld        iyh,a                         ;[0223] fd 67
                    push      hl                            ;[0225] e5
                    ld        a,b                           ;[0226] 78
                    cp        $10                           ;[0227] fe 10
                    jr        nc,$0235                      ;[0229] 30 0a
                    ld        hl,$2320                      ;[022b] 21 20 23
                    add       hl,a                          ;[022e] ed 31
                    ld        a,(hl)                        ;[0230] 7e
                    and       a                             ;[0231] a7
                    jp        m,$023a                       ;[0232] fa 3a 02
                    ld        a,$1d                         ;[0235] 3e 1d
                    and       a                             ;[0237] a7
                    pop       hl                            ;[0238] e1
                    ret                                     ;[0239] c9

                    ex        (sp),hl                       ;[023a] e3
                    push      bc                            ;[023b] c5
                    push      de                            ;[023c] d5
                    push      ix                            ;[023d] dd e5
                    push      iy                            ;[023f] fd e5
                    ld        a,iyh                         ;[0241] fd 7c
                    call      $020f                         ;[0243] cd 0f 02
                    pop       iy                            ;[0246] fd e1
                    pop       ix                            ;[0248] dd e1
                    pop       de                            ;[024a] d1
                    pop       bc                            ;[024b] c1
                    jr        nc,$0237                      ;[024c] 30 e9
                    jr        z,$025b                       ;[024e] 28 0b
                    ld        a,e                           ;[0250] 7b
                    cp        $03                           ;[0251] fe 03
                    ld        a,$14                         ;[0253] 3e 14
                    jr        nc,$0237                      ;[0255] 30 e0
                    inc       d                             ;[0257] 14
                    dec       d                             ;[0258] 15
                    jr        nz,$0237                      ;[0259] 20 dc
                    push      bc                            ;[025b] c5
                    push      de                            ;[025c] d5
                    push      hl                            ;[025d] e5
                    push      ix                            ;[025e] dd e5
                    push      iy                            ;[0260] fd e5
                    call      $0407                         ;[0262] cd 07 04
                    adc       e                             ;[0265] 8b
                    inc       e                             ;[0266] 1c
                    pop       iy                            ;[0267] fd e1
                    pop       ix                            ;[0269] dd e1
                    pop       hl                            ;[026b] e1
                    pop       de                            ;[026c] d1
                    pop       bc                            ;[026d] c1
                    jr        nc,$02a2                      ;[026e] 30 32
                    ld        a,$14                         ;[0270] 3e 14
                    jr        nz,$0237                      ;[0272] 20 c3
                    inc       e                             ;[0274] 1c
                    dec       e                             ;[0275] 1d
                    ld        a,$18                         ;[0276] 3e 18
                    jr        z,$0237                       ;[0278] 28 bd
                    ld        a,e                           ;[027a] 7b
                    cp        $03                           ;[027b] fe 03
                    jr        c,$029c                       ;[027d] 38 1d
                    cp        $05                           ;[027f] fe 05
                    ld        a,$15                         ;[0281] 3e 15
                    jr        nc,$0237                      ;[0283] 30 b2
                    push      bc                            ;[0285] c5
                    push      de                            ;[0286] d5
                    push      hl                            ;[0287] e5
                    push      ix                            ;[0288] dd e5
                    push      iy                            ;[028a] fd e5
                    ld        a,iyh                         ;[028c] fd 7c
                    call      $0130                         ;[028e] cd 30 01
                    pop       iy                            ;[0291] fd e1
                    pop       ix                            ;[0293] dd e1
                    pop       hl                            ;[0295] e1
                    pop       de                            ;[0296] d1
                    pop       bc                            ;[0297] c1
                    jr        nc,$0237                      ;[0298] 30 9d
                    jr        $02a2                         ;[029a] 18 06
                    ld        d,e                           ;[029c] 53
                    dec       d                             ;[029d] 15
                    set       1,d                           ;[029e] cb ca
                    jr        $02ad                         ;[02a0] 18 0b
                    ld        a,d                           ;[02a2] 7a
                    cp        $03                           ;[02a3] fe 03
                    jr        nc,$0281                      ;[02a5] 30 da
                    and       a                             ;[02a7] a7
                    ld        a,$17                         ;[02a8] 3e 17
                    jr        z,$0237                       ;[02aa] 28 8b
                    dec       d                             ;[02ac] 15
                    push      de                            ;[02ad] d5
                    ld        a,iyh                         ;[02ae] fd 7c
                    call      $0136                         ;[02b0] cd 36 01
                    pop       de                            ;[02b3] d1
                    pop       hl                            ;[02b4] e1
                    ret       nc                            ;[02b5] d0
                    ld        a,($2006)                     ;[02b6] 3a 06 20
                    ld        (hl),a                        ;[02b9] 77
                    bit       1,d                           ;[02ba] cb 4a
                    ret                                     ;[02bc] c9

                    push      af                            ;[02bd] f5
                    call      $015a                         ;[02be] cd 5a 01
                    pop       bc                            ;[02c1] c1
                    ret       nc                            ;[02c2] d0
                    ld        hl,$2320                      ;[02c3] 21 20 23
                    ld        a,b                           ;[02c6] 78
                    add       hl,a                          ;[02c7] ed 31
                    ld        (hl),$ff                      ;[02c9] 36 ff
                    scf                                     ;[02cb] 37
                    ret                                     ;[02cc] c9

                    push      af                            ;[02cd] f5
                    call      $015d                         ;[02ce] cd 5d 01
                    pop       bc                            ;[02d1] c1
                    jr        $02c2                         ;[02d2] 18 ee
                    push      hl                            ;[02d4] e5
                    ld        a,d                           ;[02d5] 7a
                    and       $7f                           ;[02d6] e6 7f
                    call      $010f                         ;[02d8] cd 0f 01
                    pop       hl                            ;[02db] e1
                    ret       nc                            ;[02dc] d0
                    ld        a,($2006)                     ;[02dd] 3a 06 20
                    ld        (hl),a                        ;[02e0] 77
                    ret                                     ;[02e1] c9

                    push      af                            ;[02e2] f5
                    call      $0121                         ;[02e3] cd 21 01
                    pop       bc                            ;[02e6] c1
                    ret       nc                            ;[02e7] d0
                    ld        hl,$2310                      ;[02e8] 21 10 23
                    jr        $02c6                         ;[02eb] 18 d9
                    cp        $ff                           ;[02ed] fe ff
                    jr        z,$0307                       ;[02ef] 28 16
                    push      af                            ;[02f1] f5
                    ld        b,$01                         ;[02f2] 06 01
                    call      $0127                         ;[02f4] cd 27 01
                    pop       bc                            ;[02f7] c1
                    jr        c,$02fe                       ;[02f8] 38 04
                    ld        a,$16                         ;[02fa] 3e 16
                    and       a                             ;[02fc] a7
                    ret                                     ;[02fd] c9

                    ld        a,b                           ;[02fe] 78
                    cpl                                     ;[02ff] 2f
                    ld        ($2008),a                     ;[0300] 32 08 20
                    ld        a,b                           ;[0303] 78
                    ld        ($2007),a                     ;[0304] 32 07 20
                    ld        a,($2007)                     ;[0307] 3a 07 20
                    scf                                     ;[030a] 37
                    ret                                     ;[030b] c9

                    inc       sp                            ;[030c] 33
                    inc       sp                            ;[030d] 33
                    jr        $02fa                         ;[030e] 18 ea
                    call      $041f                         ;[0310] cd 1f 04
                    inc       c                             ;[0313] 0c
                    rst       $20                           ;[0314] e7
                    ld        h,$c9                         ;[0315] 26 c9
                    call      $041f                         ;[0317] cd 1f 04
                    inc       c                             ;[031a] 0c
                    adc       l                             ;[031b] 8d
                    add       hl,hl                         ;[031c] 29
                    ret                                     ;[031d] c9

                    call      $041f                         ;[031e] cd 1f 04
                    inc       c                             ;[0321] 0c
                    sbc       $28                           ;[0322] de 28
                    ret                                     ;[0324] c9

                    call      $041f                         ;[0325] cd 1f 04
                    inc       c                             ;[0328] 0c
                    ld        a,a                           ;[0329] 7f
                    add       hl,hl                         ;[032a] 29
                    ret                                     ;[032b] c9

                    call      $041f                         ;[032c] cd 1f 04
                    inc       c                             ;[032f] 0c
                    ld        (hl),d                        ;[0330] 72
                    ld        h,$c9                         ;[0331] 26 c9
                    push      de                            ;[0333] d5
                    push      hl                            ;[0334] e5
                    ld        (hl),e                        ;[0335] 73
                    ld        a,d                           ;[0336] 7a
                    call      $0100                         ;[0337] cd 00 01
                    xor       a                             ;[033a] af
                    out       ($e3),a                       ;[033b] d3 e3
                    pop       hl                            ;[033d] e1
                    pop       de                            ;[033e] d1
                    ld        (hl),$ff                      ;[033f] 36 ff
                    inc       b                             ;[0341] 04
                    jr        $034b                         ;[0342] 18 07
                    ld        a,d                           ;[0344] 7a
                    cp        $10                           ;[0345] fe 10
                    ret       nc                            ;[0347] d0
                    ld        (hl),e                        ;[0348] 73
                    inc       hl                            ;[0349] 23
                    inc       d                             ;[034a] 14
                    djnz      $0344                         ;[034b] 10 f7
                    scf                                     ;[034d] 37
                    ret                                     ;[034e] c9

                    push      de                            ;[034f] d5
                    push      hl                            ;[0350] e5
                    ld        e,$00                         ;[0351] 1e 00
                    call      $03a0                         ;[0353] cd a0 03
                    jr        c,$0366                       ;[0356] 38 0e
                    call      $03a0                         ;[0358] cd a0 03
                    ld        a,e                           ;[035b] 7b
                    cp        $10                           ;[035c] fe 10
                    jr        nc,$0364                      ;[035e] 30 04
                    set       7,e                           ;[0360] cb fb
                    jr        $0366                         ;[0362] 18 02
                    pop       hl                            ;[0364] e1
                    push      hl                            ;[0365] e5
                    ld        a,(hl)                        ;[0366] 7e
                    and       $df                           ;[0367] e6 df
                    sub       $41                           ;[0369] d6 41
                    jr        c,$0375                       ;[036b] 38 08
                    cp        $10                           ;[036d] fe 10
                    jr        nc,$0375                      ;[036f] 30 04
                    ld        d,a                           ;[0371] 57
                    inc       hl                            ;[0372] 23
                    set       6,e                           ;[0373] cb f3
                    ld        a,(hl)                        ;[0375] 7e
                    inc       hl                            ;[0376] 23
                    cp        $3a                           ;[0377] fe 3a
                    jr        z,$037f                       ;[0379] 28 04
                    ld        e,$00                         ;[037b] 1e 00
                    jr        $0380                         ;[037d] 18 01
                    ex        (sp),hl                       ;[037f] e3
                    bit       6,e                           ;[0380] cb 73
                    jr        nz,$0388                      ;[0382] 20 04
                    ld        a,($2007)                     ;[0384] 3a 07 20
                    ld        d,a                           ;[0387] 57
                    ld        a,e                           ;[0388] 7b
                    and       $0f                           ;[0389] e6 0f
                    bit       7,e                           ;[038b] cb 7b
                    jr        nz,$039a                      ;[038d] 20 0b
                    push      de                            ;[038f] d5
                    ld        a,d                           ;[0390] 7a
                    ld        d,$ff                         ;[0391] 16 ff
                    call      $0124                         ;[0393] cd 24 01
                    pop       de                            ;[0396] d1
                    jr        c,$039a                       ;[0397] 38 01
                    xor       a                             ;[0399] af
                    swapnib                                 ;[039a] ed 23
                    or        d                             ;[039c] b2
                    pop       hl                            ;[039d] e1
                    pop       de                            ;[039e] d1
                    ret                                     ;[039f] c9

                    ld        a,(hl)                        ;[03a0] 7e
                    sub       $30                           ;[03a1] d6 30
                    ret       c                             ;[03a3] d8
                    ld        d,$0a                         ;[03a4] 16 0a
                    cp        d                             ;[03a6] ba
                    ccf                                     ;[03a7] 3f
                    ret       c                             ;[03a8] d8
                    inc       hl                            ;[03a9] 23
                    mul       d,e                           ;[03aa] ed 30
                    add       de,a                          ;[03ac] ed 32
                    and       a                             ;[03ae] a7
                    ret                                     ;[03af] c9

                    ld        b,$01                         ;[03b0] 06 01
                    ld        hl,$2320                      ;[03b2] 21 20 23
                    ld        a,b                           ;[03b5] 78
                    add       hl,a                          ;[03b6] ed 31
                    ld        a,(hl)                        ;[03b8] 7e
                    and       a                             ;[03b9] a7
                    scf                                     ;[03ba] 37
                    ret       m                             ;[03bb] f8
                    inc       hl                            ;[03bc] 23
                    inc       b                             ;[03bd] 04
                    bit       4,b                           ;[03be] cb 60
                    jr        z,$03b8                       ;[03c0] 28 f6
                    ld        a,$1d                         ;[03c2] 3e 1d
                    and       a                             ;[03c4] a7
                    ret                                     ;[03c5] c9

                    ex        (sp),hl                       ;[03c6] e3
                    push      af                            ;[03c7] f5
                    push      de                            ;[03c8] d5
                    ld        d,(hl)                        ;[03c9] 56
                    inc       hl                            ;[03ca] 23
                    ld        e,(hl)                        ;[03cb] 5e
                    inc       hl                            ;[03cc] 23
                    in        a,($e3)                       ;[03cd] db e3
                    push      af                            ;[03cf] f5
                    call      $00ce                         ;[03d0] cd ce 00
                    pop       af                            ;[03d3] f1
                    ld        ($2002),a                     ;[03d4] 32 02 20
                    ld        ($3c5d),de                    ;[03d7] ed 53 5d 3c
                    pop       de                            ;[03db] d1
                    pop       af                            ;[03dc] f1
                    ex        (sp),hl                       ;[03dd] e3
                    ld        ($2016),sp                    ;[03de] ed 73 16 20
                    call      $3c57                         ;[03e2] cd 57 3c
                    push      af                            ;[03e5] f5
                    ld        a,($2002)                     ;[03e6] 3a 02 20
                    cp        $82                           ;[03e9] fe 82
                    call      z,$1ff3                       ;[03eb] cc f3 1f
                    out       ($e3),a                       ;[03ee] d3 e3
                    pop       af                            ;[03f0] f1
                    ret                                     ;[03f1] c9

                    ld        ($2010),hl                    ;[03f2] 22 10 20
                    ld        hl,$01bd                      ;[03f5] 21 bd 01
                    push    $3cfc                           ;[03f8] ed 8a 3c fc
                    push      hl                            ;[03fc] e5
                    ld        hl,($2010)                    ;[03fd] 2a 10 20
                    nextreg $8e,$02                         ;[0400] ed 91 8e 02
                    jp        $1ff9                         ;[0404] c3 f9 1f
                    push      af                            ;[0407] f5
                    pop       iy                            ;[0408] fd e1
                    ld        a,($2006)                     ;[040a] 3a 06 20
                    ld        ($2006),a                     ;[040d] 32 06 20
                    and       $8f                           ;[0410] e6 8f
                    jp        m,$030c                       ;[0412] fa 0c 03
                    exx                                     ;[0415] d9
                    ex        (sp),hl                       ;[0416] e3
                    push      de                            ;[0417] d5
                    push      bc                            ;[0418] c5
                    ld        b,a                           ;[0419] 47
                    push      iy                            ;[041a] fd e5
                    pop       af                            ;[041c] f1
                    jr        $0425                         ;[041d] 18 06
                    exx                                     ;[041f] d9
                    ex        (sp),hl                       ;[0420] e3
                    push      de                            ;[0421] d5
                    push      bc                            ;[0422] c5
                    ld        b,(hl)                        ;[0423] 46
                    inc       hl                            ;[0424] 23
                    push      af                            ;[0425] f5
                    in        a,($e3)                       ;[0426] db e3
                    ld        c,a                           ;[0428] 4f
                    and       $c0                           ;[0429] e6 c0
                    or        b                             ;[042b] b0
                    ld        e,a                           ;[042c] 5f
                    pop       af                            ;[042d] f1
                    ld        b,a                           ;[042e] 47
                    ld        a,e                           ;[042f] 7b
                    ld        e,(hl)                        ;[0430] 5e
                    inc       hl                            ;[0431] 23
                    ld        d,(hl)                        ;[0432] 56
                    inc       hl                            ;[0433] 23
                    out       ($e3),a                       ;[0434] d3 e3
                    ld        a,b                           ;[0436] 78
                    call      $0445                         ;[0437] cd 45 04
                    exx                                     ;[043a] d9
                    ld        e,a                           ;[043b] 5f
                    ld        a,c                           ;[043c] 79
                    out       ($e3),a                       ;[043d] d3 e3
                    ld        a,e                           ;[043f] 7b
                    pop       bc                            ;[0440] c1
                    pop       de                            ;[0441] d1
                    ex        (sp),hl                       ;[0442] e3
                    exx                                     ;[0443] d9
                    ret                                     ;[0444] c9

                    push      de                            ;[0445] d5
                    exx                                     ;[0446] d9
                    ret                                     ;[0447] c9

                    out       ($e3),a                       ;[0448] d3 e3
                    call      $045d                         ;[044a] cd 5d 04
                    push      af                            ;[044d] f5
                    xor       a                             ;[044e] af
                    out       ($e3),a                       ;[044f] d3 e3
                    pop       af                            ;[0451] f1
                    nextreg $8e,$02                         ;[0452] ed 91 8e 02
                    push    $3f40                           ;[0456] ed 8a 3f 40
                    jp        $1ff9                         ;[045a] c3 f9 1f
                    push      hl                            ;[045d] e5
                    ld        hl,($5b56)                    ;[045e] 2a 56 5b
                    push      hl                            ;[0461] e5
                    pop       af                            ;[0462] f1
                    ld        hl,($5b52)                    ;[0463] 2a 52 5b
                    ret                                     ;[0466] c9

                    call      $1354                         ;[0467] cd 54 13
                    rst       $28                           ;[046a] ef
                    dec       hl                            ;[046b] 2b
                    push      bc                            ;[046c] c5
                    pop       ix                            ;[046d] dd e1
                    call      $19c2                         ;[046f] cd c2 19
                    bit       7,(iy+$0f)                    ;[0472] fd cb 0f 7e
                    scf                                     ;[0476] 37
                    ret                                     ;[0477] c9

                    nop                                     ;[0478] 00
                    nop                                     ;[0479] 00
                    nop                                     ;[047a] 00
                    nop                                     ;[047b] 00
                    nop                                     ;[047c] 00
                    nop                                     ;[047d] 00
                    nop                                     ;[047e] 00
                    nop                                     ;[047f] 00
                    nop                                     ;[0480] 00
                    nop                                     ;[0481] 00
                    nop                                     ;[0482] 00
                    nop                                     ;[0483] 00
                    nop                                     ;[0484] 00
                    nop                                     ;[0485] 00
                    nop                                     ;[0486] 00
                    nop                                     ;[0487] 00
                    nop                                     ;[0488] 00
                    nop                                     ;[0489] 00
                    nop                                     ;[048a] 00
                    nop                                     ;[048b] 00
                    nop                                     ;[048c] 00
                    nop                                     ;[048d] 00
                    nop                                     ;[048e] 00
                    nop                                     ;[048f] 00
                    nop                                     ;[0490] 00
                    nop                                     ;[0491] 00
                    nop                                     ;[0492] 00
                    nop                                     ;[0493] 00
                    nop                                     ;[0494] 00
                    nop                                     ;[0495] 00
                    nop                                     ;[0496] 00
                    nop                                     ;[0497] 00
                    nop                                     ;[0498] 00
                    nop                                     ;[0499] 00
                    nop                                     ;[049a] 00
                    nop                                     ;[049b] 00
                    nop                                     ;[049c] 00
                    nop                                     ;[049d] 00
                    nop                                     ;[049e] 00
                    nop                                     ;[049f] 00
                    nop                                     ;[04a0] 00
                    nop                                     ;[04a1] 00
                    nop                                     ;[04a2] 00
                    nop                                     ;[04a3] 00
                    nop                                     ;[04a4] 00
                    nop                                     ;[04a5] 00
                    nop                                     ;[04a6] 00
                    nop                                     ;[04a7] 00
                    nop                                     ;[04a8] 00
                    nop                                     ;[04a9] 00
                    nop                                     ;[04aa] 00
                    nop                                     ;[04ab] 00
                    nop                                     ;[04ac] 00
                    nop                                     ;[04ad] 00
                    nop                                     ;[04ae] 00
                    nop                                     ;[04af] 00
                    nop                                     ;[04b0] 00
                    nop                                     ;[04b1] 00
                    nop                                     ;[04b2] 00
                    nop                                     ;[04b3] 00
                    nop                                     ;[04b4] 00
                    nop                                     ;[04b5] 00
                    nop                                     ;[04b6] 00
                    nop                                     ;[04b7] 00
                    nop                                     ;[04b8] 00
                    nop                                     ;[04b9] 00
                    nop                                     ;[04ba] 00
                    nop                                     ;[04bb] 00
                    nop                                     ;[04bc] 00
                    nop                                     ;[04bd] 00
                    nop                                     ;[04be] 00
                    nop                                     ;[04bf] 00
                    nop                                     ;[04c0] 00
                    nop                                     ;[04c1] 00
                    nop                                     ;[04c2] 00
                    nop                                     ;[04c3] 00
                    nop                                     ;[04c4] 00
                    nop                                     ;[04c5] 00
                    nop                                     ;[04c6] 00
                    nop                                     ;[04c7] 00
                    nop                                     ;[04c8] 00
                    nop                                     ;[04c9] 00
                    nop                                     ;[04ca] 00
                    nop                                     ;[04cb] 00
                    nop                                     ;[04cc] 00
                    nop                                     ;[04cd] 00
                    nop                                     ;[04ce] 00
                    nop                                     ;[04cf] 00
                    nop                                     ;[04d0] 00
                    nop                                     ;[04d1] 00
                    nop                                     ;[04d2] 00
                    nop                                     ;[04d3] 00
                    nop                                     ;[04d4] 00
                    nop                                     ;[04d5] 00
                    nop                                     ;[04d6] 00
                    nop                                     ;[04d7] 00
                    jp        $3a3e                         ;[04d8] c3 3e 3a
                    xor       a                             ;[04db] af
                    ld        (hl),a                        ;[04dc] 77
                    ldir                                    ;[04dd] ed b0
                    scf                                     ;[04df] 37
                    ret                                     ;[04e0] c9

                    ex        (sp),hl                       ;[04e1] e3
                    ld        c,(hl)                        ;[04e2] 4e
                    inc       hl                            ;[04e3] 23
                    ex        (sp),hl                       ;[04e4] e3
                    push      ix                            ;[04e5] dd e5
                    jr        $04eb                         ;[04e7] 18 02
                    push      iy                            ;[04e9] fd e5
                    ex        (sp),hl                       ;[04eb] e3
                    ld        b,$00                         ;[04ec] 06 00
                    add       hl,bc                         ;[04ee] 09
                    ld        c,(hl)                        ;[04ef] 4e
                    inc       hl                            ;[04f0] 23
                    ld        b,(hl)                        ;[04f1] 46
                    inc       hl                            ;[04f2] 23
                    ld        e,(hl)                        ;[04f3] 5e
                    inc       hl                            ;[04f4] 23
                    ld        d,(hl)                        ;[04f5] 56
                    pop       hl                            ;[04f6] e1
                    ret                                     ;[04f7] c9

                    ex        (sp),hl                       ;[04f8] e3
                    ld        a,(hl)                        ;[04f9] 7e
                    inc       hl                            ;[04fa] 23
                    ex        (sp),hl                       ;[04fb] e3
                    push      ix                            ;[04fc] dd e5
                    jr        $0506                         ;[04fe] 18 06
                    ex        (sp),hl                       ;[0500] e3
                    ld        a,(hl)                        ;[0501] 7e
                    inc       hl                            ;[0502] 23
                    ex        (sp),hl                       ;[0503] e3
                    push      iy                            ;[0504] fd e5
                    ex        (sp),hl                       ;[0506] e3
                    add       hl,a                          ;[0507] ed 31
                    ld        (hl),c                        ;[0509] 71
                    inc       hl                            ;[050a] 23
                    ld        (hl),b                        ;[050b] 70
                    inc       hl                            ;[050c] 23
                    ld        (hl),e                        ;[050d] 73
                    inc       hl                            ;[050e] 23
                    ld        (hl),d                        ;[050f] 72
                    pop       hl                            ;[0510] e1
                    ret                                     ;[0511] c9

                    ex        (sp),hl                       ;[0512] e3
                    ex        (sp),ix                       ;[0513] dd e3
                    ex        af,af'                        ;[0515] 08
                    push      af                            ;[0516] f5
                    ld        a,i                           ;[0517] ed 57
                    ld        a,(hl)                        ;[0519] 7e
                    inc       hl                            ;[051a] 23
                    push      hl                            ;[051b] e5
                    ld        l,a                           ;[051c] 6f
                    di                                      ;[051d] f3
                    ld        a,$80                         ;[051e] 3e 80
                    out       ($e3),a                       ;[0520] d3 e3
                    ld        ($2014),sp                    ;[0522] ed 73 14 20
                    ld        a,($2015)                     ;[0526] 3a 15 20
                    rlca                                    ;[0529] 07
                    jr        c,$0534                       ;[052a] 38 08
                    rlca                                    ;[052c] 07
                    jr        c,$0534                       ;[052d] 38 05
                    ld        sp,$2100                      ;[052f] 31 00 21
                    jr        $0538                         ;[0532] 18 04
                    jp        po,$0538                      ;[0534] e2 38 05
                    ei                                      ;[0537] fb
                    push      af                            ;[0538] f5
                    nextreg $8e,$03                         ;[0539] ed 91 8e 03
                    call      $00ce                         ;[053d] cd ce 00
                    ld        a,l                           ;[0540] 7d
                    push      ix                            ;[0541] dd e5
                    pop       hl                            ;[0543] e1
                    call      $33b4                         ;[0544] cd b4 33
                    pop       af                            ;[0547] f1
                    scf                                     ;[0548] 37
                    call      $1ff3                         ;[0549] cd f3 1f
                    jr        nc,$0552                      ;[054c] 30 04
                    ld        sp,($2014)                    ;[054e] ed 7b 14 20
                    ld        a,$82                         ;[0552] 3e 82
                    out       ($e3),a                       ;[0554] d3 e3
                    jp        po,$055a                      ;[0556] e2 5a 05
                    ei                                      ;[0559] fb
                    pop       ix                            ;[055a] dd e1
                    pop       af                            ;[055c] f1
                    ex        af,af'                        ;[055d] 08
                    ex        (sp),ix                       ;[055e] dd e3
                    ret                                     ;[0560] c9

                    push      af                            ;[0561] f5
                    call      $00ce                         ;[0562] cd ce 00
                    ld        a,$0a                         ;[0565] 3e 0a
                    out       ($e3),a                       ;[0567] d3 e3
                    pop       af                            ;[0569] f1
                    ret                                     ;[056a] c9

                    jp        $387f                         ;[056b] c3 7f 38
                    call      $06c3                         ;[056e] cd c3 06
                    ret       nc                            ;[0571] d0
                    call      $07bc                         ;[0572] cd bc 07
                    scf                                     ;[0575] 37
                    ret                                     ;[0576] c9

                    push      iy                            ;[0577] fd e5
                    pop       hl                            ;[0579] e1
                    add       hl,bc                         ;[057a] 09
                    inc       (hl)                          ;[057b] 34
                    ld        a,(hl)                        ;[057c] 7e
                    inc       hl                            ;[057d] 23
                    cp        (ix+$21)                      ;[057e] dd be 21
                    ret       c                             ;[0581] d8
                    ld        c,(hl)                        ;[0582] 4e
                    inc       hl                            ;[0583] 23
                    ld        b,(hl)                        ;[0584] 46
                    inc       hl                            ;[0585] 23
                    ld        e,(hl)                        ;[0586] 5e
                    inc       hl                            ;[0587] 23
                    ld        d,(hl)                        ;[0588] 56
                    push      hl                            ;[0589] e5
                    call      $0912                         ;[058a] cd 12 09
                    pop       hl                            ;[058d] e1
                    ret       nc                            ;[058e] d0
                    call      $05a1                         ;[058f] cd a1 05
                    ld        a,$19                         ;[0592] 3e 19
                    ret       nc                            ;[0594] d0
                    ld        (hl),d                        ;[0595] 72
                    dec       hl                            ;[0596] 2b
                    ld        (hl),e                        ;[0597] 73
                    dec       hl                            ;[0598] 2b
                    ld        (hl),b                        ;[0599] 70
                    dec       hl                            ;[059a] 2b
                    ld        (hl),c                        ;[059b] 71
                    dec       hl                            ;[059c] 2b
                    ld        (hl),$00                      ;[059d] 36 00
                    scf                                     ;[059f] 37
                    ret                                     ;[05a0] c9

                    ld        a,c                           ;[05a1] 79
                    cp        $02                           ;[05a2] fe 02
                    jr        nc,$05aa                      ;[05a4] 30 04
                    ld        a,b                           ;[05a6] 78
                    or        e                             ;[05a7] b3
                    or        d                             ;[05a8] b2
                    ret       z                             ;[05a9] c8
                    push      hl                            ;[05aa] e5
                    ld        l,(ix+$22)                    ;[05ab] dd 6e 22
                    ld        h,(ix+$23)                    ;[05ae] dd 66 23
                    and       a                             ;[05b1] a7
                    sbc       hl,bc                         ;[05b2] ed 42
                    ld        l,(ix+$24)                    ;[05b4] dd 6e 24
                    ld        h,(ix+$25)                    ;[05b7] dd 66 25
                    sbc       hl,de                         ;[05ba] ed 52
                    ccf                                     ;[05bc] 3f
                    pop       hl                            ;[05bd] e1
                    ret                                     ;[05be] c9

                    call      $04e1                         ;[05bf] cd e1 04
                    cpl                                     ;[05c2] 2f
                    inc       c                             ;[05c3] 0c
                    jr        nz,$05cd                      ;[05c4] 20 07
                    inc       b                             ;[05c6] 04
                    jr        nz,$05cd                      ;[05c7] 20 04
                    inc       e                             ;[05c9] 1c
                    jr        nz,$05cd                      ;[05ca] 20 01
                    inc       d                             ;[05cc] 14
                    call      $05a1                         ;[05cd] cd a1 05
                    jr        c,$05d8                       ;[05d0] 38 06
                    ld        de,$0000                      ;[05d2] 11 00 00
                    ld        bc,$0002                      ;[05d5] 01 02 00
                    ld        a,c                           ;[05d8] 79
                    cp        (ix+$2f)                      ;[05d9] dd be 2f
                    jr        nz,$05f1                      ;[05dc] 20 13
                    ld        a,b                           ;[05de] 78
                    cp        (ix+$30)                      ;[05df] dd be 30
                    jr        nz,$05f1                      ;[05e2] 20 0d
                    ld        a,e                           ;[05e4] 7b
                    cp        (ix+$31)                      ;[05e5] dd be 31
                    jr        nz,$05f1                      ;[05e8] 20 07
                    ld        a,d                           ;[05ea] 7a
                    cp        (ix+$32)                      ;[05eb] dd be 32
                    ld        a,$1a                         ;[05ee] 3e 1a
                    ret       z                             ;[05f0] c8
                    push      bc                            ;[05f1] c5
                    push      de                            ;[05f2] d5
                    call      $0912                         ;[05f3] cd 12 09
                    jr        c,$05fb                       ;[05f6] 38 03
                    pop       hl                            ;[05f8] e1
                    pop       hl                            ;[05f9] e1
                    ret                                     ;[05fa] c9

                    ld        a,d                           ;[05fb] 7a
                    and       $0f                           ;[05fc] e6 0f
                    or        e                             ;[05fe] b3
                    or        b                             ;[05ff] b0
                    or        c                             ;[0600] b1
                    pop       de                            ;[0601] d1
                    pop       bc                            ;[0602] c1
                    jr        nz,$05c3                      ;[0603] 20 be
                    call      $04f8                         ;[0605] cd f8 04
                    cpl                                     ;[0608] 2f
                    ld        l,(ix+$2b)                    ;[0609] dd 6e 2b
                    ld        h,(ix+$2c)                    ;[060c] dd 66 2c
                    dec       hl                            ;[060f] 2b
                    ld        (ix+$2b),l                    ;[0610] dd 75 2b
                    ld        (ix+$2c),h                    ;[0613] dd 74 2c
                    ld        a,h                           ;[0616] 7c
                    and       l                             ;[0617] a5
                    inc       a                             ;[0618] 3c
                    jr        nz,$0628                      ;[0619] 20 0d
                    ld        l,(ix+$2d)                    ;[061b] dd 6e 2d
                    ld        h,(ix+$2e)                    ;[061e] dd 66 2e
                    dec       hl                            ;[0621] 2b
                    ld        (ix+$2d),l                    ;[0622] dd 75 2d
                    ld        (ix+$2e),h                    ;[0625] dd 74 2e
                    push      bc                            ;[0628] c5
                    push      de                            ;[0629] d5
                    push      iy                            ;[062a] fd e5
                    ld        iy,$0fff                      ;[062c] fd 21 ff 0f
                    ld        hl,$ffff                      ;[0630] 21 ff ff
                    call      $095a                         ;[0633] cd 5a 09
                    pop       iy                            ;[0636] fd e1
                    pop       de                            ;[0638] d1
                    pop       bc                            ;[0639] c1
                    call      c,$0ec5                       ;[063a] dc c5 0e
                    ret                                     ;[063d] c9

                    push      iy                            ;[063e] fd e5
                    push      bc                            ;[0640] c5
                    push      de                            ;[0641] d5
                    call      $05bf                         ;[0642] cd bf 05
                    ld        l,c                           ;[0645] 69
                    ld        h,b                           ;[0646] 60
                    push      de                            ;[0647] d5
                    pop       iy                            ;[0648] fd e1
                    pop       de                            ;[064a] d1
                    pop       bc                            ;[064b] c1
                    push      iy                            ;[064c] fd e5
                    push      hl                            ;[064e] e5
                    call      c,$095a                       ;[064f] dc 5a 09
                    pop       bc                            ;[0652] c1
                    pop       de                            ;[0653] d1
                    pop       iy                            ;[0654] fd e1
                    ret                                     ;[0656] c9

                    push      iy                            ;[0657] fd e5
                    call      $05a1                         ;[0659] cd a1 05
                    jr        nc,$0680                      ;[065c] 30 22
                    push      bc                            ;[065e] c5
                    push      de                            ;[065f] d5
                    call      $0912                         ;[0660] cd 12 09
                    jr        nc,$067b                      ;[0663] 30 16
                    pop       hl                            ;[0665] e1
                    pop       iy                            ;[0666] fd e1
                    push      bc                            ;[0668] c5
                    push      de                            ;[0669] d5
                    ex        de,hl                         ;[066a] eb
                    push      iy                            ;[066b] fd e5
                    pop       bc                            ;[066d] c1
                    call      $1fdf                         ;[066e] cd df 1f
                    ld        hl,$0000                      ;[0671] 21 00 00
                    ld        iy,$0000                      ;[0674] fd 21 00 00
                    call      $095a                         ;[0678] cd 5a 09
                    pop       de                            ;[067b] d1
                    pop       bc                            ;[067c] c1
                    jr        c,$0659                       ;[067d] 38 da
                    ret                                     ;[067f] c9

                    pop       iy                            ;[0680] fd e1
                    call      $0ec5                         ;[0682] cd c5 0e
                    jp        $08d1                         ;[0685] c3 d1 08
                    push      bc                            ;[0688] c5
                    push      de                            ;[0689] d5
                    xor       a                             ;[068a] af
                    call      $056e                         ;[068b] cd 6e 05
                    pop       de                            ;[068e] d1
                    pop       bc                            ;[068f] c1
                    ret       nc                            ;[0690] d0
                    push      hl                            ;[0691] e5
                    push      bc                            ;[0692] c5
                    push      de                            ;[0693] d5
                    ld        d,h                           ;[0694] 54
                    ld        e,l                           ;[0695] 5d
                    inc       de                            ;[0696] 13
                    ld        bc,$01ff                      ;[0697] 01 ff 01
                    ld        (hl),$00                      ;[069a] 36 00
                    ldir                                    ;[069c] ed b0
                    pop       de                            ;[069e] d1
                    pop       bc                            ;[069f] c1
                    xor       a                             ;[06a0] af
                    call      $06c3                         ;[06a1] cd c3 06
                    pop       hl                            ;[06a4] e1
                    ret       nc                            ;[06a5] d0
                    call      $1fef                         ;[06a6] cd ef 1f
                    push      af                            ;[06a9] f5
                    push      bc                            ;[06aa] c5
                    push      de                            ;[06ab] d5
                    push      hl                            ;[06ac] e5
                    call      $1ece                         ;[06ad] cd ce 1e
                    pop       hl                            ;[06b0] e1
                    pop       de                            ;[06b1] d1
                    pop       bc                            ;[06b2] c1
                    jr        nc,$06c1                      ;[06b3] 30 0c
                    inc       de                            ;[06b5] 13
                    ld        a,d                           ;[06b6] 7a
                    or        e                             ;[06b7] b3
                    jr        nz,$06bb                      ;[06b8] 20 01
                    inc       bc                            ;[06ba] 03
                    pop       af                            ;[06bb] f1
                    dec       a                             ;[06bc] 3d
                    jr        nz,$06a9                      ;[06bd] 20 ea
                    scf                                     ;[06bf] 37
                    ret                                     ;[06c0] c9

                    pop       de                            ;[06c1] d1
                    ret                                     ;[06c2] c9

                    cp        (ix+$21)                      ;[06c3] dd be 21
                    jr        nc,$06eb                      ;[06c6] 30 23
                    push      iy                            ;[06c8] fd e5
                    ld        iyl,a                         ;[06ca] fd 6f
                    ld        iyh,$00                       ;[06cc] fd 26 00
                    ld        hl,$0000                      ;[06cf] 21 00 00
                    call      $1fef                         ;[06d2] cd ef 1f
                    add       iy,bc                         ;[06d5] fd 09
                    adc       hl,de                         ;[06d7] ed 5a
                    dec       a                             ;[06d9] 3d
                    jr        nz,$06d5                      ;[06da] 20 f9
                    call      $04e1                         ;[06dc] cd e1 04
                    ld        h,$fd                         ;[06df] 26 fd
                    add       hl,bc                         ;[06e1] 09
                    adc       hl,de                         ;[06e2] ed 5a
                    ex        (sp),iy                       ;[06e4] fd e3
                    pop       de                            ;[06e6] d1
                    ld        c,l                           ;[06e7] 4d
                    ld        b,h                           ;[06e8] 44
                    scf                                     ;[06e9] 37
                    ret                                     ;[06ea] c9

                    ld        a,$02                         ;[06eb] 3e 02
                    ret                                     ;[06ed] c9

                    call      $06c3                         ;[06ee] cd c3 06
                    ret       nc                            ;[06f1] d0
                    jp        $0832                         ;[06f2] c3 32 08
                    ld        c,(hl)                        ;[06f5] 4e
                    inc       hl                            ;[06f6] 23
                    ld        b,(hl)                        ;[06f7] 46
                    inc       hl                            ;[06f8] 23
                    call      $0701                         ;[06f9] cd 01 07
                    dec       de                            ;[06fc] 1b
                    inc       bc                            ;[06fd] 03
                    ld        hl,$00f3                      ;[06fe] 21 f3 00
                    ld        a,$02                         ;[0701] 3e 02
                    push      af                            ;[0703] f5
                    ld        a,d                           ;[0704] 7a
                    and       $c0                           ;[0705] e6 c0
                    jr        nz,$0763                      ;[0707] 20 5a
                    pop       af                            ;[0709] f1
                    push      hl                            ;[070a] e5
                    ex        de,hl                         ;[070b] eb
                    exx                                     ;[070c] d9
                    ex        (sp),hl                       ;[070d] e3
                    push      de                            ;[070e] d5
                    push      bc                            ;[070f] c5
                    ld        d,a                           ;[0710] 57
                    ld        c,$e3                         ;[0711] 0e e3
                    in        b,(c)                         ;[0713] ed 40
                    exx                                     ;[0715] d9
                    ld        a,c                           ;[0716] 79
                    and       $03                           ;[0717] e6 03
                    jr        z,$0734                       ;[0719] 28 19
                    push      bc                            ;[071b] c5
                    ld        c,a                           ;[071c] 4f
                    exx                                     ;[071d] d9
                    ld        a,(hl)                        ;[071e] 7e
                    inc       hl                            ;[071f] 23
                    out       (c),d                         ;[0720] ed 51
                    exx                                     ;[0722] d9
                    ld        (hl),a                        ;[0723] 77
                    inc       hl                            ;[0724] 23
                    dec       c                             ;[0725] 0d
                    exx                                     ;[0726] d9
                    out       (c),b                         ;[0727] ed 41
                    jp        nz,$071e                      ;[0729] c2 1e 07
                    exx                                     ;[072c] d9
                    pop       bc                            ;[072d] c1
                    ld        a,c                           ;[072e] 79
                    and       $fc                           ;[072f] e6 fc
                    ld        c,a                           ;[0731] 4f
                    jr        $0755                         ;[0732] 18 21
                    exx                                     ;[0734] d9
                    ld        a,(hl)                        ;[0735] 7e
                    inc       hl                            ;[0736] 23
                    exx                                     ;[0737] d9
                    ld        e,a                           ;[0738] 5f
                    exx                                     ;[0739] d9
                    ld        a,(hl)                        ;[073a] 7e
                    inc       hl                            ;[073b] 23
                    exx                                     ;[073c] d9
                    ld        d,a                           ;[073d] 57
                    exx                                     ;[073e] d9
                    ld        a,(hl)                        ;[073f] 7e
                    inc       hl                            ;[0740] 23
                    ld        e,(hl)                        ;[0741] 5e
                    inc       hl                            ;[0742] 23
                    out       (c),d                         ;[0743] ed 51
                    exx                                     ;[0745] d9
                    ld        (hl),e                        ;[0746] 73
                    inc       hl                            ;[0747] 23
                    ld        (hl),d                        ;[0748] 72
                    inc       hl                            ;[0749] 23
                    ld        (hl),a                        ;[074a] 77
                    inc       hl                            ;[074b] 23
                    exx                                     ;[074c] d9
                    ld        a,e                           ;[074d] 7b
                    exx                                     ;[074e] d9
                    ld        (hl),a                        ;[074f] 77
                    inc       hl                            ;[0750] 23
                    add       bc,$fffc                      ;[0751] ed 36 fc ff
                    ld        a,b                           ;[0755] 78
                    or        c                             ;[0756] b1
                    exx                                     ;[0757] d9
                    out       (c),b                         ;[0758] ed 41
                    jr        nz,$0735                      ;[075a] 20 d9
                    pop       bc                            ;[075c] c1
                    pop       de                            ;[075d] d1
                    ex        (sp),hl                       ;[075e] e3
                    exx                                     ;[075f] d9
                    ex        de,hl                         ;[0760] eb
                    pop       hl                            ;[0761] e1
                    ret                                     ;[0762] c9

                    pop       af                            ;[0763] f1
                    ldir                                    ;[0764] ed b0
                    ret                                     ;[0766] c9

                    ld        a,$02                         ;[0767] 3e 02
                    push      af                            ;[0769] f5
                    ld        a,h                           ;[076a] 7c
                    and       $c0                           ;[076b] e6 c0
                    jr        nz,$0763                      ;[076d] 20 f4
                    pop       af                            ;[076f] f1
                    push      hl                            ;[0770] e5
                    exx                                     ;[0771] d9
                    ex        (sp),hl                       ;[0772] e3
                    push      de                            ;[0773] d5
                    push      bc                            ;[0774] c5
                    ld        d,a                           ;[0775] 57
                    ld        c,$e3                         ;[0776] 0e e3
                    in        b,(c)                         ;[0778] ed 40
                    out       (c),d                         ;[077a] ed 51
                    ld        a,(hl)                        ;[077c] 7e
                    inc       hl                            ;[077d] 23
                    out       (c),b                         ;[077e] ed 41
                    exx                                     ;[0780] d9
                    ld        (de),a                        ;[0781] 12
                    inc       de                            ;[0782] 13
                    dec       bc                            ;[0783] 0b
                    ld        a,b                           ;[0784] 78
                    or        c                             ;[0785] b1
                    exx                                     ;[0786] d9
                    jr        nz,$077a                      ;[0787] 20 f1
                    pop       bc                            ;[0789] c1
                    pop       de                            ;[078a] d1
                    ex        (sp),hl                       ;[078b] e3
                    exx                                     ;[078c] d9
                    pop       hl                            ;[078d] e1
                    ret                                     ;[078e] c9

                    ld        hl,$2330                      ;[078f] 21 30 23
                    ld        bc,$0200                      ;[0792] 01 00 02
                    push      bc                            ;[0795] c5
                    ld        (hl),c                        ;[0796] 71
                    inc       hl                            ;[0797] 23
                    inc       c                             ;[0798] 0c
                    djnz      $0796                         ;[0799] 10 fb
                    pop       bc                            ;[079b] c1
                    ld        d,c                           ;[079c] 51
                    ld        e,$07                         ;[079d] 1e 07
                    ld        (hl),d                        ;[079f] 72
                    add       hl,de                         ;[07a0] 19
                    djnz      $079f                         ;[07a1] 10 fc
                    ret                                     ;[07a3] c9

                    ld        hl,$2330                      ;[07a4] 21 30 23
                    ld        b,a                           ;[07a7] 47
                    ld        c,a                           ;[07a8] 4f
                    ld        a,(hl)                        ;[07a9] 7e
                    ld        (hl),b                        ;[07aa] 70
                    inc       hl                            ;[07ab] 23
                    ld        b,a                           ;[07ac] 47
                    cp        c                             ;[07ad] b9
                    jr        nz,$07a9                      ;[07ae] 20 f9
                    inc       a                             ;[07b0] 3c
                    ld        hl,$2140                      ;[07b1] 21 40 21
                    ld        de,$0200                      ;[07b4] 11 00 02
                    add       hl,de                         ;[07b7] 19
                    dec       a                             ;[07b8] 3d
                    jr        nz,$07b7                      ;[07b9] 20 fc
                    ret                                     ;[07bb] c9

                    push      iy                            ;[07bc] fd e5
                    call      $07c4                         ;[07be] cd c4 07
                    pop       iy                            ;[07c1] fd e1
                    ret                                     ;[07c3] c9

                    ld        iy,$2332                      ;[07c4] fd 21 32 23
                    ld        l,$02                         ;[07c8] 2e 02
                    bit       0,(iy+$00)                    ;[07ca] fd cb 00 46
                    jr        z,$07fe                       ;[07ce] 28 2e
                    ld        a,(iy+$03)                    ;[07d0] fd 7e 03
                    cp        e                             ;[07d3] bb
                    jr        nz,$07fe                      ;[07d4] 20 28
                    ld        a,(iy+$04)                    ;[07d6] fd 7e 04
                    cp        d                             ;[07d9] ba
                    jr        nz,$07fe                      ;[07da] 20 22
                    ld        a,(iy+$05)                    ;[07dc] fd 7e 05
                    cp        c                             ;[07df] b9
                    jr        nz,$07fe                      ;[07e0] 20 1c
                    ld        a,(iy+$06)                    ;[07e2] fd 7e 06
                    cp        b                             ;[07e5] b8
                    jr        nz,$07fe                      ;[07e6] 20 16
                    ld        a,ixl                         ;[07e8] dd 7d
                    cp        (iy+$01)                      ;[07ea] fd be 01
                    jr        nz,$07fe                      ;[07ed] 20 0f
                    ld        a,ixh                         ;[07ef] dd 7c
                    cp        (iy+$02)                      ;[07f1] fd be 02
                    jr        nz,$07fe                      ;[07f4] 20 08
                    ld        a,$02                         ;[07f6] 3e 02
                    sub       l                             ;[07f8] 95
                    call      $07a4                         ;[07f9] cd a4 07
                    scf                                     ;[07fc] 37
                    ret                                     ;[07fd] c9

                    push      bc                            ;[07fe] c5
                    ld        bc,$0007                      ;[07ff] 01 07 00
                    add       iy,bc                         ;[0802] fd 09
                    pop       bc                            ;[0804] c1
                    dec       l                             ;[0805] 2d
                    jr        nz,$07ca                      ;[0806] 20 c2
                    ld        a,($2331)                     ;[0808] 3a 31 23
                    push      de                            ;[080b] d5
                    call      $08b9                         ;[080c] cd b9 08
                    pop       hl                            ;[080f] e1
                    ld        (iy+$00),$01                  ;[0810] fd 36 00 01
                    ld        a,ixl                         ;[0814] dd 7d
                    ld        (iy+$01),a                    ;[0816] fd 77 01
                    ld        a,ixh                         ;[0819] dd 7c
                    ld        (iy+$02),a                    ;[081b] fd 77 02
                    ld        (iy+$03),l                    ;[081e] fd 75 03
                    ld        (iy+$04),h                    ;[0821] fd 74 04
                    ld        (iy+$05),c                    ;[0824] fd 71 05
                    ld        (iy+$06),b                    ;[0827] fd 70 06
                    ld        a,($2331)                     ;[082a] 3a 31 23
                    call      $07a4                         ;[082d] cd a4 07
                    and       a                             ;[0830] a7
                    ret                                     ;[0831] c9

                    push      iy                            ;[0832] fd e5
                    call      $07c4                         ;[0834] cd c4 07
                    jr        c,$0850                       ;[0837] 38 17
                    push      hl                            ;[0839] e5
                    ld        e,(iy+$03)                    ;[083a] fd 5e 03
                    ld        d,(iy+$04)                    ;[083d] fd 56 04
                    ld        c,(iy+$05)                    ;[0840] fd 4e 05
                    ld        b,(iy+$06)                    ;[0843] fd 46 06
                    call      $1eaf                         ;[0846] cd af 1e
                    pop       hl                            ;[0849] e1
                    jr        c,$0850                       ;[084a] 38 04
                    res       0,(iy+$00)                    ;[084c] fd cb 00 86
                    pop       iy                            ;[0850] fd e1
                    ret                                     ;[0852] c9

                    ld        a,($2330)                     ;[0853] 3a 30 23
                    push      ix                            ;[0856] dd e5
                    push      iy                            ;[0858] fd e5
                    push      af                            ;[085a] f5
                    call      $0904                         ;[085b] cd 04 09
                    ld        a,(iy+$01)                    ;[085e] fd 7e 01
                    ld        ixl,a                         ;[0861] dd 6f
                    ld        a,(iy+$02)                    ;[0863] fd 7e 02
                    ld        ixh,a                         ;[0866] dd 67
                    pop       af                            ;[0868] f1
                    call      $07b0                         ;[0869] cd b0 07
                    bit       2,(iy+$00)                    ;[086c] fd cb 00 56
                    ld        (iy+$00),$01                  ;[0870] fd 36 00 01
                    ld        e,(iy+$03)                    ;[0874] fd 5e 03
                    ld        d,(iy+$04)                    ;[0877] fd 56 04
                    ld        c,(iy+$05)                    ;[087a] fd 4e 05
                    ld        b,(iy+$06)                    ;[087d] fd 46 06
                    jr        z,$08b1                       ;[0880] 28 2f
                    ld        a,(ix+$18)                    ;[0882] dd 7e 18
                    push      af                            ;[0885] f5
                    push      hl                            ;[0886] e5
                    push      bc                            ;[0887] c5
                    push      de                            ;[0888] d5
                    call      $1ece                         ;[0889] cd ce 1e
                    pop       de                            ;[088c] d1
                    pop       bc                            ;[088d] c1
                    jr        nc,$08ad                      ;[088e] 30 1d
                    ld        l,(ix+$14)                    ;[0890] dd 6e 14
                    ld        h,(ix+$15)                    ;[0893] dd 66 15
                    add       hl,de                         ;[0896] 19
                    ex        de,hl                         ;[0897] eb
                    ld        l,(ix+$16)                    ;[0898] dd 6e 16
                    ld        h,(ix+$17)                    ;[089b] dd 66 17
                    adc       hl,bc                         ;[089e] ed 4a
                    ld        b,h                           ;[08a0] 44
                    ld        c,l                           ;[08a1] 4d
                    pop       hl                            ;[08a2] e1
                    pop       af                            ;[08a3] f1
                    dec       a                             ;[08a4] 3d
                    jr        nz,$0885                      ;[08a5] 20 de
                    scf                                     ;[08a7] 37
                    pop       iy                            ;[08a8] fd e1
                    pop       ix                            ;[08aa] dd e1
                    ret                                     ;[08ac] c9

                    pop       hl                            ;[08ad] e1
                    pop       de                            ;[08ae] d1
                    jr        $08a8                         ;[08af] 18 f7
                    call      $1ece                         ;[08b1] cd ce 1e
                    jr        $08a8                         ;[08b4] 18 f2
                    ld        a,($2330)                     ;[08b6] 3a 30 23
                    push      af                            ;[08b9] f5
                    call      $0904                         ;[08ba] cd 04 09
                    pop       af                            ;[08bd] f1
                    scf                                     ;[08be] 37
                    bit       0,(iy+$00)                    ;[08bf] fd cb 00 46
                    ret       z                             ;[08c3] c8
                    bit       1,(iy+$00)                    ;[08c4] fd cb 00 4e
                    push      bc                            ;[08c8] c5
                    call      nz,$0856                      ;[08c9] c4 56 08
                    pop       bc                            ;[08cc] c1
                    ret                                     ;[08cd] c9

                    call      $08e2                         ;[08ce] cd e2 08
                    push      iy                            ;[08d1] fd e5
                    ld        b,$02                         ;[08d3] 06 02
                    ld        a,$02                         ;[08d5] 3e 02
                    sub       b                             ;[08d7] 90
                    call      $08b9                         ;[08d8] cd b9 08
                    jr        nc,$08df                      ;[08db] 30 02
                    djnz      $08d5                         ;[08dd] 10 f6
                    pop       iy                            ;[08df] fd e1
                    ret                                     ;[08e1] c9

                    push      iy                            ;[08e2] fd e5
                    ld        a,($2330)                     ;[08e4] 3a 30 23
                    call      $0904                         ;[08e7] cd 04 09
                    set       1,(iy+$00)                    ;[08ea] fd cb 00 ce
                    pop       iy                            ;[08ee] fd e1
                    ret                                     ;[08f0] c9

                    push      iy                            ;[08f1] fd e5
                    ld        a,($2330)                     ;[08f3] 3a 30 23
                    call      $0904                         ;[08f6] cd 04 09
                    set       1,(iy+$00)                    ;[08f9] fd cb 00 ce
                    set       2,(iy+$00)                    ;[08fd] fd cb 00 d6
                    pop       iy                            ;[0901] fd e1
                    ret                                     ;[0903] c9

                    ld        iy,$232b                      ;[0904] fd 21 2b 23
                    ld        de,$0007                      ;[0908] 11 07 00
                    inc       a                             ;[090b] 3c
                    add       iy,de                         ;[090c] fd 19
                    dec       a                             ;[090e] 3d
                    jr        nz,$090c                      ;[090f] 20 fb
                    ret                                     ;[0911] c9

                    call      $05a1                         ;[0912] cd a1 05
                    ld        a,$02                         ;[0915] 3e 02
                    ret       nc                            ;[0917] d0
                    bit       7,(ix+$13)                    ;[0918] dd cb 13 7e
                    jr        nz,$0926                      ;[091c] 20 08
                    sla       c                             ;[091e] cb 21
                    rl        b                             ;[0920] cb 10
                    rl        e                             ;[0922] cb 13
                    rl        d                             ;[0924] cb 12
                    ld        l,b                           ;[0926] 68
                    ld        h,e                           ;[0927] 63
                    ld        a,d                           ;[0928] 7a
                    ld        e,c                           ;[0929] 59
                    ld        c,(ix+$19)                    ;[092a] dd 4e 19
                    ld        b,(ix+$1a)                    ;[092d] dd 46 1a
                    add       hl,bc                         ;[0930] 09
                    ex        de,hl                         ;[0931] eb
                    adc       $00                           ;[0932] ce 00
                    ld        c,a                           ;[0934] 4f
                    ld        a,$00                         ;[0935] 3e 00
                    adc       a                             ;[0937] 8f
                    ld        b,a                           ;[0938] 47
                    ld        h,$00                         ;[0939] 26 00
                    add       hl,hl                         ;[093b] 29
                    push      hl                            ;[093c] e5
                    call      $0832                         ;[093d] cd 32 08
                    pop       de                            ;[0940] d1
                    ret       nc                            ;[0941] d0
                    add       hl,de                         ;[0942] 19
                    ld        c,(hl)                        ;[0943] 4e
                    inc       hl                            ;[0944] 23
                    ld        b,(hl)                        ;[0945] 46
                    bit       7,(ix+$13)                    ;[0946] dd cb 13 7e
                    jr        nz,$0955                      ;[094a] 20 09
                    inc       hl                            ;[094c] 23
                    ld        e,(hl)                        ;[094d] 5e
                    inc       hl                            ;[094e] 23
                    ld        a,(hl)                        ;[094f] 7e
                    and       $0f                           ;[0950] e6 0f
                    ld        d,a                           ;[0952] 57
                    scf                                     ;[0953] 37
                    ret                                     ;[0954] c9

                    ld        de,$0000                      ;[0955] 11 00 00
                    scf                                     ;[0958] 37
                    ret                                     ;[0959] c9

                    ld        ($2012),hl                    ;[095a] 22 12 20
                    call      $05a1                         ;[095d] cd a1 05
                    ld        a,$02                         ;[0960] 3e 02
                    ret       nc                            ;[0962] d0
                    bit       7,(ix+$13)                    ;[0963] dd cb 13 7e
                    jr        nz,$0971                      ;[0967] 20 08
                    sla       c                             ;[0969] cb 21
                    rl        b                             ;[096b] cb 10
                    rl        e                             ;[096d] cb 13
                    rl        d                             ;[096f] cb 12
                    ld        l,b                           ;[0971] 68
                    ld        h,e                           ;[0972] 63
                    ld        a,d                           ;[0973] 7a
                    ld        e,c                           ;[0974] 59
                    ld        c,(ix+$19)                    ;[0975] dd 4e 19
                    ld        b,(ix+$1a)                    ;[0978] dd 46 1a
                    add       hl,bc                         ;[097b] 09
                    ex        de,hl                         ;[097c] eb
                    adc       $00                           ;[097d] ce 00
                    ld        c,a                           ;[097f] 4f
                    ld        a,$00                         ;[0980] 3e 00
                    adc       a                             ;[0982] 8f
                    ld        b,a                           ;[0983] 47
                    ld        h,$00                         ;[0984] 26 00
                    add       hl,hl                         ;[0986] 29
                    push      hl                            ;[0987] e5
                    call      $0832                         ;[0988] cd 32 08
                    pop       de                            ;[098b] d1
                    ret       nc                            ;[098c] d0
                    add       hl,de                         ;[098d] 19
                    ld        de,($2012)                    ;[098e] ed 5b 12 20
                    ld        (hl),e                        ;[0992] 73
                    inc       hl                            ;[0993] 23
                    ld        (hl),d                        ;[0994] 72
                    bit       7,(ix+$13)                    ;[0995] dd cb 13 7e
                    jr        nz,$09aa                      ;[0999] 20 0f
                    inc       hl                            ;[099b] 23
                    push      iy                            ;[099c] fd e5
                    pop       de                            ;[099e] d1
                    ld        (hl),e                        ;[099f] 73
                    inc       hl                            ;[09a0] 23
                    ld        a,d                           ;[09a1] 7a
                    and       $0f                           ;[09a2] e6 0f
                    ld        d,a                           ;[09a4] 57
                    ld        a,(hl)                        ;[09a5] 7e
                    and       $f0                           ;[09a6] e6 f0
                    or        d                             ;[09a8] b2
                    ld        (hl),a                        ;[09a9] 77
                    call      $08f1                         ;[09aa] cd f1 08
                    scf                                     ;[09ad] 37
                    ret                                     ;[09ae] c9

                    call      $078f                         ;[09af] cd 8f 07
                    ld        b,$00                         ;[09b2] 06 00
                    ld        ix,$2e8e                      ;[09b4] dd 21 8e 2e
                    call      $0a3f                         ;[09b8] cd 3f 0a
                    ld        b,$01                         ;[09bb] 06 01
                    ld        ix,$2ea1                      ;[09bd] dd 21 a1 2e
                    call      c,$0a3f                       ;[09c1] dc 3f 0a
                    ld        b,$01                         ;[09c4] 06 01
                    ret       nc                            ;[09c6] d0
                    inc       b                             ;[09c7] 04
                    ret                                     ;[09c8] c9

                    call      $0a0a                         ;[09c9] cd 0a 0a
                    call      $0d10                         ;[09cc] cd 10 0d
                    ld        a,d                           ;[09cf] 7a
                    inc       a                             ;[09d0] 3c
                    scf                                     ;[09d1] 37
                    call      z,$0cb1                       ;[09d2] cc b1 0c
                    ret       nc                            ;[09d5] d0
                    ld        hl,$0000                      ;[09d6] 21 00 00
                    push      hl                            ;[09d9] e5
                    pop       iy                            ;[09da] fd e1
                    ld        a,(ix+$21)                    ;[09dc] dd 7e 21
                    add       iy,bc                         ;[09df] fd 09
                    adc       hl,de                         ;[09e1] ed 5a
                    dec       a                             ;[09e3] 3d
                    jr        nz,$09df                      ;[09e4] 20 f9
                    push      iy                            ;[09e6] fd e5
                    pop       de                            ;[09e8] d1
                    srl       h                             ;[09e9] cb 3c
                    rr        l                             ;[09eb] cb 1d
                    rr        d                             ;[09ed] cb 1a
                    rr        e                             ;[09ef] cb 1b
                    ld        b,h                           ;[09f1] 44
                    ld        c,l                           ;[09f2] 4d
                    ld        h,d                           ;[09f3] 62
                    ld        l,e                           ;[09f4] 6b
                    ld        a,b                           ;[09f5] 78
                    or        c                             ;[09f6] b1
                    jr        z,$09fc                       ;[09f7] 28 03
                    ld        hl,$ffff                      ;[09f9] 21 ff ff
                    scf                                     ;[09fc] 37
                    ret                                     ;[09fd] c9

                    add       a                             ;[09fe] 87
                    ld        hl,$2e6f                      ;[09ff] 21 6f 2e
                    add       hl,a                          ;[0a02] ed 31
                    ld        d,(hl)                        ;[0a04] 56
                    dec       hl                            ;[0a05] 2b
                    ld        e,(hl)                        ;[0a06] 5e
                    ld        a,d                           ;[0a07] 7a
                    or        e                             ;[0a08] b3
                    ret                                     ;[0a09] c9

                    call      $09fe                         ;[0a0a] cd fe 09
                    push      de                            ;[0a0d] d5
                    pop       iy                            ;[0a0e] fd e1
                    ld        e,(iy+$00)                    ;[0a10] fd 5e 00
                    ld        d,(iy+$01)                    ;[0a13] fd 56 01
                    push      de                            ;[0a16] d5
                    pop       ix                            ;[0a17] dd e1
                    ret                                     ;[0a19] c9

                    call      $0a0a                         ;[0a1a] cd 0a 0a
                    push      iy                            ;[0a1d] fd e5
                    pop       hl                            ;[0a1f] e1
                    ld        de,$2e50                      ;[0a20] 11 50 2e
                    push      de                            ;[0a23] d5
                    ld        bc,$000f                      ;[0a24] 01 0f 00
                    ldir                                    ;[0a27] ed b0
                    pop       iy                            ;[0a29] fd e1
                    ret                                     ;[0a2b] c9

                    and       $01                           ;[0a2c] e6 01
                    ld        hl,$2e8e                      ;[0a2e] 21 8e 2e
                    jr        z,$0a36                       ;[0a31] 28 03
                    ld        hl,$2ea1                      ;[0a33] 21 a1 2e
                    push      ix                            ;[0a36] dd e5
                    pop       de                            ;[0a38] d1
                    ld        bc,$0013                      ;[0a39] 01 13 00
                    ldir                                    ;[0a3c] ed b0
                    ret                                     ;[0a3e] c9

                    ld        (ix+$10),b                    ;[0a3f] dd 70 10
                    ld        c,b                           ;[0a42] 48
                    call      $0efc                         ;[0a43] cd fc 0e
                    ret       nc                            ;[0a46] d0
                    ld        a,(ix+$10)                    ;[0a47] dd 7e 10
                    or        b                             ;[0a4a] b0
                    ld        (ix+$10),a                    ;[0a4b] dd 77 10
                    dec       hl                            ;[0a4e] 2b
                    ld        a,h                           ;[0a4f] 7c
                    and       l                             ;[0a50] a5
                    inc       a                             ;[0a51] 3c
                    jr        nz,$0a55                      ;[0a52] 20 01
                    dec       de                            ;[0a54] 1b
                    ld        b,h                           ;[0a55] 44
                    ld        c,l                           ;[0a56] 4d
                    call      $04f8                         ;[0a57] cd f8 04
                    rlca                                    ;[0a5a] 07
                    scf                                     ;[0a5b] 37
                    ret                                     ;[0a5c] c9

                    inc       bc                            ;[0a5d] 03
                    inc       b                             ;[0a5e] 04
                    dec       b                             ;[0a5f] 05
                    jp        nz,$0b2a                      ;[0a60] c2 2a 0b
                    ld        b,c                           ;[0a63] 41
                    push      bc                            ;[0a64] c5
                    push      ix                            ;[0a65] dd e5
                    call      $0a2c                         ;[0a67] cd 2c 0a
                    ld        ix,$0000                      ;[0a6a] dd 21 00 00
                    call      $07bc                         ;[0a6e] cd bc 07
                    ld        ($202c),hl                    ;[0a71] 22 2c 20
                    pop       ix                            ;[0a74] dd e1
                    ld        a,$01                         ;[0a76] 3e 01
                    ld        (ix+$2a),a                    ;[0a78] dd 77 2a
                    pop       bc                            ;[0a7b] c1
                    push      bc                            ;[0a7c] c5
                    dec       c                             ;[0a7d] 0d
                    ld        (ix+$11),c                    ;[0a7e] dd 71 11
                    ld        bc,$0000                      ;[0a81] 01 00 00
                    ld        (ix+$12),b                    ;[0a84] dd 70 12
                    ld        d,b                           ;[0a87] 50
                    ld        e,c                           ;[0a88] 59
                    ld        hl,($202c)                    ;[0a89] 2a 2c 20
                    push      hl                            ;[0a8c] e5
                    call      $1eaf                         ;[0a8d] cd af 1e
                    pop       hl                            ;[0a90] e1
                    jp        nc,$0b29                      ;[0a91] d2 29 0b
                    inc       h                             ;[0a94] 24
                    push      hl                            ;[0a95] e5
                    pop       iy                            ;[0a96] fd e1
                    inc       h                             ;[0a98] 24
                    dec       hl                            ;[0a99] 2b
                    ld        a,(hl)                        ;[0a9a] 7e
                    cp        $aa                           ;[0a9b] fe aa
                    jr        nz,$0aa3                      ;[0a9d] 20 04
                    dec       hl                            ;[0a9f] 2b
                    ld        a,(hl)                        ;[0aa0] 7e
                    cp        $55                           ;[0aa1] fe 55
                    jp        nz,$0b29                      ;[0aa3] c2 29 0b
                    ld        de,$00be                      ;[0aa6] 11 be 00
                    ld        a,(ix+$2a)                    ;[0aa9] dd 7e 2a
                    add       iy,de                         ;[0aac] fd 19
                    ld        e,$10                         ;[0aae] 1e 10
                    dec       a                             ;[0ab0] 3d
                    jr        nz,$0aac                      ;[0ab1] 20 f9
                    ld        a,(iy+$04)                    ;[0ab3] fd 7e 04
                    and       a                             ;[0ab6] a7
                    jr        z,$0ae1                       ;[0ab7] 28 28
                    rst       $28                           ;[0ab9] ef
                    ex        af,af'                        ;[0aba] 08
                    ld        a,d                           ;[0abb] 7a
                    or        e                             ;[0abc] b3
                    or        b                             ;[0abd] b0
                    or        c                             ;[0abe] b1
                    jr        z,$0ae1                       ;[0abf] 28 20
                    push      bc                            ;[0ac1] c5
                    push      de                            ;[0ac2] d5
                    ld        d,b                           ;[0ac3] 50
                    ld        e,c                           ;[0ac4] 59
                    pop       bc                            ;[0ac5] c1
                    push      bc                            ;[0ac6] c5
                    call      $1ed2                         ;[0ac7] cd d2 1e
                    pop       de                            ;[0aca] d1
                    pop       bc                            ;[0acb] c1
                    jr        nc,$0ae1                      ;[0acc] 30 13
                    call      $04f8                         ;[0ace] cd f8 04
                    ld        bc,$0cef                      ;[0ad1] 01 ef 0c
                    ld        a,d                           ;[0ad4] 7a
                    or        e                             ;[0ad5] b3
                    or        b                             ;[0ad6] b0
                    or        c                             ;[0ad7] b1
                    jr        z,$0ae1                       ;[0ad8] 28 07
                    call      $04f8                         ;[0ada] cd f8 04
                    rlca                                    ;[0add] 07
                    call      $0b2e                         ;[0ade] cd 2e 0b
                    pop       bc                            ;[0ae1] c1
                    jr        nc,$0b0c                      ;[0ae2] 30 28
                    djnz      $0b0c                         ;[0ae4] 10 26
                    dec       c                             ;[0ae6] 0d
                    push      iy                            ;[0ae7] fd e5
                    pop       hl                            ;[0ae9] e1
                    ld        a,(hl)                        ;[0aea] 7e
                    cp        $29                           ;[0aeb] fe 29
                    ld        de,$0f02                      ;[0aed] 11 02 0f
                    jr        nz,$0b00                      ;[0af0] 20 0e
                    ld        a,$05                         ;[0af2] 3e 05
                    add       hl,a                          ;[0af4] ed 31
                    ld        de,$202e                      ;[0af6] 11 2e 20
                    push      de                            ;[0af9] d5
                    ld        bc,$000b                      ;[0afa] 01 0b 00
                    ldir                                    ;[0afd] ed b0
                    pop       de                            ;[0aff] d1
                    push      de                            ;[0b00] d5
                    call      $1219                         ;[0b01] cd 19 12
                    pop       de                            ;[0b04] d1
                    jr        nc,$0b0a                      ;[0b05] 30 03
                    jr        nz,$0b0a                      ;[0b07] 20 01
                    ex        de,hl                         ;[0b09] eb
                    scf                                     ;[0b0a] 37
                    ret                                     ;[0b0b] c9

                    push      bc                            ;[0b0c] c5
                    ld        a,(ix+$10)                    ;[0b0d] dd 7e 10
                    call      $0a2c                         ;[0b10] cd 2c 0a
                    ld        a,(ix+$2a)                    ;[0b13] dd 7e 2a
                    inc       a                             ;[0b16] 3c
                    cp        $05                           ;[0b17] fe 05
                    jp        c,$0a78                       ;[0b19] da 78 0a
                    pop       bc                            ;[0b1c] c1
                    dec       c                             ;[0b1d] 0d
                    jr        nz,$0b2a                      ;[0b1e] 20 0a
                    ld        (ix+$2a),c                    ;[0b20] dd 71 2a
                    call      $0b2e                         ;[0b23] cd 2e 0b
                    jr        c,$0ae6                       ;[0b26] 38 be
                    ret                                     ;[0b28] c9

                    pop       hl                            ;[0b29] e1
                    ld        a,$38                         ;[0b2a] 3e 38
                    and       a                             ;[0b2c] a7
                    ret                                     ;[0b2d] c9

                    ld        b,$00                         ;[0b2e] 06 00
                    ld        c,b                           ;[0b30] 48
                    ld        d,b                           ;[0b31] 50
                    ld        e,b                           ;[0b32] 58
                    ld        hl,($202c)                    ;[0b33] 2a 2c 20
                    push      hl                            ;[0b36] e5
                    call      $1eaf                         ;[0b37] cd af 1e
                    pop       iy                            ;[0b3a] fd e1
                    ret       nc                            ;[0b3c] d0
                    rst       $28                           ;[0b3d] ef
                    dec       bc                            ;[0b3e] 0b
                    ld        a,c                           ;[0b3f] 79
                    and       a                             ;[0b40] a7
                    jr        nz,$0b2a                      ;[0b41] 20 e7
                    ld        a,b                           ;[0b43] 78
                    cp        $02                           ;[0b44] fe 02
                    jr        nz,$0b2a                      ;[0b46] 20 e2
                    ld        a,e                           ;[0b48] 7b
                    and       a                             ;[0b49] a7
                    jr        z,$0b2a                       ;[0b4a] 28 de
                    ld        (ix+$21),a                    ;[0b4c] dd 77 21
                    rst       $28                           ;[0b4f] ef
                    ld        c,$78                         ;[0b50] 0e 78
                    or        c                             ;[0b52] b1
                    jr        z,$0b2a                       ;[0b53] 28 d5
                    ld        (ix+$19),c                    ;[0b55] dd 71 19
                    ld        (ix+$1a),b                    ;[0b58] dd 70 1a
                    ld        h,b                           ;[0b5b] 60
                    ld        l,c                           ;[0b5c] 69
                    rst       $28                           ;[0b5d] ef
                    ld        d,$78                         ;[0b5e] 16 78
                    or        c                             ;[0b60] b1
                    jr        z,$0b6c                       ;[0b61] 28 09
                    ld        (ix+$13),$80                  ;[0b63] dd 36 13 80
                    ld        de,$0000                      ;[0b67] 11 00 00
                    jr        $0b71                         ;[0b6a] 18 05
                    ld        (ix+$13),a                    ;[0b6c] dd 77 13
                    rst       $28                           ;[0b6f] ef
                    inc       h                             ;[0b70] 24
                    call      $04f8                         ;[0b71] cd f8 04
                    inc       d                             ;[0b74] 14
                    ld        a,(iy+$10)                    ;[0b75] fd 7e 10
                    and       a                             ;[0b78] a7
                    jr        z,$0b2a                       ;[0b79] 28 af
                    ld        (ix+$18),a                    ;[0b7b] dd 77 18
                    push      hl                            ;[0b7e] e5
                    ex        (sp),iy                       ;[0b7f] fd e3
                    ld        hl,$0000                      ;[0b81] 21 00 00
                    add       iy,bc                         ;[0b84] fd 09
                    adc       hl,de                         ;[0b86] ed 5a
                    dec       a                             ;[0b88] 3d
                    jr        nz,$0b84                      ;[0b89] 20 f9
                    ex        (sp),iy                       ;[0b8b] fd e3
                    rst       $28                           ;[0b8d] ef
                    ld        de,$ddd1                      ;[0b8e] 11 d1 dd
                    rl        e                             ;[0b91] cb 13
                    ld        a,(hl)                        ;[0b93] 7e
                    jr        nz,$0ba4                      ;[0b94] 20 0e
                    ld        a,b                           ;[0b96] 78
                    or        c                             ;[0b97] b1
                    jr        nz,$0b2a                      ;[0b98] 20 90
                    push      de                            ;[0b9a] d5
                    rst       $28                           ;[0b9b] ef
                    inc       l                             ;[0b9c] 2c
                    call      $04f8                         ;[0b9d] cd f8 04
                    dec       e                             ;[0ba0] 1d
                    pop       de                            ;[0ba1] d1
                    jr        $0bcc                         ;[0ba2] 18 28
                    push      bc                            ;[0ba4] c5
                    ex        de,hl                         ;[0ba5] eb
                    ld        b,h                           ;[0ba6] 44
                    ld        c,l                           ;[0ba7] 4d
                    call      $04f8                         ;[0ba8] cd f8 04
                    dec       e                             ;[0bab] 1d
                    ex        de,hl                         ;[0bac] eb
                    pop       bc                            ;[0bad] c1
                    ld        a,c                           ;[0bae] 79
                    and       $0f                           ;[0baf] e6 0f
                    jr        nz,$0b98                      ;[0bb1] 20 e5
                    ld        a,$04                         ;[0bb3] 3e 04
                    srl       b                             ;[0bb5] cb 38
                    rr        c                             ;[0bb7] cb 19
                    dec       a                             ;[0bb9] 3d
                    jr        nz,$0bb5                      ;[0bba] 20 f9
                    ld        a,b                           ;[0bbc] 78
                    or        c                             ;[0bbd] b1
                    jr        z,$0b79                       ;[0bbe] 28 b9
                    ld        (ix+$1b),c                    ;[0bc0] dd 71 1b
                    ld        (ix+$1c),b                    ;[0bc3] dd 70 1c
                    ex        de,hl                         ;[0bc6] eb
                    add       hl,bc                         ;[0bc7] 09
                    ex        de,hl                         ;[0bc8] eb
                    jr        nc,$0bcc                      ;[0bc9] 30 01
                    inc       hl                            ;[0bcb] 23
                    push      hl                            ;[0bcc] e5
                    push      de                            ;[0bcd] d5
                    ex        de,hl                         ;[0bce] eb
                    ld        b,$00                         ;[0bcf] 06 00
                    ld        c,(ix+$21)                    ;[0bd1] dd 4e 21
                    and       a                             ;[0bd4] a7
                    sbc       hl,bc                         ;[0bd5] ed 42
                    ex        de,hl                         ;[0bd7] eb
                    ld        c,b                           ;[0bd8] 48
                    sbc       hl,bc                         ;[0bd9] ed 42
                    ex        de,hl                         ;[0bdb] eb
                    ld        c,(ix+$21)                    ;[0bdc] dd 4e 21
                    sbc       hl,bc                         ;[0bdf] ed 42
                    ex        de,hl                         ;[0be1] eb
                    ld        c,b                           ;[0be2] 48
                    sbc       hl,bc                         ;[0be3] ed 42
                    ex        de,hl                         ;[0be5] eb
                    ld        b,h                           ;[0be6] 44
                    ld        c,l                           ;[0be7] 4d
                    call      $04f8                         ;[0be8] cd f8 04
                    ld        h,$ef                         ;[0beb] 26 ef
                    inc       de                            ;[0bed] 13
                    ld        de,$0000                      ;[0bee] 11 00 00
                    ld        a,b                           ;[0bf1] 78
                    or        c                             ;[0bf2] b1
                    jr        nz,$0bf7                      ;[0bf3] 20 02
                    rst       $28                           ;[0bf5] ef
                    jr        nz,$0c58                      ;[0bf6] 20 60
                    ld        l,c                           ;[0bf8] 69
                    ld        b,d                           ;[0bf9] 42
                    ld        c,e                           ;[0bfa] 4b
                    pop       de                            ;[0bfb] d1
                    sbc       hl,de                         ;[0bfc] ed 52
                    ex        de,hl                         ;[0bfe] eb
                    ld        h,b                           ;[0bff] 60
                    ld        l,c                           ;[0c00] 69
                    pop       bc                            ;[0c01] c1
                    sbc       hl,bc                         ;[0c02] ed 42
                    ld        a,(ix+$21)                    ;[0c04] dd 7e 21
                    srl       a                             ;[0c07] cb 3f
                    jr        z,$0c15                       ;[0c09] 28 0a
                    srl       h                             ;[0c0b] cb 3c
                    rr        l                             ;[0c0d] cb 1d
                    rr        d                             ;[0c0f] cb 1a
                    rr        e                             ;[0c11] cb 1b
                    jr        $0c07                         ;[0c13] 18 f2
                    inc       de                            ;[0c15] 13
                    ld        a,d                           ;[0c16] 7a
                    or        e                             ;[0c17] b3
                    jr        nz,$0c1b                      ;[0c18] 20 01
                    inc       hl                            ;[0c1a] 23
                    ex        de,hl                         ;[0c1b] eb
                    ld        b,h                           ;[0c1c] 44
                    ld        c,l                           ;[0c1d] 4d
                    call      $04f8                         ;[0c1e] cd f8 04
                    ld        ($b37a),hl                    ;[0c21] 22 7a b3
                    jr        nz,$0c2e                      ;[0c24] 20 08
                    ld        hl,$0ff5                      ;[0c26] 21 f5 0f
                    sbc       hl,bc                         ;[0c29] ed 42
                    jp        nc,$0b2a                      ;[0c2b] d2 2a 0b
                    srl       d                             ;[0c2e] cb 3a
                    rr        e                             ;[0c30] cb 1b
                    rr        b                             ;[0c32] cb 18
                    rr        c                             ;[0c34] cb 19
                    call      $04f8                         ;[0c36] cd f8 04
                    cpl                                     ;[0c39] 2f
                    bit       7,(ix+$13)                    ;[0c3a] dd cb 13 7e
                    jp        nz,$0ca0                      ;[0c3e] c2 a0 0c
                    ld        e,(iy+$30)                    ;[0c41] fd 5e 30
                    ld        d,(iy+$31)                    ;[0c44] fd 56 31
                    ld        (ix+$33),e                    ;[0c47] dd 73 33
                    ld        (ix+$34),d                    ;[0c4a] dd 72 34
                    ld        bc,$0000                      ;[0c4d] 01 00 00
                    call      $0832                         ;[0c50] cd 32 08
                    jr        nc,$0c93                      ;[0c53] 30 3e
                    ld        de,$01e4                      ;[0c55] 11 e4 01
                    add       hl,de                         ;[0c58] 19
                    ld        a,(hl)                        ;[0c59] 7e
                    inc       hl                            ;[0c5a] 23
                    cp        $72                           ;[0c5b] fe 72
                    jr        nz,$0c93                      ;[0c5d] 20 34
                    ld        a,(hl)                        ;[0c5f] 7e
                    inc       hl                            ;[0c60] 23
                    cp        $72                           ;[0c61] fe 72
                    jr        nz,$0c93                      ;[0c63] 20 2e
                    ld        a,(hl)                        ;[0c65] 7e
                    inc       hl                            ;[0c66] 23
                    cp        $41                           ;[0c67] fe 41
                    jr        nz,$0c93                      ;[0c69] 20 28
                    ld        a,(hl)                        ;[0c6b] 7e
                    inc       hl                            ;[0c6c] 23
                    cp        $61                           ;[0c6d] fe 61
                    jr        nz,$0c93                      ;[0c6f] 20 22
                    ld        c,(hl)                        ;[0c71] 4e
                    inc       hl                            ;[0c72] 23
                    ld        b,(hl)                        ;[0c73] 46
                    inc       hl                            ;[0c74] 23
                    ld        e,(hl)                        ;[0c75] 5e
                    inc       hl                            ;[0c76] 23
                    ld        d,(hl)                        ;[0c77] 56
                    inc       hl                            ;[0c78] 23
                    call      $05aa                         ;[0c79] cd aa 05
                    jr        nc,$0c93                      ;[0c7c] 30 15
                    call      $04f8                         ;[0c7e] cd f8 04
                    dec       hl                            ;[0c81] 2b
                    ld        c,(hl)                        ;[0c82] 4e
                    inc       hl                            ;[0c83] 23
                    ld        b,(hl)                        ;[0c84] 46
                    inc       hl                            ;[0c85] 23
                    ld        e,(hl)                        ;[0c86] 5e
                    inc       hl                            ;[0c87] 23
                    ld        d,(hl)                        ;[0c88] 56
                    call      $04f8                         ;[0c89] cd f8 04
                    cpl                                     ;[0c8c] 2f
                    set       6,(ix+$13)                    ;[0c8d] dd cb 13 f6
                    jr        $0c9b                         ;[0c91] 18 08
                    call      $04e1                         ;[0c93] cd e1 04
                    cpl                                     ;[0c96] 2f
                    call      $04f8                         ;[0c97] cd f8 04
                    dec       hl                            ;[0c9a] 2b
                    ld        de,$0042                      ;[0c9b] 11 42 00
                    jr        $0cad                         ;[0c9e] 18 0d
                    ld        de,$ffff                      ;[0ca0] 11 ff ff
                    ld        bc,$0000                      ;[0ca3] 01 00 00
                    call      $04f8                         ;[0ca6] cd f8 04
                    dec       hl                            ;[0ca9] 2b
                    ld        de,$0026                      ;[0caa] 11 26 00
                    add       iy,de                         ;[0cad] fd 19
                    scf                                     ;[0caf] 37
                    ret                                     ;[0cb0] c9

                    ld        bc,$0000                      ;[0cb1] 01 00 00
                    push      bc                            ;[0cb4] c5
                    ld        d,b                           ;[0cb5] 50
                    ld        e,c                           ;[0cb6] 59
                    call      $04f8                         ;[0cb7] cd f8 04
                    dec       hl                            ;[0cba] 2b
                    call      $04e1                         ;[0cbb] cd e1 04
                    inc       d                             ;[0cbe] 14
                    push      de                            ;[0cbf] d5
                    pop       iy                            ;[0cc0] fd e1
                    ld        h,b                           ;[0cc2] 60
                    ld        l,c                           ;[0cc3] 69
                    call      $04e1                         ;[0cc4] cd e1 04
                    rla                                     ;[0cc7] 17
                    pop       bc                            ;[0cc8] c1
                    push      de                            ;[0cc9] d5
                    push      bc                            ;[0cca] c5
                    push      hl                            ;[0ccb] e5
                    call      $0832                         ;[0ccc] cd 32 08
                    jr        nc,$0d16                      ;[0ccf] 30 45
                    ld        b,$00                         ;[0cd1] 06 00
                    bit       7,(ix+$13)                    ;[0cd3] dd cb 13 7e
                    jr        nz,$0cee                      ;[0cd7] 20 15
                    ld        b,$80                         ;[0cd9] 06 80
                    ld        a,(hl)                        ;[0cdb] 7e
                    inc       hl                            ;[0cdc] 23
                    or        (hl)                          ;[0cdd] b6
                    inc       hl                            ;[0cde] 23
                    or        (hl)                          ;[0cdf] b6
                    inc       hl                            ;[0ce0] 23
                    ld        c,a                           ;[0ce1] 4f
                    ld        a,(hl)                        ;[0ce2] 7e
                    inc       hl                            ;[0ce3] 23
                    and       $0f                           ;[0ce4] e6 0f
                    or        c                             ;[0ce6] b1
                    call      z,$1fdf                       ;[0ce7] cc df 1f
                    djnz      $0cdb                         ;[0cea] 10 ef
                    jr        $0cf7                         ;[0cec] 18 09
                    ld        a,(hl)                        ;[0cee] 7e
                    inc       hl                            ;[0cef] 23
                    or        (hl)                          ;[0cf0] b6
                    inc       hl                            ;[0cf1] 23
                    call      z,$1fdf                       ;[0cf2] cc df 1f
                    djnz      $0cee                         ;[0cf5] 10 f7
                    pop       hl                            ;[0cf7] e1
                    pop       bc                            ;[0cf8] c1
                    pop       de                            ;[0cf9] d1
                    inc       de                            ;[0cfa] 13
                    ld        a,d                           ;[0cfb] 7a
                    or        e                             ;[0cfc] b3
                    jr        nz,$0d00                      ;[0cfd] 20 01
                    inc       bc                            ;[0cff] 03
                    dec       hl                            ;[0d00] 2b
                    ld        a,h                           ;[0d01] 7c
                    and       l                             ;[0d02] a5
                    inc       a                             ;[0d03] 3c
                    jr        nz,$0d08                      ;[0d04] 20 02
                    dec       iy                            ;[0d06] fd 2b
                    ld        a,iyh                         ;[0d08] fd 7c
                    or        iyl                           ;[0d0a] fd b5
                    or        h                             ;[0d0c] b4
                    or        l                             ;[0d0d] b5
                    jr        nz,$0cc9                      ;[0d0e] 20 b9
                    call      $04e1                         ;[0d10] cd e1 04
                    dec       hl                            ;[0d13] 2b
                    scf                                     ;[0d14] 37
                    ret                                     ;[0d15] c9

                    pop       hl                            ;[0d16] e1
                    pop       bc                            ;[0d17] c1
                    pop       de                            ;[0d18] d1
                    ret                                     ;[0d19] c9

                    push      ix                            ;[0d1a] dd e5
                    push      bc                            ;[0d1c] c5
                    push      hl                            ;[0d1d] e5
                    ld        ix,$203e                      ;[0d1e] dd 21 3e 20
                    call      $0a5d                         ;[0d22] cd 5d 0a
                    pop       hl                            ;[0d25] e1
                    jr        nc,$0d65                      ;[0d26] 30 3d
                    ld        a,(ix+$2a)                    ;[0d28] dd 7e 2a
                    add       $30                           ;[0d2b] c6 30
                    ld        (hl),a                        ;[0d2d] 77
                    inc       hl                            ;[0d2e] 23
                    ld        (hl),$3e                      ;[0d2f] 36 3e
                    inc       hl                            ;[0d31] 23
                    ex        de,hl                         ;[0d32] eb
                    ld        bc,$000b                      ;[0d33] 01 0b 00
                    ldir                                    ;[0d36] ed b0
                    ex        de,hl                         ;[0d38] eb
                    ld        b,$03                         ;[0d39] 06 03
                    ld        (hl),$20                      ;[0d3b] 36 20
                    inc       hl                            ;[0d3d] 23
                    djnz      $0d3b                         ;[0d3e] 10 fb
                    ld        (hl),$10                      ;[0d40] 36 10
                    bit       7,(ix+$13)                    ;[0d42] dd cb 13 7e
                    jr        nz,$0d49                      ;[0d46] 20 01
                    inc       (hl)                          ;[0d48] 34
                    inc       hl                            ;[0d49] 23
                    ld        b,$06                         ;[0d4a] 06 06
                    ld        (hl),$ff                      ;[0d4c] 36 ff
                    inc       hl                            ;[0d4e] 23
                    djnz      $0d4c                         ;[0d4f] 10 fb
                    ex        de,hl                         ;[0d51] eb
                    push      ix                            ;[0d52] dd e5
                    pop       hl                            ;[0d54] e1
                    ld        bc,$0007                      ;[0d55] 01 07 00
                    add       hl,bc                         ;[0d58] 09
                    ld        c,$04                         ;[0d59] 0e 04
                    ldir                                    ;[0d5b] ed b0
                    ex        de,hl                         ;[0d5d] eb
                    ld        b,$25                         ;[0d5e] 06 25
                    ld        (hl),c                        ;[0d60] 71
                    inc       hl                            ;[0d61] 23
                    djnz      $0d60                         ;[0d62] 10 fc
                    scf                                     ;[0d64] 37
                    pop       bc                            ;[0d65] c1
                    pop       ix                            ;[0d66] dd e1
                    ret                                     ;[0d68] c9

                    ld        bc,$0000                      ;[0d69] 01 00 00
                    push      af                            ;[0d6c] f5
                    push      bc                            ;[0d6d] c5
                    push      hl                            ;[0d6e] e5
                    ld        hl,$2200                      ;[0d6f] 21 00 22
                    call      $0d1a                         ;[0d72] cd 1a 0d
                    pop       hl                            ;[0d75] e1
                    jr        nc,$0db5                      ;[0d76] 30 3d
                    push      hl                            ;[0d78] e5
                    inc       hl                            ;[0d79] 23
                    ld        a,(hl)                        ;[0d7a] 7e
                    cp        $3e                           ;[0d7b] fe 3e
                    pop       hl                            ;[0d7d] e1
                    push      hl                            ;[0d7e] e5
                    ld        bc,$0010                      ;[0d7f] 01 10 00
                    ld        de,$2200                      ;[0d82] 11 00 22
                    scf                                     ;[0d85] 37
                    jr        z,$0d8d                       ;[0d86] 28 05
                    ld        c,$0e                         ;[0d88] 0e 0e
                    inc       de                            ;[0d8a] 13
                    inc       de                            ;[0d8b] 13
                    and       a                             ;[0d8c] a7
                    ld        a,(de)                        ;[0d8d] 1a
                    inc       de                            ;[0d8e] 13
                    cpi                                     ;[0d8f] ed a1
                    jr        nz,$0d9b                      ;[0d91] 20 08
                    jp        pe,$0d8d                      ;[0d93] ea 8d 0d
                    pop       hl                            ;[0d96] e1
                    pop       bc                            ;[0d97] c1
                    pop       hl                            ;[0d98] e1
                    scf                                     ;[0d99] 37
                    ret                                     ;[0d9a] c9

                    jr        nc,$0daf                      ;[0d9b] 30 12
                    ld        a,c                           ;[0d9d] 79
                    cp        $0d                           ;[0d9e] fe 0d
                    jr        nz,$0daf                      ;[0da0] 20 0d
                    ld        a,$20                         ;[0da2] 3e 20
                    dec       hl                            ;[0da4] 2b
                    ld        b,$0e                         ;[0da5] 06 0e
                    cp        (hl)                          ;[0da7] be
                    jr        nz,$0daf                      ;[0da8] 20 05
                    inc       hl                            ;[0daa] 23
                    djnz      $0da7                         ;[0dab] 10 fa
                    jr        $0d96                         ;[0dad] 18 e7
                    pop       hl                            ;[0daf] e1
                    pop       bc                            ;[0db0] c1
                    pop       af                            ;[0db1] f1
                    inc       bc                            ;[0db2] 03
                    jr        $0d6c                         ;[0db3] 18 b7
                    pop       bc                            ;[0db5] c1
                    pop       bc                            ;[0db6] c1
                    ret                                     ;[0db7] c9

                    pop       bc                            ;[0db8] c1
                    pop       bc                            ;[0db9] c1
                    pop       bc                            ;[0dba] c1
                    jp        $0e24                         ;[0dbb] c3 24 0e
                    push      ix                            ;[0dbe] dd e5
                    ld        a,e                           ;[0dc0] 7b
                    inc       e                             ;[0dc1] 1c
                    push      de                            ;[0dc2] d5
                    call      $09fe                         ;[0dc3] cd fe 09
                    pop       de                            ;[0dc6] d1
                    push      hl                            ;[0dc7] e5
                    push      de                            ;[0dc8] d5
                    xor       a                             ;[0dc9] af
                    push      af                            ;[0dca] f5
                    call      $09fe                         ;[0dcb] cd fe 09
                    jr        z,$0df1                       ;[0dce] 28 21
                    ex        de,hl                         ;[0dd0] eb
                    ld        e,(hl)                        ;[0dd1] 5e
                    inc       hl                            ;[0dd2] 23
                    ld        d,(hl)                        ;[0dd3] 56
                    push      de                            ;[0dd4] d5
                    pop       iy                            ;[0dd5] fd e1
                    ld        a,c                           ;[0dd7] 79
                    cp        (iy+$11)                      ;[0dd8] fd be 11
                    jr        nz,$0df1                      ;[0ddb] 20 14
                    ld        a,b                           ;[0ddd] 78
                    cp        (iy+$12)                      ;[0dde] fd be 12
                    jr        nz,$0df1                      ;[0de1] 20 0e
                    pop       hl                            ;[0de3] e1
                    pop       de                            ;[0de4] d1
                    push      de                            ;[0de5] d5
                    push      hl                            ;[0de6] e5
                    ld        a,(iy+$10)                    ;[0de7] fd 7e 10
                    and       $01                           ;[0dea] e6 01
                    cp        d                             ;[0dec] ba
                    ld        a,$3b                         ;[0ded] 3e 3b
                    jr        z,$0db8                       ;[0def] 28 c7
                    pop       af                            ;[0df1] f1
                    inc       a                             ;[0df2] 3c
                    cp        $10                           ;[0df3] fe 10
                    jr        c,$0dca                       ;[0df5] 38 d3
                    pop       hl                            ;[0df7] e1
                    push      hl                            ;[0df8] e5
                    ld        ix,$29db                      ;[0df9] dd 21 db 29
                    ld        de,$0035                      ;[0dfd] 11 35 00
                    add       ix,de                         ;[0e00] dd 19
                    dec       l                             ;[0e02] 2d
                    jr        nz,$0e00                      ;[0e03] 20 fb
                    ld        a,h                           ;[0e05] 7c
                    call      $0a5d                         ;[0e06] cd 5d 0a
                    jr        nc,$0db9                      ;[0e09] 30 ae
                    pop       de                            ;[0e0b] d1
                    ld        iy,$2d51                      ;[0e0c] fd 21 51 2d
                    ld        bc,$000f                      ;[0e10] 01 0f 00
                    add       iy,bc                         ;[0e13] fd 09
                    dec       e                             ;[0e15] 1d
                    jr        nz,$0e13                      ;[0e16] 20 fb
                    call      $10ca                         ;[0e18] cd ca 10
                    pop       hl                            ;[0e1b] e1
                    ld        a,iyl                         ;[0e1c] fd 7d
                    ld        (hl),a                        ;[0e1e] 77
                    inc       hl                            ;[0e1f] 23
                    ld        a,iyh                         ;[0e20] fd 7c
                    ld        (hl),a                        ;[0e22] 77
                    scf                                     ;[0e23] 37
                    pop       ix                            ;[0e24] dd e1
                    ret                                     ;[0e26] c9

                    ld        hl,$2eb4                      ;[0e27] 21 b4 2e
                    ld        b,$11                         ;[0e2a] 06 11
                    bit       7,(hl)                        ;[0e2c] cb 7e
                    jr        z,$0e39                       ;[0e2e] 28 09
                    push      hl                            ;[0e30] e5
                    add       hl,$0005                      ;[0e31] ed 34 05 00
                    cp        (hl)                          ;[0e35] be
                    jr        z,$0e5d                       ;[0e36] 28 25
                    pop       hl                            ;[0e38] e1
                    add       hl,$0006                      ;[0e39] ed 34 06 00
                    djnz      $0e2c                         ;[0e3d] 10 ed
                    call      $09fe                         ;[0e3f] cd fe 09
                    push      hl                            ;[0e42] e5
                    ex        de,hl                         ;[0e43] eb
                    ld        e,(hl)                        ;[0e44] 5e
                    inc       hl                            ;[0e45] 23
                    ld        d,(hl)                        ;[0e46] 56
                    ld        iy,$2740                      ;[0e47] fd 21 40 27
                    ld        b,$10                         ;[0e4b] 06 10
                    ld        a,(iy+$0f)                    ;[0e4d] fd 7e 0f
                    and       a                             ;[0e50] a7
                    jr        z,$0e61                       ;[0e51] 28 0e
                    ld        l,(iy+$00)                    ;[0e53] fd 6e 00
                    ld        h,(iy+$01)                    ;[0e56] fd 66 01
                    sbc       hl,de                         ;[0e59] ed 52
                    jr        nz,$0e61                      ;[0e5b] 20 04
                    pop       hl                            ;[0e5d] e1
                    ld        a,$24                         ;[0e5e] 3e 24
                    ret                                     ;[0e60] c9

                    ex        de,hl                         ;[0e61] eb
                    ld        de,$002d                      ;[0e62] 11 2d 00
                    add       iy,de                         ;[0e65] fd 19
                    ex        de,hl                         ;[0e67] eb
                    djnz      $0e4d                         ;[0e68] 10 e3
                    pop       hl                            ;[0e6a] e1
                    xor       a                             ;[0e6b] af
                    ld        (hl),a                        ;[0e6c] 77
                    inc       hl                            ;[0e6d] 23
                    ld        (hl),a                        ;[0e6e] 77
                    scf                                     ;[0e6f] 37
                    ret                                     ;[0e70] c9

                    call      $09fe                         ;[0e71] cd fe 09
                    ex        de,hl                         ;[0e74] eb
                    ld        e,(hl)                        ;[0e75] 5e
                    inc       hl                            ;[0e76] 23
                    ld        d,(hl)                        ;[0e77] 56
                    push      de                            ;[0e78] d5
                    pop       iy                            ;[0e79] fd e1
                    ld        a,(iy+$10)                    ;[0e7b] fd 7e 10
                    res       1,a                           ;[0e7e] cb 8f
                    push      af                            ;[0e80] f5
                    add       $30                           ;[0e81] c6 30
                    ld        (bc),a                        ;[0e83] 02
                    inc       bc                            ;[0e84] 03
                    ld        a,$3e                         ;[0e85] 3e 3e
                    ld        (bc),a                        ;[0e87] 02
                    inc       bc                            ;[0e88] 03
                    pop       af                            ;[0e89] f1
                    push      bc                            ;[0e8a] c5
                    ld        ($200f),a                     ;[0e8b] 32 0f 20
                    rst       $28                           ;[0e8e] ef
                    ld        de,$0021                      ;[0e8f] 11 21 00
                    ld        ($cde5),hl                    ;[0e92] 22 e5 cd
                    ld        a,(de)                        ;[0e95] 1a
                    dec       c                             ;[0e96] 0d
                    pop       hl                            ;[0e97] e1
                    pop       de                            ;[0e98] d1
                    ret       nc                            ;[0e99] d0
                    push      bc                            ;[0e9a] c5
                    ld        bc,$0010                      ;[0e9b] 01 10 00
                    ldir                                    ;[0e9e] ed b0
                    ex        de,hl                         ;[0ea0] eb
                    ld        (hl),$ff                      ;[0ea1] 36 ff
                    pop       bc                            ;[0ea3] c1
                    ld        a,($200f)                     ;[0ea4] 3a 0f 20
                    ret                                     ;[0ea7] c9

                    push      ix                            ;[0ea8] dd e5
                    push      iy                            ;[0eaa] fd e5
                    ld        b,$10                         ;[0eac] 06 10
                    push      bc                            ;[0eae] c5
                    ld        a,b                           ;[0eaf] 78
                    dec       a                             ;[0eb0] 3d
                    call      $1354                         ;[0eb1] cd 54 13
                    ld        a,(iy+$0f)                    ;[0eb4] fd 7e 0f
                    and       a                             ;[0eb7] a7
                    call      nz,$16cf                      ;[0eb8] c4 cf 16
                    pop       bc                            ;[0ebb] c1
                    djnz      $0eae                         ;[0ebc] 10 f0
                    pop       iy                            ;[0ebe] fd e1
                    pop       ix                            ;[0ec0] dd e1
                    jp        $08d1                         ;[0ec2] c3 d1 08
                    bit       6,(ix+$13)                    ;[0ec5] dd cb 13 76
                    scf                                     ;[0ec9] 37
                    ret       z                             ;[0eca] c8
                    push      hl                            ;[0ecb] e5
                    push      de                            ;[0ecc] d5
                    push      bc                            ;[0ecd] c5
                    call      $04e1                         ;[0ece] cd e1 04
                    ld        sp,$0001                      ;[0ed1] 31 01 00
                    nop                                     ;[0ed4] 00
                    call      $0832                         ;[0ed5] cd 32 08
                    jr        nc,$0eef                      ;[0ed8] 30 15
                    ld        de,$01e8                      ;[0eda] 11 e8 01
                    add       hl,de                         ;[0edd] 19
                    ex        de,hl                         ;[0ede] eb
                    push      ix                            ;[0edf] dd e5
                    pop       hl                            ;[0ee1] e1
                    ld        bc,$002b                      ;[0ee2] 01 2b 00
                    add       hl,bc                         ;[0ee5] 09
                    ld        bc,$0008                      ;[0ee6] 01 08 00
                    ldir                                    ;[0ee9] ed b0
                    call      $08e2                         ;[0eeb] cd e2 08
                    scf                                     ;[0eee] 37
                    pop       bc                            ;[0eef] c1
                    pop       de                            ;[0ef0] d1
                    pop       hl                            ;[0ef1] e1
                    ret                                     ;[0ef2] c9

                    ld        ix,$0000                      ;[0ef3] dd 21 00 00
                    ret                                     ;[0ef7] c9

                    ld        ix,$00fc                      ;[0ef8] dd 21 fc 00
                    ld        hl,$17f7                      ;[0efc] 21 f7 17
                    jp        $03f8                         ;[0eff] c3 f8 03
                    ld        c,(hl)                        ;[0f02] 4e
                    ld        c,a                           ;[0f03] 4f
                    jr        nz,$0f54                      ;[0f04] 20 4e
                    ld        b,c                           ;[0f06] 41
                    ld        c,l                           ;[0f07] 4d
                    ld        b,l                           ;[0f08] 45
                    jr        nz,$0f2b                      ;[0f09] 20 20
                    jr        nz,$0f2d                      ;[0f0b] 20 20
                    push      hl                            ;[0f0d] e5
                    xor       a                             ;[0f0e] af
                    ld        b,$0b                         ;[0f0f] 06 0b
                    rrca                                    ;[0f11] 0f
                    add       (hl)                          ;[0f12] 86
                    inc       hl                            ;[0f13] 23
                    djnz      $0f11                         ;[0f14] 10 fb
                    pop       hl                            ;[0f16] e1
                    ret                                     ;[0f17] c9

                    ld        bc,$001f                      ;[0f18] 01 1f 00
                    add       hl,bc                         ;[0f1b] 09
                    ld        b,$02                         ;[0f1c] 06 02
                    call      $0f2d                         ;[0f1e] cd 2d 0f
                    dec       hl                            ;[0f21] 2b
                    dec       hl                            ;[0f22] 2b
                    ld        b,$06                         ;[0f23] 06 06
                    call      $0f2d                         ;[0f25] cd 2d 0f
                    dec       hl                            ;[0f28] 2b
                    dec       hl                            ;[0f29] 2b
                    dec       hl                            ;[0f2a] 2b
                    ld        b,$05                         ;[0f2b] 06 05
                    ld        a,(hl)                        ;[0f2d] 7e
                    dec       hl                            ;[0f2e] 2b
                    ld        c,(hl)                        ;[0f2f] 4e
                    dec       hl                            ;[0f30] 2b
                    and       a                             ;[0f31] a7
                    jr        z,$0f3a                       ;[0f32] 28 06
                    and       c                             ;[0f34] a1
                    inc       a                             ;[0f35] 3c
                    jr        z,$0f4c                       ;[0f36] 28 14
                    jr        $0f48                         ;[0f38] 18 0e
                    ld        a,c                           ;[0f3a] 79
                    and       a                             ;[0f3b] a7
                    jr        z,$0f4c                       ;[0f3c] 28 0e
                    cp        $20                           ;[0f3e] fe 20
                    jr        nc,$0f44                      ;[0f40] 30 02
                    add       $e0                           ;[0f42] c6 e0
                    cp        $80                           ;[0f44] fe 80
                    jr        c,$0f4a                       ;[0f46] 38 02
                    ld        a,$5f                         ;[0f48] 3e 5f
                    dec       de                            ;[0f4a] 1b
                    ld        (de),a                        ;[0f4b] 12
                    djnz      $0f2d                         ;[0f4c] 10 df
                    ret                                     ;[0f4e] c9

                    dec       c                             ;[0f4f] 0d
                    ld        a,c                           ;[0f50] 79
                    jr        nz,$0f5f                      ;[0f51] 20 0c
                    ld        a,(de)                        ;[0f53] 1a
                    inc       de                            ;[0f54] 13
                    inc       a                             ;[0f55] 3c
                    jr        nz,$0f5e                      ;[0f56] 20 06
                    ld        (hl),a                        ;[0f58] 77
                    inc       hl                            ;[0f59] 23
                    ld        (hl),a                        ;[0f5a] 77
                    inc       hl                            ;[0f5b] 23
                    jr        $0f64                         ;[0f5c] 18 06
                    dec       a                             ;[0f5e] 3d
                    ld        (hl),a                        ;[0f5f] 77
                    inc       hl                            ;[0f60] 23
                    ld        (hl),c                        ;[0f61] 71
                    inc       hl                            ;[0f62] 23
                    inc       c                             ;[0f63] 0c
                    djnz      $0f4f                         ;[0f64] 10 e9
                    ret                                     ;[0f66] c9

                    push      de                            ;[0f67] d5
                    ld        bc,$0b00                      ;[0f68] 01 00 0b
                    dec       hl                            ;[0f6b] 2b
                    inc       hl                            ;[0f6c] 23
                    ld        a,(hl)                        ;[0f6d] 7e
                    cp        $ff                           ;[0f6e] fe ff
                    jr        z,$0fbf                       ;[0f70] 28 4d
                    cp        $20                           ;[0f72] fe 20
                    jr        z,$0f6c                       ;[0f74] 28 f6
                    cp        $2e                           ;[0f76] fe 2e
                    jr        z,$0f6c                       ;[0f78] 28 f2
                    ld        b,$08                         ;[0f7a] 06 08
                    call      $0ffb                         ;[0f7c] cd fb 0f
                    jr        z,$0fbc                       ;[0f7f] 28 3b
                    call      $10b8                         ;[0f81] cd b8 10
                    ld        b,$03                         ;[0f84] 06 03
                    jr        c,$0f97                       ;[0f86] 38 0f
                    call      $1059                         ;[0f88] cd 59 10
                    jr        c,$0f9e                       ;[0f8b] 38 11
                    push      hl                            ;[0f8d] e5
                    call      $1059                         ;[0f8e] cd 59 10
                    jr        c,$0f96                       ;[0f91] 38 03
                    pop       af                            ;[0f93] f1
                    jr        $0f8d                         ;[0f94] 18 f7
                    pop       hl                            ;[0f96] e1
                    call      $0ffb                         ;[0f97] cd fb 0f
                    jr        z,$0f9e                       ;[0f9a] 28 02
                    ld        c,$01                         ;[0f9c] 0e 01
                    call      $10b8                         ;[0f9e] cd b8 10
                    ld        a,$ff                         ;[0fa1] 3e ff
                    ld        (de),a                        ;[0fa3] 12
                    pop       hl                            ;[0fa4] e1
                    ld        b,$07                         ;[0fa5] 06 07
                    ld        a,(hl)                        ;[0fa7] 7e
                    inc       hl                            ;[0fa8] 23
                    cp        $20                           ;[0fa9] fe 20
                    jr        z,$0fb0                       ;[0fab] 28 03
                    djnz      $0fa7                         ;[0fad] 10 f8
                    inc       b                             ;[0faf] 04
                    dec       hl                            ;[0fb0] 2b
                    dec       c                             ;[0fb1] 0d
                    ld        c,$00                         ;[0fb2] 0e 00
                    ret       nz                            ;[0fb4] c0
                    ld        (hl),$7e                      ;[0fb5] 36 7e
                    inc       hl                            ;[0fb7] 23
                    ld        (hl),$31                      ;[0fb8] 36 31
                    inc       c                             ;[0fba] 0c
                    ret                                     ;[0fbb] c9

                    inc       b                             ;[0fbc] 04
                    inc       b                             ;[0fbd] 04
                    inc       b                             ;[0fbe] 04
                    call      $10b8                         ;[0fbf] cd b8 10
                    jr        $0fa1                         ;[0fc2] 18 dd
                    ld        a,c                           ;[0fc4] 79
                    and       a                             ;[0fc5] a7
                    jr        z,$0fb5                       ;[0fc6] 28 ed
                    push      hl                            ;[0fc8] e5
                    push      bc                            ;[0fc9] c5
                    inc       (hl)                          ;[0fca] 34
                    ld        a,(hl)                        ;[0fcb] 7e
                    cp        $3a                           ;[0fcc] fe 3a
                    jr        c,$0ff5                       ;[0fce] 38 25
                    ld        (hl),$30                      ;[0fd0] 36 30
                    dec       hl                            ;[0fd2] 2b
                    dec       c                             ;[0fd3] 0d
                    jr        nz,$0fca                      ;[0fd4] 20 f4
                    pop       bc                            ;[0fd6] c1
                    pop       af                            ;[0fd7] f1
                    ld        a,c                           ;[0fd8] 79
                    cp        b                             ;[0fd9] b8
                    jr        z,$0fe8                       ;[0fda] 28 0c
                    inc       c                             ;[0fdc] 0c
                    inc       hl                            ;[0fdd] 23
                    ld        (hl),$31                      ;[0fde] 36 31
                    inc       hl                            ;[0fe0] 23
                    ld        (hl),$30                      ;[0fe1] 36 30
                    dec       a                             ;[0fe3] 3d
                    jr        nz,$0fe0                      ;[0fe4] 20 fa
                    scf                                     ;[0fe6] 37
                    ret                                     ;[0fe7] c9

                    ld        a,b                           ;[0fe8] 78
                    cp        $06                           ;[0fe9] fe 06
                    ld        a,$1b                         ;[0feb] 3e 1b
                    ret       z                             ;[0fed] c8
                    inc       b                             ;[0fee] 04
                    dec       hl                            ;[0fef] 2b
                    ld        (hl),$7e                      ;[0ff0] 36 7e
                    ld        a,c                           ;[0ff2] 79
                    jr        $0fdc                         ;[0ff3] 18 e7
                    pop       bc                            ;[0ff5] c1
                    pop       hl                            ;[0ff6] e1
                    scf                                     ;[0ff7] 37
                    ret                                     ;[0ff8] c9

                    ld        c,$01                         ;[0ff9] 0e 01
                    ld        a,(hl)                        ;[0ffb] 7e
                    inc       hl                            ;[0ffc] 23
                    cp        $ff                           ;[0ffd] fe ff
                    ret       z                             ;[0fff] c8
                    cp        $2e                           ;[1000] fe 2e
                    jr        z,$104f                       ;[1002] 28 4b
                    cp        $20                           ;[1004] fe 20
                    jr        z,$0ff9                       ;[1006] 28 f1
                    jr        c,$1032                       ;[1008] 38 28
                    cp        $80                           ;[100a] fe 80
                    jr        nc,$1032                      ;[100c] 30 24
                    cp        $2b                           ;[100e] fe 2b
                    jr        z,$1032                       ;[1010] 28 20
                    cp        $2c                           ;[1012] fe 2c
                    jr        z,$1032                       ;[1014] 28 1c
                    cp        $3b                           ;[1016] fe 3b
                    jr        z,$1032                       ;[1018] 28 18
                    cp        $3d                           ;[101a] fe 3d
                    jr        z,$1032                       ;[101c] 28 14
                    cp        $5b                           ;[101e] fe 5b
                    jr        z,$1032                       ;[1020] 28 10
                    cp        $5d                           ;[1022] fe 5d
                    jr        z,$1032                       ;[1024] 28 0c
                    cp        $61                           ;[1026] fe 61
                    jr        c,$1036                       ;[1028] 38 0c
                    cp        $7b                           ;[102a] fe 7b
                    jr        nc,$1036                      ;[102c] 30 08
                    and       $df                           ;[102e] e6 df
                    jr        $1036                         ;[1030] 18 04
                    ld        a,$5f                         ;[1032] 3e 5f
                    ld        c,$01                         ;[1034] 0e 01
                    ld        (de),a                        ;[1036] 12
                    inc       de                            ;[1037] 13
                    djnz      $0ffb                         ;[1038] 10 c1
                    ld        a,(hl)                        ;[103a] 7e
                    inc       hl                            ;[103b] 23
                    cp        $ff                           ;[103c] fe ff
                    ret       z                             ;[103e] c8
                    cp        $2e                           ;[103f] fe 2e
                    jr        nz,$1049                      ;[1041] 20 06
                    push      hl                            ;[1043] e5
                    call      $1059                         ;[1044] cd 59 10
                    pop       hl                            ;[1047] e1
                    ret       c                             ;[1048] d8
                    dec       hl                            ;[1049] 2b
                    xor       a                             ;[104a] af
                    inc       a                             ;[104b] 3c
                    ld        c,$01                         ;[104c] 0e 01
                    ret                                     ;[104e] c9

                    push      hl                            ;[104f] e5
                    call      $1059                         ;[1050] cd 59 10
                    pop       hl                            ;[1053] e1
                    ret       c                             ;[1054] d8
                    ld        c,$01                         ;[1055] 0e 01
                    jr        $0ffb                         ;[1057] 18 a2
                    ld        a,(hl)                        ;[1059] 7e
                    inc       hl                            ;[105a] 23
                    cp        $ff                           ;[105b] fe ff
                    jr        z,$1064                       ;[105d] 28 05
                    cp        $2e                           ;[105f] fe 2e
                    jr        nz,$1059                      ;[1061] 20 f6
                    ret                                     ;[1063] c9

                    dec       a                             ;[1064] 3d
                    scf                                     ;[1065] 37
                    ret                                     ;[1066] c9

                    push      de                            ;[1067] d5
                    bit       7,(iy+$0d)                    ;[1068] fd cb 0d 7e
                    jr        z,$107f                       ;[106c] 28 11
                    ld        de,($2f26)                    ;[106e] ed 5b 26 2f
                    ld        hl,$302d                      ;[1072] 21 2d 30
                    and       a                             ;[1075] a7
                    sbc       hl,de                         ;[1076] ed 52
                    ld        b,h                           ;[1078] 44
                    ld        c,l                           ;[1079] 4d
                    ex        de,hl                         ;[107a] eb
                    pop       de                            ;[107b] d1
                    ldir                                    ;[107c] ed b0
                    ret                                     ;[107e] c9

                    call      $11ab                         ;[107f] cd ab 11
                    pop       de                            ;[1082] d1
                    ld        b,$08                         ;[1083] 06 08
                    call      $109e                         ;[1085] cd 9e 10
                    ld        a,$2e                         ;[1088] 3e 2e
                    ld        (de),a                        ;[108a] 12
                    inc       de                            ;[108b] 13
                    ld        b,$03                         ;[108c] 06 03
                    call      $109e                         ;[108e] cd 9e 10
                    dec       de                            ;[1091] 1b
                    ld        a,(de)                        ;[1092] 1a
                    cp        $2e                           ;[1093] fe 2e
                    jr        z,$1098                       ;[1095] 28 01
                    inc       de                            ;[1097] 13
                    ld        a,$ff                         ;[1098] 3e ff
                    ld        (de),a                        ;[109a] 12
                    inc       de                            ;[109b] 13
                    scf                                     ;[109c] 37
                    ret                                     ;[109d] c9

                    ld        c,$00                         ;[109e] 0e 00
                    ld        a,(hl)                        ;[10a0] 7e
                    res       7,a                           ;[10a1] cb bf
                    inc       hl                            ;[10a3] 23
                    ld        (de),a                        ;[10a4] 12
                    inc       de                            ;[10a5] 13
                    inc       c                             ;[10a6] 0c
                    cp        $20                           ;[10a7] fe 20
                    jr        z,$10ad                       ;[10a9] 28 02
                    ld        c,$00                         ;[10ab] 0e 00
                    djnz      $10a0                         ;[10ad] 10 f1
                    inc       c                             ;[10af] 0c
                    dec       c                             ;[10b0] 0d
                    ret       z                             ;[10b1] c8
                    and       a                             ;[10b2] a7
                    ex        de,hl                         ;[10b3] eb
                    sbc       hl,bc                         ;[10b4] ed 42
                    ex        de,hl                         ;[10b6] eb
                    ret                                     ;[10b7] c9

                    ld        a,$20                         ;[10b8] 3e 20
                    inc       b                             ;[10ba] 04
                    dec       b                             ;[10bb] 05
                    ret       z                             ;[10bc] c8
                    ld        (de),a                        ;[10bd] 12
                    inc       de                            ;[10be] 13
                    jr        $10bb                         ;[10bf] 18 fa
                    ld        iy,$2e50                      ;[10c1] fd 21 50 2e
                    ld        ix,($2e50)                    ;[10c5] dd 2a 50 2e
                    ret                                     ;[10c9] c9

                    ld        a,ixl                         ;[10ca] dd 7d
                    ld        (iy+$00),a                    ;[10cc] fd 77 00
                    ld        a,ixh                         ;[10cf] dd 7c
                    ld        (iy+$01),a                    ;[10d1] fd 77 01
                    bit       7,(ix+$13)                    ;[10d4] dd cb 13 7e
                    jr        nz,$10ec                      ;[10d8] 20 12
                    res       7,(iy+$02)                    ;[10da] fd cb 02 be
                    push      bc                            ;[10de] c5
                    push      de                            ;[10df] d5
                    call      $04e1                         ;[10e0] cd e1 04
                    dec       e                             ;[10e3] 1d
                    call      $0500                         ;[10e4] cd 00 05
                    inc       bc                            ;[10e7] 03
                    pop       de                            ;[10e8] d1
                    pop       bc                            ;[10e9] c1
                    jr        $10f7                         ;[10ea] 18 0b
                    set       7,(iy+$02)                    ;[10ec] fd cb 02 fe
                    xor       a                             ;[10f0] af
                    ld        (iy+$03),a                    ;[10f1] fd 77 03
                    ld        (iy+$04),a                    ;[10f4] fd 77 04
                    push      bc                            ;[10f7] c5
                    push      de                            ;[10f8] d5
                    rst       $28                           ;[10f9] ef
                    inc       bc                            ;[10fa] 03
                    call      $0500                         ;[10fb] cd 00 05
                    ex        af,af'                        ;[10fe] 08
                    pop       de                            ;[10ff] d1
                    pop       bc                            ;[1100] c1
                    xor       a                             ;[1101] af
                    ld        (iy+$07),a                    ;[1102] fd 77 07
                    ld        (iy+$0c),a                    ;[1105] fd 77 0c
                    scf                                     ;[1108] 37
                    ret                                     ;[1109] c9

                    call      $11ab                         ;[110a] cd ab 11
                    ret       nc                            ;[110d] d0
                    jr        z,$113b                       ;[110e] 28 2b
                    bit       4,a                           ;[1110] cb 67
                    jr        z,$113b                       ;[1112] 28 27
                    ld        bc,$001a                      ;[1114] 01 1a 00
                    add       hl,bc                         ;[1117] 09
                    ld        c,(hl)                        ;[1118] 4e
                    inc       hl                            ;[1119] 23
                    ld        b,(hl)                        ;[111a] 46
                    ld        de,$0000                      ;[111b] 11 00 00
                    bit       7,(ix+$13)                    ;[111e] dd cb 13 7e
                    jr        nz,$112b                      ;[1122] 20 07
                    ld        de,$fffa                      ;[1124] 11 fa ff
                    add       hl,de                         ;[1127] 19
                    ld        d,(hl)                        ;[1128] 56
                    dec       hl                            ;[1129] 2b
                    ld        e,(hl)                        ;[112a] 5e
                    ld        a,d                           ;[112b] 7a
                    or        e                             ;[112c] b3
                    or        b                             ;[112d] b0
                    or        c                             ;[112e] b1
                    jr        z,$10ca                       ;[112f] 28 99
                    call      $0500                         ;[1131] cd 00 05
                    inc       bc                            ;[1134] 03
                    res       7,(iy+$02)                    ;[1135] fd cb 02 be
                    jr        $10f7                         ;[1139] 18 bc
                    ld        a,$45                         ;[113b] 3e 45
                    and       a                             ;[113d] a7
                    ret                                     ;[113e] c9

                    inc       (iy+$0c)                      ;[113f] fd 34 0c
                    bit       4,(iy+$0c)                    ;[1142] fd cb 0c 66
                    scf                                     ;[1146] 37
                    ret       z                             ;[1147] c8
                    bit       7,(iy+$02)                    ;[1148] fd cb 02 7e
                    jr        nz,$115a                      ;[114c] 20 0c
                    ld        bc,$0007                      ;[114e] 01 07 00
                    call      $0577                         ;[1151] cd 77 05
                    ret       nc                            ;[1154] d0
                    ld        (iy+$0c),$00                  ;[1155] fd 36 0c 00
                    ret                                     ;[1159] c9

                    rst       $28                           ;[115a] ef
                    ex        af,af'                        ;[115b] 08
                    inc       bc                            ;[115c] 03
                    ld        l,(ix+$1b)                    ;[115d] dd 6e 1b
                    ld        h,(ix+$1c)                    ;[1160] dd 66 1c
                    dec       hl                            ;[1163] 2b
                    and       a                             ;[1164] a7
                    sbc       hl,bc                         ;[1165] ed 42
                    ld        a,$17                         ;[1167] 3e 17
                    ccf                                     ;[1169] 3f
                    ret       nc                            ;[116a] d0
                    ld        (iy+$08),c                    ;[116b] fd 71 08
                    ld        (iy+$09),b                    ;[116e] fd 70 09
                    ld        (iy+$0c),$00                  ;[1171] fd 36 0c 00
                    ret                                     ;[1175] c9

                    bit       7,(iy+$02)                    ;[1176] fd cb 02 7e
                    jr        nz,$1193                      ;[117a] 20 17
                    rst       $28                           ;[117c] ef
                    ex        af,af'                        ;[117d] 08
                    ld        a,(iy+$07)                    ;[117e] fd 7e 07
                    call      $06ee                         ;[1181] cd ee 06
                    ret       nc                            ;[1184] d0
                    ld        e,(iy+$0c)                    ;[1185] fd 5e 0c
                    ld        d,$00                         ;[1188] 16 00
                    ex        de,hl                         ;[118a] eb
                    add       hl,hl                         ;[118b] 29
                    add       hl,hl                         ;[118c] 29
                    add       hl,hl                         ;[118d] 29
                    add       hl,hl                         ;[118e] 29
                    add       hl,hl                         ;[118f] 29
                    add       hl,de                         ;[1190] 19
                    scf                                     ;[1191] 37
                    ret                                     ;[1192] c9

                    call      $04e1                         ;[1193] cd e1 04
                    dec       e                             ;[1196] 1d
                    ld        h,b                           ;[1197] 60
                    ld        l,c                           ;[1198] 69
                    push      de                            ;[1199] d5
                    rst       $28                           ;[119a] ef
                    ex        af,af'                        ;[119b] 08
                    pop       de                            ;[119c] d1
                    add       hl,bc                         ;[119d] 09
                    ex        de,hl                         ;[119e] eb
                    ld        bc,$0000                      ;[119f] 01 00 00
                    adc       hl,bc                         ;[11a2] ed 4a
                    ld        c,l                           ;[11a4] 4d
                    ld        b,h                           ;[11a5] 44
                    call      $0832                         ;[11a6] cd 32 08
                    jr        $1184                         ;[11a9] 18 d9
                    call      $1176                         ;[11ab] cd 76 11
                    ret       nc                            ;[11ae] d0
                    ld        a,(hl)                        ;[11af] 7e
                    and       a                             ;[11b0] a7
                    scf                                     ;[11b1] 37
                    ret       z                             ;[11b2] c8
                    cp        $e5                           ;[11b3] fe e5
                    scf                                     ;[11b5] 37
                    ret       z                             ;[11b6] c8
                    push      hl                            ;[11b7] e5
                    ld        de,$000b                      ;[11b8] 11 0b 00
                    add       hl,de                         ;[11bb] 19
                    ld        b,a                           ;[11bc] 47
                    ld        a,(hl)                        ;[11bd] 7e
                    inc       hl                            ;[11be] 23
                    inc       hl                            ;[11bf] 23
                    ld        c,(hl)                        ;[11c0] 4e
                    pop       hl                            ;[11c1] e1
                    scf                                     ;[11c2] 37
                    ret                                     ;[11c3] c9

                    call      $10f7                         ;[11c4] cd f7 10
                    ld        d,$00                         ;[11c7] 16 00
                    push      de                            ;[11c9] d5
                    call      $11ab                         ;[11ca] cd ab 11
                    pop       de                            ;[11cd] d1
                    ret       nc                            ;[11ce] d0
                    jr        nz,$11e3                      ;[11cf] 20 12
                    dec       d                             ;[11d1] 15
                    inc       d                             ;[11d2] 14
                    jr        nz,$11dc                      ;[11d3] 20 07
                    push      de                            ;[11d5] d5
                    call      $120b                         ;[11d6] cd 0b 12
                    ldir                                    ;[11d9] ed b0
                    pop       de                            ;[11db] d1
                    inc       d                             ;[11dc] 14
                    ld        a,d                           ;[11dd] 7a
                    cp        e                             ;[11de] bb
                    jr        nc,$1203                      ;[11df] 30 22
                    jr        $11e5                         ;[11e1] 18 02
                    ld        d,$00                         ;[11e3] 16 00
                    push      de                            ;[11e5] d5
                    call      $113f                         ;[11e6] cd 3f 11
                    jr        c,$11ca                       ;[11e9] 38 df
                    bit       7,(iy+$02)                    ;[11eb] fd cb 02 7e
                    jr        nz,$1201                      ;[11ef] 20 10
                    rst       $28                           ;[11f1] ef
                    ex        af,af'                        ;[11f2] 08
                    call      $063e                         ;[11f3] cd 3e 06
                    call      c,$0688                       ;[11f6] dc 88 06
                    call      c,$08d1                       ;[11f9] dc d1 08
                    call      c,$114e                       ;[11fc] dc 4e 11
                    jr        c,$11ca                       ;[11ff] 38 c9
                    pop       de                            ;[1201] d1
                    ret                                     ;[1202] c9

                    call      $120b                         ;[1203] cd 0b 12
                    ex        de,hl                         ;[1206] eb
                    ldir                                    ;[1207] ed b0
                    jr        $11ab                         ;[1209] 18 a0
                    push      iy                            ;[120b] fd e5
                    pop       hl                            ;[120d] e1
                    ld        bc,$0007                      ;[120e] 01 07 00
                    add       hl,bc                         ;[1211] 09
                    ld        de,$201a                      ;[1212] 11 1a 20
                    ld        bc,$0006                      ;[1215] 01 06 00
                    ret                                     ;[1218] c9

                    ld        iy,$2e50                      ;[1219] fd 21 50 2e
                    call      $10ca                         ;[121d] cd ca 10
                    ret       nc                            ;[1220] d0
                    call      $11ab                         ;[1221] cd ab 11
                    ret       nc                            ;[1224] d0
                    jr        z,$1235                       ;[1225] 28 0e
                    ld        e,a                           ;[1227] 5f
                    and       $3f                           ;[1228] e6 3f
                    cp        $0f                           ;[122a] fe 0f
                    jr        z,$1235                       ;[122c] 28 07
                    ld        a,e                           ;[122e] 7b
                    and       $18                           ;[122f] e6 18
                    cp        $08                           ;[1231] fe 08
                    scf                                     ;[1233] 37
                    ret       z                             ;[1234] c8
                    call      $113f                         ;[1235] cd 3f 11
                    jr        c,$1221                       ;[1238] 38 e7
                    xor       a                             ;[123a] af
                    inc       a                             ;[123b] 3c
                    scf                                     ;[123c] 37
                    ret                                     ;[123d] c9

                    ld        (iy+$0d),$00                  ;[123e] fd 36 0d 00
                    jr        $124c                         ;[1242] 18 08
                    ld        (iy+$0d),$00                  ;[1244] fd 36 0d 00
                    call      $113f                         ;[1248] cd 3f 11
                    ret       nc                            ;[124b] d0
                    call      $11ab                         ;[124c] cd ab 11
                    ret       nc                            ;[124f] d0
                    jr        nz,$1258                      ;[1250] 20 06
                    and       a                             ;[1252] a7
                    jr        nz,$1244                      ;[1253] 20 ef
                    ld        a,$17                         ;[1255] 3e 17
                    ret                                     ;[1257] c9

                    ld        e,a                           ;[1258] 5f
                    and       $3f                           ;[1259] e6 3f
                    cp        $0f                           ;[125b] fe 0f
                    jr        nz,$129d                      ;[125d] 20 3e
                    ld        a,b                           ;[125f] 78
                    bit       6,a                           ;[1260] cb 77
                    jr        nz,$127f                      ;[1262] 20 1b
                    dec       (iy+$0d)                      ;[1264] fd 35 0d
                    cp        (iy+$0d)                      ;[1267] fd be 0d
                    jr        nz,$1244                      ;[126a] 20 d8
                    ld        a,c                           ;[126c] 79
                    cp        (iy+$0e)                      ;[126d] fd be 0e
                    jr        nz,$1244                      ;[1270] 20 d2
                    ld        de,($2f26)                    ;[1272] ed 5b 26 2f
                    call      $0f18                         ;[1276] cd 18 0f
                    ld        ($2f26),de                    ;[1279] ed 53 26 2f
                    jr        $1248                         ;[127d] 18 c9
                    res       6,a                           ;[127f] cb b7
                    cp        $15                           ;[1281] fe 15
                    jr        nc,$1244                      ;[1283] 30 bf
                    ld        (iy+$0d),a                    ;[1285] fd 77 0d
                    ld        (iy+$0e),c                    ;[1288] fd 71 0e
                    push      hl                            ;[128b] e5
                    call      $120b                         ;[128c] cd 0b 12
                    ld        de,$2020                      ;[128f] 11 20 20
                    ldir                                    ;[1292] ed b0
                    pop       hl                            ;[1294] e1
                    ld        de,$302c                      ;[1295] 11 2c 30
                    ld        a,$ff                         ;[1298] 3e ff
                    ld        (de),a                        ;[129a] 12
                    jr        $1276                         ;[129b] 18 d9
                    ld        a,e                           ;[129d] 7b
                    and       $18                           ;[129e] e6 18
                    cp        $08                           ;[12a0] fe 08
                    jr        z,$1244                       ;[12a2] 28 a0
                    dec       (iy+$0d)                      ;[12a4] fd 35 0d
                    ld        (iy+$0d),$00                  ;[12a7] fd 36 0d 00
                    scf                                     ;[12ab] 37
                    ret       nz                            ;[12ac] c0
                    call      $0f0d                         ;[12ad] cd 0d 0f
                    cp        (iy+$0e)                      ;[12b0] fd be 0e
                    scf                                     ;[12b3] 37
                    ret       nz                            ;[12b4] c0
                    dec       (iy+$0d)                      ;[12b5] fd 35 0d
                    ret                                     ;[12b8] c9

                    call      $10c1                         ;[12b9] cd c1 10
                    djnz      $12ca                         ;[12bc] 10 0c
                    call      $0a1a                         ;[12be] cd 1a 0a
                    call      $10f7                         ;[12c1] cd f7 10
                    ld        hl,$2e50                      ;[12c4] 21 50 2e
                    ld        a,$0f                         ;[12c7] 3e 0f
                    ret                                     ;[12c9] c9

                    djnz      $12dc                         ;[12ca] 10 10
                    call      $0a0a                         ;[12cc] cd 0a 0a
                    push      iy                            ;[12cf] fd e5
                    pop       de                            ;[12d1] d1
                    ld        hl,$2e50                      ;[12d2] 21 50 2e
                    ld        bc,$000f                      ;[12d5] 01 0f 00
                    ldir                                    ;[12d8] ed b0
                    scf                                     ;[12da] 37
                    ret                                     ;[12db] c9

                    dec       b                             ;[12dc] 05
                    jp        z,$10ca                       ;[12dd] ca ca 10
                    djnz      $12ef                         ;[12e0] 10 0d
                    ld        hl,$2e50                      ;[12e2] 21 50 2e
                    ld        de,$2e5f                      ;[12e5] 11 5f 2e
                    ld        bc,$000f                      ;[12e8] 01 0f 00
                    ldir                                    ;[12eb] ed b0
                    scf                                     ;[12ed] 37
                    ret                                     ;[12ee] c9

                    dec       b                             ;[12ef] 05
                    jp        z,$110a                       ;[12f0] ca 0a 11
                    jp        $0014                         ;[12f3] c3 14 00
                    push      hl                            ;[12f6] e5
                    call      $1315                         ;[12f7] cd 15 13
                    ex        (sp),ix                       ;[12fa] dd e3
                    ld        bc,$0000                      ;[12fc] 01 00 00
                    ld        de,$0000                      ;[12ff] 11 00 00
                    jr        c,$1306                       ;[1302] 38 02
                    rst       $28                           ;[1304] ef
                    inc       bc                            ;[1305] 03
                    ld        (ix+$3a),c                    ;[1306] dd 71 3a
                    ld        (ix+$3b),b                    ;[1309] dd 70 3b
                    ld        (ix+$34),e                    ;[130c] dd 73 34
                    ld        (ix+$35),d                    ;[130f] dd 72 35
                    pop       ix                            ;[1312] dd e1
                    ret                                     ;[1314] c9

                    bit       7,(ix+$13)                    ;[1315] dd cb 13 7e
                    jr        nz,$1336                      ;[1319] 20 1b
                    push      hl                            ;[131b] e5
                    push      de                            ;[131c] d5
                    push      bc                            ;[131d] c5
                    call      $04e1                         ;[131e] cd e1 04
                    dec       e                             ;[1321] 1d
                    push      de                            ;[1322] d5
                    push      bc                            ;[1323] c5
                    rst       $28                           ;[1324] ef
                    inc       bc                            ;[1325] 03
                    pop       hl                            ;[1326] e1
                    and       a                             ;[1327] a7
                    sbc       hl,bc                         ;[1328] ed 42
                    pop       hl                            ;[132a] e1
                    jr        nz,$132f                      ;[132b] 20 02
                    sbc       hl,de                         ;[132d] ed 52
                    pop       bc                            ;[132f] c1
                    pop       de                            ;[1330] d1
                    pop       hl                            ;[1331] e1
                    scf                                     ;[1332] 37
                    ret       z                             ;[1333] c8
                    ccf                                     ;[1334] 3f
                    ret                                     ;[1335] c9

                    bit       7,(iy+$02)                    ;[1336] fd cb 02 7e
                    scf                                     ;[133a] 37
                    ret       nz                            ;[133b] c0
                    ccf                                     ;[133c] 3f
                    ret                                     ;[133d] c9

                    add       hl,$001a                      ;[133e] ed 34 1a 00
                    ld        a,(hl)                        ;[1342] 7e
                    inc       hl                            ;[1343] 23
                    cp        c                             ;[1344] b9
                    ret       nz                            ;[1345] c0
                    ld        a,(hl)                        ;[1346] 7e
                    cp        b                             ;[1347] b8
                    ret       nz                            ;[1348] c0
                    add       hl,$fffa                      ;[1349] ed 34 fa ff
                    ld        a,(hl)                        ;[134d] 7e
                    dec       hl                            ;[134e] 2b
                    cp        d                             ;[134f] ba
                    ret       nz                            ;[1350] c0
                    ld        a,(hl)                        ;[1351] 7e
                    cp        e                             ;[1352] bb
                    ret                                     ;[1353] c9

                    ld        b,a                           ;[1354] 47
                    push      de                            ;[1355] d5
                    ld        iy,$2713                      ;[1356] fd 21 13 27
                    ld        de,$002d                      ;[135a] 11 2d 00
                    inc       b                             ;[135d] 04
                    add       iy,de                         ;[135e] fd 19
                    djnz      $135e                         ;[1360] 10 fc
                    ld        a,(iy+$00)                    ;[1362] fd 7e 00
                    ld        ixl,a                         ;[1365] dd 6f
                    ld        a,(iy+$01)                    ;[1367] fd 7e 01
                    ld        ixh,a                         ;[136a] dd 67
                    pop       de                            ;[136c] d1
                    ret                                     ;[136d] c9

                    ld        b,$10                         ;[136e] 06 10
                    ld        hl,$2740                      ;[1370] 21 40 27
                    push      hl                            ;[1373] e5
                    push      bc                            ;[1374] c5
                    ld        d,iyh                         ;[1375] fd 54
                    ld        e,iyl                         ;[1377] fd 5d
                    ld        b,$0f                         ;[1379] 06 0f
                    ld        a,(de)                        ;[137b] 1a
                    cp        (hl)                          ;[137c] be
                    inc       de                            ;[137d] 13
                    inc       hl                            ;[137e] 23
                    jr        nz,$138a                      ;[137f] 20 09
                    djnz      $137b                         ;[1381] 10 f8
                    ld        a,(hl)                        ;[1383] 7e
                    and       a                             ;[1384] a7
                    jr        z,$138a                       ;[1385] 28 03
                    pop       bc                            ;[1387] c1
                    pop       hl                            ;[1388] e1
                    ret                                     ;[1389] c9

                    pop       bc                            ;[138a] c1
                    pop       hl                            ;[138b] e1
                    ld        de,$002d                      ;[138c] 11 2d 00
                    add       hl,de                         ;[138f] 19
                    djnz      $1373                         ;[1390] 10 e1
                    call      $1176                         ;[1392] cd 76 11
                    ret       nc                            ;[1395] d0
                    add       hl,$0014                      ;[1396] ed 34 14 00
                    ld        e,(hl)                        ;[139a] 5e
                    inc       hl                            ;[139b] 23
                    ld        d,(hl)                        ;[139c] 56
                    add       hl,$0005                      ;[139d] ed 34 05 00
                    ld        c,(hl)                        ;[13a1] 4e
                    inc       hl                            ;[13a2] 23
                    ld        b,(hl)                        ;[13a3] 46
                    xor       a                             ;[13a4] af
                    call      $06c3                         ;[13a5] cd c3 06
                    call      $1ed2                         ;[13a8] cd d2 1e
                    ld        b,h                           ;[13ab] 44
                    ld        c,l                           ;[13ac] 4d
                    ld        a,$11                         ;[13ad] 3e 11
                    ld        hl,$2eb4                      ;[13af] 21 b4 2e
                    push      af                            ;[13b2] f5
                    push      hl                            ;[13b3] e5
                    push      de                            ;[13b4] d5
                    push      bc                            ;[13b5] c5
                    ld        a,(hl)                        ;[13b6] 7e
                    inc       hl                            ;[13b7] 23
                    xor       $80                           ;[13b8] ee 80
                    cp        (ix+$10)                      ;[13ba] dd be 10
                    jr        nz,$13db                      ;[13bd] 20 1c
                    ld        c,(hl)                        ;[13bf] 4e
                    inc       hl                            ;[13c0] 23
                    ld        b,(hl)                        ;[13c1] 46
                    inc       hl                            ;[13c2] 23
                    ld        e,(hl)                        ;[13c3] 5e
                    inc       hl                            ;[13c4] 23
                    ld        d,(hl)                        ;[13c5] 56
                    pop       hl                            ;[13c6] e1
                    push      hl                            ;[13c7] e5
                    sbc       hl,de                         ;[13c8] ed 52
                    jr        nz,$13db                      ;[13ca] 20 0f
                    pop       de                            ;[13cc] d1
                    pop       hl                            ;[13cd] e1
                    push      hl                            ;[13ce] e5
                    push      de                            ;[13cf] d5
                    sbc       hl,bc                         ;[13d0] ed 42
                    jr        nz,$13db                      ;[13d2] 20 07
                    pop       bc                            ;[13d4] c1
                    pop       de                            ;[13d5] d1
                    pop       hl                            ;[13d6] e1
                    pop       bc                            ;[13d7] c1
                    ld        a,$03                         ;[13d8] 3e 03
                    ret                                     ;[13da] c9

                    pop       bc                            ;[13db] c1
                    pop       de                            ;[13dc] d1
                    pop       hl                            ;[13dd] e1
                    pop       af                            ;[13de] f1
                    add       hl,$0006                      ;[13df] ed 34 06 00
                    dec       a                             ;[13e3] 3d
                    jr        nz,$13b2                      ;[13e4] 20 cc
                    scf                                     ;[13e6] 37
                    ret                                     ;[13e7] c9

                    rst       $28                           ;[13e8] ef
                    rla                                     ;[13e9] 17
                    push      de                            ;[13ea] d5
                    push      bc                            ;[13eb] c5
                    rst       $28                           ;[13ec] ef
                    rra                                     ;[13ed] 1f
                    pop       hl                            ;[13ee] e1
                    and       a                             ;[13ef] a7
                    sbc       hl,bc                         ;[13f0] ed 42
                    pop       hl                            ;[13f2] e1
                    sbc       hl,de                         ;[13f3] ed 52
                    ret                                     ;[13f5] c9

                    call      $13e8                         ;[13f6] cd e8 13
                    ret       c                             ;[13f9] d8
                    call      $168d                         ;[13fa] cd 8d 16
                    inc       l                             ;[13fd] 2c
                    jr        nz,$1407                      ;[13fe] 20 07
                    inc       h                             ;[1400] 24
                    jr        nz,$1407                      ;[1401] 20 04
                    inc       e                             ;[1403] 1c
                    jr        nz,$1407                      ;[1404] 20 01
                    inc       d                             ;[1406] 14
                    ld        b,h                           ;[1407] 44
                    ld        c,l                           ;[1408] 4d
                    call      $0500                         ;[1409] cd 00 05
                    rra                                     ;[140c] 1f
                    scf                                     ;[140d] 37
                    ret                                     ;[140e] c9

                    call      $1354                         ;[140f] cd 54 13
                    bit       1,(iy+$0f)                    ;[1412] fd cb 0f 4e
                    jr        nz,$1407                      ;[1416] 20 ef
                    ld        a,$1c                         ;[1418] 3e 1c
                    and       a                             ;[141a] a7
                    ret                                     ;[141b] c9

                    inc       (iy+$17)                      ;[141c] fd 34 17
                    jr        nz,$142e                      ;[141f] 20 0d
                    inc       (iy+$18)                      ;[1421] fd 34 18
                    jr        nz,$142e                      ;[1424] 20 08
                    inc       (iy+$19)                      ;[1426] fd 34 19
                    jr        nz,$142e                      ;[1429] 20 03
                    inc       (iy+$1a)                      ;[142b] fd 34 1a
                    bit       6,(iy+$0f)                    ;[142e] fd cb 0f 76
                    scf                                     ;[1432] 37
                    ret       z                             ;[1433] c8
                    inc       (iy+$15)                      ;[1434] fd 34 15
                    ret       nz                            ;[1437] c0
                    inc       (iy+$16)                      ;[1438] fd 34 16
                    bit       1,(iy+$16)                    ;[143b] fd cb 16 4e
                    ret       z                             ;[143f] c8
                    ld        (iy+$16),$00                  ;[1440] fd 36 16 00
                    ld        bc,$0010                      ;[1444] 01 10 00
                    call      $0577                         ;[1447] cd 77 05
                    ret       c                             ;[144a] d8
                    res       6,(iy+$0f)                    ;[144b] fd cb 0f b6
                    cp        $19                           ;[144f] fe 19
                    scf                                     ;[1451] 37
                    ret       z                             ;[1452] c8
                    ccf                                     ;[1453] 3f
                    ret                                     ;[1454] c9

                    ld        l,(iy+$17)                    ;[1455] fd 6e 17
                    ld        h,(iy+$18)                    ;[1458] fd 66 18
                    add       hl,bc                         ;[145b] 09
                    ld        (iy+$17),l                    ;[145c] fd 75 17
                    ld        (iy+$18),h                    ;[145f] fd 74 18
                    jr        nc,$146c                      ;[1462] 30 08
                    inc       (iy+$19)                      ;[1464] fd 34 19
                    jr        nz,$146c                      ;[1467] 20 03
                    inc       (iy+$1a)                      ;[1469] fd 34 1a
                    bit       6,(iy+$0f)                    ;[146c] fd cb 0f 76
                    scf                                     ;[1470] 37
                    ret       z                             ;[1471] c8
                    bit       7,b                           ;[1472] cb 78
                    jr        nz,$14a6                      ;[1474] 20 30
                    ld        l,(iy+$15)                    ;[1476] fd 6e 15
                    ld        h,(iy+$16)                    ;[1479] fd 66 16
                    add       hl,bc                         ;[147c] 09
                    ld        (iy+$15),l                    ;[147d] fd 75 15
                    ld        (iy+$16),h                    ;[1480] fd 74 16
                    ld        bc,$0200                      ;[1483] 01 00 02
                    and       a                             ;[1486] a7
                    sbc       hl,bc                         ;[1487] ed 42
                    ret       c                             ;[1489] d8
                    ld        (iy+$15),l                    ;[148a] fd 75 15
                    ld        (iy+$16),h                    ;[148d] fd 74 16
                    ld        bc,$0010                      ;[1490] 01 10 00
                    call      $0577                         ;[1493] cd 77 05
                    jr        nc,$14a0                      ;[1496] 30 08
                    ld        l,(iy+$15)                    ;[1498] fd 6e 15
                    ld        h,(iy+$16)                    ;[149b] fd 66 16
                    jr        $1483                         ;[149e] 18 e3
                    cp        $19                           ;[14a0] fe 19
                    scf                                     ;[14a2] 37
                    jr        z,$14a6                       ;[14a3] 28 01
                    ccf                                     ;[14a5] 3f
                    res       6,(iy+$0f)                    ;[14a6] fd cb 0f b6
                    ret                                     ;[14aa] c9

                    bit       6,(iy+$0f)                    ;[14ab] fd cb 0f 76
                    jr        nz,$151c                      ;[14af] 20 6b
                    rst       $28                           ;[14b1] ef
                    dec       de                            ;[14b2] 1b
                    call      $05a1                         ;[14b3] cd a1 05
                    jr        c,$14c0                       ;[14b6] 38 08
                    call      $05bf                         ;[14b8] cd bf 05
                    ret       nc                            ;[14bb] d0
                    call      $0500                         ;[14bc] cd 00 05
                    dec       de                            ;[14bf] 1b
                    call      $0500                         ;[14c0] cd 00 05
                    ld        de,$8dcd                      ;[14c3] 11 cd 8d
                    ld        d,$dd                         ;[14c6] 16 dd
                    ld        b,(hl)                        ;[14c8] 46
                    ld        hl,$000e                      ;[14c9] 21 0e 00
                    and       a                             ;[14cc] a7
                    sbc       hl,bc                         ;[14cd] ed 42
                    jr        nc,$14d6                      ;[14cf] 30 05
                    ld        a,d                           ;[14d1] 7a
                    or        e                             ;[14d2] b3
                    jr        z,$1503                       ;[14d3] 28 2e
                    dec       de                            ;[14d5] 1b
                    sbc       hl,bc                         ;[14d6] ed 42
                    jr        nc,$14df                      ;[14d8] 30 05
                    ld        a,d                           ;[14da] 7a
                    or        e                             ;[14db] b3
                    jr        z,$1502                       ;[14dc] 28 24
                    dec       de                            ;[14de] 1b
                    push      hl                            ;[14df] e5
                    push      de                            ;[14e0] d5
                    push      bc                            ;[14e1] c5
                    rst       $28                           ;[14e2] ef
                    ld        de,$12cd                      ;[14e3] 11 cd 12
                    add       hl,bc                         ;[14e6] 09
                    jr        nc,$14fe                      ;[14e7] 30 15
                    call      $05a1                         ;[14e9] cd a1 05
                    jr        c,$14f5                       ;[14ec] 38 07
                    rst       $28                           ;[14ee] ef
                    ld        de,$3ecd                      ;[14ef] 11 cd 3e
                    ld        b,$30                         ;[14f2] 06 30
                    add       hl,bc                         ;[14f4] 09
                    call      $0500                         ;[14f5] cd 00 05
                    ld        de,$d1c1                      ;[14f8] 11 c1 d1
                    pop       hl                            ;[14fb] e1
                    jr        $14cc                         ;[14fc] 18 ce
                    pop       bc                            ;[14fe] c1
                    pop       de                            ;[14ff] d1
                    pop       hl                            ;[1500] e1
                    ret                                     ;[1501] c9

                    add       hl,bc                         ;[1502] 09
                    add       hl,bc                         ;[1503] 09
                    ld        bc,$0200                      ;[1504] 01 00 02
                    xor       a                             ;[1507] af
                    sbc       hl,bc                         ;[1508] ed 42
                    inc       a                             ;[150a] 3c
                    jr        nc,$1508                      ;[150b] 30 fb
                    add       hl,bc                         ;[150d] 09
                    dec       a                             ;[150e] 3d
                    ld        (iy+$10),a                    ;[150f] fd 77 10
                    ld        (iy+$15),l                    ;[1512] fd 75 15
                    ld        (iy+$16),h                    ;[1515] fd 74 16
                    set       6,(iy+$0f)                    ;[1518] fd cb 0f f6
                    rst       $28                           ;[151c] ef
                    ld        de,$7efd                      ;[151d] 11 fd 7e
                    djnz      $14ef                         ;[1520] 10 cd
                    jp        $fd06                         ;[1522] c3 06 fd
                    ld        l,(hl)                        ;[1525] 6e
                    dec       d                             ;[1526] 15
                    ld        h,(iy+$16)                    ;[1527] fd 66 16
                    ret                                     ;[152a] c9

                    call      $14ab                         ;[152b] cd ab 14
                    ret       nc                            ;[152e] d0
                    push      hl                            ;[152f] e5
                    call      $0832                         ;[1530] cd 32 08
                    pop       de                            ;[1533] d1
                    ret       nc                            ;[1534] d0
                    add       hl,de                         ;[1535] 19
                    scf                                     ;[1536] 37
                    ret                                     ;[1537] c9

                    call      $1354                         ;[1538] cd 54 13
                    bit       0,(iy+$0f)                    ;[153b] fd cb 0f 46
                    jr        z,$1556                       ;[153f] 28 15
                    call      $13e8                         ;[1541] cd e8 13
                    jr        nc,$155a                      ;[1544] 30 14
                    call      $152b                         ;[1546] cd 2b 15
                    jr        nc,$155c                      ;[1549] 30 11
                    ld        c,(hl)                        ;[154b] 4e
                    push      bc                            ;[154c] c5
                    call      $141c                         ;[154d] cd 1c 14
                    pop       bc                            ;[1550] c1
                    ld        a,c                           ;[1551] 79
                    cp        $1a                           ;[1552] fe 1a
                    scf                                     ;[1554] 37
                    ret                                     ;[1555] c9

                    ld        a,$1d                         ;[1556] 3e 1d
                    jr        $155c                         ;[1558] 18 02
                    ld        a,$19                         ;[155a] 3e 19
                    and       a                             ;[155c] a7
                    ret                                     ;[155d] c9

                    call      $1354                         ;[155e] cd 54 13
                    bit       1,(iy+$0f)                    ;[1561] fd cb 0f 4e
                    jr        z,$1556                       ;[1565] 28 ef
                    push      bc                            ;[1567] c5
                    call      $13f6                         ;[1568] cd f6 13
                    call      $152b                         ;[156b] cd 2b 15
                    pop       bc                            ;[156e] c1
                    jr        nc,$155c                      ;[156f] 30 eb
                    ld        (hl),c                        ;[1571] 71
                    call      $08e2                         ;[1572] cd e2 08
                    call      $141c                         ;[1575] cd 1c 14
                    scf                                     ;[1578] 37
                    ret                                     ;[1579] c9

                    call      $1354                         ;[157a] cd 54 13
                    ld        ($2009),hl                    ;[157d] 22 09 20
                    ld        ($200b),de                    ;[1580] ed 53 0b 20
                    push      bc                            ;[1584] c5
                    bit       7,c                           ;[1585] cb 79
                    call      z,$0087                       ;[1587] cc 87 00
                    bit       0,(iy+$0f)                    ;[158a] fd cb 0f 46
                    jp        z,$1628                       ;[158e] ca 28 16
                    call      $13e8                         ;[1591] cd e8 13
                    jp        nc,$1624                      ;[1594] d2 24 16
                    rst       $28                           ;[1597] ef
                    rra                                     ;[1598] 1f
                    push      de                            ;[1599] d5
                    ld        h,b                           ;[159a] 60
                    ld        l,c                           ;[159b] 69
                    rst       $28                           ;[159c] ef
                    rla                                     ;[159d] 17
                    and       a                             ;[159e] a7
                    sbc       hl,bc                         ;[159f] ed 42
                    ex        de,hl                         ;[15a1] eb
                    ld        b,h                           ;[15a2] 44
                    ld        c,l                           ;[15a3] 4d
                    pop       hl                            ;[15a4] e1
                    sbc       hl,bc                         ;[15a5] ed 42
                    ld        hl,($200b)                    ;[15a7] 2a 0b 20
                    jr        nz,$15b3                      ;[15aa] 20 07
                    and       a                             ;[15ac] a7
                    sbc       hl,de                         ;[15ad] ed 52
                    ex        de,hl                         ;[15af] eb
                    jr        nc,$15b3                      ;[15b0] 30 01
                    add       hl,de                         ;[15b2] 19
                    ld        ($200d),hl                    ;[15b3] 22 0d 20
                    call      $14ab                         ;[15b6] cd ab 14
                    jr        nc,$162a                      ;[15b9] 30 6f
                    bit       1,(iy+$0f)                    ;[15bb] fd cb 0f 4e
                    jr        nz,$15dc                      ;[15bf] 20 1b
                    ld        a,h                           ;[15c1] 7c
                    or        l                             ;[15c2] b5
                    jr        nz,$15dc                      ;[15c3] 20 17
                    ld        a,($200e)                     ;[15c5] 3a 0e 20
                    cp        $02                           ;[15c8] fe 02
                    jr        c,$15dc                       ;[15ca] 38 10
                    ld        hl,($2009)                    ;[15cc] 2a 09 20
                    call      $1eaf                         ;[15cf] cd af 1e
                    ld        ($2009),hl                    ;[15d2] 22 09 20
                    jr        nc,$162a                      ;[15d5] 30 53
                    ld        bc,$0200                      ;[15d7] 01 00 02
                    jr        $1600                         ;[15da] 18 24
                    call      $152f                         ;[15dc] cd 2f 15
                    jr        nc,$162a                      ;[15df] 30 49
                    push      hl                            ;[15e1] e5
                    ld        hl,$0200                      ;[15e2] 21 00 02
                    and       a                             ;[15e5] a7
                    sbc       hl,de                         ;[15e6] ed 52
                    pop       de                            ;[15e8] d1
                    ld        bc,($200d)                    ;[15e9] ed 4b 0d 20
                    sbc       hl,bc                         ;[15ed] ed 42
                    jr        nc,$15f4                      ;[15ef] 30 03
                    add       hl,bc                         ;[15f1] 09
                    ld        b,h                           ;[15f2] 44
                    ld        c,l                           ;[15f3] 4d
                    push      bc                            ;[15f4] c5
                    ld        hl,($2009)                    ;[15f5] 2a 09 20
                    ex        de,hl                         ;[15f8] eb
                    ldir                                    ;[15f9] ed b0
                    ld        ($2009),de                    ;[15fb] ed 53 09 20
                    pop       bc                            ;[15ff] c1
                    ld        hl,($200b)                    ;[1600] 2a 0b 20
                    and       a                             ;[1603] a7
                    sbc       hl,bc                         ;[1604] ed 42
                    ld        ($200b),hl                    ;[1606] 22 0b 20
                    push      af                            ;[1609] f5
                    push      bc                            ;[160a] c5
                    call      $1455                         ;[160b] cd 55 14
                    pop       bc                            ;[160e] c1
                    pop       af                            ;[160f] f1
                    jr        z,$161c                       ;[1610] 28 0a
                    ld        hl,($200d)                    ;[1612] 2a 0d 20
                    and       a                             ;[1615] a7
                    sbc       hl,bc                         ;[1616] ed 42
                    jr        z,$1624                       ;[1618] 28 0a
                    jr        $15b3                         ;[161a] 18 97
                    pop       bc                            ;[161c] c1
                    bit       7,c                           ;[161d] cb 79
                    call      z,$0082                       ;[161f] cc 82 00
                    scf                                     ;[1622] 37
                    ret                                     ;[1623] c9

                    ld        a,$19                         ;[1624] 3e 19
                    jr        $162a                         ;[1626] 18 02
                    ld        a,$1d                         ;[1628] 3e 1d
                    pop       bc                            ;[162a] c1
                    bit       7,c                           ;[162b] cb 79
                    call      z,$0082                       ;[162d] cc 82 00
                    and       a                             ;[1630] a7
                    ld        de,($200b)                    ;[1631] ed 5b 0b 20
                    ret                                     ;[1635] c9

                    call      $1354                         ;[1636] cd 54 13
                    ld        ($2009),hl                    ;[1639] 22 09 20
                    ld        ($200b),de                    ;[163c] ed 53 0b 20
                    push      bc                            ;[1640] c5
                    bit       7,c                           ;[1641] cb 79
                    call      z,$0087                       ;[1643] cc 87 00
                    bit       1,(iy+$0f)                    ;[1646] fd cb 0f 4e
                    jr        z,$1628                       ;[164a] 28 dc
                    call      $152b                         ;[164c] cd 2b 15
                    jr        nc,$162a                      ;[164f] 30 d9
                    push      hl                            ;[1651] e5
                    ld        hl,$0200                      ;[1652] 21 00 02
                    and       a                             ;[1655] a7
                    sbc       hl,de                         ;[1656] ed 52
                    pop       de                            ;[1658] d1
                    ld        bc,($200b)                    ;[1659] ed 4b 0b 20
                    sbc       hl,bc                         ;[165d] ed 42
                    jr        nc,$1664                      ;[165f] 30 03
                    add       hl,bc                         ;[1661] 09
                    ld        b,h                           ;[1662] 44
                    ld        c,l                           ;[1663] 4d
                    push      bc                            ;[1664] c5
                    ld        hl,($2009)                    ;[1665] 2a 09 20
                    ldir                                    ;[1668] ed b0
                    ld        ($2009),hl                    ;[166a] 22 09 20
                    call      $08e2                         ;[166d] cd e2 08
                    pop       bc                            ;[1670] c1
                    ld        hl,($200b)                    ;[1671] 2a 0b 20
                    and       a                             ;[1674] a7
                    sbc       hl,bc                         ;[1675] ed 42
                    ld        ($200b),hl                    ;[1677] 22 0b 20
                    push      af                            ;[167a] f5
                    dec       bc                            ;[167b] 0b
                    call      $1455                         ;[167c] cd 55 14
                    call      $13f6                         ;[167f] cd f6 13
                    call      $141c                         ;[1682] cd 1c 14
                    pop       af                            ;[1685] f1
                    jr        z,$161c                       ;[1686] 28 94
                    jr        $164c                         ;[1688] 18 c2
                    call      $1354                         ;[168a] cd 54 13
                    rst       $28                           ;[168d] ef
                    rla                                     ;[168e] 17
                    ld        h,b                           ;[168f] 60
                    ld        l,c                           ;[1690] 69
                    scf                                     ;[1691] 37
                    ret                                     ;[1692] c9

                    call      $1354                         ;[1693] cd 54 13
                    ld        b,h                           ;[1696] 44
                    ld        c,l                           ;[1697] 4d
                    call      $0500                         ;[1698] cd 00 05
                    rla                                     ;[169b] 17
                    res       6,(iy+$0f)                    ;[169c] fd cb 0f b6
                    scf                                     ;[16a0] 37
                    ret                                     ;[16a1] c9

                    call      $1354                         ;[16a2] cd 54 13
                    call      $11ab                         ;[16a5] cd ab 11
                    ret       nc                            ;[16a8] d0
                    add       hl,$0016                      ;[16a9] ed 34 16 00
                    ld        e,(hl)                        ;[16ad] 5e
                    inc       hl                            ;[16ae] 23
                    ld        d,(hl)                        ;[16af] 56
                    inc       hl                            ;[16b0] 23
                    ld        c,(hl)                        ;[16b1] 4e
                    inc       hl                            ;[16b2] 23
                    ld        b,(hl)                        ;[16b3] 46
                    inc       hl                            ;[16b4] 23
                    push      de                            ;[16b5] d5
                    pop       ix                            ;[16b6] dd e1
                    push      bc                            ;[16b8] c5
                    call      $16bf                         ;[16b9] cd bf 16
                    pop       bc                            ;[16bc] c1
                    scf                                     ;[16bd] 37
                    ret                                     ;[16be] c9

                    rst       $28                           ;[16bf] ef
                    rra                                     ;[16c0] 1f
                    ld        h,b                           ;[16c1] 60
                    ld        l,c                           ;[16c2] 69
                    ret                                     ;[16c3] c9

                    call      $16cc                         ;[16c4] cd cc 16
                    ld        (iy+$0f),$00                  ;[16c7] fd 36 0f 00
                    ret                                     ;[16cb] c9

                    call      $1354                         ;[16cc] cd 54 13
                    scf                                     ;[16cf] 37
                    bit       1,(iy+$0f)                    ;[16d0] fd cb 0f 4e
                    ret       z                             ;[16d4] c8
                    bit       7,(iy+$0f)                    ;[16d5] fd cb 0f 7e
                    call      nz,$18fe                      ;[16d9] c4 fe 18
                    ret       nc                            ;[16dc] d0
                    call      $08d1                         ;[16dd] cd d1 08
                    ret       nc                            ;[16e0] d0
                    call      $1176                         ;[16e1] cd 76 11
                    ret       nc                            ;[16e4] d0
                    ld        bc,$000b                      ;[16e5] 01 0b 00
                    add       hl,bc                         ;[16e8] 09
                    set       5,(hl)                        ;[16e9] cb ee
                    ld        bc,$0009                      ;[16eb] 01 09 00
                    add       hl,bc                         ;[16ee] 09
                    rst       $28                           ;[16ef] ef
                    dec       de                            ;[16f0] 1b
                    ld        (hl),e                        ;[16f1] 73
                    inc       hl                            ;[16f2] 23
                    ld        (hl),d                        ;[16f3] 72
                    inc       hl                            ;[16f4] 23
                    push      bc                            ;[16f5] c5
                    call      $18f0                         ;[16f6] cd f0 18
                    pop       bc                            ;[16f9] c1
                    ld        (hl),c                        ;[16fa] 71
                    inc       hl                            ;[16fb] 23
                    ld        (hl),b                        ;[16fc] 70
                    inc       hl                            ;[16fd] 23
                    push      hl                            ;[16fe] e5
                    call      $16bf                         ;[16ff] cd bf 16
                    pop       hl                            ;[1702] e1
                    ld        (hl),c                        ;[1703] 71
                    inc       hl                            ;[1704] 23
                    ld        (hl),b                        ;[1705] 70
                    inc       hl                            ;[1706] 23
                    ld        (hl),e                        ;[1707] 73
                    inc       hl                            ;[1708] 23
                    ld        (hl),d                        ;[1709] 72
                    call      $0853                         ;[170a] cd 53 08
                    scf                                     ;[170d] 37
                    ret                                     ;[170e] c9

                    push      af                            ;[170f] f5
                    call      $08d1                         ;[1710] cd d1 08
                    pop       af                            ;[1713] f1
                    call      $1354                         ;[1714] cd 54 13
                    scf                                     ;[1717] 37
                    jr        $16c7                         ;[1718] 18 ad
                    call      $1354                         ;[171a] cd 54 13
                    ld        b,(iy+$0f)                    ;[171d] fd 46 0f
                    push      bc                            ;[1720] c5
                    call      $16cf                         ;[1721] cd cf 16
                    pop       bc                            ;[1724] c1
                    ret       nc                            ;[1725] d0
                    ld        (iy+$0f),$00                  ;[1726] fd 36 0f 00
                    push      bc                            ;[172a] c5
                    ld        b,$10                         ;[172b] 06 10
                    call      $136e                         ;[172d] cd 6e 13
                    pop       bc                            ;[1730] c1
                    ld        (iy+$0f),b                    ;[1731] fd 70 0f
                    jr        c,$1748                       ;[1734] 38 12
                    bit       3,c                           ;[1736] cb 59
                    jr        nz,$1748                      ;[1738] 20 0e
                    bit       2,c                           ;[173a] cb 51
                    jr        z,$1766                       ;[173c] 28 28
                    bit       1,c                           ;[173e] cb 49
                    jr        nz,$1766                      ;[1740] 20 24
                    and       $06                           ;[1742] e6 06
                    cp        $02                           ;[1744] fe 02
                    jr        z,$1766                       ;[1746] 28 1e
                    ld        a,c                           ;[1748] 79
                    cp        $10                           ;[1749] fe 10
                    jr        nc,$1768                      ;[174b] 30 1b
                    and       $02                           ;[174d] e6 02
                    jr        z,$175d                       ;[174f] 28 0c
                    push      bc                            ;[1751] c5
                    call      $11ab                         ;[1752] cd ab 11
                    pop       bc                            ;[1755] c1
                    ret       nc                            ;[1756] d0
                    bit       0,a                           ;[1757] cb 47
                    ld        a,$1c                         ;[1759] 3e 1c
                    jr        nz,$176a                      ;[175b] 20 0d
                    ld        a,b                           ;[175d] 78
                    and       $f0                           ;[175e] e6 f0
                    or        c                             ;[1760] b1
                    ld        (iy+$0f),a                    ;[1761] fd 77 0f
                    scf                                     ;[1764] 37
                    ret                                     ;[1765] c9

                    ld        a,$24                         ;[1766] 3e 24
                    ld        a,$1e                         ;[1768] 3e 1e
                    and       a                             ;[176a] a7
                    ret                                     ;[176b] c9

                    ld        ($2018),ix                    ;[176c] dd 22 18 20
                    call      $1355                         ;[1770] cd 55 13
                    push      de                            ;[1773] d5
                    push      bc                            ;[1774] c5
                    push      iy                            ;[1775] fd e5
                    call      $10c1                         ;[1777] cd c1 10
                    bit       1,d                           ;[177a] cb 4a
                    jr        z,$1792                       ;[177c] 28 14
                    pop       de                            ;[177e] d1
                    call      $17ab                         ;[177f] cd ab 17
                    pop       bc                            ;[1782] c1
                    jr        nc,$178a                      ;[1783] 30 05
                    ld        b,$00                         ;[1785] 06 00
                    call      $172a                         ;[1787] cd 2a 17
                    pop       de                            ;[178a] d1
                    ret       nc                            ;[178b] d0
                    bit       0,d                           ;[178c] cb 42
                    call      z,$1956                       ;[178e] cc 56 19
                    ret                                     ;[1791] c9

                    call      $1815                         ;[1792] cd 15 18
                    pop       de                            ;[1795] d1
                    jr        nc,$1782                      ;[1796] 30 ea
                    call      $17ab                         ;[1798] cd ab 17
                    pop       bc                            ;[179b] c1
                    jr        nc,$178a                      ;[179c] 30 ec
                    ld        b,$00                         ;[179e] 06 00
                    call      $172a                         ;[17a0] cd 2a 17
                    pop       de                            ;[17a3] d1
                    ret       nc                            ;[17a4] d0
                    bit       0,d                           ;[17a5] cb 42
                    call      z,$18fe                       ;[17a7] cc fe 18
                    ret                                     ;[17aa] c9

                    push      iy                            ;[17ab] fd e5
                    pop       hl                            ;[17ad] e1
                    push      de                            ;[17ae] d5
                    pop       iy                            ;[17af] fd e1
                    ld        bc,$000f                      ;[17b1] 01 0f 00
                    ldir                                    ;[17b4] ed b0
                    ld        h,d                           ;[17b6] 62
                    ld        l,e                           ;[17b7] 6b
                    inc       de                            ;[17b8] 13
                    ld        (hl),$00                      ;[17b9] 36 00
                    ld        bc,$001d                      ;[17bb] 01 1d 00
                    ldir                                    ;[17be] ed b0
                    call      $11ab                         ;[17c0] cd ab 11
                    ret       nc                            ;[17c3] d0
                    ld        bc,$001a                      ;[17c4] 01 1a 00
                    add       hl,bc                         ;[17c7] 09
                    push      iy                            ;[17c8] fd e5
                    ld        bc,$001b                      ;[17ca] 01 1b 00
                    add       iy,bc                         ;[17cd] fd 09
                    push      iy                            ;[17cf] fd e5
                    pop       de                            ;[17d1] d1
                    pop       iy                            ;[17d2] fd e1
                    ld        bc,$0002                      ;[17d4] 01 02 00
                    ldir                                    ;[17d7] ed b0
                    inc       de                            ;[17d9] 13
                    inc       de                            ;[17da] 13
                    ld        bc,$0004                      ;[17db] 01 04 00
                    ldir                                    ;[17de] ed b0
                    bit       7,(ix+$13)                    ;[17e0] dd cb 13 7e
                    ld        de,$0000                      ;[17e4] 11 00 00
                    jr        nz,$17f0                      ;[17e7] 20 07
                    ld        bc,$fff4                      ;[17e9] 01 f4 ff
                    add       hl,bc                         ;[17ec] 09
                    ld        e,(hl)                        ;[17ed] 5e
                    inc       hl                            ;[17ee] 23
                    ld        d,(hl)                        ;[17ef] 56
                    ld        (iy+$1d),e                    ;[17f0] fd 73 1d
                    ld        (iy+$1e),d                    ;[17f3] fd 72 1e
                    rst       $28                           ;[17f6] ef
                    dec       de                            ;[17f7] 1b
                    call      $0500                         ;[17f8] cd 00 05
                    ld        de,$182a                      ;[17fb] 11 2a 18
                    jr        nz,$17fd                      ;[17fe] 20 fd
                    ld        (hl),l                        ;[1800] 75
                    dec       hl                            ;[1801] 2b
                    ld        (iy+$2c),h                    ;[1802] fd 74 2c
                    call      $19c2                         ;[1805] cd c2 19
                    rst       $28                           ;[1808] ef
                    add       hl,hl                         ;[1809] 29
                    ld        a,d                           ;[180a] 7a
                    or        e                             ;[180b] b3
                    scf                                     ;[180c] 37
                    ret       z                             ;[180d] c8
                    ld        bc,$0008                      ;[180e] 01 08 00
                    ldir                                    ;[1811] ed b0
                    scf                                     ;[1813] 37
                    ret                                     ;[1814] c9

                    push      hl                            ;[1815] e5
                    ld        a,(hl)                        ;[1816] 7e
                    inc       hl                            ;[1817] 23
                    cp        $2e                           ;[1818] fe 2e
                    jr        nz,$182a                      ;[181a] 20 0e
                    ld        a,(hl)                        ;[181c] 7e
                    cp        $2e                           ;[181d] fe 2e
                    jr        nz,$1823                      ;[181f] 20 02
                    inc       hl                            ;[1821] 23
                    ld        a,(hl)                        ;[1822] 7e
                    cp        $ff                           ;[1823] fe ff
                    ld        a,$14                         ;[1825] 3e 14
                    jp        z,$178a                       ;[1827] ca 8a 17
                    pop       hl                            ;[182a] e1
                    push      hl                            ;[182b] e5
                    ld        de,$202e                      ;[182c] 11 2e 20
                    call      $0f67                         ;[182f] cd 67 0f
                    push      hl                            ;[1832] e5
                    push      bc                            ;[1833] c5
                    push      ix                            ;[1834] dd e5
                    call      $10f7                         ;[1836] cd f7 10
                    call      $11ab                         ;[1839] cd ab 11
                    jr        z,$1852                       ;[183c] 28 14
                    and       $08                           ;[183e] e6 08
                    jr        nz,$1852                      ;[1840] 20 10
                    ld        b,$0b                         ;[1842] 06 0b
                    ld        de,$202e                      ;[1844] 11 2e 20
                    ld        a,(de)                        ;[1847] 1a
                    inc       de                            ;[1848] 13
                    xor       (hl)                          ;[1849] ae
                    inc       hl                            ;[184a] 23
                    jr        nz,$1852                      ;[184b] 20 05
                    djnz      $1847                         ;[184d] 10 f8
                    scf                                     ;[184f] 37
                    jr        $1857                         ;[1850] 18 05
                    call      $113f                         ;[1852] cd 3f 11
                    jr        c,$1839                       ;[1855] 38 e2
                    pop       ix                            ;[1857] dd e1
                    pop       bc                            ;[1859] c1
                    pop       hl                            ;[185a] e1
                    jr        nc,$1864                      ;[185b] 30 07
                    call      $0fc4                         ;[185d] cd c4 0f
                    jr        c,$1832                       ;[1860] 38 d0
                    pop       hl                            ;[1862] e1
                    ret                                     ;[1863] c9

                    ld        hl,$202e                      ;[1864] 21 2e 20
                    call      $0f0d                         ;[1867] cd 0d 0f
                    ld        d,a                           ;[186a] 57
                    pop       hl                            ;[186b] e1
                    ld        e,$02                         ;[186c] 1e 02
                    ld        bc,$000d                      ;[186e] 01 0d 00
                    ld        a,$ff                         ;[1871] 3e ff
                    cpir                                    ;[1873] ed b1
                    jr        z,$187c                       ;[1875] 28 05
                    inc       e                             ;[1877] 1c
                    cp        (hl)                          ;[1878] be
                    jr        nz,$186e                      ;[1879] 20 f3
                    dec       e                             ;[187b] 1d
                    ld        a,e                           ;[187c] 7b
                    cp        $16                           ;[187d] fe 16
                    ld        a,$14                         ;[187f] 3e 14
                    ret       nc                            ;[1881] d0
                    add       hl,bc                         ;[1882] 09
                    push      hl                            ;[1883] e5
                    push      de                            ;[1884] d5
                    call      $11c4                         ;[1885] cd c4 11
                    pop       bc                            ;[1888] c1
                    pop       hl                            ;[1889] e1
                    ret       nc                            ;[188a] d0
                    dec       c                             ;[188b] 0d
                    set       6,c                           ;[188c] cb f1
                    ld        de,$000d                      ;[188e] 11 0d 00
                    and       a                             ;[1891] a7
                    sbc       hl,de                         ;[1892] ed 52
                    push      bc                            ;[1894] c5
                    push      hl                            ;[1895] e5
                    call      $1176                         ;[1896] cd 76 11
                    pop       de                            ;[1899] d1
                    pop       bc                            ;[189a] c1
                    ret       nc                            ;[189b] d0
                    push      de                            ;[189c] d5
                    ld        (hl),c                        ;[189d] 71
                    inc       hl                            ;[189e] 23
                    res       6,c                           ;[189f] cb b1
                    push      bc                            ;[18a1] c5
                    ld        bc,$0501                      ;[18a2] 01 01 05
                    call      $0f4f                         ;[18a5] cd 4f 0f
                    ld        (hl),$0f                      ;[18a8] 36 0f
                    inc       hl                            ;[18aa] 23
                    ld        (hl),$00                      ;[18ab] 36 00
                    inc       hl                            ;[18ad] 23
                    pop       af                            ;[18ae] f1
                    push      af                            ;[18af] f5
                    ld        (hl),a                        ;[18b0] 77
                    inc       hl                            ;[18b1] 23
                    ld        b,$06                         ;[18b2] 06 06
                    call      $0f4f                         ;[18b4] cd 4f 0f
                    xor       a                             ;[18b7] af
                    ld        (hl),a                        ;[18b8] 77
                    inc       hl                            ;[18b9] 23
                    ld        (hl),a                        ;[18ba] 77
                    inc       hl                            ;[18bb] 23
                    ld        b,$02                         ;[18bc] 06 02
                    call      $0f4f                         ;[18be] cd 4f 0f
                    call      $08e2                         ;[18c1] cd e2 08
                    call      $113f                         ;[18c4] cd 3f 11
                    pop       bc                            ;[18c7] c1
                    pop       hl                            ;[18c8] e1
                    ret       nc                            ;[18c9] d0
                    dec       c                             ;[18ca] 0d
                    jr        nz,$188e                      ;[18cb] 20 c1
                    call      $1176                         ;[18cd] cd 76 11
                    ret       nc                            ;[18d0] d0
                    ex        de,hl                         ;[18d1] eb
                    ld        hl,$202e                      ;[18d2] 21 2e 20
                    ld        bc,$000b                      ;[18d5] 01 0b 00
                    ldir                                    ;[18d8] ed b0
                    ld        h,d                           ;[18da] 62
                    ld        l,e                           ;[18db] 6b
                    ld        (hl),$20                      ;[18dc] 36 20
                    inc       hl                            ;[18de] 23
                    inc       de                            ;[18df] 13
                    ld        (hl),b                        ;[18e0] 70
                    ld        c,$13                         ;[18e1] 0e 13
                    inc       de                            ;[18e3] 13
                    ldir                                    ;[18e4] ed b0
                    add       hl,$fff7                      ;[18e6] ed 34 f7 ff
                    call      $18f0                         ;[18ea] cd f0 18
                    jp        $08ce                         ;[18ed] c3 ce 08
                    push      hl                            ;[18f0] e5
                    call      $00f2                         ;[18f1] cd f2 00
                    pop       hl                            ;[18f4] e1
                    ld        (hl),e                        ;[18f5] 73
                    inc       hl                            ;[18f6] 23
                    ld        (hl),d                        ;[18f7] 72
                    inc       hl                            ;[18f8] 23
                    ld        (hl),c                        ;[18f9] 71
                    inc       hl                            ;[18fa] 23
                    ld        (hl),b                        ;[18fb] 70
                    inc       hl                            ;[18fc] 23
                    ret                                     ;[18fd] c9

                    ld        d,$00                         ;[18fe] 16 00
                    ld        e,d                           ;[1900] 5a
                    ld        h,d                           ;[1901] 62
                    ld        l,$7f                         ;[1902] 2e 7f
                    call      $1696                         ;[1904] cd 96 16
                    call      $13f6                         ;[1907] cd f6 13
                    call      $19d6                         ;[190a] cd d6 19
                    ret       nc                            ;[190d] d0
                    set       7,(iy+$0f)                    ;[190e] fd cb 0f fe
                    push      hl                            ;[1912] e5
                    ex        de,hl                         ;[1913] eb
                    ld        hl,$19e9                      ;[1914] 21 e9 19
                    ld        bc,$000b                      ;[1917] 01 0b 00
                    ldir                                    ;[191a] ed b0
                    push      de                            ;[191c] d5
                    call      $19c2                         ;[191d] cd c2 19
                    rst       $28                           ;[1920] ef
                    dec       hl                            ;[1921] 2b
                    ex        de,hl                         ;[1922] eb
                    ld        h,b                           ;[1923] 60
                    ld        l,c                           ;[1924] 69
                    ld        a,h                           ;[1925] 7c
                    or        l                             ;[1926] b5
                    jr        z,$192e                       ;[1927] 28 05
                    ld        bc,$0008                      ;[1929] 01 08 00
                    ldir                                    ;[192c] ed b0
                    pop       de                            ;[192e] d1
                    push      iy                            ;[192f] fd e5
                    pop       hl                            ;[1931] e1
                    ld        bc,$001f                      ;[1932] 01 1f 00
                    add       hl,bc                         ;[1935] 09
                    ld        bc,$000c                      ;[1936] 01 0c 00
                    ldir                                    ;[1939] ed b0
                    ld        h,d                           ;[193b] 62
                    ld        l,e                           ;[193c] 6b
                    inc       de                            ;[193d] 13
                    ld        (hl),$00                      ;[193e] 36 00
                    ld        bc,$0067                      ;[1940] 01 67 00
                    ldir                                    ;[1943] ed b0
                    pop       hl                            ;[1945] e1
                    call      $19e1                         ;[1946] cd e1 19
                    ld        (hl),a                        ;[1949] 77
                    call      $0853                         ;[194a] cd 53 08
                    ld        d,$00                         ;[194d] 16 00
                    ld        e,d                           ;[194f] 5a
                    ld        h,d                           ;[1950] 62
                    ld        l,$80                         ;[1951] 2e 80
                    jp        $1696                         ;[1953] c3 96 16
                    res       7,(iy+$0f)                    ;[1956] fd cb 0f be
                    call      $16bf                         ;[195a] cd bf 16
                    ld        a,l                           ;[195d] 7d
                    and       $80                           ;[195e] e6 80
                    or        h                             ;[1960] b4
                    or        e                             ;[1961] b3
                    or        d                             ;[1962] b2
                    jr        z,$198c                       ;[1963] 28 27
                    call      $19d6                         ;[1965] cd d6 19
                    jr        nc,$198c                      ;[1968] 30 22
                    call      $19ae                         ;[196a] cd ae 19
                    jr        nz,$198c                      ;[196d] 20 1d
                    set       7,(iy+$0f)                    ;[196f] fd cb 0f fe
                    push      iy                            ;[1973] fd e5
                    pop       hl                            ;[1975] e1
                    ld        bc,$001f                      ;[1976] 01 1f 00
                    add       hl,bc                         ;[1979] 09
                    ld        bc,$000c                      ;[197a] 01 0c 00
                    ex        de,hl                         ;[197d] eb
                    ldir                                    ;[197e] ed b0
                    call      $1805                         ;[1980] cd 05 18
                    ld        d,$00                         ;[1983] 16 00
                    ld        e,d                           ;[1985] 5a
                    ld        h,d                           ;[1986] 62
                    ld        l,$80                         ;[1987] 2e 80
                    jp        $1696                         ;[1989] c3 96 16
                    call      $16bf                         ;[198c] cd bf 16
                    ex        de,hl                         ;[198f] eb
                    ld        a,h                           ;[1990] 7c
                    or        l                             ;[1991] b5
                    jr        z,$1997                       ;[1992] 28 03
                    ld        de,$ffff                      ;[1994] 11 ff ff
                    call      $19c2                         ;[1997] cd c2 19
                    xor       a                             ;[199a] af
                    ld        b,$03                         ;[199b] 06 03
                    ld        (hl),b                        ;[199d] 70
                    inc       hl                            ;[199e] 23
                    ld        (hl),e                        ;[199f] 73
                    inc       hl                            ;[19a0] 23
                    ld        (hl),d                        ;[19a1] 72
                    inc       hl                            ;[19a2] 23
                    ld        (hl),a                        ;[19a3] 77
                    inc       hl                            ;[19a4] 23
                    ld        (hl),$40                      ;[19a5] 36 40
                    inc       hl                            ;[19a7] 23
                    ld        (hl),a                        ;[19a8] 77
                    djnz      $19a7                         ;[19a9] 10 fc
                    jp        $1805                         ;[19ab] c3 05 18
                    push      hl                            ;[19ae] e5
                    call      $19e1                         ;[19af] cd e1 19
                    cp        (hl)                          ;[19b2] be
                    pop       de                            ;[19b3] d1
                    ret       nz                            ;[19b4] c0
                    ld        hl,$19e9                      ;[19b5] 21 e9 19
                    ld        b,$0b                         ;[19b8] 06 0b
                    ld        a,(de)                        ;[19ba] 1a
                    cp        (hl)                          ;[19bb] be
                    ret       nz                            ;[19bc] c0
                    inc       de                            ;[19bd] 13
                    inc       hl                            ;[19be] 23
                    djnz      $19ba                         ;[19bf] 10 f9
                    ret                                     ;[19c1] c9

                    push      iy                            ;[19c2] fd e5
                    pop       hl                            ;[19c4] e1
                    ld        bc,$0023                      ;[19c5] 01 23 00
                    add       hl,bc                         ;[19c8] 09
                    ret                                     ;[19c9] c9

                    call      $11ab                         ;[19ca] cd ab 11
                    ret       nc                            ;[19cd] d0
                    ld        a,(hl)                        ;[19ce] 7e
                    cp        $2e                           ;[19cf] fe 2e
                    ld        a,$1e                         ;[19d1] 3e 1e
                    ret       z                             ;[19d3] c8
                    scf                                     ;[19d4] 37
                    ret                                     ;[19d5] c9

                    ld        h,$00                         ;[19d6] 26 00
                    ld        l,h                           ;[19d8] 6c
                    ld        d,h                           ;[19d9] 54
                    ld        e,h                           ;[19da] 5c
                    call      $1696                         ;[19db] cd 96 16
                    jp        $152b                         ;[19de] c3 2b 15
                    xor       a                             ;[19e1] af
                    ld        b,$7f                         ;[19e2] 06 7f
                    add       (hl)                          ;[19e4] 86
                    inc       hl                            ;[19e5] 23
                    djnz      $19e4                         ;[19e6] 10 fc
                    ret                                     ;[19e8] c9

                    ld        d,b                           ;[19e9] 50
                    ld        c,h                           ;[19ea] 4c
                    ld        d,l                           ;[19eb] 55
                    ld        d,e                           ;[19ec] 53
                    inc       sp                            ;[19ed] 33
                    ld        b,h                           ;[19ee] 44
                    ld        c,a                           ;[19ef] 4f
                    ld        d,e                           ;[19f0] 53
                    ld        a,(de)                        ;[19f1] 1a
                    ld        bc,$e500                      ;[19f2] 01 00 e5
                    push      de                            ;[19f5] d5
                    push      bc                            ;[19f6] c5
                    ld        ix,$0139                      ;[19f7] dd 21 39 01
                    call      $01f0                         ;[19fb] cd f0 01
                    pop       bc                            ;[19fe] c1
                    pop       ix                            ;[19ff] dd e1
                    pop       hl                            ;[1a01] e1
                    ret       nc                            ;[1a02] d0
                    call      $1a46                         ;[1a03] cd 46 1a
                    jr        z,$1a2a                       ;[1a06] 28 22
                    bit       1,b                           ;[1a08] cb 48
                    jr        z,$1a19                       ;[1a0a] 28 0d
                    ld        a,$7f                         ;[1a0c] 3e 7f
                    in        a,($fe)                       ;[1a0e] db fe
                    rra                                     ;[1a10] 1f
                    jr        c,$1a19                       ;[1a11] 38 06
                    ld        a,$fe                         ;[1a13] 3e fe
                    in        a,($fe)                       ;[1a15] db fe
                    rra                                     ;[1a17] 1f
                    ret       nc                            ;[1a18] d0
                    push      bc                            ;[1a19] c5
                    push      hl                            ;[1a1a] e5
                    push      ix                            ;[1a1b] dd e5
                    xor       a                             ;[1a1d] af
                    ld        ix,$013c                      ;[1a1e] dd 21 3c 01
                    call      $01f0                         ;[1a22] cd f0 01
                    pop       de                            ;[1a25] d1
                    pop       hl                            ;[1a26] e1
                    pop       bc                            ;[1a27] c1
                    jr        $19f4                         ;[1a28] 18 ca
                    bit       5,b                           ;[1a2a] cb 68
                    scf                                     ;[1a2c] 37
                    ret       z                             ;[1a2d] c8
                    push      bc                            ;[1a2e] c5
                    push      de                            ;[1a2f] d5
                    push      hl                            ;[1a30] e5
                    push      ix                            ;[1a31] dd e5
                    pop       bc                            ;[1a33] c1
                    ex        de,hl                         ;[1a34] eb
                    inc       bc                            ;[1a35] 03
                    ld        h,b                           ;[1a36] 60
                    ld        l,c                           ;[1a37] 69
                    add       hl,$001d                      ;[1a38] ed 34 1d 00
                    call      $1a93                         ;[1a3c] cd 93 1a
                    pop       hl                            ;[1a3f] e1
                    pop       de                            ;[1a40] d1
                    pop       bc                            ;[1a41] c1
                    scf                                     ;[1a42] 37
                    ret       z                             ;[1a43] c8
                    jr        $1a08                         ;[1a44] 18 c2
                    ld        a,(ix+$00)                    ;[1a46] dd 7e 00
                    bit       4,a                           ;[1a49] cb 67
                    jr        z,$1a61                       ;[1a4b] 28 14
                    bit       6,c                           ;[1a4d] cb 71
                    ret       nz                            ;[1a4f] c0
                    bit       5,c                           ;[1a50] cb 69
                    jr        z,$1a64                       ;[1a52] 28 10
                    ld        a,(ix+$01)                    ;[1a54] dd 7e 01
                    cp        $2e                           ;[1a57] fe 2e
                    ld        a,(ix+$00)                    ;[1a59] dd 7e 00
                    jr        nz,$1a64                      ;[1a5c] 20 06
                    xor       a                             ;[1a5e] af
                    inc       a                             ;[1a5f] 3c
                    ret                                     ;[1a60] c9

                    bit       7,c                           ;[1a61] cb 79
                    ret       nz                            ;[1a63] c0
                    bit       4,c                           ;[1a64] cb 61
                    ret       z                             ;[1a66] c8
                    and       $06                           ;[1a67] e6 06
                    ret                                     ;[1a69] c9

                    push      hl                            ;[1a6a] e5
                    add       hl,$001c                      ;[1a6b] ed 34 1c 00
                    ld        c,(hl)                        ;[1a6f] 4e
                    inc       hl                            ;[1a70] 23
                    ld        b,(hl)                        ;[1a71] 46
                    inc       hl                            ;[1a72] 23
                    ldir                                    ;[1a73] ed b0
                    dec       de                            ;[1a75] 1b
                    ld        a,$ff                         ;[1a76] 3e ff
                    ld        (de),a                        ;[1a78] 12
                    pop       hl                            ;[1a79] e1
                    add       hl,$000c                      ;[1a7a] ed 34 0c 00
                    ld        e,(hl)                        ;[1a7e] 5e
                    inc       hl                            ;[1a7f] 23
                    ld        d,(hl)                        ;[1a80] 56
                    inc       hl                            ;[1a81] 23
                    ld        c,(hl)                        ;[1a82] 4e
                    inc       hl                            ;[1a83] 23
                    ld        b,(hl)                        ;[1a84] 46
                    inc       hl                            ;[1a85] 23
                    ld        a,(hl)                        ;[1a86] 7e
                    inc       hl                            ;[1a87] 23
                    ld        ixl,a                         ;[1a88] dd 6f
                    ld        a,(hl)                        ;[1a8a] 7e
                    inc       hl                            ;[1a8b] 23
                    ld        ixh,a                         ;[1a8c] dd 67
                    ld        a,(hl)                        ;[1a8e] 7e
                    inc       hl                            ;[1a8f] 23
                    ld        h,(hl)                        ;[1a90] 66
                    ld        l,a                           ;[1a91] 6f
                    ret                                     ;[1a92] c9

                    push      bc                            ;[1a93] c5
                    push      de                            ;[1a94] d5
                    jr        z,$1aea                       ;[1a95] 28 53
                    ld        a,(de)                        ;[1a97] 1a
                    inc       de                            ;[1a98] 13
                    cp        $ff                           ;[1a99] fe ff
                    jr        z,$1ab4                       ;[1a9b] 28 17
                    cp        $2a                           ;[1a9d] fe 2a
                    jr        z,$1abb                       ;[1a9f] 28 1a
                    cp        $3f                           ;[1aa1] fe 3f
                    ld        c,(hl)                        ;[1aa3] 4e
                    inc       hl                            ;[1aa4] 23
                    jr        nz,$1aac                      ;[1aa5] 20 05
                    inc       c                             ;[1aa7] 0c
                    jr        nz,$1a97                      ;[1aa8] 20 ed
                    jr        $1aea                         ;[1aaa] 18 3e
                    cp        c                             ;[1aac] b9
                    call      nz,$1b3c                      ;[1aad] c4 3c 1b
                    jr        z,$1a97                       ;[1ab0] 28 e5
                    jr        $1aea                         ;[1ab2] 18 36
                    ld        a,(hl)                        ;[1ab4] 7e
                    inc       a                             ;[1ab5] 3c
                    jr        nz,$1aea                      ;[1ab6] 20 32
                    pop       de                            ;[1ab8] d1
                    pop       hl                            ;[1ab9] e1
                    ret                                     ;[1aba] c9

                    ld        a,(hl)                        ;[1abb] 7e
                    inc       hl                            ;[1abc] 23
                    cp        $ff                           ;[1abd] fe ff
                    jr        z,$1ad3                       ;[1abf] 28 12
                    cp        $2e                           ;[1ac1] fe 2e
                    jr        nz,$1abb                      ;[1ac3] 20 f6
                    push      hl                            ;[1ac5] e5
                    ld        a,(hl)                        ;[1ac6] 7e
                    inc       hl                            ;[1ac7] 23
                    cp        $ff                           ;[1ac8] fe ff
                    jr        z,$1ae3                       ;[1aca] 28 17
                    cp        $2e                           ;[1acc] fe 2e
                    jr        nz,$1ac6                      ;[1ace] 20 f6
                    pop       af                            ;[1ad0] f1
                    jr        $1ac5                         ;[1ad1] 18 f2
                    ld        a,(de)                        ;[1ad3] 1a
                    cp        $ff                           ;[1ad4] fe ff
                    jr        z,$1ab8                       ;[1ad6] 28 e0
                    cp        $2e                           ;[1ad8] fe 2e
                    jr        nz,$1aea                      ;[1ada] 20 0e
                    inc       de                            ;[1adc] 13
                    ld        a,(de)                        ;[1add] 1a
                    inc       a                             ;[1ade] 3c
                    jr        z,$1ab8                       ;[1adf] 28 d7
                    jr        $1aea                         ;[1ae1] 18 07
                    pop       hl                            ;[1ae3] e1
                    ld        a,(de)                        ;[1ae4] 1a
                    inc       de                            ;[1ae5] 13
                    cp        $2e                           ;[1ae6] fe 2e
                    jr        z,$1a97                       ;[1ae8] 28 ad
                    pop       de                            ;[1aea] d1
                    pop       hl                            ;[1aeb] e1
                    ld        b,$08                         ;[1aec] 06 08
                    ld        a,(hl)                        ;[1aee] 7e
                    cp        $2e                           ;[1aef] fe 2e
                    jr        nz,$1b06                      ;[1af1] 20 13
                    call      $1b15                         ;[1af3] cd 15 1b
                    ret       nz                            ;[1af6] c0
                    ld        a,(de)                        ;[1af7] 1a
                    cp        $2e                           ;[1af8] fe 2e
                    jr        nz,$1afd                      ;[1afa] 20 01
                    inc       de                            ;[1afc] 13
                    ld        b,$03                         ;[1afd] 06 03
                    call      $1b15                         ;[1aff] cd 15 1b
                    ret       nz                            ;[1b02] c0
                    ld        a,(de)                        ;[1b03] 1a
                    inc       a                             ;[1b04] 3c
                    ret                                     ;[1b05] c9

                    call      $1b15                         ;[1b06] cd 15 1b
                    jr        z,$1af7                       ;[1b09] 28 ec
                    cp        $2e                           ;[1b0b] fe 2e
                    ret       nz                            ;[1b0d] c0
                    dec       hl                            ;[1b0e] 2b
                    call      $1b32                         ;[1b0f] cd 32 1b
                    ret       nz                            ;[1b12] c0
                    jr        $1afd                         ;[1b13] 18 e8
                    ld        a,(de)                        ;[1b15] 1a
                    inc       de                            ;[1b16] 13
                    cp        $2a                           ;[1b17] fe 2a
                    jr        z,$1b2d                       ;[1b19] 28 12
                    cp        $ff                           ;[1b1b] fe ff
                    jr        z,$1b31                       ;[1b1d] 28 12
                    ld        c,(hl)                        ;[1b1f] 4e
                    inc       hl                            ;[1b20] 23
                    cp        $3f                           ;[1b21] fe 3f
                    jr        z,$1b2a                       ;[1b23] 28 05
                    cp        c                             ;[1b25] b9
                    call      nz,$1b3c                      ;[1b26] c4 3c 1b
                    ret       nz                            ;[1b29] c0
                    djnz      $1b15                         ;[1b2a] 10 e9
                    ret                                     ;[1b2c] c9

                    inc       hl                            ;[1b2d] 23
                    djnz      $1b2d                         ;[1b2e] 10 fd
                    ret                                     ;[1b30] c9

                    dec       de                            ;[1b31] 1b
                    ld        a,(hl)                        ;[1b32] 7e
                    inc       hl                            ;[1b33] 23
                    cp        $20                           ;[1b34] fe 20
                    ld        a,$00                         ;[1b36] 3e 00
                    ret       nz                            ;[1b38] c0
                    djnz      $1b32                         ;[1b39] 10 f7
                    ret                                     ;[1b3b] c9

                    push      af                            ;[1b3c] f5
                    ld        a,c                           ;[1b3d] 79
                    cp        $20                           ;[1b3e] fe 20
                    jr        c,$1b46                       ;[1b40] 38 04
                    cp        $80                           ;[1b42] fe 80
                    jr        c,$1b4a                       ;[1b44] 38 04
                    pop       af                            ;[1b46] f1
                    cp        $5f                           ;[1b47] fe 5f
                    ret                                     ;[1b49] c9

                    pop       af                            ;[1b4a] f1
                    or        $20                           ;[1b4b] f6 20
                    cp        $61                           ;[1b4d] fe 61
                    ret       c                             ;[1b4f] d8
                    cp        $7b                           ;[1b50] fe 7b
                    jr        nc,$1b58                      ;[1b52] 30 04
                    set       5,c                           ;[1b54] cb e9
                    cp        c                             ;[1b56] b9
                    ret                                     ;[1b57] c9

                    cp        $00                           ;[1b58] fe 00
                    ret                                     ;[1b5a] c9

                    ld        ixl,$41                       ;[1b5b] dd 2e 41
                    ld        a,(hl)                        ;[1b5e] 7e
                    inc       hl                            ;[1b5f] 23
                    call      $1b94                         ;[1b60] cd 94 1b
                    ret       c                             ;[1b63] d8
                    ld        ixl,$c1                       ;[1b64] dd 2e c1
                    dec       hl                            ;[1b67] 2b
                    ld        a,(hl)                        ;[1b68] 7e
                    inc       hl                            ;[1b69] 23
                    call      $1b94                         ;[1b6a] cd 94 1b
                    ret       c                             ;[1b6d] d8
                    cp        $20                           ;[1b6e] fe 20
                    ccf                                     ;[1b70] 3f
                    ret       nc                            ;[1b71] d0
                    cp        $80                           ;[1b72] fe 80
                    ret       nc                            ;[1b74] d0
                    cp        $22                           ;[1b75] fe 22
                    ret       z                             ;[1b77] c8
                    cp        $3a                           ;[1b78] fe 3a
                    ret       z                             ;[1b7a] c8
                    cp        $3c                           ;[1b7b] fe 3c
                    ret       z                             ;[1b7d] c8
                    cp        $3e                           ;[1b7e] fe 3e
                    ret       z                             ;[1b80] c8
                    cp        $60                           ;[1b81] fe 60
                    ret       z                             ;[1b83] c8
                    cp        $7c                           ;[1b84] fe 7c
                    ret       z                             ;[1b86] c8
                    cp        $3f                           ;[1b87] fe 3f
                    jr        z,$1b8f                       ;[1b89] 28 04
                    cp        $2a                           ;[1b8b] fe 2a
                    jr        nz,$1b68                      ;[1b8d] 20 d9
                    ld        ixl,$81                       ;[1b8f] dd 2e 81
                    jr        $1b68                         ;[1b92] 18 d4
                    cp        $2f                           ;[1b94] fe 2f
                    scf                                     ;[1b96] 37
                    ret       z                             ;[1b97] c8
                    cp        $5c                           ;[1b98] fe 5c
                    scf                                     ;[1b9a] 37
                    ret       z                             ;[1b9b] c8
                    cp        $ff                           ;[1b9c] fe ff
                    ccf                                     ;[1b9e] 3f
                    ret                                     ;[1b9f] c9

                    xor       a                             ;[1ba0] af
                    ld        d,a                           ;[1ba1] 57
                    ld        e,a                           ;[1ba2] 5f
                    ld        h,a                           ;[1ba3] 67
                    ld        l,a                           ;[1ba4] 6f
                    inc       a                             ;[1ba5] 3c
                    ld        ix,$013c                      ;[1ba6] dd 21 3c 01
                    call      $01f0                         ;[1baa] cd f0 01
                    ret                                     ;[1bad] c9

                    ld        b,$03                         ;[1bae] 06 03
                    ld        ix,$0127                      ;[1bb0] dd 21 27 01
                    call      $01f0                         ;[1bb4] cd f0 01
                    ret                                     ;[1bb7] c9

                    ld        b,$00                         ;[1bb8] 06 00
                    push      bc                            ;[1bba] c5
                    push      hl                            ;[1bbb] e5
                    ld        b,$01                         ;[1bbc] 06 01
                    ld        ix,$0127                      ;[1bbe] dd 21 27 01
                    call      $01f0                         ;[1bc2] cd f0 01
                    pop       de                            ;[1bc5] d1
                    pop       bc                            ;[1bc6] c1
                    ret       nc                            ;[1bc7] d0
                    push      af                            ;[1bc8] f5
                    push      hl                            ;[1bc9] e5
                    ex        de,hl                         ;[1bca] eb
                    ld        a,(hl)                        ;[1bcb] 7e
                    cp        $2f                           ;[1bcc] fe 2f
                    jr        z,$1bd4                       ;[1bce] 28 04
                    cp        $5c                           ;[1bd0] fe 5c
                    jr        nz,$1bee                      ;[1bd2] 20 1a
                    inc       hl                            ;[1bd4] 23
                    push      bc                            ;[1bd5] c5
                    push      hl                            ;[1bd6] e5
                    call      $1bae                         ;[1bd7] cd ae 1b
                    pop       hl                            ;[1bda] e1
                    pop       bc                            ;[1bdb] c1
                    ld        a,b                           ;[1bdc] 78
                    and       a                             ;[1bdd] a7
                    jr        z,$1bee                       ;[1bde] 28 0e
                    dec       bc                            ;[1be0] 0b
                    ld        a,(bc)                        ;[1be1] 0a
                    cp        $3a                           ;[1be2] fe 3a
                    jr        nz,$1be0                      ;[1be4] 20 fa
                    inc       bc                            ;[1be6] 03
                    ld        a,$2f                         ;[1be7] 3e 2f
                    ld        (bc),a                        ;[1be9] 02
                    inc       bc                            ;[1bea] 03
                    ld        a,$ff                         ;[1beb] 3e ff
                    ld        (bc),a                        ;[1bed] 02
                    push      bc                            ;[1bee] c5
                    push      hl                            ;[1bef] e5
                    call      $1b5b                         ;[1bf0] cd 5b 1b
                    pop       de                            ;[1bf3] d1
                    pop       bc                            ;[1bf4] c1
                    inc       a                             ;[1bf5] 3c
                    jr        nc,$1c0a                      ;[1bf6] 30 12
                    jr        nz,$1c0f                      ;[1bf8] 20 15
                    scf                                     ;[1bfa] 37
                    sbc       hl,de                         ;[1bfb] ed 52
                    inc       h                             ;[1bfd] 24
                    dec       h                             ;[1bfe] 25
                    jr        nz,$1c0a                      ;[1bff] 20 09
                    ex        de,hl                         ;[1c01] eb
                    pop       de                            ;[1c02] d1
                    pop       af                            ;[1c03] f1
                    ld        ixh,a                         ;[1c04] dd 67
                    push      ix                            ;[1c06] dd e5
                    pop       af                            ;[1c08] f1
                    ret                                     ;[1c09] c9

                    ld        a,$14                         ;[1c0a] 3e 14
                    pop       hl                            ;[1c0c] e1
                    pop       bc                            ;[1c0d] c1
                    ret                                     ;[1c0e] c9

                    push      hl                            ;[1c0f] e5
                    scf                                     ;[1c10] 37
                    sbc       hl,de                         ;[1c11] ed 52
                    ld        a,h                           ;[1c13] 7c
                    pop       hl                            ;[1c14] e1
                    jr        z,$1c0a                       ;[1c15] 28 f3
                    and       a                             ;[1c17] a7
                    jr        nz,$1c0a                      ;[1c18] 20 f0
                    dec       hl                            ;[1c1a] 2b
                    push      hl                            ;[1c1b] e5
                    ld        a,(hl)                        ;[1c1c] 7e
                    push      af                            ;[1c1d] f5
                    ld        (hl),$ff                      ;[1c1e] 36 ff
                    ex        de,hl                         ;[1c20] eb
                    call      $1c35                         ;[1c21] cd 35 1c
                    pop       de                            ;[1c24] d1
                    pop       hl                            ;[1c25] e1
                    ld        (hl),d                        ;[1c26] 72
                    inc       hl                            ;[1c27] 23
                    jr        nc,$1c0c                      ;[1c28] 30 e2
                    ld        a,(hl)                        ;[1c2a] 7e
                    inc       a                             ;[1c2b] 3c
                    jr        nz,$1bee                      ;[1c2c] 20 c0
                    ld        ixl,$41                       ;[1c2e] dd 2e 41
                    jr        $1c02                         ;[1c31] 18 cf
                    ld        b,$00                         ;[1c33] 06 00
                    ld        a,(hl)                        ;[1c35] 7e
                    cp        $2e                           ;[1c36] fe 2e
                    jr        nz,$1c40                      ;[1c38] 20 06
                    inc       hl                            ;[1c3a] 23
                    ld        a,(hl)                        ;[1c3b] 7e
                    dec       hl                            ;[1c3c] 2b
                    inc       a                             ;[1c3d] 3c
                    scf                                     ;[1c3e] 37
                    ret       z                             ;[1c3f] c8
                    push      bc                            ;[1c40] c5
                    push      hl                            ;[1c41] e5
                    call      $1c8b                         ;[1c42] cd 8b 1c
                    pop       hl                            ;[1c45] e1
                    pop       bc                            ;[1c46] c1
                    ld        a,$45                         ;[1c47] 3e 45
                    ret       nc                            ;[1c49] d0
                    ccf                                     ;[1c4a] 3f
                    ret       z                             ;[1c4b] c8
                    ld        a,b                           ;[1c4c] 78
                    and       a                             ;[1c4d] a7
                    jr        z,$1c7f                       ;[1c4e] 28 2f
                    ld        a,(hl)                        ;[1c50] 7e
                    cp        $2e                           ;[1c51] fe 2e
                    jr        nz,$1c6d                      ;[1c53] 20 18
                    inc       hl                            ;[1c55] 23
                    cp        (hl)                          ;[1c56] be
                    jr        nz,$1c6d                      ;[1c57] 20 14
                    inc       hl                            ;[1c59] 23
                    ld        a,(hl)                        ;[1c5a] 7e
                    inc       a                             ;[1c5b] 3c
                    jr        nz,$1c6d                      ;[1c5c] 20 0f
                    dec       bc                            ;[1c5e] 0b
                    dec       bc                            ;[1c5f] 0b
                    ld        a,(bc)                        ;[1c60] 0a
                    call      $1b94                         ;[1c61] cd 94 1b
                    jr        c,$1c7b                       ;[1c64] 38 15
                    cp        $3a                           ;[1c66] fe 3a
                    jr        nz,$1c5f                      ;[1c68] 20 f5
                    inc       bc                            ;[1c6a] 03
                    jr        $1c78                         ;[1c6b] 18 0b
                    ld        d,b                           ;[1c6d] 50
                    ld        e,c                           ;[1c6e] 59
                    ld        hl,$21c1                      ;[1c6f] 21 c1 21
                    call      $1083                         ;[1c72] cd 83 10
                    ld        b,d                           ;[1c75] 42
                    ld        c,e                           ;[1c76] 4b
                    dec       bc                            ;[1c77] 0b
                    ld        a,$2f                         ;[1c78] 3e 2f
                    ld        (bc),a                        ;[1c7a] 02
                    inc       bc                            ;[1c7b] 03
                    ld        a,$ff                         ;[1c7c] 3e ff
                    ld        (bc),a                        ;[1c7e] 02
                    push      bc                            ;[1c7f] c5
                    ld        b,$05                         ;[1c80] 06 05
                    ld        ix,$0127                      ;[1c82] dd 21 27 01
                    call      $01f0                         ;[1c86] cd f0 01
                    pop       bc                            ;[1c89] c1
                    ret                                     ;[1c8a] c9

                    push      hl                            ;[1c8b] e5
                    call      $1ba0                         ;[1c8c] cd a0 1b
                    pop       hl                            ;[1c8f] e1
                    ld        de,$21c0                      ;[1c90] 11 c0 21
                    ld        bc,$2000                      ;[1c93] 01 00 20
                    call      $19f4                         ;[1c96] cd f4 19
                    ret       nc                            ;[1c99] d0
                    ld        a,($21c0)                     ;[1c9a] 3a c0 21
                    bit       4,a                           ;[1c9d] cb 67
                    scf                                     ;[1c9f] 37
                    ret                                     ;[1ca0] c9

                    call      $10c1                         ;[1ca1] cd c1 10
                    push      de                            ;[1ca4] d5
                    push      bc                            ;[1ca5] c5
                    call      $123e                         ;[1ca6] cd 3e 12
                    pop       bc                            ;[1ca9] c1
                    ld        c,e                           ;[1caa] 4b
                    pop       de                            ;[1cab] d1
                    ret       nc                            ;[1cac] d0
                    push      bc                            ;[1cad] c5
                    ld        a,c                           ;[1cae] 79
                    ld        (de),a                        ;[1caf] 12
                    inc       de                            ;[1cb0] 13
                    ld        b,$0b                         ;[1cb1] 06 0b
                    ld        a,(hl)                        ;[1cb3] 7e
                    inc       hl                            ;[1cb4] 23
                    cp        $80                           ;[1cb5] fe 80
                    jr        nc,$1cbd                      ;[1cb7] 30 04
                    cp        $20                           ;[1cb9] fe 20
                    jr        nc,$1cbf                      ;[1cbb] 30 02
                    ld        a,$5f                         ;[1cbd] 3e 5f
                    ld        (de),a                        ;[1cbf] 12
                    inc       de                            ;[1cc0] 13
                    djnz      $1cb3                         ;[1cc1] 10 f0
                    add       hl,$000b                      ;[1cc3] ed 34 0b 00
                    ld        bc,$0004                      ;[1cc7] 01 04 00
                    ldir                                    ;[1cca] ed b0
                    inc       hl                            ;[1ccc] 23
                    inc       hl                            ;[1ccd] 23
                    ld        c,$04                         ;[1cce] 0e 04
                    ldir                                    ;[1cd0] ed b0
                    pop       bc                            ;[1cd2] c1
                    push      de                            ;[1cd3] d5
                    bit       6,b                           ;[1cd4] cb 70
                    jr        z,$1d19                       ;[1cd6] 28 41
                    dec       hl                            ;[1cd8] 2b
                    ld        a,(hl)                        ;[1cd9] 7e
                    dec       hl                            ;[1cda] 2b
                    or        (hl)                          ;[1cdb] b6
                    dec       hl                            ;[1cdc] 2b
                    or        (hl)                          ;[1cdd] b6
                    dec       hl                            ;[1cde] 2b
                    jr        nz,$1ce5                      ;[1cdf] 20 04
                    bit       7,(hl)                        ;[1ce1] cb 7e
                    jr        z,$1d0c                       ;[1ce3] 28 27
                    add       hl,$fff8                      ;[1ce5] ed 34 f8 ff
                    ld        e,(hl)                        ;[1ce9] 5e
                    inc       hl                            ;[1cea] 23
                    ld        d,(hl)                        ;[1ceb] 56
                    add       hl,$0005                      ;[1cec] ed 34 05 00
                    ld        c,(hl)                        ;[1cf0] 4e
                    inc       hl                            ;[1cf1] 23
                    ld        b,(hl)                        ;[1cf2] 46
                    xor       a                             ;[1cf3] af
                    call      $06ee                         ;[1cf4] cd ee 06
                    jr        nc,$1d0c                      ;[1cf7] 30 13
                    call      $19ae                         ;[1cf9] cd ae 19
                    jr        nz,$1d0c                      ;[1cfc] 20 0e
                    ex        de,hl                         ;[1cfe] eb
                    add       hl,$0004                      ;[1cff] ed 34 04 00
                    pop       de                            ;[1d03] d1
                    push      de                            ;[1d04] d5
                    ld        bc,$0008                      ;[1d05] 01 08 00
                    ldir                                    ;[1d08] ed b0
                    jr        $1d19                         ;[1d0a] 18 0d
                    pop       de                            ;[1d0c] d1
                    push      de                            ;[1d0d] d5
                    ld        a,$ff                         ;[1d0e] 3e ff
                    ld        (de),a                        ;[1d10] 12
                    inc       de                            ;[1d11] 13
                    ld        b,$07                         ;[1d12] 06 07
                    xor       a                             ;[1d14] af
                    ld        (de),a                        ;[1d15] 12
                    inc       de                            ;[1d16] 13
                    djnz      $1d15                         ;[1d17] 10 fc
                    pop       de                            ;[1d19] d1
                    add       de,$000a                      ;[1d1a] ed 35 0a 00
                    push      de                            ;[1d1e] d5
                    call      $1067                         ;[1d1f] cd 67 10
                    pop       hl                            ;[1d22] e1
                    push      de                            ;[1d23] d5
                    ex        de,hl                         ;[1d24] eb
                    and       a                             ;[1d25] a7
                    sbc       hl,de                         ;[1d26] ed 52
                    ex        de,hl                         ;[1d28] eb
                    dec       hl                            ;[1d29] 2b
                    ld        (hl),d                        ;[1d2a] 72
                    dec       hl                            ;[1d2b] 2b
                    ld        (hl),e                        ;[1d2c] 73
                    pop       de                            ;[1d2d] d1
                    scf                                     ;[1d2e] 37
                    ret                                     ;[1d2f] c9

                    call      $10c1                         ;[1d30] cd c1 10
                    jr        nc,$1d9a                      ;[1d33] 30 65
                    ld        de,$0000                      ;[1d35] 11 00 00
                    ld        l,(iy+$08)                    ;[1d38] fd 6e 08
                    ld        h,(iy+$09)                    ;[1d3b] fd 66 09
                    ld        b,$04                         ;[1d3e] 06 04
                    bit       7,(iy+$02)                    ;[1d40] fd cb 02 7e
                    jr        nz,$1d84                      ;[1d44] 20 3e
                    push      de                            ;[1d46] d5
                    push      de                            ;[1d47] d5
                    rst       $28                           ;[1d48] ef
                    inc       bc                            ;[1d49] 03
                    push      de                            ;[1d4a] d5
                    push      bc                            ;[1d4b] c5
                    rst       $28                           ;[1d4c] ef
                    ex        af,af'                        ;[1d4d] 08
                    ld        h,b                           ;[1d4e] 60
                    ld        l,c                           ;[1d4f] 69
                    pop       bc                            ;[1d50] c1
                    and       a                             ;[1d51] a7
                    sbc       hl,bc                         ;[1d52] ed 42
                    ex        de,hl                         ;[1d54] eb
                    pop       de                            ;[1d55] d1
                    jr        nz,$1d5c                      ;[1d56] 20 04
                    sbc       hl,de                         ;[1d58] ed 52
                    jr        z,$1d72                       ;[1d5a] 28 16
                    call      $0912                         ;[1d5c] cd 12 09
                    pop       hl                            ;[1d5f] e1
                    jr        nc,$1d98                      ;[1d60] 30 36
                    call      $1fef                         ;[1d62] cd ef 1f
                    add       a                             ;[1d65] 87
                    jr        c,$1d6c                       ;[1d66] 38 04
                    add       h                             ;[1d68] 84
                    ld        h,a                           ;[1d69] 67
                    jr        nc,$1d6f                      ;[1d6a] 30 03
                    ex        (sp),hl                       ;[1d6c] e3
                    inc       hl                            ;[1d6d] 23
                    ex        (sp),hl                       ;[1d6e] e3
                    push      hl                            ;[1d6f] e5
                    jr        $1d4a                         ;[1d70] 18 d8
                    pop       hl                            ;[1d72] e1
                    pop       de                            ;[1d73] d1
                    ld        a,(iy+$07)                    ;[1d74] fd 7e 07
                    add       a                             ;[1d77] 87
                    add       h                             ;[1d78] 84
                    ld        h,a                           ;[1d79] 67
                    jr        nc,$1d7d                      ;[1d7a] 30 01
                    inc       de                            ;[1d7c] 13
                    ld        l,h                           ;[1d7d] 6c
                    ld        h,e                           ;[1d7e] 63
                    ld        e,d                           ;[1d7f] 5a
                    ld        d,$00                         ;[1d80] 16 00
                    ld        b,$03                         ;[1d82] 06 03
                    add       hl,hl                         ;[1d84] 29
                    ex        de,hl                         ;[1d85] eb
                    adc       hl,hl                         ;[1d86] ed 6a
                    ex        de,hl                         ;[1d88] eb
                    djnz      $1d84                         ;[1d89] 10 f9
                    ld        c,(iy+$0c)                    ;[1d8b] fd 4e 0c
                    ld        b,$00                         ;[1d8e] 06 00
                    add       hl,bc                         ;[1d90] 09
                    ex        de,hl                         ;[1d91] eb
                    ld        c,b                           ;[1d92] 48
                    adc       hl,bc                         ;[1d93] ed 4a
                    ex        de,hl                         ;[1d95] eb
                    scf                                     ;[1d96] 37
                    ret                                     ;[1d97] c9

                    pop       hl                            ;[1d98] e1
                    ret                                     ;[1d99] c9

                    jp        z,$113f                       ;[1d9a] ca 3f 11
                    push      de                            ;[1d9d] d5
                    push      hl                            ;[1d9e] e5
                    call      $10f7                         ;[1d9f] cd f7 10
                    pop       hl                            ;[1da2] e1
                    pop       de                            ;[1da3] d1
                    ld        a,d                           ;[1da4] 7a
                    or        e                             ;[1da5] b3
                    or        h                             ;[1da6] b4
                    or        l                             ;[1da7] b5
                    scf                                     ;[1da8] 37
                    ret       z                             ;[1da9] c8
                    dec       hl                            ;[1daa] 2b
                    ld        a,h                           ;[1dab] 7c
                    and       l                             ;[1dac] a5
                    inc       a                             ;[1dad] 3c
                    jr        nz,$1db1                      ;[1dae] 20 01
                    dec       de                            ;[1db0] 1b
                    push      de                            ;[1db1] d5
                    push      hl                            ;[1db2] e5
                    call      $113f                         ;[1db3] cd 3f 11
                    jr        $1da2                         ;[1db6] 18 ea
                    ld        iyh,a                         ;[1db8] fd 67
                    call      $0204                         ;[1dba] cd 04 02
                    call      $040d                         ;[1dbd] cd 0d 04
                    jp        $c91d                         ;[1dc0] c3 1d c9
                    inc       b                             ;[1dc3] 04
                    dec       b                             ;[1dc4] 05
                    jr        z,$1dd8                       ;[1dc5] 28 11
                    dec       b                             ;[1dc7] 05
                    jp        nz,$3d01                      ;[1dc8] c2 01 3d
                    push      hl                            ;[1dcb] e5
                    call      $1df0                         ;[1dcc] cd f0 1d
                    pop       de                            ;[1dcf] d1
                    ld        a,(hl)                        ;[1dd0] 7e
                    ldi                                     ;[1dd1] ed a0
                    inc       a                             ;[1dd3] 3c
                    jr        nz,$1dd0                      ;[1dd4] 20 fa
                    scf                                     ;[1dd6] 37
                    ret                                     ;[1dd7] c9

                    push      af                            ;[1dd8] f5
                    call      $1df0                         ;[1dd9] cd f0 1d
                    pop       de                            ;[1ddc] d1
                    ret       nc                            ;[1ddd] d0
                    ld        a,d                           ;[1dde] 7a
                    push      af                            ;[1ddf] f5
                    call      $1e7a                         ;[1de0] cd 7a 1e
                    pop       af                            ;[1de3] f1
                    and       $0f                           ;[1de4] e6 0f
                    ld        b,$02                         ;[1de6] 06 02
                    ld        ix,$0127                      ;[1de8] dd 21 27 01
                    call      $01f0                         ;[1dec] cd f0 01
                    ret                                     ;[1def] c9

                    push      hl                            ;[1df0] e5
                    push      af                            ;[1df1] f5
                    call      $1e05                         ;[1df2] cd 05 1e
                    pop       af                            ;[1df5] f1
                    ex        (sp),hl                       ;[1df6] e3
                    ld        b,d                           ;[1df7] 42
                    ld        c,e                           ;[1df8] 4b
                    call      $1bba                         ;[1df9] cd ba 1b
                    pop       de                            ;[1dfc] d1
                    ret       nc                            ;[1dfd] d0
                    push      de                            ;[1dfe] d5
                    call      m,$1c35                       ;[1dff] fc 35 1c
                    pop       de                            ;[1e02] d1
                    ex        de,hl                         ;[1e03] eb
                    ret                                     ;[1e04] c9

                    inc       de                            ;[1e05] 13
                    inc       de                            ;[1e06] 13
                    and       $0f                           ;[1e07] e6 0f
                    push      af                            ;[1e09] f5
                    add       $20                           ;[1e0a] c6 20
                    ld        h,a                           ;[1e0c] 67
                    ld        l,$00                         ;[1e0d] 2e 00
                    ld        bc,$00fe                      ;[1e0f] 01 fe 00
                    ld        a,$09                         ;[1e12] 3e 09
                    push      de                            ;[1e14] d5
                    call      $0769                         ;[1e15] cd 69 07
                    pop       hl                            ;[1e18] e1
                    pop       de                            ;[1e19] d1
                    ld        a,(hl)                        ;[1e1a] 7e
                    cpl                                     ;[1e1b] 2f
                    inc       hl                            ;[1e1c] 23
                    cp        (hl)                          ;[1e1d] be
                    jr        nz,$1e31                      ;[1e1e] 20 11
                    and       $0f                           ;[1e20] e6 0f
                    cp        d                             ;[1e22] ba
                    jr        nz,$1e31                      ;[1e23] 20 0c
                    ld        d,(hl)                        ;[1e25] 56
                    inc       hl                            ;[1e26] 23
                    ld        a,(hl)                        ;[1e27] 7e
                    dec       hl                            ;[1e28] 2b
                    cp        $2f                           ;[1e29] fe 2f
                    jr        z,$1e3a                       ;[1e2b] 28 0d
                    cp        $5c                           ;[1e2d] fe 5c
                    jr        z,$1e3a                       ;[1e2f] 28 09
                    ld        (hl),d                        ;[1e31] 72
                    inc       hl                            ;[1e32] 23
                    ld        (hl),$2f                      ;[1e33] 36 2f
                    inc       hl                            ;[1e35] 23
                    ld        (hl),$ff                      ;[1e36] 36 ff
                    dec       hl                            ;[1e38] 2b
                    dec       hl                            ;[1e39] 2b
                    ld        (hl),$3a                      ;[1e3a] 36 3a
                    ld        a,d                           ;[1e3c] 7a
                    and       $0f                           ;[1e3d] e6 0f
                    add       $41                           ;[1e3f] c6 41
                    dec       hl                            ;[1e41] 2b
                    ld        (hl),a                        ;[1e42] 77
                    ld        b,$fd                         ;[1e43] 06 fd
                    ld        a,d                           ;[1e45] 7a
                    call      $041f                         ;[1e46] cd 1f 04
                    nop                                     ;[1e49] 00
                    inc       b                             ;[1e4a] 04
                    ld        (bc),a                        ;[1e4b] 02
                    bit       6,a                           ;[1e4c] cb 77
                    jr        z,$1e65                       ;[1e4e] 28 15
                    dec       hl                            ;[1e50] 2b
                    inc       b                             ;[1e51] 04
                    ld        a,d                           ;[1e52] 7a
                    swapnib                                 ;[1e53] ed 23
                    and       $0f                           ;[1e55] e6 0f
                    cp        $0a                           ;[1e57] fe 0a
                    jr        c,$1e62                       ;[1e59] 38 07
                    add       $26                           ;[1e5b] c6 26
                    ld        (hl),a                        ;[1e5d] 77
                    dec       hl                            ;[1e5e] 2b
                    inc       b                             ;[1e5f] 04
                    ld        a,$01                         ;[1e60] 3e 01
                    add       $30                           ;[1e62] c6 30
                    ld        (hl),a                        ;[1e64] 77
                    ld        c,d                           ;[1e65] 4a
                    ld        d,h                           ;[1e66] 54
                    ld        e,l                           ;[1e67] 5d
                    inc       de                            ;[1e68] 13
                    ld        a,(de)                        ;[1e69] 1a
                    inc       a                             ;[1e6a] 3c
                    ret       z                             ;[1e6b] c8
                    cp        $21                           ;[1e6c] fe 21
                    jr        c,$1e76                       ;[1e6e] 38 06
                    cp        $80                           ;[1e70] fe 80
                    jr        nc,$1e76                      ;[1e72] 30 02
                    djnz      $1e68                         ;[1e74] 10 f2
                    ld        a,$ff                         ;[1e76] 3e ff
                    ld        (de),a                        ;[1e78] 12
                    ret                                     ;[1e79] c9

                    ld        d,a                           ;[1e7a] 57
                    inc       hl                            ;[1e7b] 23
                    ld        a,(hl)                        ;[1e7c] 7e
                    cp        $3a                           ;[1e7d] fe 3a
                    jr        nz,$1e7b                      ;[1e7f] 20 fa
                    ld        (hl),d                        ;[1e81] 72
                    dec       hl                            ;[1e82] 2b
                    ld        a,(hl)                        ;[1e83] 7e
                    push      af                            ;[1e84] f5
                    ld        a,d                           ;[1e85] 7a
                    cpl                                     ;[1e86] 2f
                    ld        (hl),a                        ;[1e87] 77
                    ld        a,d                           ;[1e88] 7a
                    and       $0f                           ;[1e89] e6 0f
                    add       $20                           ;[1e8b] c6 20
                    ld        d,a                           ;[1e8d] 57
                    ld        e,$00                         ;[1e8e] 1e 00
                    ld        bc,$00fe                      ;[1e90] 01 fe 00
                    ld        a,$09                         ;[1e93] 3e 09
                    push      hl                            ;[1e95] e5
                    call      $0703                         ;[1e96] cd 03 07
                    pop       hl                            ;[1e99] e1
                    pop       af                            ;[1e9a] f1
                    ld        (hl),a                        ;[1e9b] 77
                    inc       hl                            ;[1e9c] 23
                    ld        (hl),$3a                      ;[1e9d] 36 3a
                    ret                                     ;[1e9f] c9

                    xor       a                             ;[1ea0] af
                    out       ($e3),a                       ;[1ea1] d3 e3
                    jp        $1ff9                         ;[1ea3] c3 f9 1f
                    nop                                     ;[1ea6] 00
                    nop                                     ;[1ea7] 00
                    nop                                     ;[1ea8] 00
                    nop                                     ;[1ea9] 00
                    nop                                     ;[1eaa] 00
                    nop                                     ;[1eab] 00
                    nop                                     ;[1eac] 00
                    nop                                     ;[1ead] 00
                    nop                                     ;[1eae] 00
                    xor       a                             ;[1eaf] af
                    push      bc                            ;[1eb0] c5
                    push      de                            ;[1eb1] d5
                    push      hl                            ;[1eb2] e5
                    call      $1ed2                         ;[1eb3] cd d2 1e
                    ex        (sp),ix                       ;[1eb6] dd e3
                    jr        nc,$1ec9                      ;[1eb8] 30 0f
                    push      af                            ;[1eba] f5
                    bit       7,a                           ;[1ebb] cb 7f
                    jr        z,$1ec5                       ;[1ebd] 28 06
                    pop       af                            ;[1ebf] f1
                    call      $1f92                         ;[1ec0] cd 92 1f
                    jr        $1ec9                         ;[1ec3] 18 04
                    pop       af                            ;[1ec5] f1
                    call      $1f5c                         ;[1ec6] cd 5c 1f
                    pop       ix                            ;[1ec9] dd e1
                    pop       de                            ;[1ecb] d1
                    pop       bc                            ;[1ecc] c1
                    ret                                     ;[1ecd] c9

                    ld        a,$80                         ;[1ece] 3e 80
                    jr        $1eb0                         ;[1ed0] 18 de
                    ld        l,(ix+$07)                    ;[1ed2] dd 6e 07
                    ld        h,(ix+$08)                    ;[1ed5] dd 66 08
                    and       a                             ;[1ed8] a7
                    sbc       hl,de                         ;[1ed9] ed 52
                    ld        l,(ix+$09)                    ;[1edb] dd 6e 09
                    ld        h,(ix+$0a)                    ;[1ede] dd 66 0a
                    sbc       hl,bc                         ;[1ee1] ed 42
                    jr        nc,$1ee9                      ;[1ee3] 30 04
                    ld        a,$02                         ;[1ee5] 3e 02
                    and       a                             ;[1ee7] a7
                    ret                                     ;[1ee8] c9

                    ld        l,(ix+$01)                    ;[1ee9] dd 6e 01
                    ld        h,(ix+$02)                    ;[1eec] dd 66 02
                    add       hl,de                         ;[1eef] 19
                    ex        de,hl                         ;[1ef0] eb
                    ld        l,(ix+$03)                    ;[1ef1] dd 6e 03
                    ld        h,(ix+$04)                    ;[1ef4] dd 66 04
                    adc       hl,bc                         ;[1ef7] ed 4a
                    bit       1,(ix+$10)                    ;[1ef9] dd cb 10 4e
                    jr        nz,$1f09                      ;[1efd] 20 0a
                    ld        h,l                           ;[1eff] 65
                    ld        l,d                           ;[1f00] 6a
                    ld        d,e                           ;[1f01] 53
                    ld        e,$00                         ;[1f02] 1e 00
                    ex        de,hl                         ;[1f04] eb
                    add       hl,hl                         ;[1f05] 29
                    ex        de,hl                         ;[1f06] eb
                    adc       hl,hl                         ;[1f07] ed 6a
                    bit       0,(ix+$10)                    ;[1f09] dd cb 10 46
                    scf                                     ;[1f0d] 37
                    ret                                     ;[1f0e] c9

                    ld        h,$00                         ;[1f0f] 26 00
                    ld        l,$00                         ;[1f11] 2e 00
                    ld        d,l                           ;[1f13] 55
                    ld        e,l                           ;[1f14] 5d
                    ld        b,$ff                         ;[1f15] 06 ff
                    ld        c,a                           ;[1f17] 4f
                    ld        a,$fe                         ;[1f18] 3e fe
                    jr        z,$1f1e                       ;[1f1a] 28 02
                    ld        a,$fd                         ;[1f1c] 3e fd
                    out       ($e7),a                       ;[1f1e] d3 e7
                    in        a,($eb)                       ;[1f20] db eb
                    ld        a,c                           ;[1f22] 79
                    ld        c,$eb                         ;[1f23] 0e eb
                    out       (c),a                         ;[1f25] ed 79
                    ld        a,h                           ;[1f27] 7c
                    out       (c),a                         ;[1f28] ed 79
                    ld        a,l                           ;[1f2a] 7d
                    out       (c),a                         ;[1f2b] ed 79
                    ld        a,d                           ;[1f2d] 7a
                    out       (c),a                         ;[1f2e] ed 79
                    ld        a,e                           ;[1f30] 7b
                    out       (c),a                         ;[1f31] ed 79
                    ld        a,b                           ;[1f33] 78
                    out       (c),a                         ;[1f34] ed 79
                    call      $1f3d                         ;[1f36] cd 3d 1f
                    and       a                             ;[1f39] a7
                    ret       nz                            ;[1f3a] c0
                    scf                                     ;[1f3b] 37
                    ret                                     ;[1f3c] c9

                    ld        bc,$0032                      ;[1f3d] 01 32 00
                    in        a,($eb)                       ;[1f40] db eb
                    cp        $ff                           ;[1f42] fe ff
                    ret       nz                            ;[1f44] c0
                    djnz      $1f40                         ;[1f45] 10 f9
                    dec       c                             ;[1f47] 0d
                    jr        nz,$1f40                      ;[1f48] 20 f6
                    ret                                     ;[1f4a] c9

                    ld        e,$0a                         ;[1f4b] 1e 0a
                    call      $1f3d                         ;[1f4d] cd 3d 1f
                    cp        $fe                           ;[1f50] fe fe
                    jr        z,$1f5a                       ;[1f52] 28 06
                    jr        c,$1f5a                       ;[1f54] 38 04
                    dec       e                             ;[1f56] 1d
                    jr        nz,$1f4d                      ;[1f57] 20 f4
                    ccf                                     ;[1f59] 3f
                    ccf                                     ;[1f5a] 3f
                    ret                                     ;[1f5b] c9

                    ld        a,$51                         ;[1f5c] 3e 51
                    call      $1f15                         ;[1f5e] cd 15 1f
                    call      $1f4b                         ;[1f61] cd 4b 1f
                    ld        a,$00                         ;[1f64] 3e 00
                    jr        nc,$1f79                      ;[1f66] 30 11
                    push      ix                            ;[1f68] dd e5
                    pop       hl                            ;[1f6a] e1
                    ld        bc,$00eb                      ;[1f6b] 01 eb 00
                    inir                                    ;[1f6e] ed b2
                    inir                                    ;[1f70] ed b2
                    in        a,($eb)                       ;[1f72] db eb
                    nop                                     ;[1f74] 00
                    nop                                     ;[1f75] 00
                    in        a,($eb)                       ;[1f76] db eb
                    scf                                     ;[1f78] 37
                    push      af                            ;[1f79] f5
                    in        a,($eb)                       ;[1f7a] db eb
                    ld        a,$ff                         ;[1f7c] 3e ff
                    out       ($e7),a                       ;[1f7e] d3 e7
                    nop                                     ;[1f80] 00
                    in        a,($eb)                       ;[1f81] db eb
                    pop       af                            ;[1f83] f1
                    ret                                     ;[1f84] c9

                    ld        a,$4c                         ;[1f85] 3e 4c
                    call      $1f0f                         ;[1f87] cd 0f 1f
                    in        a,($eb)                       ;[1f8a] db eb
                    and       a                             ;[1f8c] a7
                    scf                                     ;[1f8d] 37
                    jr        z,$1f8a                       ;[1f8e] 28 fa
                    jr        $1f79                         ;[1f90] 18 e7
                    push      af                            ;[1f92] f5
                    ld        a,$58                         ;[1f93] 3e 58
                    call      $1f15                         ;[1f95] cd 15 1f
                    jr        nc,$1fd9                      ;[1f98] 30 3f
                    ld        a,$fe                         ;[1f9a] 3e fe
                    out       ($eb),a                       ;[1f9c] d3 eb
                    ld        bc,$00eb                      ;[1f9e] 01 eb 00
                    push      ix                            ;[1fa1] dd e5
                    pop       hl                            ;[1fa3] e1
                    otir                                    ;[1fa4] ed b3
                    otir                                    ;[1fa6] ed b3
                    ld        a,$ff                         ;[1fa8] 3e ff
                    out       ($eb),a                       ;[1faa] d3 eb
                    nop                                     ;[1fac] 00
                    nop                                     ;[1fad] 00
                    out       ($eb),a                       ;[1fae] d3 eb
                    call      $1f3d                         ;[1fb0] cd 3d 1f
                    and       $1f                           ;[1fb3] e6 1f
                    cp        $05                           ;[1fb5] fe 05
                    jr        nz,$1fd9                      ;[1fb7] 20 20
                    call      $1f3d                         ;[1fb9] cd 3d 1f
                    and       a                             ;[1fbc] a7
                    jr        z,$1fb9                       ;[1fbd] 28 fa
                    pop       af                            ;[1fbf] f1
                    ld        a,$4d                         ;[1fc0] 3e 4d
                    push      hl                            ;[1fc2] e5
                    call      $1f0f                         ;[1fc3] cd 0f 1f
                    pop       hl                            ;[1fc6] e1
                    jr        nc,$1fda                      ;[1fc7] 30 11
                    in        a,($eb)                       ;[1fc9] db eb
                    and       a                             ;[1fcb] a7
                    scf                                     ;[1fcc] 37
                    jr        z,$1fdd                       ;[1fcd] 28 0e
                    and       $23                           ;[1fcf] e6 23
                    ld        a,$01                         ;[1fd1] 3e 01
                    jr        nz,$1fdd                      ;[1fd3] 20 08
                    ld        a,$03                         ;[1fd5] 3e 03
                    jr        $1fdd                         ;[1fd7] 18 04
                    pop       af                            ;[1fd9] f1
                    ld        a,$00                         ;[1fda] 3e 00
                    and       a                             ;[1fdc] a7
                    jr        $1f79                         ;[1fdd] 18 9a
                    inc       (ix+$2b)                      ;[1fdf] dd 34 2b
                    ret       nz                            ;[1fe2] c0
                    inc       (ix+$2c)                      ;[1fe3] dd 34 2c
                    ret       nz                            ;[1fe6] c0
                    inc       (ix+$2d)                      ;[1fe7] dd 34 2d
                    ret       nz                            ;[1fea] c0
                    inc       (ix+$2e)                      ;[1feb] dd 34 2e
                    ret                                     ;[1fee] c9

                    ld        a,(ix+$21)                    ;[1fef] dd 7e 21
                    ret                                     ;[1ff2] c9

                    push      af                            ;[1ff3] f5
                    ld        a,$80                         ;[1ff4] 3e 80
                    out       ($e3),a                       ;[1ff6] d3 e3
                    pop       af                            ;[1ff8] f1
                    ret                                     ;[1ff9] c9

                    ret                                     ;[1ffa] c9

                    rst       $20                           ;[1ffb] e7
                    pop       hl                            ;[1ffc] e1
                    pop       af                            ;[1ffd] f1
                    ei                                      ;[1ffe] fb
                    ret                                     ;[1fff] c9

