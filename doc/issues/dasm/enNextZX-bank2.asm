                    nop                                     ;[0000] 00
                    jr        $0000                         ;[0001] 18 fd
                    nop                                     ;[0003] 00
                    nop                                     ;[0004] 00
                    nop                                     ;[0005] 00
                    nop                                     ;[0006] 00
                    nop                                     ;[0007] 00
                    jp        $3f18                         ;[0008] c3 18 3f
                    ld        l,$62                         ;[000b] 2e 62
                    ld        h,c                           ;[000d] 61
                    ld        l,e                           ;[000e] 6b
                    rst       $38                           ;[000f] ff
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
                    push      bc                            ;[0039] c5
                    push      hl                            ;[003a] e5
                    ld        bc,$243b                      ;[003b] 01 3b 24
                    in        l,(c)                         ;[003e] ed 68
                    push      hl                            ;[0040] e5
                    push      bc                            ;[0041] c5
                    xor       a                             ;[0042] af
                    ld        hl,$0045                      ;[0043] 21 45 00
                    call      $3f31                         ;[0046] cd 31 3f
                    pop       bc                            ;[0049] c1
                    pop       hl                            ;[004a] e1
                    out       (c),l                         ;[004b] ed 69
                    pop       hl                            ;[004d] e1
                    pop       bc                            ;[004e] c1
                    pop       af                            ;[004f] f1
                    ei                                      ;[0050] fb
                    ret                                     ;[0051] c9

                    ld        a,$3a                         ;[0052] 3e 3a
                    and       a                             ;[0054] a7
                    ret                                     ;[0055] c9

                    jp        $116b                         ;[0056] c3 6b 11
                    jp        $14d7                         ;[0059] c3 d7 14
                    jp        $157e                         ;[005c] c3 7e 15
                    jp        $1574                         ;[005f] c3 74 15
                    jp        $1583                         ;[0062] c3 83 15
                    nop                                     ;[0065] 00
                    retn                                    ;[0066] ed 45

                    rst       $08                           ;[0068] cf
                    ld        bc,$04dd                      ;[0069] 01 dd 04
                    ret                                     ;[006c] c9

                    rst       $08                           ;[006d] cf
                    ld        bc,$001d                      ;[006e] 01 1d 00
                    ret                                     ;[0071] c9

                    rst       $08                           ;[0072] cf
                    ld        bc,$02e0                      ;[0073] 01 e0 02
                    ret                                     ;[0076] c9

                    nop                                     ;[0077] 00
                    nop                                     ;[0078] 00
                    nop                                     ;[0079] 00
                    nop                                     ;[007a] 00
                    nop                                     ;[007b] 00
                    nop                                     ;[007c] 00
                    nop                                     ;[007d] 00
                    nop                                     ;[007e] 00
                    nop                                     ;[007f] 00
                    nop                                     ;[0080] 00
                    nop                                     ;[0081] 00
                    nop                                     ;[0082] 00
                    nop                                     ;[0083] 00
                    nop                                     ;[0084] 00
                    nop                                     ;[0085] 00
                    nop                                     ;[0086] 00
                    nop                                     ;[0087] 00
                    nop                                     ;[0088] 00
                    nop                                     ;[0089] 00
                    nop                                     ;[008a] 00
                    nop                                     ;[008b] 00
                    nop                                     ;[008c] 00
                    nop                                     ;[008d] 00
                    nop                                     ;[008e] 00
                    nop                                     ;[008f] 00
                    nop                                     ;[0090] 00
                    nop                                     ;[0091] 00
                    nop                                     ;[0092] 00
                    nop                                     ;[0093] 00
                    nop                                     ;[0094] 00
                    nop                                     ;[0095] 00
                    nop                                     ;[0096] 00
                    nop                                     ;[0097] 00
                    nop                                     ;[0098] 00
                    nop                                     ;[0099] 00
                    nop                                     ;[009a] 00
                    nop                                     ;[009b] 00
                    nop                                     ;[009c] 00
                    nop                                     ;[009d] 00
                    nop                                     ;[009e] 00
                    nop                                     ;[009f] 00
                    jp        $1a89                         ;[00a0] c3 89 1a
                    jp        $19c7                         ;[00a3] c3 c7 19
                    jp        $1af8                         ;[00a6] c3 f8 1a
                    jp        $01db                         ;[00a9] c3 db 01
                    jp        $1863                         ;[00ac] c3 63 18
                    jp        $1892                         ;[00af] c3 92 18
                    jp        $0052                         ;[00b2] c3 52 00
                    jp        $01e6                         ;[00b5] c3 e6 01
                    jp        $0052                         ;[00b8] c3 52 00
                    jp        $0052                         ;[00bb] c3 52 00
                    jp        $0052                         ;[00be] c3 52 00
                    jp        $0052                         ;[00c1] c3 52 00
                    jp        $01eb                         ;[00c4] c3 eb 01
                    jp        $0052                         ;[00c7] c3 52 00
                    jp        $0052                         ;[00ca] c3 52 00
                    jp        $0740                         ;[00cd] c3 40 07
                    jp        $07cf                         ;[00d0] c3 cf 07
                    jp        $0052                         ;[00d3] c3 52 00
                    jp        $0052                         ;[00d6] c3 52 00
                    jp        $16c5                         ;[00d9] c3 c5 16
                    jp        $178f                         ;[00dc] c3 8f 17
                    jp        $16a4                         ;[00df] c3 a4 16
                    jp        $162d                         ;[00e2] c3 2d 16
                    jp        $16c1                         ;[00e5] c3 c1 16
                    jp        $15d6                         ;[00e8] c3 d6 15
                    jp        $15e2                         ;[00eb] c3 e2 15
                    jp        $17b4                         ;[00ee] c3 b4 17
                    jp        $03cf                         ;[00f1] c3 cf 03
                    jp        $0451                         ;[00f4] c3 51 04
                    jp        $0478                         ;[00f7] c3 78 04
                    jp        $0052                         ;[00fa] c3 52 00
                    jp        $082e                         ;[00fd] c3 2e 08
                    jp        $2560                         ;[0100] c3 60 25
                    jp        $2562                         ;[0103] c3 62 25
                    jp        $0322                         ;[0106] c3 22 03
                    jp        $0258                         ;[0109] c3 58 02
                    jp        $0269                         ;[010c] c3 69 02
                    jp        $0226                         ;[010f] c3 26 02
                    jp        $027a                         ;[0112] c3 7a 02
                    jp        $02d6                         ;[0115] c3 d6 02
                    jp        $022c                         ;[0118] c3 2c 02
                    jp        $0232                         ;[011b] c3 32 02
                    jp        $0200                         ;[011e] c3 00 02
                    jp        $01f0                         ;[0121] c3 f0 01
                    jp        $2848                         ;[0124] c3 48 28
                    jp        $285d                         ;[0127] c3 5d 28
                    jp        $2572                         ;[012a] c3 72 25
                    jp        $0550                         ;[012d] c3 50 05
                    jp        $055e                         ;[0130] c3 5e 05
                    jp        $0238                         ;[0133] c3 38 02
                    jp        $023e                         ;[0136] c3 3e 02
                    jp        $0246                         ;[0139] c3 46 02
                    jp        $05e0                         ;[013c] c3 e0 05
                    jp        $05e5                         ;[013f] c3 e5 05
                    jp        $01f8                         ;[0142] c3 f8 01
                    jp        $024c                         ;[0145] c3 4c 02
                    jp        $0214                         ;[0148] c3 14 02
                    jp        $0052                         ;[014b] c3 52 00
                    jp        $25d5                         ;[014e] c3 d5 25
                    jp        $2119                         ;[0151] c3 19 21
                    jp        $25cf                         ;[0154] c3 cf 25
                    jp        $25d3                         ;[0157] c3 d3 25
                    jp        $25d3                         ;[015a] c3 d3 25
                    jp        $25d3                         ;[015d] c3 d3 25
                    jp        $25d3                         ;[0160] c3 d3 25
                    jp        $27bf                         ;[0163] c3 bf 27
                    jp        $25cf                         ;[0166] c3 cf 25
                    jp        $25cf                         ;[0169] c3 cf 25
                    jp        $25cf                         ;[016c] c3 cf 25
                    jp        $25cf                         ;[016f] c3 cf 25
                    jp        $25cf                         ;[0172] c3 cf 25
                    jp        $25cf                         ;[0175] c3 cf 25
                    jp        $25cf                         ;[0178] c3 cf 25
                    jp        $2786                         ;[017b] c3 86 27
                    jp        $27ae                         ;[017e] c3 ae 27
                    jp        $25cf                         ;[0181] c3 cf 25
                    jp        $264f                         ;[0184] c3 4f 26
                    jp        $265e                         ;[0187] c3 5e 26
                    jp        $26a0                         ;[018a] c3 a0 26
                    jp        $25d3                         ;[018d] c3 d3 25
                    jp        $27d4                         ;[0190] c3 d4 27
                    jp        $25cf                         ;[0193] c3 cf 25
                    jp        $25d3                         ;[0196] c3 d3 25
                    jp        $25d3                         ;[0199] c3 d3 25
                    jp        $25d3                         ;[019c] c3 d3 25
                    jp        $0052                         ;[019f] c3 52 00
                    jp        $0052                         ;[01a2] c3 52 00
                    jp        $1fac                         ;[01a5] c3 ac 1f
                    jp        $0052                         ;[01a8] c3 52 00
                    jp        $0052                         ;[01ab] c3 52 00
                    jp        $0052                         ;[01ae] c3 52 00
                    jp        $0527                         ;[01b1] c3 27 05
                    jp        $01e0                         ;[01b4] c3 e0 01
                    jp        $020b                         ;[01b7] c3 0b 02
                    jp        $10df                         ;[01ba] c3 df 10
                    jp        $0e9f                         ;[01bd] c3 9f 0e
                    jp        $0f9d                         ;[01c0] c3 9d 0f
                    jp        $15b7                         ;[01c3] c3 b7 15
                    jp        $15c1                         ;[01c6] c3 c1 15
                    jp        $0fbf                         ;[01c9] c3 bf 0f
                    jp        $1008                         ;[01cc] c3 08 10
                    jp        $1010                         ;[01cf] c3 10 10
                    jp        $1a8e                         ;[01d2] c3 8e 1a
                    jp        $1016                         ;[01d5] c3 16 10
                    jp        $1099                         ;[01d8] c3 99 10
                    rst       $08                           ;[01db] cf
                    nop                                     ;[01dc] 00
                    inc       bc                            ;[01dd] 03
                    ld        bc,$79c9                      ;[01de] 01 c9 79
                    rst       $08                           ;[01e1] cf
                    nop                                     ;[01e2] 00
                    ld        b,$01                         ;[01e3] 06 01
                    ret                                     ;[01e5] c9

                    rst       $08                           ;[01e6] cf
                    nop                                     ;[01e7] 00
                    add       hl,bc                         ;[01e8] 09
                    ld        bc,$cfc9                      ;[01e9] 01 c9 cf
                    nop                                     ;[01ec] 00
                    inc       c                             ;[01ed] 0c
                    ld        bc,$cdc9                      ;[01ee] 01 c9 cd
                    or        h                             ;[01f1] b4
                    dec       b                             ;[01f2] 05
                    rst       $08                           ;[01f3] cf
                    nop                                     ;[01f4] 00
                    jr        $01f8                         ;[01f5] 18 01
                    ret                                     ;[01f7] c9

                    call      $05b4                         ;[01f8] cd b4 05
                    rst       $08                           ;[01fb] cf
                    nop                                     ;[01fc] 00
                    dec       de                            ;[01fd] 1b
                    ld        bc,$ddc9                      ;[01fe] 01 c9 dd
                    ld        l,a                           ;[0201] 6f
                    rst       $08                           ;[0202] cf
                    nop                                     ;[0203] 00
                    ld        c,a                           ;[0204] 4f
                    inc       bc                            ;[0205] 03
                    rst       $08                           ;[0206] cf
                    nop                                     ;[0207] 00
                    ld        a,e                           ;[0208] 7b
                    ld        bc,$cfc9                      ;[0209] 01 c9 cf
                    nop                                     ;[020c] 00
                    ld        c,a                           ;[020d] 4f
                    inc       bc                            ;[020e] 03
                    rst       $08                           ;[020f] cf
                    nop                                     ;[0210] 00
                    ld        a,(hl)                        ;[0211] 7e
                    ld        bc,$cfc9                      ;[0212] 01 c9 cf
                    nop                                     ;[0215] 00
                    ld        c,a                           ;[0216] 4f
                    inc       bc                            ;[0217] 03
                    rst       $08                           ;[0218] cf
                    nop                                     ;[0219] 00
                    adc       d                             ;[021a] 8a
                    ld        bc,$cfc9                      ;[021b] 01 c9 cf
                    nop                                     ;[021e] 00
                    ld        c,a                           ;[021f] 4f
                    inc       bc                            ;[0220] 03
                    rst       $08                           ;[0221] cf
                    nop                                     ;[0222] 00
                    add       h                             ;[0223] 84
                    ld        bc,$78c9                      ;[0224] 01 c9 78
                    rst       $08                           ;[0227] cf
                    nop                                     ;[0228] 00
                    ccf                                     ;[0229] 3f
                    ld        bc,$78c9                      ;[022a] 01 c9 78
                    rst       $08                           ;[022d] cf
                    nop                                     ;[022e] 00
                    ld        c,b                           ;[022f] 48
                    ld        bc,$78c9                      ;[0230] 01 c9 78
                    rst       $08                           ;[0233] cf
                    nop                                     ;[0234] 00
                    ld        c,e                           ;[0235] 4b
                    ld        bc,$78c9                      ;[0236] 01 c9 78
                    rst       $08                           ;[0239] cf
                    nop                                     ;[023a] 00
                    ld        c,(hl)                        ;[023b] 4e
                    ld        bc,$78c9                      ;[023c] 01 c9 78
                    ld        d,$00                         ;[023f] 16 00
                    rst       $08                           ;[0241] cf
                    nop                                     ;[0242] 00
                    ld        d,c                           ;[0243] 51
                    ld        bc,$78c9                      ;[0244] 01 c9 78
                    rst       $08                           ;[0247] cf
                    nop                                     ;[0248] 00
                    ld        d,h                           ;[0249] 54
                    ld        bc,$79c9                      ;[024a] 01 c9 79
                    and       $f8                           ;[024d] e6 f8
                    ld        a,$1e                         ;[024f] 3e 1e
                    ret       nz                            ;[0251] c0
                    ld        a,b                           ;[0252] 78
                    rst       $08                           ;[0253] cf
                    nop                                     ;[0254] 00
                    ld        d,a                           ;[0255] 57
                    ld        bc,$78c9                      ;[0256] 01 c9 78
                    ld        ($f505),sp                    ;[0259] ed 73 05 f5
                    ld        sp,$f505                      ;[025d] 31 05 f5
                    rst       $08                           ;[0260] cf
                    nop                                     ;[0261] 00
                    ld        (hl),l                        ;[0262] 75
                    ld        bc,$7bed                      ;[0263] 01 ed 7b
                    dec       b                             ;[0266] 05
                    push      af                            ;[0267] f5
                    ret                                     ;[0268] c9

                    ld        a,b                           ;[0269] 78
                    ld        ($f505),sp                    ;[026a] ed 73 05 f5
                    ld        sp,$f505                      ;[026e] 31 05 f5
                    rst       $08                           ;[0271] cf
                    nop                                     ;[0272] 00
                    ld        a,b                           ;[0273] 78
                    ld        bc,$7bed                      ;[0274] 01 ed 7b
                    dec       b                             ;[0277] 05
                    push      af                            ;[0278] f5
                    ret                                     ;[0279] c9

                    ld        a,h                           ;[027a] 7c
                    and       $c0                           ;[027b] e6 c0
                    jr        nz,$0292                      ;[027d] 20 13
                    push      bc                            ;[027f] c5
                    push      de                            ;[0280] d5
                    push      hl                            ;[0281] e5
                    call      $022c                         ;[0282] cd 2c 02
                    pop       hl                            ;[0285] e1
                    pop       de                            ;[0286] d1
                    pop       bc                            ;[0287] c1
                    jr        nc,$02d1                      ;[0288] 30 47
                    inc       hl                            ;[028a] 23
                    dec       de                            ;[028b] 1b
                    ld        a,d                           ;[028c] 7a
                    or        e                             ;[028d] b3
                    jr        nz,$027a                      ;[028e] 20 ea
                    scf                                     ;[0290] 37
                    ret                                     ;[0291] c9

                    push      hl                            ;[0292] e5
                    dec       de                            ;[0293] 1b
                    add       hl,de                         ;[0294] 19
                    inc       de                            ;[0295] 13
                    jr        c,$02a9                       ;[0296] 38 11
                    inc       hl                            ;[0298] 23
                    ex        (sp),hl                       ;[0299] e3
                    call      $02a3                         ;[029a] cd a3 02
                    pop       hl                            ;[029d] e1
                    jr        nc,$02d1                      ;[029e] 30 31
                    xor       a                             ;[02a0] af
                    scf                                     ;[02a1] 37
                    ret                                     ;[02a2] c9

                    ld        a,b                           ;[02a3] 78
                    rst       $08                           ;[02a4] cf
                    nop                                     ;[02a5] 00
                    ld        b,d                           ;[02a6] 42
                    ld        bc,$e1c9                      ;[02a7] 01 c9 e1
                    push      de                            ;[02aa] d5
                    push      hl                            ;[02ab] e5
                    ex        de,hl                         ;[02ac] eb
                    ld        hl,$0000                      ;[02ad] 21 00 00
                    and       a                             ;[02b0] a7
                    sbc       hl,de                         ;[02b1] ed 52
                    ex        de,hl                         ;[02b3] eb
                    pop       hl                            ;[02b4] e1
                    ex        (sp),hl                       ;[02b5] e3
                    and       a                             ;[02b6] a7
                    sbc       hl,de                         ;[02b7] ed 52
                    ex        (sp),hl                       ;[02b9] e3
                    push      hl                            ;[02ba] e5
                    push      de                            ;[02bb] d5
                    push      bc                            ;[02bc] c5
                    call      $02a3                         ;[02bd] cd a3 02
                    pop       bc                            ;[02c0] c1
                    pop       hl                            ;[02c1] e1
                    jr        nc,$02c9                      ;[02c2] 30 05
                    pop       de                            ;[02c4] d1
                    add       hl,de                         ;[02c5] 19
                    pop       de                            ;[02c6] d1
                    jr        $027a                         ;[02c7] 18 b1
                    sbc       hl,de                         ;[02c9] ed 52
                    pop       bc                            ;[02cb] c1
                    add       hl,bc                         ;[02cc] 09
                    pop       bc                            ;[02cd] c1
                    ex        de,hl                         ;[02ce] eb
                    add       hl,bc                         ;[02cf] 09
                    ex        de,hl                         ;[02d0] eb
                    ld        b,a                           ;[02d1] 47
                    ld        a,d                           ;[02d2] 7a
                    or        e                             ;[02d3] b3
                    ld        a,b                           ;[02d4] 78
                    ret                                     ;[02d5] c9

                    ld        a,h                           ;[02d6] 7c
                    and       $c0                           ;[02d7] e6 c0
                    jr        nz,$02ef                      ;[02d9] 20 14
                    push      bc                            ;[02db] c5
                    push      de                            ;[02dc] d5
                    push      hl                            ;[02dd] e5
                    ld        c,(hl)                        ;[02de] 4e
                    call      $0232                         ;[02df] cd 32 02
                    pop       hl                            ;[02e2] e1
                    pop       de                            ;[02e3] d1
                    pop       bc                            ;[02e4] c1
                    jr        nc,$02d1                      ;[02e5] 30 ea
                    inc       hl                            ;[02e7] 23
                    dec       de                            ;[02e8] 1b
                    ld        a,d                           ;[02e9] 7a
                    or        e                             ;[02ea] b3
                    jr        nz,$02d6                      ;[02eb] 20 e9
                    scf                                     ;[02ed] 37
                    ret                                     ;[02ee] c9

                    push      hl                            ;[02ef] e5
                    dec       de                            ;[02f0] 1b
                    add       hl,de                         ;[02f1] 19
                    inc       de                            ;[02f2] 13
                    jr        c,$0302                       ;[02f3] 38 0d
                    inc       hl                            ;[02f5] 23
                    ex        (sp),hl                       ;[02f6] e3
                    call      $02fc                         ;[02f7] cd fc 02
                    jr        $029d                         ;[02fa] 18 a1
                    ld        a,b                           ;[02fc] 78
                    rst       $08                           ;[02fd] cf
                    nop                                     ;[02fe] 00
                    ld        b,l                           ;[02ff] 45
                    ld        bc,$e1c9                      ;[0300] 01 c9 e1
                    push      de                            ;[0303] d5
                    push      hl                            ;[0304] e5
                    ex        de,hl                         ;[0305] eb
                    ld        hl,$0000                      ;[0306] 21 00 00
                    and       a                             ;[0309] a7
                    sbc       hl,de                         ;[030a] ed 52
                    ex        de,hl                         ;[030c] eb
                    pop       hl                            ;[030d] e1
                    ex        (sp),hl                       ;[030e] e3
                    and       a                             ;[030f] a7
                    sbc       hl,de                         ;[0310] ed 52
                    ex        (sp),hl                       ;[0312] e3
                    push      hl                            ;[0313] e5
                    push      de                            ;[0314] d5
                    push      bc                            ;[0315] c5
                    call      $02fc                         ;[0316] cd fc 02
                    pop       bc                            ;[0319] c1
                    pop       hl                            ;[031a] e1
                    jr        nc,$02c9                      ;[031b] 30 ac
                    pop       de                            ;[031d] d1
                    add       hl,de                         ;[031e] 19
                    pop       de                            ;[031f] d1
                    jr        $02d6                         ;[0320] 18 b4
                    call      $03ac                         ;[0322] cd ac 03
                    ret       nc                            ;[0325] d0
                    ld        a,e                           ;[0326] 7b
                    sub       $03                           ;[0327] d6 03
                    jr        nz,$0384                      ;[0329] 20 59
                    push      hl                            ;[032b] e5
                    ld        hl,$314a                      ;[032c] 21 4a 31
                    rst       $08                           ;[032f] cf
                    rlca                                    ;[0330] 07
                    dec       e                             ;[0331] 1d
                    nop                                     ;[0332] 00
                    pop       hl                            ;[0333] e1
                    inc       e                             ;[0334] 1c
                    rra                                     ;[0335] 1f
                    jr        c,$0384                       ;[0336] 38 4c
                    push      bc                            ;[0338] c5
                    push      de                            ;[0339] d5
                    push      hl                            ;[033a] e5
                    ld        e,$00                         ;[033b] 1e 00
                    call      $0384                         ;[033d] cd 84 03
                    pop       hl                            ;[0340] e1
                    pop       de                            ;[0341] d1
                    pop       bc                            ;[0342] c1
                    ret       c                             ;[0343] d8
                    cp        $18                           ;[0344] fe 18
                    scf                                     ;[0346] 37
                    ccf                                     ;[0347] 3f
                    ret       nz                            ;[0348] c0
                    push      bc                            ;[0349] c5
                    push      de                            ;[034a] d5
                    push      hl                            ;[034b] e5
                    ld        b,$00                         ;[034c] 06 00
                    ld        de,$ff00                      ;[034e] 11 00 ff
                    push      de                            ;[0351] d5
                    ld        a,(hl)                        ;[0352] 7e
                    inc       hl                            ;[0353] 23
                    ld        (de),a                        ;[0354] 12
                    inc       de                            ;[0355] 13
                    cp        $2e                           ;[0356] fe 2e
                    jr        nz,$035c                      ;[0358] 20 02
                    ld        b,d                           ;[035a] 42
                    ld        c,e                           ;[035b] 4b
                    inc       a                             ;[035c] 3c
                    jr        nz,$0352                      ;[035d] 20 f3
                    ld        h,d                           ;[035f] 62
                    ld        l,e                           ;[0360] 6b
                    scf                                     ;[0361] 37
                    sbc       hl,bc                         ;[0362] ed 42
                    ld        a,l                           ;[0364] 7d
                    and       $fc                           ;[0365] e6 fc
                    or        h                             ;[0367] b4
                    jr        nz,$036c                      ;[0368] 20 02
                    ld        d,b                           ;[036a] 50
                    ld        e,c                           ;[036b] 59
                    dec       de                            ;[036c] 1b
                    ld        hl,$000b                      ;[036d] 21 0b 00
                    ld        bc,$0005                      ;[0370] 01 05 00
                    ldir                                    ;[0373] ed b0
                    pop       hl                            ;[0375] e1
                    push      hl                            ;[0376] e5
                    call      $021d                         ;[0377] cd 1d 02
                    pop       de                            ;[037a] d1
                    pop       hl                            ;[037b] e1
                    push      hl                            ;[037c] e5
                    call      $0598                         ;[037d] cd 98 05
                    pop       hl                            ;[0380] e1
                    pop       de                            ;[0381] d1
                    pop       bc                            ;[0382] c1
                    ret       nc                            ;[0383] d0
                    ld        a,c                           ;[0384] 79
                    and       $f8                           ;[0385] e6 f8
                    ld        a,$1e                         ;[0387] 3e 1e
                    ret       nz                            ;[0389] c0
                    push      de                            ;[038a] d5
                    ld        d,b                           ;[038b] 50
                    ld        e,$38                         ;[038c] 1e 38
                    mul       d,e                           ;[038e] ed 30
                    add       de,$dbd0                      ;[0390] ed 35 d0 db
                    push      de                            ;[0394] d5
                    pop       ix                            ;[0395] dd e1
                    pop       de                            ;[0397] d1
                    rst       $08                           ;[0398] cf
                    nop                                     ;[0399] 00
                    ld        c,a                           ;[039a] 4f
                    inc       bc                            ;[039b] 03
                    ld        ($f505),sp                    ;[039c] ed 73 05 f5
                    ld        sp,$f505                      ;[03a0] 31 05 f5
                    rst       $08                           ;[03a3] cf
                    nop                                     ;[03a4] 00
                    ld        (hl),d                        ;[03a5] 72
                    ld        bc,$7bed                      ;[03a6] 01 ed 7b
                    dec       b                             ;[03a9] 05
                    push      af                            ;[03aa] f5
                    ret                                     ;[03ab] c9

                    bit       3,c                           ;[03ac] cb 59
                    scf                                     ;[03ae] 37
                    ret       z                             ;[03af] c8
                    res       3,c                           ;[03b0] cb 99
                    push      bc                            ;[03b2] c5
                    call      $28a5                         ;[03b3] cd a5 28
                    jr        nc,$03bf                      ;[03b6] 30 07
                    pop       bc                            ;[03b8] c1
                    push      bc                            ;[03b9] c5
                    bit       1,c                           ;[03ba] cb 49
                    call      nz,$2928                      ;[03bc] c4 28 29
                    pop       bc                            ;[03bf] c1
                    ld        de,$0202                      ;[03c0] 11 02 02
                    ret                                     ;[03c3] c9

                    ld        hl,$2310                      ;[03c4] 21 10 23
                    add       hl,a                          ;[03c7] ed 31
                    rst       $08                           ;[03c9] cf
                    nop                                     ;[03ca] 00
                    dec       e                             ;[03cb] 1d
                    nop                                     ;[03cc] 00
                    add       a                             ;[03cd] 87
                    ret                                     ;[03ce] c9

                    ld        d,a                           ;[03cf] 57
                    ld        a,l                           ;[03d0] 7d
                    call      $05b4                         ;[03d1] cd b4 05
                    ld        e,a                           ;[03d4] 5f
                    call      $03c4                         ;[03d5] cd c4 03
                    ld        a,$3e                         ;[03d8] 3e 3e
                    ret       nc                            ;[03da] d0
                    ld        a,d                           ;[03db] 7a
                    cp        $ff                           ;[03dc] fe ff
                    jr        z,$03e6                       ;[03de] 28 06
                    ld        a,e                           ;[03e0] 7b
                    rst       $08                           ;[03e1] cf
                    nop                                     ;[03e2] 00
                    ld        l,c                           ;[03e3] 69
                    ld        bc,$d5c9                      ;[03e4] 01 c9 d5
                    push      hl                            ;[03e7] e5
                    push      bc                            ;[03e8] c5
                    ld        h,b                           ;[03e9] 60
                    ld        l,c                           ;[03ea] 69
                    ld        a,e                           ;[03eb] 7b
                    add       $30                           ;[03ec] c6 30
                    ld        d,a                           ;[03ee] 57
                    ld        e,$00                         ;[03ef] 1e 00
                    ld        bc,$0100                      ;[03f1] 01 00 01
                    ld        a,$09                         ;[03f4] 3e 09
                    rst       $08                           ;[03f6] cf
                    nop                                     ;[03f7] 00
                    inc       bc                            ;[03f8] 03
                    rlca                                    ;[03f9] 07
                    pop       bc                            ;[03fa] c1
                    call      $2259                         ;[03fb] cd 59 22
                    pop       hl                            ;[03fe] e1
                    pop       de                            ;[03ff] d1
                    ret       nc                            ;[0400] d0
                    push      hl                            ;[0401] e5
                    ld        hl,$2302                      ;[0402] 21 02 23
                    ld        bc,$0eff                      ;[0405] 01 ff 0e
                    rst       $08                           ;[0408] cf
                    nop                                     ;[0409] 00
                    dec       e                             ;[040a] 1d
                    nop                                     ;[040b] 00
                    inc       hl                            ;[040c] 23
                    and       a                             ;[040d] a7
                    jp        m,$0434                       ;[040e] fa 34 04
                    cp        c                             ;[0411] b9
                    jr        z,$0434                       ;[0412] 28 20
                    ld        c,a                           ;[0414] 4f
                    ld        a,$10                         ;[0415] 3e 10
                    sub       b                             ;[0417] 90
                    or        $80                           ;[0418] f6 80
                    ex        (sp),hl                       ;[041a] e3
                    push      bc                            ;[041b] c5
                    push      de                            ;[041c] d5
                    push      hl                            ;[041d] e5
                    push      ix                            ;[041e] dd e5
                    ld        d,a                           ;[0420] 57
                    ld        bc,$f75f                      ;[0421] 01 5f f7
                    rst       $08                           ;[0424] cf
                    nop                                     ;[0425] 00
                    ld        l,c                           ;[0426] 69
                    ld        bc,$e1dd                      ;[0427] 01 dd e1
                    pop       hl                            ;[042a] e1
                    pop       de                            ;[042b] d1
                    pop       bc                            ;[042c] c1
                    ex        (sp),hl                       ;[042d] e3
                    jr        c,$043e                       ;[042e] 38 0e
                    cp        $09                           ;[0430] fe 09
                    jr        nz,$043b                      ;[0432] 20 07
                    djnz      $0408                         ;[0434] 10 d2
                    call      $04f4                         ;[0436] cd f4 04
                    ld        a,$09                         ;[0439] 3e 09
                    and       a                             ;[043b] a7
                    pop       hl                            ;[043c] e1
                    ret                                     ;[043d] c9

                    pop       hl                            ;[043e] e1
                    ld        c,(ix+$11)                    ;[043f] dd 4e 11
                    ld        b,(ix+$12)                    ;[0442] dd 46 12
                    ld        a,e                           ;[0445] 7b
                    add       a                             ;[0446] 87
                    ld        hl,$e438                      ;[0447] 21 38 e4
                    add       hl,a                          ;[044a] ed 31
                    ld        (hl),c                        ;[044c] 71
                    inc       hl                            ;[044d] 23
                    ld        (hl),b                        ;[044e] 70
                    scf                                     ;[044f] 37
                    ret                                     ;[0450] c9

                    ld        a,l                           ;[0451] 7d
                    call      $05b4                         ;[0452] cd b4 05
                    push      af                            ;[0455] f5
                    rst       $08                           ;[0456] cf
                    nop                                     ;[0457] 00
                    ld        l,h                           ;[0458] 6c
                    ld        bc,$38d1                      ;[0459] 01 d1 38
                    ld        b,$fe                         ;[045c] 06 fe
                    ld        d,$37                         ;[045e] 16 37
                    ret       z                             ;[0460] c8
                    ccf                                     ;[0461] 3f
                    ret                                     ;[0462] c9

                    ld        a,d                           ;[0463] 7a
                    add       a                             ;[0464] 87
                    ld        hl,$e438                      ;[0465] 21 38 e4
                    add       hl,a                          ;[0468] ed 31
                    ld        e,(hl)                        ;[046a] 5e
                    inc       hl                            ;[046b] 23
                    ld        d,(hl)                        ;[046c] 56
                    ld        a,d                           ;[046d] 7a
                    or        e                             ;[046e] b3
                    scf                                     ;[046f] 37
                    ret       z                             ;[0470] c8
                    xor       a                             ;[0471] af
                    ld        (hl),a                        ;[0472] 77
                    dec       hl                            ;[0473] 2b
                    ld        (hl),a                        ;[0474] 77
                    jp        $04fa                         ;[0475] c3 fa 04
                    ld        a,l                           ;[0478] 7d
                    push      af                            ;[0479] f5
                    push      bc                            ;[047a] c5
                    and       $7f                           ;[047b] e6 7f
                    call      $05b4                         ;[047d] cd b4 05
                    ld        bc,$f700                      ;[0480] 01 00 f7
                    push      af                            ;[0483] f5
                    ld        a,$ff                         ;[0484] 3e ff
                    ld        (bc),a                        ;[0486] 02
                    pop       af                            ;[0487] f1
                    rst       $08                           ;[0488] cf
                    nop                                     ;[0489] 00
                    ld        e,$01                         ;[048a] 1e 01
                    jr        nc,$04ba                      ;[048c] 30 2c
                    inc       a                             ;[048e] 3c
                    jr        nz,$04b9                      ;[048f] 20 28
                    pop       de                            ;[0491] d1
                    pop       af                            ;[0492] f1
                    push      af                            ;[0493] f5
                    push      de                            ;[0494] d5
                    ld        hl,$201f                      ;[0495] 21 1f 20
                    ld        de,$f700                      ;[0498] 11 00 f7
                    ld        bc,$0006                      ;[049b] 01 06 00
                    ldir                                    ;[049e] ed b0
                    and       $7f                           ;[04a0] e6 7f
                    call      $05b4                         ;[04a2] cd b4 05
                    add       $30                           ;[04a5] c6 30
                    ld        h,a                           ;[04a7] 67
                    ld        l,$00                         ;[04a8] 2e 00
                    ld        c,$f9                         ;[04aa] 0e f9
                    ld        a,$09                         ;[04ac] 3e 09
                    rst       $08                           ;[04ae] cf
                    nop                                     ;[04af] 00
                    ld        l,c                           ;[04b0] 69
                    rlca                                    ;[04b1] 07
                    ld        a,$ff                         ;[04b2] 3e ff
                    ld        (de),a                        ;[04b4] 12
                    xor       a                             ;[04b5] af
                    ld        b,a                           ;[04b6] 47
                    ld        c,a                           ;[04b7] 4f
                    scf                                     ;[04b8] 37
                    dec       a                             ;[04b9] 3d
                    pop       de                            ;[04ba] d1
                    pop       hl                            ;[04bb] e1
                    push      af                            ;[04bc] f5
                    push      bc                            ;[04bd] c5
                    jr        nc,$04e8                      ;[04be] 30 28
                    ld        b,$ff                         ;[04c0] 06 ff
                    bit       7,h                           ;[04c2] cb 7c
                    push      af                            ;[04c4] f5
                    ld        hl,$f700                      ;[04c5] 21 00 f7
                    jr        nz,$04cc                      ;[04c8] 20 02
                    ld        b,$12                         ;[04ca] 06 12
                    ld        a,(hl)                        ;[04cc] 7e
                    inc       hl                            ;[04cd] 23
                    cp        $ff                           ;[04ce] fe ff
                    jr        z,$04d6                       ;[04d0] 28 04
                    ld        (de),a                        ;[04d2] 12
                    inc       de                            ;[04d3] 13
                    djnz      $04cc                         ;[04d4] 10 f6
                    pop       af                            ;[04d6] f1
                    jr        z,$04de                       ;[04d7] 28 05
                    ld        a,$ff                         ;[04d9] 3e ff
                    ld        (de),a                        ;[04db] 12
                    jr        $04e8                         ;[04dc] 18 0a
                    inc       b                             ;[04de] 04
                    dec       b                             ;[04df] 05
                    jr        z,$04e8                       ;[04e0] 28 06
                    ld        a,$20                         ;[04e2] 3e 20
                    ld        (de),a                        ;[04e4] 12
                    inc       de                            ;[04e5] 13
                    djnz      $04e4                         ;[04e6] 10 fc
                    pop       bc                            ;[04e8] c1
                    pop       af                            ;[04e9] f1
                    ld        l,$00                         ;[04ea] 2e 00
                    inc       l                             ;[04ec] 2c
                    ret       c                             ;[04ed] d8
                    cp        $16                           ;[04ee] fe 16
                    scf                                     ;[04f0] 37
                    ret       z                             ;[04f1] c8
                    ccf                                     ;[04f2] 3f
                    ret                                     ;[04f3] c9

                    ld        e,(ix+$11)                    ;[04f4] dd 5e 11
                    ld        d,(ix+$12)                    ;[04f7] dd 56 12
                    ld        h,d                           ;[04fa] 62
                    ld        l,e                           ;[04fb] 6b
                    inc       de                            ;[04fc] 13
                    ld        bc,$0005                      ;[04fd] 01 05 00
                    rst       $08                           ;[0500] cf
                    nop                                     ;[0501] 00
                    in        a,($04)                       ;[0502] db 04
                    scf                                     ;[0504] 37
                    ret                                     ;[0505] c9

                    rst       $08                           ;[0506] cf
                    nop                                     ;[0507] 00
                    ld        c,a                           ;[0508] 4f
                    inc       bc                            ;[0509] 03
                    push      af                            ;[050a] f5
                    push      hl                            ;[050b] e5
                    and       $0f                           ;[050c] e6 0f
                    call      $0555                         ;[050e] cd 55 05
                    pop       hl                            ;[0511] e1
                    pop       de                            ;[0512] d1
                    ret       nc                            ;[0513] d0
                    ld        a,d                           ;[0514] 7a
                    push      af                            ;[0515] f5
                    push      hl                            ;[0516] e5
                    swapnib                                 ;[0517] ed 23
                    and       $0f                           ;[0519] e6 0f
                    call      $0130                         ;[051b] cd 30 01
                    pop       hl                            ;[051e] e1
                    pop       de                            ;[051f] d1
                    ret       nc                            ;[0520] d0
                    ld        a,d                           ;[0521] 7a
                    ld        b,$00                         ;[0522] 06 00
                    push      hl                            ;[0524] e5
                    jr        $052f                         ;[0525] 18 08
                    ld        b,a                           ;[0527] 47
                    push      hl                            ;[0528] e5
                    push      bc                            ;[0529] c5
                    rst       $08                           ;[052a] cf
                    nop                                     ;[052b] 00
                    ld        c,a                           ;[052c] 4f
                    inc       bc                            ;[052d] 03
                    pop       bc                            ;[052e] c1
                    ld        de,$ff00                      ;[052f] 11 00 ff
                    push      hl                            ;[0532] e5
                    push      bc                            ;[0533] c5
                    ld        ($f505),sp                    ;[0534] ed 73 05 f5
                    ld        sp,$f505                      ;[0538] 31 05 f5
                    rst       $08                           ;[053b] cf
                    nop                                     ;[053c] 00
                    add       c                             ;[053d] 81
                    ld        bc,$7bed                      ;[053e] 01 ed 7b
                    dec       b                             ;[0541] 05
                    push      af                            ;[0542] f5
                    pop       bc                            ;[0543] c1
                    pop       hl                            ;[0544] e1
                    pop       de                            ;[0545] d1
                    ret       nc                            ;[0546] d0
                    dec       b                             ;[0547] 05
                    ret       nz                            ;[0548] c0
                    ld        a,(hl)                        ;[0549] 7e
                    ldi                                     ;[054a] ed a0
                    inc       a                             ;[054c] 3c
                    jr        nz,$0549                      ;[054d] 20 fa
                    ret                                     ;[054f] c9

                    cp        $ff                           ;[0550] fe ff
                    call      nz,$05b4                      ;[0552] c4 b4 05
                    rst       $08                           ;[0555] cf
                    nop                                     ;[0556] 00
                    ld        l,a                           ;[0557] 6f
                    ld        bc,$c6d0                      ;[0558] 01 d0 c6
                    ld        b,c                           ;[055b] 41
                    scf                                     ;[055c] 37
                    ret                                     ;[055d] c9

                    push      af                            ;[055e] f5
                    ld        a,$ff                         ;[055f] 3e ff
                    rst       $08                           ;[0561] cf
                    nop                                     ;[0562] 00
                    ld        l,a                           ;[0563] 6f
                    ld        bc,$5fd1                      ;[0564] 01 d1 5f
                    call      $03c4                         ;[0567] cd c4 03
                    jp        m,$0577                       ;[056a] fa 77 05
                    xor       a                             ;[056d] af
                    inc       d                             ;[056e] 14
                    scf                                     ;[056f] 37
                    ret       z                             ;[0570] c8
                    dec       d                             ;[0571] 15
                    ret       z                             ;[0572] c8
                    ld        a,$14                         ;[0573] 3e 14
                    and       a                             ;[0575] a7
                    ret                                     ;[0576] c9

                    push      de                            ;[0577] d5
                    ld        a,e                           ;[0578] 7b
                    rst       $08                           ;[0579] cf
                    nop                                     ;[057a] 00
                    inc       h                             ;[057b] 24
                    ld        bc,$d0d1                      ;[057c] 01 d1 d0
                    inc       d                             ;[057f] 14
                    ret       z                             ;[0580] c8
                    ld        a,e                           ;[0581] 7b
                    add       $20                           ;[0582] c6 20
                    ld        h,a                           ;[0584] 67
                    ld        l,$01                         ;[0585] 2e 01
                    dec       d                             ;[0587] 15
                    ld        a,d                           ;[0588] 7a
                    swapnib                                 ;[0589] ed 23
                    or        e                             ;[058b] b3
                    rst       $08                           ;[058c] cf
                    add       hl,bc                         ;[058d] 09
                    ret       po                            ;[058e] e0
                    ld        (bc),a                        ;[058f] 02
                    cpl                                     ;[0590] 2f
                    dec       hl                            ;[0591] 2b
                    rst       $08                           ;[0592] cf
                    add       hl,bc                         ;[0593] 09
                    ret       po                            ;[0594] e0
                    ld        (bc),a                        ;[0595] 02
                    scf                                     ;[0596] 37
                    ret                                     ;[0597] c9

                    rst       $08                           ;[0598] cf
                    nop                                     ;[0599] 00
                    ld        c,a                           ;[059a] 4f
                    inc       bc                            ;[059b] 03
                    push      af                            ;[059c] f5
                    ex        de,hl                         ;[059d] eb
                    rst       $08                           ;[059e] cf
                    nop                                     ;[059f] 00
                    ld        c,a                           ;[05a0] 4f
                    inc       bc                            ;[05a1] 03
                    ex        de,hl                         ;[05a2] eb
                    pop       bc                            ;[05a3] c1
                    ld        ($f505),sp                    ;[05a4] ed 73 05 f5
                    ld        sp,$f505                      ;[05a8] 31 05 f5
                    rst       $08                           ;[05ab] cf
                    nop                                     ;[05ac] 00
                    add       a                             ;[05ad] 87
                    ld        bc,$7bed                      ;[05ae] 01 ed 7b
                    dec       b                             ;[05b1] 05
                    push      af                            ;[05b2] f5
                    ret                                     ;[05b3] c9

                    and       $df                           ;[05b4] e6 df
                    sub       $41                           ;[05b6] d6 41
                    jr        c,$05bd                       ;[05b8] 38 03
                    cp        $10                           ;[05ba] fe 10
                    ret       c                             ;[05bc] d8
                    pop       af                            ;[05bd] f1
                    ld        a,$16                         ;[05be] 3e 16
                    and       a                             ;[05c0] a7
                    ret                                     ;[05c1] c9

                    rst       $08                           ;[05c2] cf
                    dec       c                             ;[05c3] 0d
                    dec       e                             ;[05c4] 1d
                    nop                                     ;[05c5] 00
                    ret                                     ;[05c6] c9

                    rst       $08                           ;[05c7] cf
                    nop                                     ;[05c8] 00
                    ret       po                            ;[05c9] e0
                    ld        (bc),a                        ;[05ca] 02
                    ret                                     ;[05cb] c9

                    push      hl                            ;[05cc] e5
                    ld        hl,$2006                      ;[05cd] 21 06 20
                    rst       $08                           ;[05d0] cf
                    nop                                     ;[05d1] 00
                    dec       e                             ;[05d2] 1d
                    nop                                     ;[05d3] 00
                    and       $0f                           ;[05d4] e6 0f
                    pop       hl                            ;[05d6] e1
                    ld        ($5b52),hl                    ;[05d7] 22 52 5b
                    ld        hl,$04dd                      ;[05da] 21 dd 04
                    jp        $3f2a                         ;[05dd] c3 2a 3f
                    rst       $08                           ;[05e0] cf
                    dec       c                             ;[05e1] 0d
                    or        (hl)                          ;[05e2] b6
                    inc       (hl)                          ;[05e3] 34
                    ret                                     ;[05e4] c9

                    rst       $08                           ;[05e5] cf
                    dec       c                             ;[05e6] 0d
                    cp        h                             ;[05e7] bc
                    inc       (hl)                          ;[05e8] 34
                    ret                                     ;[05e9] c9

                    rst       $08                           ;[05ea] cf
                    dec       c                             ;[05eb] 0d
                    ld        c,l                           ;[05ec] 4d
                    inc       (hl)                          ;[05ed] 34
                    ret                                     ;[05ee] c9

                    rst       $08                           ;[05ef] cf
                    dec       c                             ;[05f0] 0d
                    call      p,$c934                       ;[05f1] f4 34 c9
                    ld        b,$02                         ;[05f4] 06 02
                    rst       $08                           ;[05f6] cf
                    nop                                     ;[05f7] 00
                    or        d                             ;[05f8] b2
                    inc       bc                            ;[05f9] 03
                    ret                                     ;[05fa] c9

                    rst       $08                           ;[05fb] cf
                    dec       c                             ;[05fc] 0d
                    ld        ($c934),a                     ;[05fd] 32 34 c9
                    rst       $08                           ;[0600] cf
                    dec       c                             ;[0601] 0d
                    ld        b,b                           ;[0602] 40
                    inc       (hl)                          ;[0603] 34
                    ret                                     ;[0604] c9

                    ld        ix,$e46c                      ;[0605] dd 21 6c e4
                    ld        e,$14                         ;[0609] 1e 14
                    ld        a,(ix+$00)                    ;[060b] dd 7e 00
                    and       a                             ;[060e] a7
                    scf                                     ;[060f] 37
                    ret       z                             ;[0610] c8
                    push      bc                            ;[0611] c5
                    ld        bc,$0013                      ;[0612] 01 13 00
                    add       ix,bc                         ;[0615] dd 09
                    pop       bc                            ;[0617] c1
                    dec       e                             ;[0618] 1d
                    jr        nz,$060b                      ;[0619] 20 f0
                    ld        a,$3c                         ;[061b] 3e 3c
                    and       a                             ;[061d] a7
                    ret                                     ;[061e] c9

                    sub       $05                           ;[061f] d6 05
                    push      ix                            ;[0621] dd e5
                    push      de                            ;[0623] d5
                    push      hl                            ;[0624] e5
                    ld        d,a                           ;[0625] 57
                    ld        ix,$e46c                      ;[0626] dd 21 6c e4
                    ld        e,$14                         ;[062a] 1e 14
                    ld        a,(ix+$00)                    ;[062c] dd 7e 00
                    and       a                             ;[062f] a7
                    jr        z,$0646                       ;[0630] 28 14
                    ld        a,(ix+$10)                    ;[0632] dd 7e 10
                    and       $01                           ;[0635] e6 01
                    cp        d                             ;[0637] ba
                    jr        nz,$0646                      ;[0638] 20 0c
                    ld        l,(ix+$11)                    ;[063a] dd 6e 11
                    ld        h,(ix+$12)                    ;[063d] dd 66 12
                    sbc       hl,bc                         ;[0640] ed 42
                    ld        a,$3b                         ;[0642] 3e 3b
                    jr        z,$0651                       ;[0644] 28 0b
                    push      bc                            ;[0646] c5
                    ld        bc,$0013                      ;[0647] 01 13 00
                    add       ix,bc                         ;[064a] dd 09
                    pop       bc                            ;[064c] c1
                    dec       e                             ;[064d] 1d
                    jr        nz,$062c                      ;[064e] 20 dc
                    scf                                     ;[0650] 37
                    pop       hl                            ;[0651] e1
                    pop       de                            ;[0652] d1
                    pop       ix                            ;[0653] dd e1
                    ret                                     ;[0655] c9

                    sub       $05                           ;[0656] d6 05
                    ccf                                     ;[0658] 3f
                    jr        nc,$066c                      ;[0659] 30 11
                    cp        $02                           ;[065b] fe 02
                    jr        nc,$066c                      ;[065d] 30 0d
                    srl       a                             ;[065f] cb 3f
                    ld        ix,$e458                      ;[0661] dd 21 58 e4
                    jr        nc,$066b                      ;[0665] 30 04
                    ld        ix,$e462                      ;[0667] dd 21 62 e4
                    and       a                             ;[066b] a7
                    ld        a,$16                         ;[066c] 3e 16
                    ret       nz                            ;[066e] c0
                    ld        a,(ix+$00)                    ;[066f] dd 7e 00
                    or        (ix+$01)                      ;[0672] dd b6 01
                    ld        a,$16                         ;[0675] 3e 16
                    ret       z                             ;[0677] c8
                    scf                                     ;[0678] 37
                    ret                                     ;[0679] c9

                    ld        a,c                           ;[067a] 79
                    sub       $05                           ;[067b] d6 05
                    ld        c,a                           ;[067d] 4f
                    ld        ix,$3135                      ;[067e] dd 21 35 31
                    jp        nc,$17f7                      ;[0682] d2 f7 17
                    cp        $ff                           ;[0685] fe ff
                    ld        a,$16                         ;[0687] 3e 16
                    ccf                                     ;[0689] 3f
                    ret       nc                            ;[068a] d0
                    rst       $08                           ;[068b] cf
                    dec       c                             ;[068c] 0d
                    dec       a                             ;[068d] 3d
                    dec       (hl)                          ;[068e] 35
                    ld        h,$00                         ;[068f] 26 00
                    ld        d,h                           ;[0691] 54
                    ld        e,h                           ;[0692] 5c
                    ld        ix,$313b                      ;[0693] dd 21 3b 31
                    scf                                     ;[0697] 37
                    ret                                     ;[0698] c9

                    call      $0656                         ;[0699] cd 56 06
                    ret       nc                            ;[069c] d0
                    ld        l,(ix+$06)                    ;[069d] dd 6e 06
                    ld        h,(ix+$07)                    ;[06a0] dd 66 07
                    and       a                             ;[06a3] a7
                    sbc       hl,bc                         ;[06a4] ed 42
                    ld        a,$38                         ;[06a6] 3e 38
                    ccf                                     ;[06a8] 3f
                    ret       nc                            ;[06a9] d0
                    ld        l,(ix+$08)                    ;[06aa] dd 6e 08
                    ld        h,(ix+$09)                    ;[06ad] dd 66 09
                    push      hl                            ;[06b0] e5
                    pop       ix                            ;[06b1] dd e1
                    push      bc                            ;[06b3] c5
                    srl       b                             ;[06b4] cb 38
                    rr        c                             ;[06b6] cb 19
                    srl       b                             ;[06b8] cb 38
                    rr        c                             ;[06ba] cb 19
                    srl       b                             ;[06bc] cb 38
                    rr        c                             ;[06be] cb 19
                    ld        d,b                           ;[06c0] 50
                    ld        e,c                           ;[06c1] 59
                    pop       bc                            ;[06c2] c1
                    ld        a,c                           ;[06c3] 79
                    and       $07                           ;[06c4] e6 07
                    ld        c,$00                         ;[06c6] 0e 00
                    rra                                     ;[06c8] 1f
                    rr        c                             ;[06c9] cb 19
                    rra                                     ;[06cb] 1f
                    rr        c                             ;[06cc] cb 19
                    ld        b,a                           ;[06ce] 47
                    scf                                     ;[06cf] 37
                    ret                                     ;[06d0] c9

                    push      bc                            ;[06d1] c5
                    push      ix                            ;[06d2] dd e5
                    push      hl                            ;[06d4] e5
                    call      $0699                         ;[06d5] cd 99 06
                    jr        nc,$06f4                      ;[06d8] 30 1a
                    push      bc                            ;[06da] c5
                    ld        bc,$0700                      ;[06db] 01 00 07
                    ld        hl,$e090                      ;[06de] 21 90 e0
                    call      $1863                         ;[06e1] cd 63 18
                    pop       bc                            ;[06e4] c1
                    jr        nc,$06f4                      ;[06e5] 30 0d
                    ld        hl,$e090                      ;[06e7] 21 90 e0
                    add       hl,bc                         ;[06ea] 09
                    pop       de                            ;[06eb] d1
                    ld        bc,$0040                      ;[06ec] 01 40 00
                    ldir                                    ;[06ef] ed b0
                    scf                                     ;[06f1] 37
                    jr        $06f5                         ;[06f2] 18 01
                    pop       hl                            ;[06f4] e1
                    pop       ix                            ;[06f5] dd e1
                    pop       bc                            ;[06f7] c1
                    ret                                     ;[06f8] c9

                    push      ix                            ;[06f9] dd e5
                    ld        bc,$0000                      ;[06fb] 01 00 00
                    push      af                            ;[06fe] f5
                    push      bc                            ;[06ff] c5
                    push      hl                            ;[0700] e5
                    ld        hl,$f712                      ;[0701] 21 12 f7
                    call      $06d1                         ;[0704] cd d1 06
                    jr        nc,$072e                      ;[0707] 30 25
                    pop       hl                            ;[0709] e1
                    push      hl                            ;[070a] e5
                    ld        de,$f712                      ;[070b] 11 12 f7
                    ld        b,$10                         ;[070e] 06 10
                    ld        a,(de)                        ;[0710] 1a
                    cp        (hl)                          ;[0711] be
                    jr        z,$0729                       ;[0712] 28 15
                    cp        $41                           ;[0714] fe 41
                    jr        c,$071c                       ;[0716] 38 04
                    cp        $5b                           ;[0718] fe 5b
                    jr        c,$0724                       ;[071a] 38 08
                    cp        $61                           ;[071c] fe 61
                    jr        c,$0734                       ;[071e] 38 14
                    cp        $7b                           ;[0720] fe 7b
                    jr        nc,$0734                      ;[0722] 30 10
                    xor       $20                           ;[0724] ee 20
                    cp        (hl)                          ;[0726] be
                    jr        nz,$0734                      ;[0727] 20 0b
                    inc       de                            ;[0729] 13
                    inc       hl                            ;[072a] 23
                    djnz      $0710                         ;[072b] 10 e3
                    scf                                     ;[072d] 37
                    pop       hl                            ;[072e] e1
                    pop       bc                            ;[072f] c1
                    pop       hl                            ;[0730] e1
                    pop       ix                            ;[0731] dd e1
                    ret                                     ;[0733] c9

                    pop       hl                            ;[0734] e1
                    pop       bc                            ;[0735] c1
                    inc       bc                            ;[0736] 03
                    ld        a,b                           ;[0737] 78
                    or        c                             ;[0738] b1
                    ld        a,$38                         ;[0739] 3e 38
                    jr        z,$0730                       ;[073b] 28 f3
                    pop       af                            ;[073d] f1
                    jr        $06fe                         ;[073e] 18 be
                    push      af                            ;[0740] f5
                    call      $061f                         ;[0741] cd 1f 06
                    jr        nc,$074b                      ;[0744] 30 05
                    call      $0605                         ;[0746] cd 05 06
                    jr        c,$074d                       ;[0749] 38 02
                    pop       hl                            ;[074b] e1
                    ret                                     ;[074c] c9

                    pop       af                            ;[074d] f1
                    push      af                            ;[074e] f5
                    ld        hl,$f712                      ;[074f] 21 12 f7
                    call      $00c4                         ;[0752] cd c4 00
                    jr        nc,$074b                      ;[0755] 30 f4
                    ld        a,($f722)                     ;[0757] 3a 22 f7
                    dec       a                             ;[075a] 3d
                    cp        $fd                           ;[075b] fe fd
                    ld        a,$38                         ;[075d] 3e 38
                    jr        nc,$074b                      ;[075f] 30 ea
                    pop       af                            ;[0761] f1
                    call      $07d5                         ;[0762] cd d5 07
                    di                                      ;[0765] f3
                    push      ix                            ;[0766] dd e5
                    pop       de                            ;[0768] d1
                    ld        hl,$f722                      ;[0769] 21 22 f7
                    ld        bc,$0010                      ;[076c] 01 10 00
                    ldir                                    ;[076f] ed b0
                    push      iy                            ;[0771] fd e5
                    push      ix                            ;[0773] dd e5
                    ld        c,(ix+$01)                    ;[0775] dd 4e 01
                    ld        b,(ix+$02)                    ;[0778] dd 46 02
                    ld        d,(ix+$03)                    ;[077b] dd 56 03
                    call      $00a9                         ;[077e] cd a9 00
                    ld        e,(ix+$03)                    ;[0781] dd 5e 03
                    push      de                            ;[0784] d5
                    ld        e,(ix+$04)                    ;[0785] dd 5e 04
                    ld        d,(ix+$05)                    ;[0788] dd 56 05
                    push      de                            ;[078b] d5
                    pop       ix                            ;[078c] dd e1
                    ld        iy,$0000                      ;[078e] fd 21 00 00
                    ld        hl,$0000                      ;[0792] 21 00 00
                    ld        d,h                           ;[0795] 54
                    ld        e,l                           ;[0796] 5d
                    add       iy,bc                         ;[0797] fd 09
                    adc       hl,de                         ;[0799] ed 5a
                    dec       ix                            ;[079b] dd 2b
                    ld        a,ixh                         ;[079d] dd 7c
                    or        ixl                           ;[079f] dd b5
                    jr        nz,$0797                      ;[07a1] 20 f4
                    pop       bc                            ;[07a3] c1
                    ld        a,b                           ;[07a4] 78
                    ld        b,$00                         ;[07a5] 06 00
                    and       a                             ;[07a7] a7
                    jr        z,$07b1                       ;[07a8] 28 07
                    add       iy,bc                         ;[07aa] fd 09
                    adc       hl,de                         ;[07ac] ed 5a
                    dec       a                             ;[07ae] 3d
                    jr        nz,$07aa                      ;[07af] 20 f9
                    pop       ix                            ;[07b1] dd e1
                    ld        a,iyl                         ;[07b3] fd 7d
                    ld        (ix+$01),a                    ;[07b5] dd 77 01
                    ld        a,iyh                         ;[07b8] fd 7c
                    ld        (ix+$02),a                    ;[07ba] dd 77 02
                    ld        (ix+$03),l                    ;[07bd] dd 75 03
                    ld        (ix+$04),h                    ;[07c0] dd 74 04
                    xor       a                             ;[07c3] af
                    ld        (ix+$05),a                    ;[07c4] dd 77 05
                    ld        (ix+$06),a                    ;[07c7] dd 77 06
                    pop       iy                            ;[07ca] fd e1
                    ei                                      ;[07cc] fb
                    scf                                     ;[07cd] 37
                    ret                                     ;[07ce] c9

                    ld        (ix+$00),$00                  ;[07cf] dd 36 00 00
                    scf                                     ;[07d3] 37
                    ret                                     ;[07d4] c9

                    push      af                            ;[07d5] f5
                    sub       $05                           ;[07d6] d6 05
                    ld        (ix+$10),a                    ;[07d8] dd 77 10
                    ld        (ix+$11),c                    ;[07db] dd 71 11
                    ld        (ix+$12),b                    ;[07de] dd 70 12
                    and       a                             ;[07e1] a7
                    call      $1a82                         ;[07e2] cd 82 1a
                    jr        nc,$07eb                      ;[07e5] 30 04
                    set       1,(ix+$10)                    ;[07e7] dd cb 10 ce
                    pop       af                            ;[07eb] f1
                    ret                                     ;[07ec] c9

                    ex        de,hl                         ;[07ed] eb
                    xor       a                             ;[07ee] af
                    ex        af,af'                        ;[07ef] 08
                    push      af                            ;[07f0] f5
                    xor       a                             ;[07f1] af
                    ld        d,a                           ;[07f2] 57
                    ld        e,a                           ;[07f3] 5f
                    srl       b                             ;[07f4] cb 38
                    jr        nc,$07ff                      ;[07f6] 30 07
                    ld        c,a                           ;[07f8] 4f
                    ex        af,af'                        ;[07f9] 08
                    ex        de,hl                         ;[07fa] eb
                    add       hl,de                         ;[07fb] 19
                    ex        de,hl                         ;[07fc] eb
                    adc       c                             ;[07fd] 89
                    ex        af,af'                        ;[07fe] 08
                    jr        z,$0806                       ;[07ff] 28 05
                    add       hl,hl                         ;[0801] 29
                    adc       a                             ;[0802] 8f
                    jp        $07f4                         ;[0803] c3 f4 07
                    pop       af                            ;[0806] f1
                    ex        af,af'                        ;[0807] 08
                    ret                                     ;[0808] c9

                    push      af                            ;[0809] f5
                    call      $0e09                         ;[080a] cd 09 0e
                    or        $08                           ;[080d] f6 08
                    out       (c),a                         ;[080f] ed 79
                    ld        a,($5c93)                     ;[0811] 3a 93 5c
                    nextreg $07,a                           ;[0814] ed 92 07
                    pop       af                            ;[0817] f1
                    ret                                     ;[0818] c9

                    nop                                     ;[0819] 00
                    jr        nc,$081c                      ;[081a] 30 00
                    inc       bc                            ;[081c] 03
                    nop                                     ;[081d] 00
                    nop                                     ;[081e] 00
                    nop                                     ;[081f] 00
                    nop                                     ;[0820] 00
                    nop                                     ;[0821] 00
                    nop                                     ;[0822] 00
                    nop                                     ;[0823] 00
                    nop                                     ;[0824] 00
                    rst       $38                           ;[0825] ff
                    nop                                     ;[0826] 00
                    nop                                     ;[0827] 00
                    nop                                     ;[0828] 00
                    nop                                     ;[0829] 00
                    nop                                     ;[082a] 00
                    nop                                     ;[082b] 00
                    nop                                     ;[082c] 00
                    nop                                     ;[082d] 00
                    ld        d,h                           ;[082e] 54
                    ld        e,l                           ;[082f] 5d
                    ld        a,(de)                        ;[0830] 1a
                    inc       de                            ;[0831] 13
                    cp        $ff                           ;[0832] fe ff
                    jr        nz,$0830                      ;[0834] 20 fa
                    dec       de                            ;[0836] 1b
                    dec       de                            ;[0837] 1b
                    dec       de                            ;[0838] 1b
                    ld        a,(de)                        ;[0839] 1a
                    cp        $2e                           ;[083a] fe 2e
                    inc       de                            ;[083c] 13
                    ld        a,(de)                        ;[083d] 1a
                    jr        nz,$084c                      ;[083e] 20 0c
                    and       $df                           ;[0840] e6 df
                    cp        $4f                           ;[0842] fe 4f
                    jp        z,$0ca6                       ;[0844] ca a6 0c
                    cp        $50                           ;[0847] fe 50
                    scf                                     ;[0849] 37
                    jr        z,$0844                       ;[084a] 28 f8
                    ld        a,(de)                        ;[084c] 1a
                    and       $df                           ;[084d] e6 df
                    ld        ($5c92),a                     ;[084f] 32 92 5c
                    call      $0e09                         ;[0852] cd 09 0e
                    and       $e7                           ;[0855] e6 e7
                    out       (c),a                         ;[0857] ed 79
                    ld        a,$07                         ;[0859] 3e 07
                    call      $0e0b                         ;[085b] cd 0b 0e
                    and       $03                           ;[085e] e6 03
                    ld        ($5c93),a                     ;[0860] 32 93 5c
                    cp        $04                           ;[0863] fe 04
                    jr        nc,$086b                      ;[0865] 30 04
                    ld        a,$03                         ;[0867] 3e 03
                    out       (c),a                         ;[0869] ed 79
                    ld        bc,$0001                      ;[086b] 01 01 00
                    ld        d,b                           ;[086e] 50
                    ld        e,$02                         ;[086f] 1e 02
                    call      $0106                         ;[0871] cd 06 01
                    jr        nc,$0809                      ;[0874] 30 93
                    ld        hl,$0000                      ;[0876] 21 00 00
                    ld        d,h                           ;[0879] 54
                    ld        e,l                           ;[087a] 5d
                    call      $013f                         ;[087b] cd 3f 01
                    ld        b,$00                         ;[087e] 06 00
                    call      $0139                         ;[0880] cd 39 01
                    ld        a,e                           ;[0883] 7b
                    or        d                             ;[0884] b2
                    ld        ($5d00),a                     ;[0885] 32 00 5d
                    call      $3f00                         ;[0888] cd 00 3f
                    nop                                     ;[088b] 00
                    dec       d                             ;[088c] 15
                    di                                      ;[088d] f3
                    ld        sp,$5bff                      ;[088e] 31 ff 5b
                    ld        hl,$5800                      ;[0891] 21 00 58
                    ld        de,$5801                      ;[0894] 11 01 58
                    ld        bc,$02ff                      ;[0897] 01 ff 02
                    ld        (hl),l                        ;[089a] 75
                    ldir                                    ;[089b] ed b0
                    xor       a                             ;[089d] af
                    out       ($fe),a                       ;[089e] d3 fe
                    ld        hl,$0819                      ;[08a0] 21 19 08
                    ld        de,$5d45                      ;[08a3] 11 45 5d
                    ld        bc,$0015                      ;[08a6] 01 15 00
                    ldir                                    ;[08a9] ed b0
                    ld        a,$04                         ;[08ab] 3e 04
                    ld        ($5d79),a                     ;[08ad] 32 79 5d
                    ld        a,($5c92)                     ;[08b0] 3a 92 5c
                    bit       6,a                           ;[08b3] cb 77
                    jr        z,$0913                       ;[08b5] 28 5c
                    ld        hl,$5d08                      ;[08b7] 21 08 5d
                    ld        a,$1b                         ;[08ba] 3e 1b
                    call      $0ad2                         ;[08bc] cd d2 0a
                    ld        sp,$5d07                      ;[08bf] 31 07 5d
                    pop       af                            ;[08c2] f1
                    ld        ($5d2d),a                     ;[08c3] 32 2d 5d
                    pop       hl                            ;[08c6] e1
                    ld        ($5d36),hl                    ;[08c7] 22 36 5d
                    pop       hl                            ;[08ca] e1
                    ld        ($5d34),hl                    ;[08cb] 22 34 5d
                    pop       hl                            ;[08ce] e1
                    ld        ($5d32),hl                    ;[08cf] 22 32 5d
                    pop       hl                            ;[08d2] e1
                    ld        ($5d38),hl                    ;[08d3] 22 38 5d
                    pop       hl                            ;[08d6] e1
                    ld        ($5d27),hl                    ;[08d7] 22 27 5d
                    pop       hl                            ;[08da] e1
                    ld        ($5d30),hl                    ;[08db] 22 30 5d
                    pop       hl                            ;[08de] e1
                    ld        ($5d25),hl                    ;[08df] 22 25 5d
                    pop       hl                            ;[08e2] e1
                    ld        ($5d3a),hl                    ;[08e3] 22 3a 5d
                    pop       hl                            ;[08e6] e1
                    ld        ($5d3c),hl                    ;[08e7] 22 3c 5d
                    pop       hl                            ;[08ea] e1
                    ld        a,l                           ;[08eb] 7d
                    rrca                                    ;[08ec] 0f
                    rrca                                    ;[08ed] 0f
                    and       $01                           ;[08ee] e6 01
                    ld        ($5d3e),a                     ;[08f0] 32 3e 5d
                    ld        a,h                           ;[08f3] 7c
                    ld        ($5d2e),a                     ;[08f4] 32 2e 5d
                    pop       hl                            ;[08f7] e1
                    ld        ($5d23),hl                    ;[08f8] 22 23 5d
                    pop       hl                            ;[08fb] e1
                    ld        ($5d2b),hl                    ;[08fc] 22 2b 5d
                    pop       hl                            ;[08ff] e1
                    ld        a,l                           ;[0900] 7d
                    ld        ($5d40),a                     ;[0901] 32 40 5d
                    ld        a,h                           ;[0904] 7c
                    add       a                             ;[0905] 87
                    ld        ($5d2f),a                     ;[0906] 32 2f 5d
                    ld        sp,$5bff                      ;[0909] 31 ff 5b
                    ld        hl,$2313                      ;[090c] 21 13 23
                    ld        b,$00                         ;[090f] 06 00
                    jr        $093a                         ;[0911] 18 27
                    ld        hl,$5d23                      ;[0913] 21 23 5d
                    ld        a,$1e                         ;[0916] 3e 1e
                    call      $0ad2                         ;[0918] cd d2 0a
                    ld        hl,$5d2f                      ;[091b] 21 2f 5d
                    ld        a,(hl)                        ;[091e] 7e
                    cp        $ff                           ;[091f] fe ff
                    jr        nz,$0926                      ;[0921] 20 03
                    ld        a,$01                         ;[0923] 3e 01
                    ld        (hl),a                        ;[0925] 77
                    set       0,(hl)                        ;[0926] cb c6
                    ld        hl,$5d2e                      ;[0928] 21 2e 5d
                    rl        (hl)                          ;[092b] cb 16
                    rrca                                    ;[092d] 0f
                    rr        (hl)                          ;[092e] cb 1e
                    and       $10                           ;[0930] e6 10
                    ld        b,a                           ;[0932] 47
                    ld        hl,($5d29)                    ;[0933] 2a 29 5d
                    ld        a,h                           ;[0936] 7c
                    or        l                             ;[0937] b5
                    jr        z,$09a1                       ;[0938] 28 67
                    ld        a,b                           ;[093a] 78
                    ld        ($5d05),a                     ;[093b] 32 05 5d
                    ld        ($5d43),hl                    ;[093e] 22 43 5d
                    call      $0af0                         ;[0941] cd f0 0a
                    ld        a,($5c92)                     ;[0944] 3a 92 5c
                    cp        $58                           ;[0947] fe 58
                    call      z,$0b86                       ;[0949] cc 86 0b
                    ld        a,$05                         ;[094c] 3e 05
                    call      $0b71                         ;[094e] cd 71 0b
                    ld        a,$02                         ;[0951] 3e 02
                    call      $0b76                         ;[0953] cd 76 0b
                    xor       a                             ;[0956] af
                    call      $0b76                         ;[0957] cd 76 0b
                    ld        a,($5d00)                     ;[095a] 3a 00 5d
                    and       a                             ;[095d] a7
                    jr        z,$099e                       ;[095e] 28 3e
                    ld        a,$04                         ;[0960] 3e 04
                    ld        ($5d45),a                     ;[0962] 32 45 5d
                    and       a                             ;[0965] a7
                    call      $0b60                         ;[0966] cd 60 0b
                    ld        ($5d43),bc                    ;[0969] ed 43 43 5d
                    ld        a,e                           ;[096d] 7b
                    ld        ($5d46),a                     ;[096e] 32 46 5d
                    nextreg $53,$00                         ;[0971] ed 91 53 00
                    nextreg $54,$01                         ;[0975] ed 91 54 01
                    ld        hl,$6000                      ;[0979] 21 00 60
                    and       $07                           ;[097c] e6 07
                    call      $5d80                         ;[097e] cd 80 5d
                    xor       a                             ;[0981] af
                    cp        $02                           ;[0982] fe 02
                    jr        z,$0997                       ;[0984] 28 11
                    cp        $05                           ;[0986] fe 05
                    jr        z,$0997                       ;[0988] 28 0d
                    ld        d,a                           ;[098a] 57
                    ld        a,($5d46)                     ;[098b] 3a 46 5d
                    and       $07                           ;[098e] e6 07
                    cp        d                             ;[0990] ba
                    ld        a,d                           ;[0991] 7a
                    push      af                            ;[0992] f5
                    call      nz,$0b71                      ;[0993] c4 71 0b
                    pop       af                            ;[0996] f1
                    inc       a                             ;[0997] 3c
                    cp        $08                           ;[0998] fe 08
                    jr        c,$0982                       ;[099a] 38 e6
                    ld        a,$08                         ;[099c] 3e 08
                    jp        $0a60                         ;[099e] c3 60 0a
                    ld        hl,$5d41                      ;[09a1] 21 41 5d
                    ld        a,$02                         ;[09a4] 3e 02
                    call      $0ad2                         ;[09a6] cd d2 0a
                    ld        hl,($5d41)                    ;[09a9] 2a 41 5d
                    ld        a,h                           ;[09ac] 7c
                    and       a                             ;[09ad] a7
                    jr        nz,$0a23                      ;[09ae] 20 73
                    ld        a,l                           ;[09b0] 7d
                    cp        $38                           ;[09b1] fe 38
                    jr        nc,$0a30                      ;[09b3] 30 7b
                    push      af                            ;[09b5] f5
                    ld        hl,$5d43                      ;[09b6] 21 43 5d
                    call      $0ad2                         ;[09b9] cd d2 0a
                    call      $0af0                         ;[09bc] cd f0 0a
                    ld        a,$08                         ;[09bf] 3e 08
                    call      $0e0b                         ;[09c1] cd 0b 0e
                    and       $fe                           ;[09c4] e6 fe
                    ld        hl,$5d40                      ;[09c6] 21 40 5d
                    bit       2,(hl)                        ;[09c9] cb 56
                    jr        z,$09cf                       ;[09cb] 28 02
                    or        $01                           ;[09cd] f6 01
                    out       (c),a                         ;[09cf] ed 79
                    pop       af                            ;[09d1] f1
                    ld        hl,$5d45                      ;[09d2] 21 45 5d
                    cp        $17                           ;[09d5] fe 17
                    jr        nz,$09e3                      ;[09d7] 20 0a
                    ld        a,(hl)                        ;[09d9] 7e
                    cp        $03                           ;[09da] fe 03
                    jr        c,$09e3                       ;[09dc] 38 05
                    cp        $05                           ;[09de] fe 05
                    jr        nc,$09e3                      ;[09e0] 30 01
                    inc       (hl)                          ;[09e2] 34
                    ld        a,($5d48)                     ;[09e3] 3a 48 5d
                    add       a                             ;[09e6] 87
                    ld        a,(hl)                        ;[09e7] 7e
                    jr        nc,$09fa                      ;[09e8] 30 10
                    cp        $09                           ;[09ea] fe 09
                    jr        nc,$09fa                      ;[09ec] 30 0c
                    cp        $04                           ;[09ee] fe 04
                    jr        c,$09fa                       ;[09f0] 38 08
                    cp        $07                           ;[09f2] fe 07
                    ld        a,$0c                         ;[09f4] 3e 0c
                    jr        c,$09f9                       ;[09f6] 38 01
                    inc       a                             ;[09f8] 3c
                    ld        (hl),a                        ;[09f9] 77
                    cp        $07                           ;[09fa] fe 07
                    jr        nc,$0a03                      ;[09fc] 30 05
                    ld        a,$04                         ;[09fe] 3e 04
                    ld        ($5d79),a                     ;[0a00] 32 79 5d
                    ld        a,(hl)                        ;[0a03] 7e
                    cp        $04                           ;[0a04] fe 04
                    jr        nc,$0a35                      ;[0a06] 30 2d
                    ld        a,$30                         ;[0a08] 3e 30
                    ld        ($5d46),a                     ;[0a0a] 32 46 5d
                    ld        d,$03                         ;[0a0d] 16 03
                    push      de                            ;[0a0f] d5
                    call      $0b54                         ;[0a10] cd 54 0b
                    cp        $04                           ;[0a13] fe 04
                    ld        e,$02                         ;[0a15] 1e 02
                    jr        z,$0a25                       ;[0a17] 28 0c
                    cp        $05                           ;[0a19] fe 05
                    ld        e,$00                         ;[0a1b] 1e 00
                    jr        z,$0a25                       ;[0a1d] 28 06
                    cp        $08                           ;[0a1f] fe 08
                    ld        e,$05                         ;[0a21] 1e 05
                    jr        nz,$0a30                      ;[0a23] 20 0b
                    ld        a,e                           ;[0a25] 7b
                    call      $0b71                         ;[0a26] cd 71 0b
                    pop       de                            ;[0a29] d1
                    dec       d                             ;[0a2a] 15
                    jr        nz,$0a0f                      ;[0a2b] 20 e2
                    xor       a                             ;[0a2d] af
                    jr        $0a60                         ;[0a2e] 18 30
                    ld        a,$15                         ;[0a30] 3e 15
                    jp        $0ad9                         ;[0a32] c3 d9 0a
                    ld        d,$08                         ;[0a35] 16 08
                    push      de                            ;[0a37] d5
                    call      $0b54                         ;[0a38] cd 54 0b
                    sub       $03                           ;[0a3b] d6 03
                    jr        c,$0a30                       ;[0a3d] 38 f1
                    cp        $08                           ;[0a3f] fe 08
                    jr        nc,$0a30                      ;[0a41] 30 ed
                    call      $0b71                         ;[0a43] cd 71 0b
                    pop       de                            ;[0a46] d1
                    dec       d                             ;[0a47] 15
                    jr        nz,$0a37                      ;[0a48] 20 ed
                    ld        a,($5d45)                     ;[0a4a] 3a 45 5d
                    ld        b,$18                         ;[0a4d] 06 18
                    cp        $50                           ;[0a4f] fe 50
                    jr        z,$0a61                       ;[0a51] 28 0e
                    ld        b,$10                         ;[0a53] 06 10
                    cp        $51                           ;[0a55] fe 51
                    jr        z,$0a61                       ;[0a57] 28 08
                    cp        $0c                           ;[0a59] fe 0c
                    ld        a,$08                         ;[0a5b] 3e 08
                    jr        c,$0a60                       ;[0a5d] 38 01
                    inc       a                             ;[0a5f] 3c
                    ld        b,a                           ;[0a60] 47
                    nextreg $56,$0e                         ;[0a61] ed 91 56 0e
                    nextreg $57,$0f                         ;[0a65] ed 91 57 0f
                    ld        a,($5c92)                     ;[0a69] 3a 92 5c
                    cp        $58                           ;[0a6c] fe 58
                    ld        de,$00ff                      ;[0a6e] 11 ff 00
                    jr        z,$0aaa                       ;[0a71] 28 37
                    ld        a,b                           ;[0a73] 78
                    xor       $18                           ;[0a74] ee 18
                    push      af                            ;[0a76] f5
                    call      $0d70                         ;[0a77] cd 70 0d
                    jr        nc,$0ad9                      ;[0a7a] 30 5d
                    ld        d,a                           ;[0a7c] 57
                    push      de                            ;[0a7d] d5
                    ld        b,$00                         ;[0a7e] 06 00
                    call      $0109                         ;[0a80] cd 09 01
                    pop       de                            ;[0a83] d1
                    nextreg $b8,$00                         ;[0a84] ed 91 b8 00
                    bit       5,d                           ;[0a88] cb 6a
                    jr        z,$0a93                       ;[0a8a] 28 07
                    ld        a,($5d48)                     ;[0a8c] 3a 48 5d
                    bit       2,a                           ;[0a8f] cb 57
                    jr        z,$0a9c                       ;[0a91] 28 09
                    ld        a,$84                         ;[0a93] 3e 84
                    call      $0e0b                         ;[0a95] cd 0b 0e
                    or        $01                           ;[0a98] f6 01
                    out       (c),a                         ;[0a9a] ed 79
                    pop       af                            ;[0a9c] f1
                    cp        $09                           ;[0a9d] fe 09
                    jr        nc,$0aaa                      ;[0a9f] 30 09
                    ld        a,$0a                         ;[0aa1] 3e 0a
                    call      $0e0b                         ;[0aa3] cd 0b 0e
                    and       $ef                           ;[0aa6] e6 ef
                    out       (c),a                         ;[0aa8] ed 79
                    call      $0809                         ;[0aaa] cd 09 08
                    ld        a,$02                         ;[0aad] 3e 02
                    call      $0e0b                         ;[0aaf] cd 0b 0e
                    and       $80                           ;[0ab2] e6 80
                    or        $08                           ;[0ab4] f6 08
                    out       (c),a                         ;[0ab6] ed 79
                    nop                                     ;[0ab8] 00
                    ld        b,$00                         ;[0ab9] 06 00
                    push      de                            ;[0abb] d5
                    nextreg $56,$0e                         ;[0abc] ed 91 56 0e
                    nextreg $57,$0f                         ;[0ac0] ed 91 57 0f
                    call      $0112                         ;[0ac4] cd 12 01
                    pop       hl                            ;[0ac7] e1
                    ret       c                             ;[0ac8] d8
                    cp        $19                           ;[0ac9] fe 19
                    scf                                     ;[0acb] 37
                    ccf                                     ;[0acc] 3f
                    ret       nz                            ;[0acd] c0
                    sbc       hl,de                         ;[0ace] ed 52
                    cp        a                             ;[0ad0] bf
                    ret                                     ;[0ad1] c9

                    ld        e,a                           ;[0ad2] 5f
                    ld        d,$00                         ;[0ad3] 16 00
                    call      $0ab9                         ;[0ad5] cd b9 0a
                    ret       c                             ;[0ad8] d8
                    push      af                            ;[0ad9] f5
                    call      $3f00                         ;[0ada] cd 00 3f
                    and       (hl)                          ;[0add] a6
                    scf                                     ;[0ade] 37
                    pop       af                            ;[0adf] f1
                    cp        $0a                           ;[0ae0] fe 0a
                    ld        d,$3e                         ;[0ae2] 16 3e
                    jr        c,$0ae8                       ;[0ae4] 38 02
                    ld        d,$19                         ;[0ae6] 16 19
                    add       d                             ;[0ae8] 82
                    call      $3f00                         ;[0ae9] cd 00 3f
                    inc       (hl)                          ;[0aec] 34
                    dec       c                             ;[0aed] 0d
                    jr        $0aee                         ;[0aee] 18 fe
                    ld        hl,$0c22                      ;[0af0] 21 22 0c
                    ld        de,$5d80                      ;[0af3] 11 80 5d
                    ld        bc,$0084                      ;[0af6] 01 84 00
                    ldir                                    ;[0af9] ed b0
                    ld        ($5d03),bc                    ;[0afb] ed 43 03 5d
                    ld        a,$12                         ;[0aff] 3e 12
                    call      $0e0b                         ;[0b01] cd 0b 0e
                    ld        hl,$5d01                      ;[0b04] 21 01 5d
                    ld        (hl),a                        ;[0b07] 77
                    inc       hl                            ;[0b08] 23
                    inc       a                             ;[0b09] 3c
                    ld        (hl),a                        ;[0b0a] 77
                    dec       hl                            ;[0b0b] 2b
                    push      hl                            ;[0b0c] e5
                    ld        a,(hl)                        ;[0b0d] 7e
                    call      $0b14                         ;[0b0e] cd 14 0b
                    pop       hl                            ;[0b11] e1
                    inc       hl                            ;[0b12] 23
                    ld        a,(hl)                        ;[0b13] 7e
                    ld        c,a                           ;[0b14] 4f
                    ld        hl,$c000                      ;[0b15] 21 00 c0
                    ld        de,$4000                      ;[0b18] 11 00 40
                    call      $0ab9                         ;[0b1b] cd b9 0a
                    ret       c                             ;[0b1e] d8
                    ret       z                             ;[0b1f] c8
                    jr        $0ad9                         ;[0b20] 18 b7
                    ld        hl,($5d03)                    ;[0b22] 2a 03 5d
                    bit       6,h                           ;[0b25] cb 74
                    jr        z,$0b39                       ;[0b27] 28 10
                    res       6,h                           ;[0b29] cb b4
                    push      hl                            ;[0b2b] e5
                    ld        hl,$5d01                      ;[0b2c] 21 01 5d
                    ld        a,(hl)                        ;[0b2f] 7e
                    inc       hl                            ;[0b30] 23
                    ld        d,(hl)                        ;[0b31] 56
                    ld        (hl),a                        ;[0b32] 77
                    dec       hl                            ;[0b33] 2b
                    ld        (hl),d                        ;[0b34] 72
                    call      $0b14                         ;[0b35] cd 14 0b
                    pop       hl                            ;[0b38] e1
                    ld        de,$5d01                      ;[0b39] 11 01 5d
                    ld        a,(de)                        ;[0b3c] 1a
                    inc       de                            ;[0b3d] 13
                    add       a                             ;[0b3e] 87
                    nextreg $53,a                           ;[0b3f] ed 92 53
                    inc       a                             ;[0b42] 3c
                    nextreg $54,a                           ;[0b43] ed 92 54
                    ld        a,(de)                        ;[0b46] 1a
                    add       a                             ;[0b47] 87
                    nextreg $55,a                           ;[0b48] ed 92 55
                    inc       a                             ;[0b4b] 3c
                    nextreg $56,a                           ;[0b4c] ed 92 56
                    add       hl,$6000                      ;[0b4f] ed 34 00 60
                    ret                                     ;[0b53] c9

                    scf                                     ;[0b54] 37
                    call      $0b60                         ;[0b55] cd 60 0b
                    ld        a,b                           ;[0b58] 78
                    and       c                             ;[0b59] a1
                    inc       a                             ;[0b5a] 3c
                    ld        ($5d05),a                     ;[0b5b] 32 05 5d
                    ld        a,e                           ;[0b5e] 7b
                    ret                                     ;[0b5f] c9

                    push      af                            ;[0b60] f5
                    call      $0b22                         ;[0b61] cd 22 0b
                    ld        c,(hl)                        ;[0b64] 4e
                    inc       hl                            ;[0b65] 23
                    ld        b,(hl)                        ;[0b66] 46
                    inc       hl                            ;[0b67] 23
                    ld        e,(hl)                        ;[0b68] 5e
                    inc       hl                            ;[0b69] 23
                    pop       af                            ;[0b6a] f1
                    jr        c,$0b7e                       ;[0b6b] 38 11
                    ld        d,(hl)                        ;[0b6d] 56
                    inc       hl                            ;[0b6e] 23
                    jr        $0b7e                         ;[0b6f] 18 0d
                    ld        h,$00                         ;[0b71] 26 00
                    ld        ($5d06),hl                    ;[0b73] 22 06 5d
                    push      af                            ;[0b76] f5
                    call      $0b22                         ;[0b77] cd 22 0b
                    pop       af                            ;[0b7a] f1
                    call      $5d80                         ;[0b7b] cd 80 5d
                    add       hl,$a000                      ;[0b7e] ed 34 00 a0
                    ld        ($5d03),hl                    ;[0b82] 22 03 5d
                    ret                                     ;[0b85] c9

                    ld        a,($5d01)                     ;[0b86] 3a 01 5d
                    add       a                             ;[0b89] 87
                    nextreg $53,a                           ;[0b8a] ed 92 53
                    add       $03                           ;[0b8d] c6 03
                    nextreg $55,a                           ;[0b8f] ed 92 55
                    ld        a,($7c5c)                     ;[0b92] 3a 5c 7c
                    cp        $fb                           ;[0b95] fe fb
                    jr        nz,$0ba3                      ;[0b97] 20 0a
                    ld        hl,($7c5d)                    ;[0b99] 2a 5d 7c
                    ld        de,$4ded                      ;[0b9c] 11 ed 4d
                    sbc       hl,de                         ;[0b9f] ed 52
                    jr        z,$0bbf                       ;[0ba1] 28 1c
                    ld        hl,$0bd4                      ;[0ba3] 21 d4 0b
                    ld        a,(hl)                        ;[0ba6] 7e
                    and       a                             ;[0ba7] a7
                    ret       z                             ;[0ba8] c8
                    ld        b,a                           ;[0ba9] 47
                    inc       hl                            ;[0baa] 23
                    ld        e,(hl)                        ;[0bab] 5e
                    inc       hl                            ;[0bac] 23
                    ld        d,(hl)                        ;[0bad] 56
                    inc       hl                            ;[0bae] 23
                    ld        a,(de)                        ;[0baf] 1a
                    inc       de                            ;[0bb0] 13
                    sub       (hl)                          ;[0bb1] 96
                    jr        nz,$0bcb                      ;[0bb2] 20 17
                    djnz      $0bae                         ;[0bb4] 10 f8
                    ld        b,$02                         ;[0bb6] 06 02
                    inc       hl                            ;[0bb8] 23
                    ld        e,(hl)                        ;[0bb9] 5e
                    inc       hl                            ;[0bba] 23
                    ld        d,(hl)                        ;[0bbb] 56
                    ld        (de),a                        ;[0bbc] 12
                    djnz      $0bb8                         ;[0bbd] 10 f9
                    ld        a,$05                         ;[0bbf] 3e 05
                    call      $0e0b                         ;[0bc1] cd 0b 0e
                    and       $05                           ;[0bc4] e6 05
                    or        $42                           ;[0bc6] f6 42
                    out       (c),a                         ;[0bc8] ed 79
                    ret                                     ;[0bca] c9

                    inc       hl                            ;[0bcb] 23
                    djnz      $0bcb                         ;[0bcc] 10 fd
                    inc       hl                            ;[0bce] 23
                    inc       hl                            ;[0bcf] 23
                    inc       hl                            ;[0bd0] 23
                    inc       hl                            ;[0bd1] 23
                    jr        $0ba6                         ;[0bd2] 18 d2
                    inc       c                             ;[0bd4] 0c
                    ld        l,b                           ;[0bd5] 68
                    ld        a,h                           ;[0bd6] 7c
                    exx                                     ;[0bd7] d9
                    pop       iy                            ;[0bd8] fd e1
                    pop       ix                            ;[0bda] dd e1
                    pop       hl                            ;[0bdc] e1
                    pop       de                            ;[0bdd] d1
                    pop       bc                            ;[0bde] c1
                    pop       af                            ;[0bdf] f1
                    ei                                      ;[0be0] fb
                    reti                                    ;[0be1] ed 4d

                    ld        h,h                           ;[0be3] 64
                    ld        a,h                           ;[0be4] 7c
                    ld        l,b                           ;[0be5] 68
                    ld        a,h                           ;[0be6] 7c
                    dec       c                             ;[0be7] 0d
                    ld        h,h                           ;[0be8] 64
                    ld        a,h                           ;[0be9] 7c
                    exx                                     ;[0bea] d9
                    exx                                     ;[0beb] d9
                    pop       iy                            ;[0bec] fd e1
                    pop       ix                            ;[0bee] dd e1
                    pop       hl                            ;[0bf0] e1
                    pop       de                            ;[0bf1] d1
                    pop       bc                            ;[0bf2] c1
                    pop       af                            ;[0bf3] f1
                    ei                                      ;[0bf4] fb
                    reti                                    ;[0bf5] ed 4d

                    ld        h,h                           ;[0bf7] 64
                    ld        a,h                           ;[0bf8] 7c
                    ld        h,l                           ;[0bf9] 65
                    ld        a,h                           ;[0bfa] 7c
                    inc       c                             ;[0bfb] 0c
                    ld        l,e                           ;[0bfc] 6b
                    ld        a,h                           ;[0bfd] 7c
                    exx                                     ;[0bfe] d9
                    pop       iy                            ;[0bff] fd e1
                    pop       ix                            ;[0c01] dd e1
                    pop       hl                            ;[0c03] e1
                    pop       de                            ;[0c04] d1
                    pop       bc                            ;[0c05] c1
                    pop       af                            ;[0c06] f1
                    ei                                      ;[0c07] fb
                    reti                                    ;[0c08] ed 4d

                    ld        h,h                           ;[0c0a] 64
                    ld        a,h                           ;[0c0b] 7c
                    ld        l,e                           ;[0c0c] 6b
                    ld        a,h                           ;[0c0d] 7c
                    inc       c                             ;[0c0e] 0c
                    cpl                                     ;[0c0f] 2f
                    xor       c                             ;[0c10] a9
                    exx                                     ;[0c11] d9
                    pop       iy                            ;[0c12] fd e1
                    pop       ix                            ;[0c14] dd e1
                    pop       de                            ;[0c16] d1
                    pop       bc                            ;[0c17] c1
                    pop       hl                            ;[0c18] e1
                    pop       af                            ;[0c19] f1
                    ei                                      ;[0c1a] fb
                    reti                                    ;[0c1b] ed 4d

                    dec       hl                            ;[0c1d] 2b
                    xor       c                             ;[0c1e] a9
                    cpl                                     ;[0c1f] 2f
                    xor       c                             ;[0c20] a9
                    nop                                     ;[0c21] 00
                    add       a                             ;[0c22] 87
                    nextreg $57,a                           ;[0c23] ed 92 57
                    inc       a                             ;[0c26] 3c
                    push      af                            ;[0c27] f5
                    cp        $0b                           ;[0c28] fe 0b
                    jr        z,$0c80                       ;[0c2a] 28 54
                    cp        $0f                           ;[0c2c] fe 0f
                    jr        z,$0c95                       ;[0c2e] 28 65
                    ld        de,$e000                      ;[0c30] 11 00 e0
                    ld        a,($5d05)                     ;[0c33] 3a 05 5d
                    and       a                             ;[0c36] a7
                    jr        nz,$0c4b                      ;[0c37] 20 12
                    call      $5da0                         ;[0c39] cd a0 5d
                    pop       af                            ;[0c3c] f1
                    nextreg $57,a                           ;[0c3d] ed 92 57
                    ld        d,$e0                         ;[0c40] 16 e0
                    ld        bc,$2000                      ;[0c42] 01 00 20
                    ldir                                    ;[0c45] ed b0
                    xor       a                             ;[0c47] af
                    out       ($e3),a                       ;[0c48] d3 e3
                    ret                                     ;[0c4a] c9

                    call      $5db2                         ;[0c4b] cd b2 5d
                    pop       af                            ;[0c4e] f1
                    nextreg $57,a                           ;[0c4f] ed 92 57
                    ld        d,$e0                         ;[0c52] 16 e0
                    ld        bc,($5d06)                    ;[0c54] ed 4b 06 5d
                    inc       b                             ;[0c58] 04
                    dec       b                             ;[0c59] 05
                    jr        nz,$0c7c                      ;[0c5a] 20 20
                    ld        a,(hl)                        ;[0c5c] 7e
                    inc       hl                            ;[0c5d] 23
                    cp        $ed                           ;[0c5e] fe ed
                    jr        z,$0c71                       ;[0c60] 28 0f
                    ld        (de),a                        ;[0c62] 12
                    inc       e                             ;[0c63] 1c
                    jr        nz,$0c58                      ;[0c64] 20 f2
                    inc       d                             ;[0c66] 14
                    bit       5,d                           ;[0c67] cb 6a
                    jr        nz,$0c58                      ;[0c69] 20 ed
                    ld        ($5d06),bc                    ;[0c6b] ed 43 06 5d
                    jr        $0c47                         ;[0c6f] 18 d6
                    ld        b,$01                         ;[0c71] 06 01
                    ld        c,(hl)                        ;[0c73] 4e
                    inc       hl                            ;[0c74] 23
                    cp        c                             ;[0c75] b9
                    jr        nz,$0c62                      ;[0c76] 20 ea
                    ld        b,(hl)                        ;[0c78] 46
                    inc       hl                            ;[0c79] 23
                    ld        c,(hl)                        ;[0c7a] 4e
                    inc       hl                            ;[0c7b] 23
                    ld        a,c                           ;[0c7c] 79
                    dec       b                             ;[0c7d] 05
                    jr        $0c62                         ;[0c7e] 18 e2
                    ld        a,$86                         ;[0c80] 3e 86
                    out       ($e3),a                       ;[0c82] d3 e3
                    call      $5dff                         ;[0c84] cd ff 5d
                    pop       af                            ;[0c87] f1
                    nextreg $57,a                           ;[0c88] ed 92 57
                    ld        d,$e0                         ;[0c8b] 16 e0
                    ld        a,($5d05)                     ;[0c8d] 3a 05 5d
                    and       a                             ;[0c90] a7
                    jr        nz,$0c54                      ;[0c91] 20 c1
                    jr        $0c42                         ;[0c93] 18 ad
                    pop       af                            ;[0c95] f1
                    ld        a,$84                         ;[0c96] 3e 84
                    out       ($e3),a                       ;[0c98] d3 e3
                    call      $5dff                         ;[0c9a] cd ff 5d
                    ld        a,$85                         ;[0c9d] 3e 85
                    out       ($e3),a                       ;[0c9f] d3 e3
                    ld        de,$2000                      ;[0ca1] 11 00 20
                    jr        $0c8d                         ;[0ca4] 18 e7
                    push      hl                            ;[0ca6] e5
                    push      af                            ;[0ca7] f5
                    sub       $4f                           ;[0ca8] d6 4f
                    add       a                             ;[0caa] 87
                    add       a                             ;[0cab] 87
                    add       a                             ;[0cac] 87
                    call      $0d70                         ;[0cad] cd 70 0d
                    pop       de                            ;[0cb0] d1
                    pop       bc                            ;[0cb1] c1
                    ret       nc                            ;[0cb2] d0
                    ld        hl,$8000                      ;[0cb3] 21 00 80
                    ld        sp,hl                         ;[0cb6] f9
                    push      de                            ;[0cb7] d5
                    push      bc                            ;[0cb8] c5
                    call      $0d50                         ;[0cb9] cd 50 0d
                    pop       hl                            ;[0cbc] e1
                    ld        de,$8000                      ;[0cbd] 11 00 80
                    pop       af                            ;[0cc0] f1
                    push      af                            ;[0cc1] f5
                    jr        nc,$0cc6                      ;[0cc2] 30 02
                    ld        e,$09                         ;[0cc4] 1e 09
                    ld        a,$ff                         ;[0cc6] 3e ff
                    call      $0e46                         ;[0cc8] cd 46 0e
                    jp        nc,$0ad9                      ;[0ccb] d2 d9 0a
                    ld        h,$c0                         ;[0cce] 26 c0
                    call      $0d50                         ;[0cd0] cd 50 0d
                    ld        hl,$0d07                      ;[0cd3] 21 07 0d
                    ld        de,$f000                      ;[0cd6] 11 00 f0
                    ld        bc,$0049                      ;[0cd9] 01 49 00
                    ldir                                    ;[0cdc] ed b0
                    di                                      ;[0cde] f3
                    ld        a,$0a                         ;[0cdf] 3e 0a
                    call      $0e0b                         ;[0ce1] cd 0b 0e
                    and       $ef                           ;[0ce4] e6 ef
                    out       (c),a                         ;[0ce6] ed 79
                    ld        bc,$7ffd                      ;[0ce8] 01 fd 7f
                    ld        a,$2f                         ;[0ceb] 3e 2f
                    out       (c),a                         ;[0ced] ed 79
                    xor       a                             ;[0cef] af
                    ex        af,af'                        ;[0cf0] 08
                    pop       af                            ;[0cf1] f1
                    ld        iy,$4000                      ;[0cf2] fd 21 00 40
                    im        1                             ;[0cf6] ed 56
                    ld        hl,$8000                      ;[0cf8] 21 00 80
                    ld        de,$4000                      ;[0cfb] 11 00 40
                    ld        b,d                           ;[0cfe] 42
                    ld        c,e                           ;[0cff] 4b
                    ldir                                    ;[0d00] ed b0
                    ex        de,hl                         ;[0d02] eb
                    ld        sp,hl                         ;[0d03] f9
                    jp        $f000                         ;[0d04] c3 00 f0
                    nextreg $8c,$a0                         ;[0d07] ed 91 8c a0
                    ei                                      ;[0d0b] fb
                    jr        c,$0d29                       ;[0d0c] 38 1b
                    ld        a,$0e                         ;[0d0e] 3e 0e
                    ld        i,a                           ;[0d10] ed 47
                    ld        hl,$3faa                      ;[0d12] 21 aa 3f
                    push      hl                            ;[0d15] e5
                    call      $0747                         ;[0d16] cd 47 07
                    ld        hl,$88ff                      ;[0d19] 21 ff 88
                    ld        ($4000),hl                    ;[0d1c] 22 00 40
                    ld        hl,$0447                      ;[0d1f] 21 47 04
                    push      hl                            ;[0d22] e5
                    ld        bc,$0000                      ;[0d23] 01 00 00
                    jp        $0934                         ;[0d26] c3 34 09
                    ld        hl,$f040                      ;[0d29] 21 40 f0
                    ld        de,$4000                      ;[0d2c] 11 00 40
                    ld        bc,$0009                      ;[0d2f] 01 09 00
                    ldir                                    ;[0d32] ed b0
                    ld        ix,$0281                      ;[0d34] dd 21 81 02
                    ld        a,$1e                         ;[0d38] 3e 1e
                    ld        i,a                           ;[0d3a] ed 47
                    ld        hl,$3e00                      ;[0d3c] 21 00 3e
                    push      hl                            ;[0d3f] e5
                    ld        hl,$0676                      ;[0d40] 21 76 06
                    push      hl                            ;[0d43] e5
                    jp        $0207                         ;[0d44] c3 07 02
                    rst       $38                           ;[0d47] ff
                    add       b                             ;[0d48] 80
                    call      m,$007f                       ;[0d49] fc 7f 00
                    add       b                             ;[0d4c] 80
                    nop                                     ;[0d4d] 00
                    cp        $ff                           ;[0d4e] fe ff
                    ld        l,$00                         ;[0d50] 2e 00
                    ld        d,h                           ;[0d52] 54
                    ld        e,$01                         ;[0d53] 1e 01
                    ld        bc,$3fff                      ;[0d55] 01 ff 3f
                    ld        (hl),l                        ;[0d58] 75
                    ldir                                    ;[0d59] ed b0
                    ret                                     ;[0d5b] c9

                    ld        bc,$f650                      ;[0d5c] 01 50 f6
                    rst       $08                           ;[0d5f] cf
                    sub       d                             ;[0d60] 92
                    ret                                     ;[0d61] c9

                    ld        e,a                           ;[0d62] 5f
                    sub       $a5                           ;[0d63] d6 a5
                    jp        nc,$0c10                      ;[0d65] d2 10 0c
                    ld        bc,$fb50                      ;[0d68] 01 50 fb
                    rst       $08                           ;[0d6b] cf
                    sub       d                             ;[0d6c] 92
                    ret                                     ;[0d6d] c9

                    call      $f50e                         ;[0d6e] cd 0e f5
                    call      $1e36                         ;[0d71] cd 36 1e
                    pop       af                            ;[0d74] f1
                    push      af                            ;[0d75] f5
                    call      $0e37                         ;[0d76] cd 37 0e
                    pop       bc                            ;[0d79] c1
                    ret       nc                            ;[0d7a] d0
                    push      bc                            ;[0d7b] c5
                    bit       4,b                           ;[0d7c] cb 60
                    jr        z,$0dac                       ;[0d7e] 28 2c
                    ld        a,($5b68)                     ;[0d80] 3a 68 5b
                    bit       4,a                           ;[0d83] cb 67
                    jr        nz,$0dac                      ;[0d85] 20 25
                    ld        hl,$0d5c                      ;[0d87] 21 5c 0d
                    ld        de,$c000                      ;[0d8a] 11 00 c0
                    ld        bc,$0014                      ;[0d8d] 01 14 00
                    push      de                            ;[0d90] d5
                    ldir                                    ;[0d91] ed b0
                    pop       hl                            ;[0d93] e1
                    ld        de,$0eac                      ;[0d94] 11 ac 0e
                    ld        c,$06                         ;[0d97] 0e 06
                    call      $0e94                         ;[0d99] cd 94 0e
                    ld        de,$0ecd                      ;[0d9c] 11 cd 0e
                    ld        c,$0c                         ;[0d9f] 0e 0c
                    call      $0e94                         ;[0da1] cd 94 0e
                    ld        de,$15be                      ;[0da4] 11 be 15
                    ld        c,$02                         ;[0da7] 0e 02
                    call      $0e94                         ;[0da9] cd 94 0e
                    pop       bc                            ;[0dac] c1
                    ld        a,b                           ;[0dad] 78
                    cp        $18                           ;[0dae] fe 18
                    ld        a,$a0                         ;[0db0] 3e a0
                    ld        d,$01                         ;[0db2] 16 01
                    ld        e,$c2                         ;[0db4] 1e c2
                    jr        nz,$0dbe                      ;[0db6] 20 06
                    ld        a,$90                         ;[0db8] 3e 90
                    dec       d                             ;[0dba] 15
                    ld        e,$c0                         ;[0dbb] 1e c0
                    ld        b,e                           ;[0dbd] 43
                    nextreg $03,a                           ;[0dbe] ed 92 03
                    ld        a,b                           ;[0dc1] 78
                    and       $10                           ;[0dc2] e6 10
                    xor       $10                           ;[0dc4] ee 10
                    add       a                             ;[0dc6] 87
                    or        $80                           ;[0dc7] f6 80
                    push      af                            ;[0dc9] f5
                    ld        a,$83                         ;[0dca] 3e 83
                    call      $0e0b                         ;[0dcc] cd 0b 0e
                    inc       a                             ;[0dcf] 3c
                    jr        nz,$0de0                      ;[0dd0] 20 0e
                    nextreg $83,$3f                         ;[0dd2] ed 91 83 3f
                    ld        a,d                           ;[0dd6] 7a
                    nextreg $84,a                           ;[0dd7] ed 92 84
                    nextreg $85,$01                         ;[0dda] ed 91 85 01
                    jr        $0de9                         ;[0dde] 18 09
                    ld        a,$82                         ;[0de0] 3e 82
                    call      $0e0b                         ;[0de2] cd 0b 0e
                    and       $25                           ;[0de5] e6 25
                    or        e                             ;[0de7] b3
                    ld        e,a                           ;[0de8] 5f
                    xor       a                             ;[0de9] af
                    nextreg $14,a                           ;[0dea] ed 92 14
                    nextreg $4a,a                           ;[0ded] ed 92 4a
                    nextreg $d8,a                           ;[0df0] ed 92 d8
                    call      $0e09                         ;[0df3] cd 09 0e
                    and       $fc                           ;[0df6] e6 fc
                    or        $01                           ;[0df8] f6 01
                    out       (c),a                         ;[0dfa] ed 79
                    dec       b                             ;[0dfc] 05
                    ld        a,$08                         ;[0dfd] 3e 08
                    call      $0e0b                         ;[0dff] cd 0b 0e
                    and       $bf                           ;[0e02] e6 bf
                    out       (c),a                         ;[0e04] ed 79
                    pop       af                            ;[0e06] f1
                    scf                                     ;[0e07] 37
                    ret                                     ;[0e08] c9

                    ld        a,$06                         ;[0e09] 3e 06
                    ld        bc,$243b                      ;[0e0b] 01 3b 24
                    out       (c),a                         ;[0e0e] ed 79
                    inc       b                             ;[0e10] 04
                    in        a,(c)                         ;[0e11] ed 78
                    ret                                     ;[0e13] c9

                    nextreg $82,$da                         ;[0e14] ed 91 82 da
                    nextreg $83,$2b                         ;[0e18] ed 91 83 2b
                    nextreg $84,$01                         ;[0e1c] ed 91 84 01
                    nextreg $85,$00                         ;[0e20] ed 91 85 00
                    call      $0e09                         ;[0e24] cd 09 0e
                    and       $fc                           ;[0e27] e6 fc
                    or        $01                           ;[0e29] f6 01
                    out       (c),a                         ;[0e2b] ed 79
                    ld        a,$08                         ;[0e2d] 3e 08
                    call      $0e0b                         ;[0e2f] cd 0b 0e
                    and       $bb                           ;[0e32] e6 bb
                    out       (c),a                         ;[0e34] ed 79
                    ret                                     ;[0e36] c9

                    nextreg $8c,$c0                         ;[0e37] ed 91 8c c0
                    ld        de,$0000                      ;[0e3b] 11 00 00
                    and       $fe                           ;[0e3e] e6 fe
                    sub       $10                           ;[0e40] d6 10
                    jr        z,$0e46                       ;[0e42] 28 02
                    ld        a,$04                         ;[0e44] 3e 04
                    ld        i,a                           ;[0e46] ed 47
                    push      de                            ;[0e48] d5
                    ld        bc,$0101                      ;[0e49] 01 01 01
                    ld        de,$0002                      ;[0e4c] 11 02 00
                    call      $0106                         ;[0e4f] cd 06 01
                    jr        nc,$0e72                      ;[0e52] 30 1e
                    ld        hl,$c000                      ;[0e54] 21 00 c0
                    ld        de,$1000                      ;[0e57] 11 00 10
                    push      hl                            ;[0e5a] e5
                    push      de                            ;[0e5b] d5
                    ld        bc,$0107                      ;[0e5c] 01 07 01
                    call      $0112                         ;[0e5f] cd 12 01
                    pop       bc                            ;[0e62] c1
                    pop       hl                            ;[0e63] e1
                    pop       de                            ;[0e64] d1
                    push      af                            ;[0e65] f5
                    call      $0e81                         ;[0e66] cd 81 0e
                    pop       af                            ;[0e69] f1
                    push      de                            ;[0e6a] d5
                    jr        c,$0e54                       ;[0e6b] 38 e7
                    cp        $19                           ;[0e6d] fe 19
                    scf                                     ;[0e6f] 37
                    jr        z,$0e73                       ;[0e70] 28 01
                    and       a                             ;[0e72] a7
                    pop       hl                            ;[0e73] e1
                    push      af                            ;[0e74] f5
                    ld        b,$01                         ;[0e75] 06 01
                    call      $0109                         ;[0e77] cd 09 01
                    ld        b,$01                         ;[0e7a] 06 01
                    call      nc,$010c                      ;[0e7c] d4 0c 01
                    pop       af                            ;[0e7f] f1
                    ret                                     ;[0e80] c9

                    ld        a,i                           ;[0e81] ed 57
                    inc       a                             ;[0e83] 3c
                    jr        z,$0e91                       ;[0e84] 28 0b
                    ld        i,a                           ;[0e86] ed 47
                    res       6,d                           ;[0e88] cb b2
                    cp        $09                           ;[0e8a] fe 09
                    ret       nc                            ;[0e8c] d0
                    cp        $05                           ;[0e8d] fe 05
                    jr        nc,$0e94                      ;[0e8f] 30 03
                    ldir                                    ;[0e91] ed b0
                    ret                                     ;[0e93] c9

                    push    $5b43                           ;[0e94] ed 8a 5b 43
                    push    $001b                           ;[0e98] ed 8a 00 1b
                    jp        $5b3e                         ;[0e9c] c3 3e 5b
                    ld        bc,$243b                      ;[0e9f] 01 3b 24
                    ld        a,$57                         ;[0ea2] 3e 57
                    out       (c),a                         ;[0ea4] ed 79
                    inc       b                             ;[0ea6] 04
                    in        d,(c)                         ;[0ea7] ed 50
                    ld        a,$10                         ;[0ea9] 3e 10
                    out       (c),a                         ;[0eab] ed 79
                    ld        ix,$e000                      ;[0ead] dd 21 00 e0
                    ld        a,($e050)                     ;[0eb1] 3a 50 e0
                    bit       0,h                           ;[0eb4] cb 44
                    jr        z,$0ebf                       ;[0eb6] 28 07
                    ld        ix,$e020                      ;[0eb8] dd 21 20 e0
                    ld        a,($e051)                     ;[0ebc] 3a 51 e0
                    ld        b,a                           ;[0ebf] 47
                    inc       l                             ;[0ec0] 2c
                    dec       l                             ;[0ec1] 2d
                    jr        z,$0f3c                       ;[0ec2] 28 78
                    dec       l                             ;[0ec4] 2d
                    jr        nz,$0f09                      ;[0ec5] 20 42
                    dec       a                             ;[0ec7] 3d
                    rrca                                    ;[0ec8] 0f
                    rrca                                    ;[0ec9] 0f
                    rrca                                    ;[0eca] 0f
                    and       $1f                           ;[0ecb] e6 1f
                    add       ixl                           ;[0ecd] dd 85
                    ld        ixl,a                         ;[0ecf] dd 6f
                    ld        a,b                           ;[0ed1] 78
                    dec       a                             ;[0ed2] 3d
                    and       $07                           ;[0ed3] e6 07
                    inc       a                             ;[0ed5] 3c
                    ld        c,$80                         ;[0ed6] 0e 80
                    rlc       c                             ;[0ed8] cb 01
                    dec       a                             ;[0eda] 3d
                    jr        nz,$0ed8                      ;[0edb] 20 fb
                    ld        e,b                           ;[0edd] 58
                    ld        a,(ix+$00)                    ;[0ede] dd 7e 00
                    and       c                             ;[0ee1] a1
                    jr        z,$0ef4                       ;[0ee2] 28 10
                    rrc       c                             ;[0ee4] cb 09
                    dec       e                             ;[0ee6] 1d
                    jr        z,$0eef                       ;[0ee7] 28 06
                    jr        nc,$0ede                      ;[0ee9] 30 f3
                    dec       ix                            ;[0eeb] dd 2b
                    jr        $0ede                         ;[0eed] 18 ef
                    ld        a,$24                         ;[0eef] 3e 24
                    and       a                             ;[0ef1] a7
                    jr        $0f61                         ;[0ef2] 18 6d
                    dec       e                             ;[0ef4] 1d
                    ld        a,(ix+$00)                    ;[0ef5] dd 7e 00
                    or        c                             ;[0ef8] b1
                    ld        (ix+$00),a                    ;[0ef9] dd 77 00
                    bit       7,h                           ;[0efc] cb 7c
                    jr        z,$0f07                       ;[0efe] 28 07
                    ld        a,(ix+$28)                    ;[0f00] dd 7e 28
                    or        c                             ;[0f03] b1
                    ld        (ix+$28),a                    ;[0f04] dd 77 28
                    jr        $0f60                         ;[0f07] 18 57
                    ld        a,l                           ;[0f09] 7d
                    cp        $03                           ;[0f0a] fe 03
                    jr        z,$0f6c                       ;[0f0c] 28 5e
                    ld        a,e                           ;[0f0e] 7b
                    cp        b                             ;[0f0f] b8
                    jr        nc,$0f67                      ;[0f10] 30 55
                    rrca                                    ;[0f12] 0f
                    rrca                                    ;[0f13] 0f
                    rrca                                    ;[0f14] 0f
                    and       $1f                           ;[0f15] e6 1f
                    add       ixl                           ;[0f17] dd 85
                    ld        ixl,a                         ;[0f19] dd 6f
                    ld        c,$01                         ;[0f1b] 0e 01
                    ld        a,e                           ;[0f1d] 7b
                    and       $07                           ;[0f1e] e6 07
                    jr        z,$0f27                       ;[0f20] 28 05
                    rlc       c                             ;[0f22] cb 01
                    dec       a                             ;[0f24] 3d
                    jr        nz,$0f22                      ;[0f25] 20 fb
                    ld        a,(ix+$00)                    ;[0f27] dd 7e 00
                    dec       l                             ;[0f2a] 2d
                    jr        nz,$0f3f                      ;[0f2b] 20 12
                    and       c                             ;[0f2d] a1
                    jr        z,$0ef5                       ;[0f2e] 28 c5
                    bit       7,h                           ;[0f30] cb 7c
                    jr        z,$0eef                       ;[0f32] 28 bb
                    ld        a,(ix+$28)                    ;[0f34] dd 7e 28
                    and       c                             ;[0f37] a1
                    jr        nz,$0ef5                      ;[0f38] 20 bb
                    jr        $0eef                         ;[0f3a] 18 b3
                    ld        e,b                           ;[0f3c] 58
                    jr        $0f60                         ;[0f3d] 18 21
                    dec       l                             ;[0f3f] 2d
                    jr        nz,$0f67                      ;[0f40] 20 25
                    and       c                             ;[0f42] a1
                    jr        z,$0f60                       ;[0f43] 28 1b
                    ld        a,(ix+$28)                    ;[0f45] dd 7e 28
                    and       c                             ;[0f48] a1
                    jr        z,$0f4f                       ;[0f49] 28 04
                    bit       7,h                           ;[0f4b] cb 7c
                    jr        z,$0eef                       ;[0f4d] 28 a0
                    ld        a,c                           ;[0f4f] 79
                    cpl                                     ;[0f50] 2f
                    ld        c,a                           ;[0f51] 4f
                    ld        a,(ix+$00)                    ;[0f52] dd 7e 00
                    and       c                             ;[0f55] a1
                    ld        (ix+$00),a                    ;[0f56] dd 77 00
                    ld        a,(ix+$28)                    ;[0f59] dd 7e 28
                    and       c                             ;[0f5c] a1
                    ld        (ix+$28),a                    ;[0f5d] dd 77 28
                    scf                                     ;[0f60] 37
                    ld        bc,$253b                      ;[0f61] 01 3b 25
                    out       (c),d                         ;[0f64] ed 51
                    ret                                     ;[0f66] c9

                    ld        a,$15                         ;[0f67] 3e 15
                    and       a                             ;[0f69] a7
                    jr        $0f61                         ;[0f6a] 18 f5
                    ld        e,$00                         ;[0f6c] 1e 00
                    ld        c,$01                         ;[0f6e] 0e 01
                    ld        a,(ix+$00)                    ;[0f70] dd 7e 00
                    and       c                             ;[0f73] a1
                    jr        nz,$0f77                      ;[0f74] 20 01
                    inc       e                             ;[0f76] 1c
                    rlc       c                             ;[0f77] cb 01
                    dec       b                             ;[0f79] 05
                    jr        nc,$0f70                      ;[0f7a] 30 f4
                    inc       ix                            ;[0f7c] dd 23
                    jr        nz,$0f70                      ;[0f7e] 20 f0
                    jr        $0f60                         ;[0f80] 18 de
                    ld        de,($5c59)                    ;[0f82] ed 5b 59 5c
                    push      de                            ;[0f86] d5
                    ld        a,(hl)                        ;[0f87] 7e
                    inc       hl                            ;[0f88] 23
                    ld        (de),a                        ;[0f89] 12
                    inc       de                            ;[0f8a] 13
                    cp        $0d                           ;[0f8b] fe 0d
                    jr        nz,$0f87                      ;[0f8d] 20 f8
                    ex        de,hl                         ;[0f8f] eb
                    ld        (hl),$80                      ;[0f90] 36 80
                    inc       hl                            ;[0f92] 23
                    ld        ($5c61),hl                    ;[0f93] 22 61 5c
                    ld        ($5c63),hl                    ;[0f96] 22 63 5c
                    ld        ($5c65),hl                    ;[0f99] 22 65 5c
                    pop       hl                            ;[0f9c] e1
                    ex        de,hl                         ;[0f9d] eb
                    ld        hl,$5b68                      ;[0f9e] 21 68 5b
                    ld        a,(hl)                        ;[0fa1] 7e
                    and       $01                           ;[0fa2] e6 01
                    push      af                            ;[0fa4] f5
                    set       0,(hl)                        ;[0fa5] cb c6
                    ld        (iy+$00),$ff                  ;[0fa7] fd 36 00 ff
                    call      $27e3                         ;[0fab] cd e3 27
                    call      $3f00                         ;[0fae] cd 00 3f
                    or        h                             ;[0fb1] b4
                    ld        a,(bc)                        ;[0fb2] 0a
                    call      $280d                         ;[0fb3] cd 0d 28
                    ld        hl,$5b68                      ;[0fb6] 21 68 5b
                    res       0,(hl)                        ;[0fb9] cb 86
                    pop       af                            ;[0fbb] f1
                    or        (hl)                          ;[0fbc] b6
                    ld        (hl),a                        ;[0fbd] 77
                    ret                                     ;[0fbe] c9

                    ld        a,b                           ;[0fbf] 78
                    cp        $03                           ;[0fc0] fe 03
                    jr        nc,$0fc7                      ;[0fc2] 30 03
                    ld        a,c                           ;[0fc4] 79
                    cp        $1a                           ;[0fc5] fe 1a
                    ld        a,$15                         ;[0fc7] 3e 15
                    ret       nc                            ;[0fc9] d0
                    push      hl                            ;[0fca] e5
                    djnz      $0fe4                         ;[0fcb] 10 17
                    ld        hl,$3300                      ;[0fcd] 21 00 33
                    ld        b,c                           ;[0fd0] 41
                    ld        c,$00                         ;[0fd1] 0e 00
                    srl       b                             ;[0fd3] cb 38
                    rr        c                             ;[0fd5] cb 19
                    add       hl,bc                         ;[0fd7] 09
                    pop       bc                            ;[0fd8] c1
                    ld        a,c                           ;[0fd9] 79
                    cp        $40                           ;[0fda] fe 40
                    ld        a,$15                         ;[0fdc] 3e 15
                    ret       nc                            ;[0fde] d0
                    push      bc                            ;[0fdf] c5
                    ld        b,$00                         ;[0fe0] 06 00
                    jr        $0ff7                         ;[0fe2] 18 13
                    djnz      $0ff2                         ;[0fe4] 10 0c
                    ld        a,c                           ;[0fe6] 79
                    cp        $02                           ;[0fe7] fe 02
                    pop       hl                            ;[0fe9] e1
                    jr        nc,$0fc7                      ;[0fea] 30 db
                    push      hl                            ;[0fec] e5
                    ld        hl,$3148                      ;[0fed] 21 48 31
                    jr        $0ff5                         ;[0ff0] 18 03
                    ld        hl,$3264                      ;[0ff2] 21 64 32
                    ld        b,$00                         ;[0ff5] 06 00
                    add       hl,bc                         ;[0ff7] 09
                    add       hl,bc                         ;[0ff8] 09
                    pop       bc                            ;[0ff9] c1
                    djnz      $1002                         ;[0ffa] 10 06
                    rst       $08                           ;[0ffc] cf
                    rlca                                    ;[0ffd] 07
                    ld        c,h                           ;[0ffe] 4c
                    jr        z,$1038                       ;[0fff] 28 37
                    ret                                     ;[1001] c9

                    rst       $08                           ;[1002] cf
                    rlca                                    ;[1003] 07
                    ld        d,a                           ;[1004] 57
                    jr        z,$103e                       ;[1005] 28 37
                    ret                                     ;[1007] c9

                    rst       $08                           ;[1008] cf
                    nop                                     ;[1009] 00
                    jp        p,$3e00                       ;[100a] f2 00 3e
                    ld        a,($c93f)                     ;[100d] 3a 3f c9
                    rst       $08                           ;[1010] cf
                    nop                                     ;[1011] 00
                    push      af                            ;[1012] f5
                    nop                                     ;[1013] 00
                    ccf                                     ;[1014] 3f
                    ret                                     ;[1015] c9

                    and       a                             ;[1016] a7
                    jr        z,$1049                       ;[1017] 28 30
                    dec       a                             ;[1019] 3d
                    jr        nz,$102a                      ;[101a] 20 0e
                    ld        a,b                           ;[101c] 78
                    cp        $03                           ;[101d] fe 03
                    jr        nc,$102a                      ;[101f] 30 09
                    cp        $01                           ;[1021] fe 01
                    jr        nz,$102e                      ;[1023] 20 09
                    ld        a,c                           ;[1025] 79
                    cp        $04                           ;[1026] fe 04
                    jr        c,$1037                       ;[1028] 38 0d
                    ld        a,$15                         ;[102a] 3e 15
                    and       a                             ;[102c] a7
                    ret                                     ;[102d] c9

                    and       a                             ;[102e] a7
                    jr        z,$1035                       ;[102f] 28 04
                    ld        c,$01                         ;[1031] 0e 01
                    jr        $1042                         ;[1033] 18 0d
                    ld        c,$ff                         ;[1035] 0e ff
                    push      bc                            ;[1037] c5
                    xor       a                             ;[1038] af
                    ld        bc,$123b                      ;[1039] 01 3b 12
                    out       (c),a                         ;[103c] ed 79
                    ld        ($5b7b),a                     ;[103e] 32 7b 5b
                    pop       bc                            ;[1041] c1
                    ld        a,b                           ;[1042] 78
                    ld        d,c                           ;[1043] 51
                    call      $3f00                         ;[1044] cd 00 3f
                    add       hl,sp                         ;[1047] 39
                    inc       d                             ;[1048] 14
                    ld        bc,$243b                      ;[1049] 01 3b 24
                    ld        a,$57                         ;[104c] 3e 57
                    out       (c),a                         ;[104e] ed 79
                    inc       b                             ;[1050] 04
                    in        d,(c)                         ;[1051] ed 50
                    ld        a,$10                         ;[1053] 3e 10
                    out       (c),a                         ;[1055] ed 79
                    ld        a,($5c7f)                     ;[1057] 3a 7f 5c
                    and       $0f                           ;[105a] e6 0f
                    push      af                            ;[105c] f5
                    push      de                            ;[105d] d5
                    ld        hl,$1620                      ;[105e] 21 20 16
                    ld        de,($5c8d)                    ;[1061] ed 5b 8d 5c
                    ld        bc,$0800                      ;[1065] 01 00 08
                    ld        d,c                           ;[1068] 51
                    jr        z,$1092                       ;[1069] 28 27
                    add       $f2                           ;[106b] c6 f2
                    ld        ixh,a                         ;[106d] dd 67
                    ld        ixl,$00                       ;[106f] dd 2e 00
                    ld        e,(ix+$1f)                    ;[1072] dd 5e 1f
                    ld        d,(ix+$20)                    ;[1075] dd 56 20
                    ld        c,(ix+$25)                    ;[1078] dd 4e 25
                    ld        b,(ix+$0f)                    ;[107b] dd 46 0f
                    ld        l,(ix+$1c)                    ;[107e] dd 6e 1c
                    ld        h,(ix+$12)                    ;[1081] dd 66 12
                    bit       6,(ix+$19)                    ;[1084] dd cb 19 76
                    jr        z,$1092                       ;[1088] 28 08
                    ld        h,$20                         ;[108a] 26 20
                    cp        $f3                           ;[108c] fe f3
                    jr        nz,$1092                      ;[108e] 20 02
                    ld        h,$10                         ;[1090] 26 10
                    pop       af                            ;[1092] f1
                    nextreg $57,a                           ;[1093] ed 92 57
                    pop       af                            ;[1096] f1
                    scf                                     ;[1097] 37
                    ret                                     ;[1098] c9

                    ld        a,b                           ;[1099] 78
                    cp        $02                           ;[109a] fe 02
                    jr        nc,$102a                      ;[109c] 30 8c
                    push      af                            ;[109e] f5
                    inc       sp                            ;[109f] 33
                    ld        a,($5c3c)                     ;[10a0] 3a 3c 5c
                    push      af                            ;[10a3] f5
                    inc       sp                            ;[10a4] 33
                    res       7,(iy+$02)                    ;[10a5] fd cb 02 be
                    call      $27e3                         ;[10a9] cd e3 27
                    call      $3e18                         ;[10ac] cd 18 3e
                    ld        sp,$cd15                      ;[10af] 31 15 cd
                    dec       c                             ;[10b2] 0d
                    jr        z,$1098                       ;[10b3] 28 e3
                    ld        (iy+$02),l                    ;[10b5] fd 75 02
                    push      de                            ;[10b8] d5
                    push      af                            ;[10b9] f5
                    jr        nc,$10d7                      ;[10ba] 30 1b
                    dec       h                             ;[10bc] 25
                    jr        z,$10d7                       ;[10bd] 28 18
                    ld        hl,($5c59)                    ;[10bf] 2a 59 5c
                    ld        de,($5c5d)                    ;[10c2] ed 5b 5d 5c
                    scf                                     ;[10c6] 37
                    sbc       hl,de                         ;[10c7] ed 52
                    jr        nc,$10d7                      ;[10c9] 30 0c
                    ex        de,hl                         ;[10cb] eb
                    add       hl,bc                         ;[10cc] 09
                    ld        ($5c5d),hl                    ;[10cd] 22 5d 5c
                    ld        hl,($5c55)                    ;[10d0] 2a 55 5c
                    add       hl,bc                         ;[10d3] 09
                    ld        ($5c55),hl                    ;[10d4] 22 55 5c
                    pop       af                            ;[10d7] f1
                    pop       de                            ;[10d8] d1
                    pop       hl                            ;[10d9] e1
                    ld        (iy+$00),$ff                  ;[10da] fd 36 00 ff
                    ret                                     ;[10de] c9

                    push      hl                            ;[10df] e5
                    push      af                            ;[10e0] f5
                    ld        a,($d5b8)                     ;[10e1] 3a b8 d5
                    ld        hl,($d5e9)                    ;[10e4] 2a e9 d5
                    ld        h,a                           ;[10e7] 67
                    pop       af                            ;[10e8] f1
                    ex        (sp),hl                       ;[10e9] e3
                    call      $3e00                         ;[10ea] cd 00 3e
                    ld        d,d                           ;[10ed] 52
                    inc       l                             ;[10ee] 2c
                    ex        (sp),hl                       ;[10ef] e3
                    push      af                            ;[10f0] f5
                    ld        a,h                           ;[10f1] 7c
                    ld        ($d5b8),a                     ;[10f2] 32 b8 d5
                    ld        a,l                           ;[10f5] 7d
                    ld        ($d5e9),a                     ;[10f6] 32 e9 d5
                    pop       af                            ;[10f9] f1
                    pop       hl                            ;[10fa] e1
                    ret                                     ;[10fb] c9

                    add       $03                           ;[10fc] c6 03
                    cp        $13                           ;[10fe] fe 13
                    jr        nc,$1111                      ;[1100] 30 0f
                    rlca                                    ;[1102] 07
                    ld        hl,$5c10                      ;[1103] 21 10 5c
                    ld        c,a                           ;[1106] 4f
                    ld        b,$00                         ;[1107] 06 00
                    add       hl,bc                         ;[1109] 09
                    ld        c,(hl)                        ;[110a] 4e
                    inc       hl                            ;[110b] 23
                    ld        b,(hl)                        ;[110c] 46
                    dec       hl                            ;[110d] 2b
                    scf                                     ;[110e] 37
                    ret                                     ;[110f] c9

                    pop       bc                            ;[1110] c1
                    ld        a,$17                         ;[1111] 3e 17
                    and       a                             ;[1113] a7
                    ret                                     ;[1114] c9

                    inc       hl                            ;[1115] 23
                    inc       hl                            ;[1116] 23
                    ld        a,(hl)                        ;[1117] 7e
                    inc       hl                            ;[1118] 23
                    and       a                             ;[1119] a7
                    ret       z                             ;[111a] c8
                    cp        c                             ;[111b] b9
                    jr        nz,$1115                      ;[111c] 20 f7
                    scf                                     ;[111e] 37
                    ret                                     ;[111f] c9

                    ld        a,($5c7f)                     ;[1120] 3a 7f 5c
                    and       $0f                           ;[1123] e6 0f
                    cp        $01                           ;[1125] fe 01
                    jr        nz,$112a                      ;[1127] 20 01
                    ld        l,h                           ;[1129] 6c
                    ld        h,$00                         ;[112a] 26 00
                    push      hl                            ;[112c] e5
                    call      $113d                         ;[112d] cd 3d 11
                    jr        nc,$113b                      ;[1130] 30 09
                    ex        de,hl                         ;[1132] eb
                    ex        (sp),hl                       ;[1133] e3
                    and       a                             ;[1134] a7
                    sbc       hl,de                         ;[1135] ed 52
                    ccf                                     ;[1137] 3f
                    pop       hl                            ;[1138] e1
                    ex        de,hl                         ;[1139] eb
                    ret                                     ;[113a] c9

                    pop       hl                            ;[113b] e1
                    ret                                     ;[113c] c9

                    ld        a,b                           ;[113d] 78
                    or        c                             ;[113e] b1
                    ret       z                             ;[113f] c8
                    ld        a,(de)                        ;[1140] 1a
                    cp        $2c                           ;[1141] fe 2c
                    scf                                     ;[1143] 37
                    ccf                                     ;[1144] 3f
                    ret       nz                            ;[1145] c0
                    inc       de                            ;[1146] 13
                    dec       bc                            ;[1147] 0b
                    ld        hl,$0000                      ;[1148] 21 00 00
                    ld        a,b                           ;[114b] 78
                    or        c                             ;[114c] b1
                    scf                                     ;[114d] 37
                    ret       z                             ;[114e] c8
                    ld        a,(de)                        ;[114f] 1a
                    cp        $20                           ;[1150] fe 20
                    jr        z,$1167                       ;[1152] 28 13
                    sub       $30                           ;[1154] d6 30
                    ret       c                             ;[1156] d8
                    cp        $0a                           ;[1157] fe 0a
                    ccf                                     ;[1159] 3f
                    ret       c                             ;[115a] d8
                    push      de                            ;[115b] d5
                    add       hl,hl                         ;[115c] 29
                    ld        d,h                           ;[115d] 54
                    ld        e,l                           ;[115e] 5d
                    add       hl,hl                         ;[115f] 29
                    add       hl,hl                         ;[1160] 29
                    add       hl,de                         ;[1161] 19
                    ld        d,$00                         ;[1162] 16 00
                    ld        e,a                           ;[1164] 5f
                    add       hl,de                         ;[1165] 19
                    pop       de                            ;[1166] d1
                    inc       de                            ;[1167] 13
                    dec       bc                            ;[1168] 0b
                    jr        $114b                         ;[1169] 18 e0
                    push      bc                            ;[116b] c5
                    call      $10fc                         ;[116c] cd fc 10
                    jr        nc,$1110                      ;[116f] 30 9f
                    ld        a,b                           ;[1171] 78
                    or        c                             ;[1172] b1
                    jr        z,$118b                       ;[1173] 28 16
                    push      hl                            ;[1175] e5
                    ld        hl,($5c4f)                    ;[1176] 2a 4f 5c
                    add       hl,bc                         ;[1179] 09
                    inc       hl                            ;[117a] 23
                    inc       hl                            ;[117b] 23
                    inc       hl                            ;[117c] 23
                    ld        a,(hl)                        ;[117d] 7e
                    pop       hl                            ;[117e] e1
                    cp        $4b                           ;[117f] fe 4b
                    jr        z,$118b                       ;[1181] 28 08
                    cp        $53                           ;[1183] fe 53
                    jr        z,$118b                       ;[1185] 28 04
                    cp        $50                           ;[1187] fe 50
                    jr        nz,$1110                      ;[1189] 20 85
                    pop       bc                            ;[118b] c1
                    push      hl                            ;[118c] e5
                    ld        hl,$11cb                      ;[118d] 21 cb 11
                    ld        a,b                           ;[1190] 78
                    or        c                             ;[1191] b1
                    jr        z,$11c6                       ;[1192] 28 32
                    dec       bc                            ;[1194] 0b
                    ld        a,b                           ;[1195] 78
                    or        c                             ;[1196] b1
                    inc       bc                            ;[1197] 03
                    ld        a,(de)                        ;[1198] 1a
                    jr        z,$11ac                       ;[1199] 28 11
                    ld        hl,$11e3                      ;[119b] 21 e3 11
                    inc       de                            ;[119e] 13
                    ld        a,(de)                        ;[119f] 1a
                    dec       de                            ;[11a0] 1b
                    cp        $3e                           ;[11a1] fe 3e
                    ld        a,$49                         ;[11a3] 3e 49
                    jr        nz,$11ac                      ;[11a5] 20 05
                    ld        a,(de)                        ;[11a7] 1a
                    inc       de                            ;[11a8] 13
                    inc       de                            ;[11a9] 13
                    dec       bc                            ;[11aa] 0b
                    dec       bc                            ;[11ab] 0b
                    and       $df                           ;[11ac] e6 df
                    push      bc                            ;[11ae] c5
                    ld        c,a                           ;[11af] 4f
                    call      $1117                         ;[11b0] cd 17 11
                    pop       bc                            ;[11b3] c1
                    jr        nc,$11c6                      ;[11b4] 30 10
                    ld        a,(hl)                        ;[11b6] 7e
                    inc       hl                            ;[11b7] 23
                    ld        h,(hl)                        ;[11b8] 66
                    ld        l,a                           ;[11b9] 6f
                    call      $11c5                         ;[11ba] cd c5 11
                    pop       hl                            ;[11bd] e1
                    jr        nc,$11c7                      ;[11be] 30 07
                    ld        (hl),e                        ;[11c0] 73
                    inc       hl                            ;[11c1] 23
                    ld        (hl),d                        ;[11c2] 72
                    scf                                     ;[11c3] 37
                    ret                                     ;[11c4] c9

                    jp        (hl)                          ;[11c5] e9
                    pop       bc                            ;[11c6] c1
                    ld        a,$0e                         ;[11c7] 3e 0e
                    and       a                             ;[11c9] a7
                    ret                                     ;[11ca] c9

                    ld        c,e                           ;[11cb] 4b
                    push      de                            ;[11cc] d5
                    ld        de,$d953                      ;[11cd] 11 53 d9
                    ld        de,$dd50                      ;[11d0] 11 50 dd
                    ld        de,$1e00                      ;[11d3] 11 00 1e
                    ld        bc,$0618                      ;[11d6] 01 18 06
                    ld        e,$06                         ;[11d9] 1e 06
                    jr        $11df                         ;[11db] 18 02
                    ld        e,$10                         ;[11dd] 1e 10
                    ld        d,$00                         ;[11df] 16 00
                    scf                                     ;[11e1] 37
                    ret                                     ;[11e2] c9

                    ld        c,c                           ;[11e3] 49
                    rlca                                    ;[11e4] 07
                    ld        (de),a                        ;[11e5] 12
                    ld        c,a                           ;[11e6] 4f
                    nop                                     ;[11e7] 00
                    ld        (de),a                        ;[11e8] 12
                    ld        d,l                           ;[11e9] 55
                    ld        sp,hl                         ;[11ea] f9
                    ld        de,$724d                      ;[11eb] 11 4d 72
                    ld        (de),a                        ;[11ee] 12
                    ld        d,(hl)                        ;[11ef] 56
                    xor       l                             ;[11f0] ad
                    ld        (de),a                        ;[11f1] 12
                    ld        d,a                           ;[11f2] 57
                    ld        c,h                           ;[11f3] 4c
                    inc       de                            ;[11f4] 13
                    ld        b,h                           ;[11f5] 44
                    di                                      ;[11f6] f3
                    ld        (de),a                        ;[11f7] 12
                    nop                                     ;[11f8] 00
                    ld        hl,$0202                      ;[11f9] 21 02 02
                    ld        a,$03                         ;[11fc] 3e 03
                    jr        $120c                         ;[11fe] 18 0c
                    ld        hl,$0204                      ;[1200] 21 04 02
                    ld        a,$02                         ;[1203] 3e 02
                    jr        $120c                         ;[1205] 18 05
                    ld        hl,$0002                      ;[1207] 21 02 00
                    ld        a,$05                         ;[120a] 3e 05
                    push      af                            ;[120c] f5
                    push      hl                            ;[120d] e5
                    ld        hl,$da35                      ;[120e] 21 35 da
                    ld        a,b                           ;[1211] 78
                    and       a                             ;[1212] a7
                    jr        nz,$1218                      ;[1213] 20 03
                    ld        b,c                           ;[1215] 41
                    jr        $121a                         ;[1216] 18 02
                    ld        b,$ff                         ;[1218] 06 ff
                    ex        de,hl                         ;[121a] eb
                    ld        a,(hl)                        ;[121b] 7e
                    inc       hl                            ;[121c] 23
                    call      $15a0                         ;[121d] cd a0 15
                    ld        (de),a                        ;[1220] 12
                    inc       de                            ;[1221] 13
                    call      $1589                         ;[1222] cd 89 15
                    djnz      $121b                         ;[1225] 10 f4
                    call      $15a0                         ;[1227] cd a0 15
                    ld        a,$ff                         ;[122a] 3e ff
                    ld        (de),a                        ;[122c] 12
                    call      $1589                         ;[122d] cd 89 15
                    call      $15a0                         ;[1230] cd a0 15
                    call      $05f4                         ;[1233] cd f4 05
                    call      $1589                         ;[1236] cd 89 15
                    jr        c,$123e                       ;[1239] 38 03
                    pop       hl                            ;[123b] e1
                    pop       hl                            ;[123c] e1
                    ret                                     ;[123d] c9

                    ld        hl,$da35                      ;[123e] 21 35 da
                    pop       de                            ;[1241] d1
                    pop       af                            ;[1242] f1
                    ld        c,a                           ;[1243] 4f
                    push      bc                            ;[1244] c5
                    call      $15a0                         ;[1245] cd a0 15
                    call      $0106                         ;[1248] cd 06 01
                    call      $1589                         ;[124b] cd 89 15
                    pop       bc                            ;[124e] c1
                    ret       nc                            ;[124f] d0
                    push      bc                            ;[1250] c5
                    ld        hl,$1265                      ;[1251] 21 65 12
                    ld        bc,$000e                      ;[1254] 01 0e 00
                    ld        de,$000d                      ;[1257] 11 0d 00
                    call      $14a7                         ;[125a] cd a7 14
                    ld        bc,$000d                      ;[125d] 01 0d 00
                    add       hl,bc                         ;[1260] 09
                    pop       bc                            ;[1261] c1
                    ld        (hl),b                        ;[1262] 70
                    scf                                     ;[1263] 37
                    ret                                     ;[1264] c9

                    ld        c,l                           ;[1265] 4d
                    ld        e,e                           ;[1266] 5b
                    ld        c,l                           ;[1267] 4d
                    ld        e,e                           ;[1268] 5b
                    ld        b,(hl)                        ;[1269] 46
                    jr        nc,$1271                      ;[126a] 30 05
                    daa                                     ;[126c] 27
                    dec       b                             ;[126d] 05
                    ex        af,af'                        ;[126e] 08
                    dec       b                             ;[126f] 05
                    ld        c,$00                         ;[1270] 0e 00
                    call      $1148                         ;[1272] cd 48 11
                    push      hl                            ;[1275] e5
                    call      $113d                         ;[1276] cd 3d 11
                    pop       de                            ;[1279] d1
                    ret       nc                            ;[127a] d0
                    ld        a,b                           ;[127b] 78
                    or        c                             ;[127c] b1
                    ret       nz                            ;[127d] c0
                    ld        a,h                           ;[127e] 7c
                    or        l                             ;[127f] b5
                    ret       z                             ;[1280] c8
                    push      de                            ;[1281] d5
                    push      hl                            ;[1282] e5
                    ld        hl,$12a0                      ;[1283] 21 a0 12
                    ld        bc,$0013                      ;[1286] 01 13 00
                    ld        de,$000d                      ;[1289] 11 0d 00
                    call      $14a7                         ;[128c] cd a7 14
                    ld        bc,$000d                      ;[128f] 01 0d 00
                    add       hl,bc                         ;[1292] 09
                    pop       bc                            ;[1293] c1
                    ld        (hl),c                        ;[1294] 71
                    inc       hl                            ;[1295] 23
                    ld        (hl),b                        ;[1296] 70
                    inc       hl                            ;[1297] 23
                    inc       hl                            ;[1298] 23
                    inc       hl                            ;[1299] 23
                    pop       bc                            ;[129a] c1
                    ld        (hl),c                        ;[129b] 71
                    inc       hl                            ;[129c] 23
                    ld        (hl),b                        ;[129d] 70
                    scf                                     ;[129e] 37
                    ret                                     ;[129f] c9

                    ld        c,l                           ;[12a0] 4d
                    ld        e,e                           ;[12a1] 5b
                    ld        c,l                           ;[12a2] 4d
                    ld        e,e                           ;[12a3] 5b
                    ld        c,l                           ;[12a4] 4d
                    inc       sp                            ;[12a5] 33
                    nop                                     ;[12a6] 00
                    sbc       d                             ;[12a7] 9a
                    inc       b                             ;[12a8] 04
                    sbc       a                             ;[12a9] 9f
                    inc       b                             ;[12aa] 04
                    inc       de                            ;[12ab] 13
                    nop                                     ;[12ac] 00
                    ld        a,b                           ;[12ad] 78
                    or        c                             ;[12ae] b1
                    ret       z                             ;[12af] c8
                    ld        h,d                           ;[12b0] 62
                    ld        l,e                           ;[12b1] 6b
                    add       hl,bc                         ;[12b2] 09
                    dec       hl                            ;[12b3] 2b
                    ld        a,(hl)                        ;[12b4] 7e
                    cp        $24                           ;[12b5] fe 24
                    scf                                     ;[12b7] 37
                    ccf                                     ;[12b8] 3f
                    ret       nz                            ;[12b9] c0
                    add       bc,$0010                      ;[12ba] ed 36 10 00
                    ld        ($5c5f),de                    ;[12be] ed 53 5f 5c
                    push      bc                            ;[12c2] c5
                    ld        hl,$12e8                      ;[12c3] 21 e8 12
                    ld        de,$000b                      ;[12c6] 11 0b 00
                    call      $14a7                         ;[12c9] cd a7 14
                    pop       bc                            ;[12cc] c1
                    push      de                            ;[12cd] d5
                    add       hl,$000b                      ;[12ce] ed 34 0b 00
                    ld        (hl),c                        ;[12d2] 71
                    inc       hl                            ;[12d3] 23
                    ld        (hl),b                        ;[12d4] 70
                    inc       hl                            ;[12d5] 23
                    inc       hl                            ;[12d6] 23
                    inc       hl                            ;[12d7] 23
                    ex        de,hl                         ;[12d8] eb
                    add       bc,$fff0                      ;[12d9] ed 36 f0 ff
                    ld        hl,($5c5f)                    ;[12dd] 2a 5f 5c
                    ldir                                    ;[12e0] ed b0
                    ld        a,$28                         ;[12e2] 3e 28
                    ld        (de),a                        ;[12e4] 12
                    pop       de                            ;[12e5] d1
                    scf                                     ;[12e6] 37
                    ret                                     ;[12e7] c9

                    ld        c,l                           ;[12e8] 4d
                    ld        e,e                           ;[12e9] 5b
                    ld        c,l                           ;[12ea] 4d
                    ld        e,e                           ;[12eb] 5b
                    ld        d,(hl)                        ;[12ec] 56
                    rst       $30                           ;[12ed] f7
                    ld        a,$e0                         ;[12ee] 3e e0
                    ld        a,$03                         ;[12f0] 3e 03
                    ccf                                     ;[12f2] 3f
                    ld        a,b                           ;[12f3] 78
                    or        c                             ;[12f4] b1
                    ret       z                             ;[12f5] c8
                    ld        a,(de)                        ;[12f6] 1a
                    inc       de                            ;[12f7] 13
                    dec       bc                            ;[12f8] 0b
                    push      af                            ;[12f9] f5
                    ld        hl,$0000                      ;[12fa] 21 00 00
                    ld        a,b                           ;[12fd] 78
                    or        c                             ;[12fe] b1
                    jr        z,$131d                       ;[12ff] 28 1c
                    ld        a,(de)                        ;[1301] 1a
                    inc       de                            ;[1302] 13
                    dec       bc                            ;[1303] 0b
                    cp        $3e                           ;[1304] fe 3e
                    jr        z,$131e                       ;[1306] 28 16
                    cp        $2c                           ;[1308] fe 2c
                    jr        z,$130e                       ;[130a] 28 02
                    pop       af                            ;[130c] f1
                    ret                                     ;[130d] c9

                    call      $1148                         ;[130e] cd 48 11
                    push      hl                            ;[1311] e5
                    ld        hl,$0000                      ;[1312] 21 00 00
                    call      $113d                         ;[1315] cd 3d 11
                    ld        a,b                           ;[1318] 78
                    or        c                             ;[1319] b1
                    pop       bc                            ;[131a] c1
                    jr        nz,$130c                      ;[131b] 20 ef
                    ex        de,hl                         ;[131d] eb
                    ex        de,hl                         ;[131e] eb
                    ld        d,b                           ;[131f] 50
                    ld        e,c                           ;[1320] 59
                    pop       af                            ;[1321] f1
                    ld        c,a                           ;[1322] 4f
                    ld        b,$f9                         ;[1323] 06 f9
                    push      bc                            ;[1325] c5
                    call      $01cf                         ;[1326] cd cf 01
                    pop       de                            ;[1329] d1
                    ret       nc                            ;[132a] d0
                    ld        d,a                           ;[132b] 57
                    push      de                            ;[132c] d5
                    ld        hl,$1343                      ;[132d] 21 43 13
                    ld        bc,$0007                      ;[1330] 01 07 00
                    ld        de,$0005                      ;[1333] 11 05 00
                    call      $14a7                         ;[1336] cd a7 14
                    ld        bc,$0005                      ;[1339] 01 05 00
                    add       hl,bc                         ;[133c] 09
                    pop       bc                            ;[133d] c1
                    ld        (hl),c                        ;[133e] 71
                    inc       hl                            ;[133f] 23
                    ld        (hl),b                        ;[1340] 70
                    scf                                     ;[1341] 37
                    ret                                     ;[1342] c9

                    ld        c,l                           ;[1343] 4d
                    ld        e,e                           ;[1344] 5b
                    ld        c,l                           ;[1345] 4d
                    ld        e,e                           ;[1346] 5b
                    ld        b,h                           ;[1347] 44
                    pop       hl                            ;[1348] e1
                    pop       hl                            ;[1349] e1
                    pop       hl                            ;[134a] e1
                    ret                                     ;[134b] c9

                    call      $1148                         ;[134c] cd 48 11
                    ld        a,h                           ;[134f] 7c
                    and       a                             ;[1350] a7
                    ret       nz                            ;[1351] c0
                    ld        h,$18                         ;[1352] 26 18
                    ld        a,($5c7f)                     ;[1354] 3a 7f 5c
                    and       $0f                           ;[1357] e6 0f
                    cp        $01                           ;[1359] fe 01
                    jr        nz,$135f                      ;[135b] 20 02
                    ld        h,$0c                         ;[135d] 26 0c
                    ld        a,l                           ;[135f] 7d
                    cp        h                             ;[1360] bc
                    ret       nc                            ;[1361] d0
                    push      hl                            ;[1362] e5
                    ld        hl,$0f1f                      ;[1363] 21 1f 0f
                    call      $1120                         ;[1366] cd 20 11
                    jr        nc,$134a                      ;[1369] 30 df
                    ld        a,l                           ;[136b] 7d
                    pop       hl                            ;[136c] e1
                    ld        h,a                           ;[136d] 67
                    push      hl                            ;[136e] e5
                    ld        a,$0c                         ;[136f] 3e 0c
                    sub       l                             ;[1371] 95
                    ld        h,a                           ;[1372] 67
                    ld        a,$18                         ;[1373] 3e 18
                    sub       l                             ;[1375] 95
                    ld        l,a                           ;[1376] 6f
                    call      $1120                         ;[1377] cd 20 11
                    jr        nc,$134a                      ;[137a] 30 ce
                    ex        (sp),hl                       ;[137c] e3
                    push      hl                            ;[137d] e5
                    ld        a,$20                         ;[137e] 3e 20
                    sub       h                             ;[1380] 94
                    ld        l,a                           ;[1381] 6f
                    ld        a,$10                         ;[1382] 3e 10
                    sub       h                             ;[1384] 94
                    ld        h,a                           ;[1385] 67
                    call      $1120                         ;[1386] cd 20 11
                    ld        a,l                           ;[1389] 7d
                    pop       hl                            ;[138a] e1
                    ex        (sp),hl                       ;[138b] e3
                    ld        h,a                           ;[138c] 67
                    push      hl                            ;[138d] e5
                    call      $113d                         ;[138e] cd 3d 11
                    ld        a,$08                         ;[1391] 3e 08
                    jr        nc,$13aa                      ;[1393] 30 15
                    ld        a,h                           ;[1395] 7c
                    and       a                             ;[1396] a7
                    jr        nz,$1349                      ;[1397] 20 b0
                    ld        a,l                           ;[1399] 7d
                    cp        $03                           ;[139a] fe 03
                    ccf                                     ;[139c] 3f
                    jr        nc,$1349                      ;[139d] 30 aa
                    cp        $09                           ;[139f] fe 09
                    jr        nc,$1349                      ;[13a1] 30 a6
                    push      af                            ;[13a3] f5
                    call      $113d                         ;[13a4] cd 3d 11
                    jr        c,$13bd                       ;[13a7] 38 14
                    pop       af                            ;[13a9] f1
                    push      af                            ;[13aa] f5
                    ld        hl,$148e                      ;[13ab] 21 8e 14
                    sub       $03                           ;[13ae] d6 03
                    jr        z,$13b7                       ;[13b0] 28 05
                    inc       hl                            ;[13b2] 23
                    inc       hl                            ;[13b3] 23
                    dec       a                             ;[13b4] 3d
                    jr        nz,$13b2                      ;[13b5] 20 fb
                    ld        a,(hl)                        ;[13b7] 7e
                    inc       hl                            ;[13b8] 23
                    ld        h,(hl)                        ;[13b9] 66
                    ld        l,a                           ;[13ba] 6f
                    jr        $13c3                         ;[13bb] 18 06
                    ld        a,h                           ;[13bd] 7c
                    and       $c0                           ;[13be] e6 c0
                    jr        z,$1348                       ;[13c0] 28 86
                    dec       h                             ;[13c2] 25
                    ld        a,b                           ;[13c3] 78
                    or        c                             ;[13c4] b1
                    jr        nz,$1348                      ;[13c5] 20 81
                    push      hl                            ;[13c7] e5
                    ld        hl,$149a                      ;[13c8] 21 9a 14
                    ld        bc,$0030                      ;[13cb] 01 30 00
                    ld        de,$000d                      ;[13ce] 11 0d 00
                    call      $14a7                         ;[13d1] cd a7 14
                    ld        bc,$000d                      ;[13d4] 01 0d 00
                    add       hl,bc                         ;[13d7] 09
                    pop       bc                            ;[13d8] c1
                    ld        (hl),c                        ;[13d9] 71
                    inc       hl                            ;[13da] 23
                    ld        (hl),b                        ;[13db] 70
                    inc       hl                            ;[13dc] 23
                    pop       af                            ;[13dd] f1
                    ld        (hl),a                        ;[13de] 77
                    inc       hl                            ;[13df] 23
                    ld        b,a                           ;[13e0] 47
                    xor       a                             ;[13e1] af
                    scf                                     ;[13e2] 37
                    rra                                     ;[13e3] 1f
                    djnz      $13e2                         ;[13e4] 10 fc
                    ld        (hl),a                        ;[13e6] 77
                    inc       hl                            ;[13e7] 23
                    pop       bc                            ;[13e8] c1
                    ld        (hl),b                        ;[13e9] 70
                    inc       hl                            ;[13ea] 23
                    ld        (hl),c                        ;[13eb] 71
                    inc       hl                            ;[13ec] 23
                    ex        de,hl                         ;[13ed] eb
                    ex        (sp),hl                       ;[13ee] e3
                    ex        de,hl                         ;[13ef] eb
                    push      de                            ;[13f0] d5
                    ld        (hl),d                        ;[13f1] 72
                    inc       hl                            ;[13f2] 23
                    ld        (hl),e                        ;[13f3] 73
                    inc       hl                            ;[13f4] 23
                    ld        a,e                           ;[13f5] 7b
                    ex        de,hl                         ;[13f6] eb
                    add       hl,bc                         ;[13f7] 09
                    ex        de,hl                         ;[13f8] eb
                    dec       d                             ;[13f9] 15
                    dec       e                             ;[13fa] 1d
                    ld        (hl),d                        ;[13fb] 72
                    inc       hl                            ;[13fc] 23
                    ld        (hl),e                        ;[13fd] 73
                    inc       hl                            ;[13fe] 23
                    add       a                             ;[13ff] 87
                    add       a                             ;[1400] 87
                    add       a                             ;[1401] 87
                    push      af                            ;[1402] f5
                    ld        (hl),a                        ;[1403] 77
                    inc       hl                            ;[1404] 23
                    ld        a,e                           ;[1405] 7b
                    inc       a                             ;[1406] 3c
                    add       a                             ;[1407] 87
                    add       a                             ;[1408] 87
                    add       a                             ;[1409] 87
                    ld        (hl),a                        ;[140a] 77
                    inc       hl                            ;[140b] 23
                    xor       a                             ;[140c] af
                    ld        (hl),a                        ;[140d] 77
                    inc       hl                            ;[140e] 23
                    ld        (hl),a                        ;[140f] 77
                    inc       hl                            ;[1410] 23
                    ld        (hl),a                        ;[1411] 77
                    inc       hl                            ;[1412] 23
                    ld        de,$fff3                      ;[1413] 11 f3 ff
                    ex        de,hl                         ;[1416] eb
                    add       hl,de                         ;[1417] 19
                    ld        c,(hl)                        ;[1418] 4e
                    ld        l,b                           ;[1419] 68
                    ld        h,$00                         ;[141a] 26 00
                    ld        b,h                           ;[141c] 44
                    add       hl,hl                         ;[141d] 29
                    add       hl,hl                         ;[141e] 29
                    add       hl,hl                         ;[141f] 29
                    ld        a,($5c7f)                     ;[1420] 3a 7f 5c
                    and       $0f                           ;[1423] e6 0f
                    cp        $09                           ;[1425] fe 09
                    push      af                            ;[1427] f5
                    ld        a,$00                         ;[1428] 3e 00
                    jr        nz,$142d                      ;[142a] 20 01
                    add       hl,hl                         ;[142c] 29
                    and       a                             ;[142d] a7
                    sbc       hl,bc                         ;[142e] ed 42
                    inc       a                             ;[1430] 3c
                    jr        nc,$142d                      ;[1431] 30 fa
                    dec       a                             ;[1433] 3d
                    ex        de,hl                         ;[1434] eb
                    ld        (hl),a                        ;[1435] 77
                    inc       hl                            ;[1436] 23
                    pop       af                            ;[1437] f1
                    ld        (hl),$08                      ;[1438] 36 08
                    jr        nz,$143e                      ;[143a] 20 02
                    ld        (hl),$10                      ;[143c] 36 10
                    inc       hl                            ;[143e] 23
                    ld        (hl),a                        ;[143f] 77
                    inc       hl                            ;[1440] 23
                    ld        c,$00                         ;[1441] 0e 00
                    and       a                             ;[1443] a7
                    jr        z,$1473                       ;[1444] 28 2d
                    cp        $05                           ;[1446] fe 05
                    jr        nc,$1478                      ;[1448] 30 2e
                    push      af                            ;[144a] f5
                    add       $f2                           ;[144b] c6 f2
                    ld        d,a                           ;[144d] 57
                    ld        e,$20                         ;[144e] 1e 20
                    ld        a,i                           ;[1450] ed 57
                    push      af                            ;[1452] f5
                    di                                      ;[1453] f3
                    push      hl                            ;[1454] e5
                    ld        bc,$243b                      ;[1455] 01 3b 24
                    ld        a,$57                         ;[1458] 3e 57
                    out       (c),a                         ;[145a] ed 79
                    inc       b                             ;[145c] 04
                    in        l,(c)                         ;[145d] ed 68
                    ld        a,$10                         ;[145f] 3e 10
                    out       (c),a                         ;[1461] ed 79
                    ld        a,(de)                        ;[1463] 1a
                    ld        h,a                           ;[1464] 67
                    out       (c),l                         ;[1465] ed 69
                    ld        c,h                           ;[1467] 4c
                    pop       hl                            ;[1468] e1
                    pop       af                            ;[1469] f1
                    jp        po,$146e                      ;[146a] e2 6e 14
                    ei                                      ;[146d] fb
                    pop       af                            ;[146e] f1
                    add       $5e                           ;[146f] c6 5e
                    jr        $147e                         ;[1471] 18 0b
                    ld        a,($5c8d)                     ;[1473] 3a 8d 5c
                    jr        $1482                         ;[1476] 18 0a
                    srl       a                             ;[1478] cb 3f
                    srl       a                             ;[147a] cb 3f
                    add       $60                           ;[147c] c6 60
                    ld        e,a                           ;[147e] 5f
                    ld        d,$5b                         ;[147f] 16 5b
                    ld        a,(de)                        ;[1481] 1a
                    ld        (hl),a                        ;[1482] 77
                    inc       hl                            ;[1483] 23
                    ld        (hl),c                        ;[1484] 71
                    inc       hl                            ;[1485] 23
                    pop       af                            ;[1486] f1
                    pop       de                            ;[1487] d1
                    ld        (hl),d                        ;[1488] 72
                    inc       hl                            ;[1489] 23
                    ld        (hl),a                        ;[148a] 77
                    pop       de                            ;[148b] d1
                    scf                                     ;[148c] 37
                    ret                                     ;[148d] c9

                    nop                                     ;[148e] 00
                    call      pe,$ec00                      ;[148f] ec 00 ec
                    nop                                     ;[1492] 00
                    rst       $28                           ;[1493] ef
                    nop                                     ;[1494] 00
                    rst       $28                           ;[1495] ef
                    nop                                     ;[1496] 00
                    rst       $30                           ;[1497] f7
                    nop                                     ;[1498] 00
                    ei                                      ;[1499] fb
                    ld        c,l                           ;[149a] 4d
                    ld        e,e                           ;[149b] 5b
                    ld        c,l                           ;[149c] 4d
                    ld        e,e                           ;[149d] 5b
                    ld        d,a                           ;[149e] 57
                    ld        a,a                           ;[149f] 7f
                    daa                                     ;[14a0] 27
                    ld        l,l                           ;[14a1] 6d
                    inc       c                             ;[14a2] 0c
                    cp        (hl)                          ;[14a3] be
                    dec       hl                            ;[14a4] 2b
                    jr        nc,$14a7                      ;[14a5] 30 00
                    push      hl                            ;[14a7] e5
                    push      de                            ;[14a8] d5
                    push      bc                            ;[14a9] c5
                    ld        hl,($5c53)                    ;[14aa] 2a 53 5c
                    dec       hl                            ;[14ad] 2b
                    push      hl                            ;[14ae] e5
                    exx                                     ;[14af] d9
                    call      $3e00                         ;[14b0] cd 00 3e
                    halt                                    ;[14b3] 76
                    dec       b                             ;[14b4] 05
                    pop       de                            ;[14b5] d1
                    pop       hl                            ;[14b6] e1
                    pop       bc                            ;[14b7] c1
                    and       a                             ;[14b8] a7
                    sbc       hl,bc                         ;[14b9] ed 42
                    ex        (sp),hl                       ;[14bb] e3
                    push      de                            ;[14bc] d5
                    ldir                                    ;[14bd] ed b0
                    pop       hl                            ;[14bf] e1
                    pop       bc                            ;[14c0] c1
                    ld        a,b                           ;[14c1] 78
                    or        c                             ;[14c2] b1
                    jr        z,$14cb                       ;[14c3] 28 06
                    xor       a                             ;[14c5] af
                    ld        (de),a                        ;[14c6] 12
                    inc       de                            ;[14c7] 13
                    dec       bc                            ;[14c8] 0b
                    jr        $14c1                         ;[14c9] 18 f6
                    push      hl                            ;[14cb] e5
                    ld        de,($5c4f)                    ;[14cc] ed 5b 4f 5c
                    and       a                             ;[14d0] a7
                    sbc       hl,de                         ;[14d1] ed 52
                    inc       hl                            ;[14d3] 23
                    ex        de,hl                         ;[14d4] eb
                    pop       hl                            ;[14d5] e1
                    ret                                     ;[14d6] c9

                    call      $10fc                         ;[14d7] cd fc 10
                    ret       nc                            ;[14da] d0
                    ld        a,b                           ;[14db] 78
                    or        c                             ;[14dc] b1
                    scf                                     ;[14dd] 37
                    ret       z                             ;[14de] c8
                    push      hl                            ;[14df] e5
                    ld        hl,($5c4f)                    ;[14e0] 2a 4f 5c
                    add       hl,bc                         ;[14e3] 09
                    inc       hl                            ;[14e4] 23
                    inc       hl                            ;[14e5] 23
                    inc       hl                            ;[14e6] 23
                    ld        c,(hl)                        ;[14e7] 4e
                    ex        de,hl                         ;[14e8] eb
                    ld        hl,$1514                      ;[14e9] 21 14 15
                    call      $1117                         ;[14ec] cd 17 11
                    jp        nc,$1111                      ;[14ef] d2 11 11
                    ld        a,(hl)                        ;[14f2] 7e
                    inc       hl                            ;[14f3] 23
                    ld        h,(hl)                        ;[14f4] 66
                    ld        l,a                           ;[14f5] 6f
                    call      $11c5                         ;[14f6] cd c5 11
                    pop       hl                            ;[14f9] e1
                    ld        a,$12                         ;[14fa] 3e 12
                    ret       nc                            ;[14fc] d0
                    ld        bc,$0000                      ;[14fd] 01 00 00
                    ld        de,$a3e2                      ;[1500] 11 e2 a3
                    ex        de,hl                         ;[1503] eb
                    add       hl,de                         ;[1504] 19
                    jr        c,$150e                       ;[1505] 38 07
                    ld        bc,$153b                      ;[1507] 01 3b 15
                    add       hl,bc                         ;[150a] 09
                    ld        c,(hl)                        ;[150b] 4e
                    inc       hl                            ;[150c] 23
                    ld        b,(hl)                        ;[150d] 46
                    ex        de,hl                         ;[150e] eb
                    ld        (hl),c                        ;[150f] 71
                    inc       hl                            ;[1510] 23
                    ld        (hl),b                        ;[1511] 70
                    scf                                     ;[1512] 37
                    ret                                     ;[1513] c9

                    ld        c,e                           ;[1514] 4b
                    ld        (de),a                        ;[1515] 12
                    dec       d                             ;[1516] 15
                    ld        d,e                           ;[1517] 53
                    ld        (de),a                        ;[1518] 12
                    dec       d                             ;[1519] 15
                    ld        d,b                           ;[151a] 50
                    ld        (de),a                        ;[151b] 12
                    dec       d                             ;[151c] 15
                    ld        b,(hl)                        ;[151d] 46
                    dec       sp                            ;[151e] 3b
                    dec       d                             ;[151f] 15
                    ld        c,l                           ;[1520] 4d
                    ld        c,h                           ;[1521] 4c
                    dec       d                             ;[1522] 15
                    ld        d,(hl)                        ;[1523] 56
                    ld        c,h                           ;[1524] 4c
                    dec       d                             ;[1525] 15
                    ld        d,a                           ;[1526] 57
                    ld        c,h                           ;[1527] 4c
                    dec       d                             ;[1528] 15
                    ld        b,h                           ;[1529] 44
                    ld        h,b                           ;[152a] 60
                    dec       d                             ;[152b] 15
                    nop                                     ;[152c] 00
                    ld        bc,$0600                      ;[152d] 01 00 06
                    nop                                     ;[1530] 00
                    dec       bc                            ;[1531] 0b
                    nop                                     ;[1532] 00
                    ld        bc,$0100                      ;[1533] 01 00 01
                    nop                                     ;[1536] 00
                    ld        b,$00                         ;[1537] 06 00
                    djnz      $153b                         ;[1539] 10 00
                    ld        hl,$0009                      ;[153b] 21 09 00
                    add       hl,de                         ;[153e] 19
                    ld        b,(hl)                        ;[153f] 46
                    push      de                            ;[1540] d5
                    call      $15a0                         ;[1541] cd a0 15
                    call      $0109                         ;[1544] cd 09 01
                    call      $1589                         ;[1547] cd 89 15
                    pop       de                            ;[154a] d1
                    ret       nc                            ;[154b] d0
                    ld        hl,$0007                      ;[154c] 21 07 00
                    add       hl,de                         ;[154f] 19
                    ld        c,(hl)                        ;[1550] 4e
                    inc       hl                            ;[1551] 23
                    ld        b,(hl)                        ;[1552] 46
                    dec       de                            ;[1553] 1b
                    dec       de                            ;[1554] 1b
                    dec       de                            ;[1555] 1b
                    dec       de                            ;[1556] 1b
                    ex        de,hl                         ;[1557] eb
                    exx                                     ;[1558] d9
                    call      $3e00                         ;[1559] cd 00 3e
                    ld        a,$05                         ;[155c] 3e 05
                    scf                                     ;[155e] 37
                    ret                                     ;[155f] c9

                    ex        de,hl                         ;[1560] eb
                    push      hl                            ;[1561] e5
                    inc       hl                            ;[1562] 23
                    ld        c,(hl)                        ;[1563] 4e
                    inc       hl                            ;[1564] 23
                    ld        d,(hl)                        ;[1565] 56
                    ld        b,$fa                         ;[1566] 06 fa
                    call      $01cf                         ;[1568] cd cf 01
                    pop       de                            ;[156b] d1
                    ld        a,$24                         ;[156c] 3e 24
                    ret       nc                            ;[156e] d0
                    ld        bc,$0007                      ;[156f] 01 07 00
                    jr        $1553                         ;[1572] 18 df
                    exx                                     ;[1574] d9
                    ld        de,$0000                      ;[1575] 11 00 00
                    call      $3e00                         ;[1578] cd 00 3e
                    push      hl                            ;[157b] e5
                    inc       bc                            ;[157c] 03
                    ret                                     ;[157d] c9

                    ld        de,$0002                      ;[157e] 11 02 00
                    jr        $1578                         ;[1581] 18 f5
                    exx                                     ;[1583] d9
                    ld        de,$0004                      ;[1584] 11 04 00
                    jr        $1578                         ;[1587] 18 ef
                    nextreg $8e,$0a                         ;[1589] ed 91 8e 0a
                    ex        af,af'                        ;[158d] 08
                    pop       af                            ;[158e] f1
                    ld        ($5b52),hl                    ;[158f] 22 52 5b
                    ld        hl,($5b6a)                    ;[1592] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[1595] ed 73 6a 5b
                    ld        sp,hl                         ;[1599] f9
                    ld        hl,($5b52)                    ;[159a] 2a 52 5b
                    push      af                            ;[159d] f5
                    ex        af,af'                        ;[159e] 08
                    ret                                     ;[159f] c9

                    ex        af,af'                        ;[15a0] 08
                    pop       af                            ;[15a1] f1
                    ld        ($5b52),hl                    ;[15a2] 22 52 5b
                    ld        hl,($5b6a)                    ;[15a5] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[15a8] ed 73 6a 5b
                    ld        sp,hl                         ;[15ac] f9
                    ld        hl,($5b52)                    ;[15ad] 2a 52 5b
                    push      af                            ;[15b0] f5
                    ex        af,af'                        ;[15b1] 08
                    nextreg $8e,$7a                         ;[15b2] ed 91 8e 7a
                    ret                                     ;[15b6] c9

                    ld        ix,($5c51)                    ;[15b7] dd 2a 51 5c
                    call      $3e00                         ;[15bb] cd 00 3e
                    ld        sp,hl                         ;[15be] f9
                    daa                                     ;[15bf] 27
                    ret                                     ;[15c0] c9

                    ld        ix,($5c51)                    ;[15c1] dd 2a 51 5c
                    call      $3e00                         ;[15c5] cd 00 3e
                    ld        d,c                           ;[15c8] 51
                    daa                                     ;[15c9] 27
                    ret                                     ;[15ca] c9

                    ld        a,(ix+$00)                    ;[15cb] dd 7e 00
                    cp        $02                           ;[15ce] fe 02
                    scf                                     ;[15d0] 37
                    ret       z                             ;[15d1] c8
                    ld        a,$3d                         ;[15d2] 3e 3d
                    and       a                             ;[15d4] a7
                    ret                                     ;[15d5] c9

                    call      $15cb                         ;[15d6] cd cb 15
                    ret       nc                            ;[15d9] d0
                    ld        c,(ix+$0c)                    ;[15da] dd 4e 0c
                    ld        b,(ix+$0d)                    ;[15dd] dd 46 0d
                    scf                                     ;[15e0] 37
                    ret                                     ;[15e1] c9

                    call      $15cb                         ;[15e2] cd cb 15
                    ret       nc                            ;[15e5] d0
                    push      hl                            ;[15e6] e5
                    ld        l,(ix+$0e)                    ;[15e7] dd 6e 0e
                    ld        h,(ix+$0f)                    ;[15ea] dd 66 0f
                    and       a                             ;[15ed] a7
                    sbc       hl,bc                         ;[15ee] ed 42
                    pop       hl                            ;[15f0] e1
                    ccf                                     ;[15f1] 3f
                    ld        a,$15                         ;[15f2] 3e 15
                    ret       nc                            ;[15f4] d0
                    ld        (ix+$0c),c                    ;[15f5] dd 71 0c
                    ld        (ix+$0d),b                    ;[15f8] dd 70 0d
                    scf                                     ;[15fb] 37
                    ret                                     ;[15fc] c9

                    call      $15cb                         ;[15fd] cd cb 15
                    ret       nc                            ;[1600] d0
                    ld        a,b                           ;[1601] 78
                    push      af                            ;[1602] f5
                    push      hl                            ;[1603] e5
                    ld        e,(ix+$0c)                    ;[1604] dd 5e 0c
                    ld        d,(ix+$0d)                    ;[1607] dd 56 0d
                    ld        l,(ix+$0e)                    ;[160a] dd 6e 0e
                    ld        h,(ix+$0f)                    ;[160d] dd 66 0f
                    and       a                             ;[1610] a7
                    sbc       hl,de                         ;[1611] ed 52
                    jr        nc,$1618                      ;[1613] 30 03
                    ld        de,$0000                      ;[1615] 11 00 00
                    push      de                            ;[1618] d5
                    ld        b,(ix+$0b)                    ;[1619] dd 46 0b
                    call      $07ed                         ;[161c] cd ed 07
                    ld        c,a                           ;[161f] 4f
                    pop       hl                            ;[1620] e1
                    inc       hl                            ;[1621] 23
                    ld        (ix+$0c),l                    ;[1622] dd 75 0c
                    ld        (ix+$0d),h                    ;[1625] dd 74 0d
                    pop       hl                            ;[1628] e1
                    pop       af                            ;[1629] f1
                    ld        b,a                           ;[162a] 47
                    scf                                     ;[162b] 37
                    ret                                     ;[162c] c9

                    call      $15fd                         ;[162d] cd fd 15
                    ret       nc                            ;[1630] d0
                    push      bc                            ;[1631] c5
                    push      hl                            ;[1632] e5
                    call      $1897                         ;[1633] cd 97 18
                    jr        nc,$16a1                      ;[1636] 30 69
                    ld        a,$52                         ;[1638] 3e 52
                    call      $18dc                         ;[163a] cd dc 18
                    jr        nc,$16a1                      ;[163d] 30 62
                    call      $1933                         ;[163f] cd 33 19
                    pop       hl                            ;[1642] e1
                    jr        nc,$169c                      ;[1643] 30 57
                    pop       bc                            ;[1645] c1
                    ld        d,(ix+$0b)                    ;[1646] dd 56 0b
                    ld        a,b                           ;[1649] 78
                    call      $3e4c                         ;[164a] cd 4c 3e
                    push      bc                            ;[164d] c5
                    ld        c,$eb                         ;[164e] 0e eb
                    ld        e,$20                         ;[1650] 1e 20
                    ini                                     ;[1652] ed a2
                    ini                                     ;[1654] ed a2
                    ini                                     ;[1656] ed a2
                    ini                                     ;[1658] ed a2
                    ini                                     ;[165a] ed a2
                    ini                                     ;[165c] ed a2
                    ini                                     ;[165e] ed a2
                    ini                                     ;[1660] ed a2
                    ini                                     ;[1662] ed a2
                    ini                                     ;[1664] ed a2
                    ini                                     ;[1666] ed a2
                    ini                                     ;[1668] ed a2
                    ini                                     ;[166a] ed a2
                    ini                                     ;[166c] ed a2
                    ini                                     ;[166e] ed a2
                    ini                                     ;[1670] ed a2
                    dec       e                             ;[1672] 1d
                    jp        nz,$1652                      ;[1673] c2 52 16
                    in        a,(c)                         ;[1676] ed 78
                    nop                                     ;[1678] 00
                    in        a,(c)                         ;[1679] ed 78
                    nop                                     ;[167b] 00
                    in        a,(c)                         ;[167c] ed 78
                    cp        $ff                           ;[167e] fe ff
                    jr        z,$167c                       ;[1680] 28 fa
                    cp        $fe                           ;[1682] fe fe
                    jr        nz,$169c                      ;[1684] 20 16
                    dec       d                             ;[1686] 15
                    jr        nz,$1650                      ;[1687] 20 c7
                    scf                                     ;[1689] 37
                    push      af                            ;[168a] f5
                    ld        a,$07                         ;[168b] 3e 07
                    call      $3e4c                         ;[168d] cd 4c 3e
                    bit       0,(ix+$10)                    ;[1690] dd cb 10 46
                    push      hl                            ;[1694] e5
                    call      $196d                         ;[1695] cd 6d 19
                    pop       hl                            ;[1698] e1
                    pop       af                            ;[1699] f1
                    pop       bc                            ;[169a] c1
                    ret                                     ;[169b] c9

                    ld        a,$00                         ;[169c] 3e 00
                    and       a                             ;[169e] a7
                    jr        $168a                         ;[169f] 18 e9
                    pop       hl                            ;[16a1] e1
                    jr        $168a                         ;[16a2] 18 e6
                    call      $15fd                         ;[16a4] cd fd 15
                    ret       nc                            ;[16a7] d0
                    ld        a,(ix+$0b)                    ;[16a8] dd 7e 0b
                    push      af                            ;[16ab] f5
                    push      de                            ;[16ac] d5
                    call      $1892                         ;[16ad] cd 92 18
                    pop       de                            ;[16b0] d1
                    jr        nc,$16bf                      ;[16b1] 30 0c
                    inc       de                            ;[16b3] 13
                    ld        a,d                           ;[16b4] 7a
                    or        e                             ;[16b5] b3
                    jr        nz,$16b9                      ;[16b6] 20 01
                    inc       c                             ;[16b8] 0c
                    pop       af                            ;[16b9] f1
                    dec       a                             ;[16ba] 3d
                    jr        nz,$16ab                      ;[16bb] 20 ee
                    scf                                     ;[16bd] 37
                    ret                                     ;[16be] c9

                    pop       de                            ;[16bf] d1
                    ret                                     ;[16c0] c9

                    ld        a,$3a                         ;[16c1] 3e 3a
                    and       a                             ;[16c3] a7
                    ret                                     ;[16c4] c9

                    bit       7,a                           ;[16c5] cb 7f
                    res       7,a                           ;[16c7] cb bf
                    push      af                            ;[16c9] f5
                    and       a                             ;[16ca] a7
                    jr        z,$16cf                       ;[16cb] 28 02
                    cp        $21                           ;[16cd] fe 21
                    ld        a,$15                         ;[16cf] 3e 15
                    jr        nc,$16bf                      ;[16d1] 30 ec
                    pop       af                            ;[16d3] f1
                    push      af                            ;[16d4] f5
                    jr        z,$172c                       ;[16d5] 28 55
                    ld        a,$02                         ;[16d7] 3e 02
                    call      $225b                         ;[16d9] cd 5b 22
                    jr        nc,$16bf                      ;[16dc] 30 e1
                    pop       af                            ;[16de] f1
                    push      af                            ;[16df] f5
                    call      $240b                         ;[16e0] cd 0b 24
                    ld        c,(ix+$07)                    ;[16e3] dd 4e 07
                    ld        b,(ix+$08)                    ;[16e6] dd 46 08
                    jr        nz,$1705                      ;[16e9] 20 1a
                    add       hl,$0020                      ;[16eb] ed 34 20 00
                    ld        a,(hl)                        ;[16ef] 7e
                    cp        $02                           ;[16f0] fe 02
                    scf                                     ;[16f2] 37
                    ccf                                     ;[16f3] 3f
                    ld        a,$09                         ;[16f4] 3e 09
                    jr        nz,$171c                      ;[16f6] 20 24
                    dec       bc                            ;[16f8] 0b
                    push      ix                            ;[16f9] dd e5
                    pop       de                            ;[16fb] d1
                    add       de,$0001                      ;[16fc] ed 35 01 00
                    push      bc                            ;[1700] c5
                    call      $239f                         ;[1701] cd 9f 23
                    pop       bc                            ;[1704] c1
                    pop       af                            ;[1705] f1
                    push      af                            ;[1706] f5
                    ld        e,a                           ;[1707] 5f
                    ld        d,$00                         ;[1708] 16 00
                    call      $17db                         ;[170a] cd db 17
                    pop       af                            ;[170d] f1
                    dec       a                             ;[170e] 3d
                    cp        l                             ;[170f] bd
                    jr        z,$1713                       ;[1710] 28 01
                    dec       bc                            ;[1712] 0b
                    inc       a                             ;[1713] 3c
                    call      $177c                         ;[1714] cd 7c 17
                    push      ix                            ;[1717] dd e5
                    call      $0605                         ;[1719] cd 05 06
                    push      af                            ;[171c] f5
                    call      nc,$04f4                      ;[171d] d4 f4 04
                    pop       af                            ;[1720] f1
                    pop       hl                            ;[1721] e1
                    ret       nc                            ;[1722] d0
                    push      ix                            ;[1723] dd e5
                    pop       de                            ;[1725] d1
                    ld        bc,$0013                      ;[1726] 01 13 00
                    ldir                                    ;[1729] ed b0
                    ret                                     ;[172b] c9

                    push      bc                            ;[172c] c5
                    ld        hl,$203a                      ;[172d] 21 3a 20
                    ld        a,$09                         ;[1730] 3e 09
                    call      $1cb2                         ;[1732] cd b2 1c
                    jr        nc,$178c                      ;[1735] 30 55
                    dec       b                             ;[1737] 05
                    ld        a,$40                         ;[1738] 3e 40
                    ccf                                     ;[173a] 3f
                    jr        z,$178c                       ;[173b] 28 4f
                    ld        hl,$fb70                      ;[173d] 21 70 fb
                    push      bc                            ;[1740] c5
                    push      hl                            ;[1741] e5
                    ld        a,(hl)                        ;[1742] 7e
                    ld        ($d83b),a                     ;[1743] 32 3b d8
                    ld        a,$02                         ;[1746] 3e 02
                    ld        bc,$d82b                      ;[1748] 01 2b d8
                    call      $225b                         ;[174b] cd 5b 22
                    jr        nc,$1767                      ;[174e] 30 17
                    ld        a,$01                         ;[1750] 3e 01
                    call      $16df                         ;[1752] cd df 16
                    jr        nc,$1767                      ;[1755] 30 10
                    pop       hl                            ;[1757] e1
                    pop       de                            ;[1758] d1
                    pop       bc                            ;[1759] c1
                    pop       af                            ;[175a] f1
                    push      af                            ;[175b] f5
                    push      bc                            ;[175c] c5
                    push      de                            ;[175d] d5
                    push      hl                            ;[175e] e5
                    call      $17b4                         ;[175f] cd b4 17
                    jr        c,$1774                       ;[1762] 38 10
                    call      $178f                         ;[1764] cd 8f 17
                    pop       hl                            ;[1767] e1
                    ld        bc,$000d                      ;[1768] 01 0d 00
                    add       hl,bc                         ;[176b] 09
                    pop       bc                            ;[176c] c1
                    djnz      $1740                         ;[176d] 10 d1
                    ld        a,$40                         ;[176f] 3e 40
                    and       a                             ;[1771] a7
                    jr        $178c                         ;[1772] 18 18
                    pop       hl                            ;[1774] e1
                    pop       bc                            ;[1775] c1
                    pop       bc                            ;[1776] c1
                    pop       af                            ;[1777] f1
                    scf                                     ;[1778] 37
                    ret                                     ;[1779] c9

                    pop       bc                            ;[177a] c1
                    pop       af                            ;[177b] f1
                    ld        (ix+$0e),c                    ;[177c] dd 71 0e
                    ld        (ix+$0f),b                    ;[177f] dd 70 0f
                    ld        (ix+$0b),a                    ;[1782] dd 77 0b
                    ld        bc,$0000                      ;[1785] 01 00 00
                    jp        $15f5                         ;[1788] c3 f5 15
                    pop       bc                            ;[178b] c1
                    pop       bc                            ;[178c] c1
                    pop       bc                            ;[178d] c1
                    ret                                     ;[178e] c9

                    call      $15cb                         ;[178f] cd cb 15
                    ret       nc                            ;[1792] d0
                    call      $04f4                         ;[1793] cd f4 04
                    ld        (ix+$00),$00                  ;[1796] dd 36 00 00
                    scf                                     ;[179a] 37
                    ret                                     ;[179b] c9

                    ld        hl,$0000                      ;[179c] 21 00 00
                    ld        d,a                           ;[179f] 57
                    dec       a                             ;[17a0] 3d
                    ld        e,a                           ;[17a1] 5f
                    cp        $20                           ;[17a2] fe 20
                    ld        a,$15                         ;[17a4] 3e 15
                    jr        nc,$178b                      ;[17a6] 30 e3
                    ld        a,h                           ;[17a8] 7c
                    add       hl,bc                         ;[17a9] 09
                    adc       $00                           ;[17aa] ce 00
                    dec       d                             ;[17ac] 15
                    jr        nz,$17a9                      ;[17ad] 20 fa
                    add       hl,de                         ;[17af] 19
                    adc       d                             ;[17b0] 8a
                    ex        de,hl                         ;[17b1] eb
                    ld        l,a                           ;[17b2] 6f
                    ret                                     ;[17b3] c9

                    ld        d,a                           ;[17b4] 57
                    call      $15cb                         ;[17b5] cd cb 15
                    ret       nc                            ;[17b8] d0
                    ld        a,d                           ;[17b9] 7a
                    push      af                            ;[17ba] f5
                    push      bc                            ;[17bb] c5
                    call      $179c                         ;[17bc] cd 9c 17
                    ld        a,(ix+$09)                    ;[17bf] dd 7e 09
                    cp        l                             ;[17c2] bd
                    jr        c,$17d5                       ;[17c3] 38 10
                    jr        nz,$177a                      ;[17c5] 20 b3
                    ld        a,(ix+$08)                    ;[17c7] dd 7e 08
                    cp        d                             ;[17ca] ba
                    jr        c,$17d5                       ;[17cb] 38 08
                    jr        nz,$177a                      ;[17cd] 20 ab
                    ld        a,(ix+$07)                    ;[17cf] dd 7e 07
                    cp        e                             ;[17d2] bb
                    jr        nc,$177a                      ;[17d3] 30 a5
                    pop       bc                            ;[17d5] c1
                    pop       bc                            ;[17d6] c1
                    ld        a,$15                         ;[17d7] 3e 15
                    and       a                             ;[17d9] a7
                    ret                                     ;[17da] c9

                    ld        a,b                           ;[17db] 78
                    ld        hl,$0000                      ;[17dc] 21 00 00
                    ld        b,$10                         ;[17df] 06 10
                    and       a                             ;[17e1] a7
                    rl        c                             ;[17e2] cb 11
                    rla                                     ;[17e4] 17
                    adc       hl,hl                         ;[17e5] ed 6a
                    sbc       hl,de                         ;[17e7] ed 52
                    jr        nc,$17ec                      ;[17e9] 30 01
                    add       hl,de                         ;[17eb] 19
                    djnz      $17e2                         ;[17ec] 10 f4
                    rl        c                             ;[17ee] cb 11
                    rla                                     ;[17f0] 17
                    cpl                                     ;[17f1] 2f
                    ld        b,a                           ;[17f2] 47
                    ld        a,c                           ;[17f3] 79
                    cpl                                     ;[17f4] 2f
                    ld        c,a                           ;[17f5] 4f
                    ret                                     ;[17f6] c9

                    ld        a,c                           ;[17f7] 79
                    cp        $02                           ;[17f8] fe 02
                    ld        a,$41                         ;[17fa] 3e 41
                    ret       nc                            ;[17fc] d0
                    inc       c                             ;[17fd] 0c
                    dec       c                             ;[17fe] 0d
                    ld        hl,$f700                      ;[17ff] 21 00 f7
                    call      $1904                         ;[1802] cd 04 19
                    jr        nc,$1840                      ;[1805] 30 39
                    and       a                             ;[1807] a7
                    jr        z,$1843                       ;[1808] 28 39
                    ld        hl,($f706)                    ;[180a] 2a 06 f7
                    ld        a,l                           ;[180d] 7d
                    and       $03                           ;[180e] e6 03
                    ld        l,h                           ;[1810] 6c
                    ld        h,a                           ;[1811] 67
                    ld        a,($f708)                     ;[1812] 3a 08 f7
                    rlca                                    ;[1815] 07
                    adc       hl,hl                         ;[1816] ed 6a
                    rlca                                    ;[1818] 07
                    adc       hl,hl                         ;[1819] ed 6a
                    inc       hl                            ;[181b] 23
                    ld        bc,($f709)                    ;[181c] ed 4b 09 f7
                    ld        a,c                           ;[1820] 79
                    and       $03                           ;[1821] e6 03
                    rl        b                             ;[1823] cb 10
                    rla                                     ;[1825] 17
                    ld        c,a                           ;[1826] 4f
                    ld        a,($f705)                     ;[1827] 3a 05 f7
                    and       $0f                           ;[182a] e6 0f
                    add       c                             ;[182c] 81
                    sub       $07                           ;[182d] d6 07
                    ld        de,$0000                      ;[182f] 11 00 00
                    add       hl,hl                         ;[1832] 29
                    ex        de,hl                         ;[1833] eb
                    adc       hl,hl                         ;[1834] ed 6a
                    ex        de,hl                         ;[1836] eb
                    dec       a                             ;[1837] 3d
                    jr        nz,$1832                      ;[1838] 20 f8
                    ld        b,$00                         ;[183a] 06 00
                    ld        c,$80                         ;[183c] 0e 80
                    scf                                     ;[183e] 37
                    ret                                     ;[183f] c9

                    ld        a,$00                         ;[1840] 3e 00
                    ret                                     ;[1842] c9

                    ld        hl,($f708)                    ;[1843] 2a 08 f7
                    ld        a,h                           ;[1846] 7c
                    ld        h,l                           ;[1847] 65
                    ld        l,a                           ;[1848] 6f
                    ld        a,($f707)                     ;[1849] 3a 07 f7
                    and       $3f                           ;[184c] e6 3f
                    ld        de,$0001                      ;[184e] 11 01 00
                    add       hl,de                         ;[1851] 19
                    adc       d                             ;[1852] 8a
                    add       hl,hl                         ;[1853] 29
                    adc       a                             ;[1854] 8f
                    add       hl,hl                         ;[1855] 29
                    adc       a                             ;[1856] 8f
                    ld        e,h                           ;[1857] 5c
                    ld        h,l                           ;[1858] 65
                    ld        l,d                           ;[1859] 6a
                    ld        d,a                           ;[185a] 57
                    ld        b,$02                         ;[185b] 06 02
                    jr        nc,$183c                      ;[185d] 30 dd
                    dec       de                            ;[185f] 1b
                    dec       hl                            ;[1860] 2b
                    jr        $183c                         ;[1861] 18 d9
                    ld        a,b                           ;[1863] 78
                    res       7,a                           ;[1864] cb bf
                    push      bc                            ;[1866] c5
                    push      de                            ;[1867] d5
                    push      hl                            ;[1868] e5
                    call      $1897                         ;[1869] cd 97 18
                    ex        (sp),ix                       ;[186c] dd e3
                    jr        nc,$1886                      ;[186e] 30 16
                    push      af                            ;[1870] f5
                    and       $7f                           ;[1871] e6 7f
                    call      $3e4c                         ;[1873] cd 4c 3e
                    pop       af                            ;[1876] f1
                    push      af                            ;[1877] f5
                    bit       7,a                           ;[1878] cb 7f
                    jr        z,$1882                       ;[187a] 28 06
                    pop       af                            ;[187c] f1
                    call      $197a                         ;[187d] cd 7a 19
                    jr        $1886                         ;[1880] 18 04
                    pop       af                            ;[1882] f1
                    call      $1944                         ;[1883] cd 44 19
                    pop       ix                            ;[1886] dd e1
                    push      af                            ;[1888] f5
                    ld        a,$07                         ;[1889] 3e 07
                    call      $3e4c                         ;[188b] cd 4c 3e
                    pop       af                            ;[188e] f1
                    pop       de                            ;[188f] d1
                    pop       bc                            ;[1890] c1
                    ret                                     ;[1891] c9

                    ld        a,b                           ;[1892] 78
                    set       7,a                           ;[1893] cb ff
                    jr        $1866                         ;[1895] 18 cf
                    ld        b,$00                         ;[1897] 06 00
                    ld        l,(ix+$07)                    ;[1899] dd 6e 07
                    ld        h,(ix+$08)                    ;[189c] dd 66 08
                    and       a                             ;[189f] a7
                    sbc       hl,de                         ;[18a0] ed 52
                    ld        l,(ix+$09)                    ;[18a2] dd 6e 09
                    ld        h,(ix+$0a)                    ;[18a5] dd 66 0a
                    sbc       hl,bc                         ;[18a8] ed 42
                    jr        nc,$18b0                      ;[18aa] 30 04
                    ld        a,$02                         ;[18ac] 3e 02
                    and       a                             ;[18ae] a7
                    ret                                     ;[18af] c9

                    ld        l,(ix+$01)                    ;[18b0] dd 6e 01
                    ld        h,(ix+$02)                    ;[18b3] dd 66 02
                    add       hl,de                         ;[18b6] 19
                    ex        de,hl                         ;[18b7] eb
                    ld        l,(ix+$03)                    ;[18b8] dd 6e 03
                    ld        h,(ix+$04)                    ;[18bb] dd 66 04
                    adc       hl,bc                         ;[18be] ed 4a
                    bit       1,(ix+$10)                    ;[18c0] dd cb 10 4e
                    jr        nz,$18d0                      ;[18c4] 20 0a
                    ld        h,l                           ;[18c6] 65
                    ld        l,d                           ;[18c7] 6a
                    ld        d,e                           ;[18c8] 53
                    ld        e,$00                         ;[18c9] 1e 00
                    ex        de,hl                         ;[18cb] eb
                    add       hl,hl                         ;[18cc] 29
                    ex        de,hl                         ;[18cd] eb
                    adc       hl,hl                         ;[18ce] ed 6a
                    bit       0,(ix+$10)                    ;[18d0] dd cb 10 46
                    scf                                     ;[18d4] 37
                    ret                                     ;[18d5] c9

                    ld        h,$00                         ;[18d6] 26 00
                    ld        l,$00                         ;[18d8] 2e 00
                    ld        d,l                           ;[18da] 55
                    ld        e,l                           ;[18db] 5d
                    ld        b,$ff                         ;[18dc] 06 ff
                    ld        c,a                           ;[18de] 4f
                    ld        a,$fe                         ;[18df] 3e fe
                    jr        z,$18e5                       ;[18e1] 28 02
                    ld        a,$fd                         ;[18e3] 3e fd
                    out       ($e7),a                       ;[18e5] d3 e7
                    in        a,($eb)                       ;[18e7] db eb
                    ld        a,c                           ;[18e9] 79
                    ld        c,$eb                         ;[18ea] 0e eb
                    out       (c),a                         ;[18ec] ed 79
                    ld        a,h                           ;[18ee] 7c
                    out       (c),a                         ;[18ef] ed 79
                    ld        a,l                           ;[18f1] 7d
                    out       (c),a                         ;[18f2] ed 79
                    ld        a,d                           ;[18f4] 7a
                    out       (c),a                         ;[18f5] ed 79
                    ld        a,e                           ;[18f7] 7b
                    out       (c),a                         ;[18f8] ed 79
                    ld        a,b                           ;[18fa] 78
                    out       (c),a                         ;[18fb] ed 79
                    call      $1925                         ;[18fd] cd 25 19
                    and       a                             ;[1900] a7
                    ret       nz                            ;[1901] c0
                    scf                                     ;[1902] 37
                    ret                                     ;[1903] c9

                    push      hl                            ;[1904] e5
                    push      af                            ;[1905] f5
                    call      $19dd                         ;[1906] cd dd 19
                    pop       af                            ;[1909] f1
                    push      de                            ;[190a] d5
                    ld        a,$49                         ;[190b] 3e 49
                    call      $18d6                         ;[190d] cd d6 18
                    pop       de                            ;[1910] d1
                    pop       hl                            ;[1911] e1
                    jr        nc,$1923                      ;[1912] 30 0f
                    call      $1933                         ;[1914] cd 33 19
                    jr        nc,$1923                      ;[1917] 30 0a
                    ld        b,$12                         ;[1919] 06 12
                    ld        c,$eb                         ;[191b] 0e eb
                    ini                                     ;[191d] ed a2
                    jr        nz,$191d                      ;[191f] 20 fc
                    scf                                     ;[1921] 37
                    ld        a,d                           ;[1922] 7a
                    jr        $1961                         ;[1923] 18 3c
                    ld        bc,$0032                      ;[1925] 01 32 00
                    in        a,($eb)                       ;[1928] db eb
                    cp        $ff                           ;[192a] fe ff
                    ret       nz                            ;[192c] c0
                    djnz      $1928                         ;[192d] 10 f9
                    dec       c                             ;[192f] 0d
                    jr        nz,$1928                      ;[1930] 20 f6
                    ret                                     ;[1932] c9

                    ld        e,$0a                         ;[1933] 1e 0a
                    call      $1925                         ;[1935] cd 25 19
                    cp        $fe                           ;[1938] fe fe
                    jr        z,$1942                       ;[193a] 28 06
                    jr        c,$1942                       ;[193c] 38 04
                    dec       e                             ;[193e] 1d
                    jr        nz,$1935                      ;[193f] 20 f4
                    ccf                                     ;[1941] 3f
                    ccf                                     ;[1942] 3f
                    ret                                     ;[1943] c9

                    ld        a,$51                         ;[1944] 3e 51
                    call      $18dc                         ;[1946] cd dc 18
                    call      $1933                         ;[1949] cd 33 19
                    ld        a,$00                         ;[194c] 3e 00
                    jr        nc,$1961                      ;[194e] 30 11
                    push      ix                            ;[1950] dd e5
                    pop       hl                            ;[1952] e1
                    ld        bc,$00eb                      ;[1953] 01 eb 00
                    inir                                    ;[1956] ed b2
                    inir                                    ;[1958] ed b2
                    in        a,($eb)                       ;[195a] db eb
                    nop                                     ;[195c] 00
                    nop                                     ;[195d] 00
                    in        a,($eb)                       ;[195e] db eb
                    scf                                     ;[1960] 37
                    push      af                            ;[1961] f5
                    in        a,($eb)                       ;[1962] db eb
                    ld        a,$ff                         ;[1964] 3e ff
                    out       ($e7),a                       ;[1966] d3 e7
                    nop                                     ;[1968] 00
                    in        a,($eb)                       ;[1969] db eb
                    pop       af                            ;[196b] f1
                    ret                                     ;[196c] c9

                    ld        a,$4c                         ;[196d] 3e 4c
                    call      $18d6                         ;[196f] cd d6 18
                    in        a,($eb)                       ;[1972] db eb
                    and       a                             ;[1974] a7
                    scf                                     ;[1975] 37
                    jr        z,$1972                       ;[1976] 28 fa
                    jr        $1961                         ;[1978] 18 e7
                    push      af                            ;[197a] f5
                    ld        a,$58                         ;[197b] 3e 58
                    call      $18dc                         ;[197d] cd dc 18
                    jr        nc,$19c1                      ;[1980] 30 3f
                    ld        a,$fe                         ;[1982] 3e fe
                    out       ($eb),a                       ;[1984] d3 eb
                    ld        bc,$00eb                      ;[1986] 01 eb 00
                    push      ix                            ;[1989] dd e5
                    pop       hl                            ;[198b] e1
                    otir                                    ;[198c] ed b3
                    otir                                    ;[198e] ed b3
                    ld        a,$ff                         ;[1990] 3e ff
                    out       ($eb),a                       ;[1992] d3 eb
                    nop                                     ;[1994] 00
                    nop                                     ;[1995] 00
                    out       ($eb),a                       ;[1996] d3 eb
                    call      $1925                         ;[1998] cd 25 19
                    and       $1f                           ;[199b] e6 1f
                    cp        $05                           ;[199d] fe 05
                    jr        nz,$19c1                      ;[199f] 20 20
                    call      $1925                         ;[19a1] cd 25 19
                    and       a                             ;[19a4] a7
                    jr        z,$19a1                       ;[19a5] 28 fa
                    pop       af                            ;[19a7] f1
                    ld        a,$4d                         ;[19a8] 3e 4d
                    push      hl                            ;[19aa] e5
                    call      $18d6                         ;[19ab] cd d6 18
                    pop       hl                            ;[19ae] e1
                    jr        nc,$19c2                      ;[19af] 30 11
                    in        a,($eb)                       ;[19b1] db eb
                    and       a                             ;[19b3] a7
                    scf                                     ;[19b4] 37
                    jr        z,$19c5                       ;[19b5] 28 0e
                    and       $23                           ;[19b7] e6 23
                    ld        a,$01                         ;[19b9] 3e 01
                    jr        nz,$19c5                      ;[19bb] 20 08
                    ld        a,$03                         ;[19bd] 3e 03
                    jr        $19c5                         ;[19bf] 18 04
                    pop       af                            ;[19c1] f1
                    ld        a,$00                         ;[19c2] 3e 00
                    and       a                             ;[19c4] a7
                    jr        $1961                         ;[19c5] 18 9a
                    ld        bc,$0200                      ;[19c7] 01 00 02
                    ld        a,$02                         ;[19ca] 3e 02
                    sub       b                             ;[19cc] 90
                    ld        hl,$e090                      ;[19cd] 21 90 e0
                    push      bc                            ;[19d0] c5
                    call      $1904                         ;[19d1] cd 04 19
                    pop       bc                            ;[19d4] c1
                    jr        c,$19d8                       ;[19d5] 38 01
                    dec       c                             ;[19d7] 0d
                    inc       c                             ;[19d8] 0c
                    djnz      $19ca                         ;[19d9] 10 ef
                    ld        a,c                           ;[19db] 79
                    ret                                     ;[19dc] c9

                    push      af                            ;[19dd] f5
                    call      $196d                         ;[19de] cd 6d 19
                    pop       af                            ;[19e1] f1
                    push      af                            ;[19e2] f5
                    ld        a,$40                         ;[19e3] 3e 40
                    ld        hl,$0000                      ;[19e5] 21 00 00
                    ld        d,h                           ;[19e8] 54
                    ld        e,l                           ;[19e9] 5d
                    ld        b,$95                         ;[19ea] 06 95
                    call      $18de                         ;[19ec] cd de 18
                    dec       a                             ;[19ef] 3d
                    jr        nz,$1a3d                      ;[19f0] 20 4b
                    ld        bc,$0078                      ;[19f2] 01 78 00
                    pop       af                            ;[19f5] f1
                    push      af                            ;[19f6] f5
                    push      bc                            ;[19f7] c5
                    ld        a,$48                         ;[19f8] 3e 48
                    ld        hl,$0000                      ;[19fa] 21 00 00
                    ld        de,$01aa                      ;[19fd] 11 aa 01
                    ld        b,$87                         ;[1a00] 06 87
                    call      $18de                         ;[1a02] cd de 18
                    pop       bc                            ;[1a05] c1
                    bit       2,a                           ;[1a06] cb 57
                    ld        h,$00                         ;[1a08] 26 00
                    jr        nz,$1a26                      ;[1a0a] 20 1a
                    dec       a                             ;[1a0c] 3d
                    jr        nz,$1a3f                      ;[1a0d] 20 30
                    in        a,($eb)                       ;[1a0f] db eb
                    ld        h,a                           ;[1a11] 67
                    nop                                     ;[1a12] 00
                    in        a,($eb)                       ;[1a13] db eb
                    ld        l,a                           ;[1a15] 6f
                    nop                                     ;[1a16] 00
                    in        a,($eb)                       ;[1a17] db eb
                    and       $0f                           ;[1a19] e6 0f
                    ld        d,a                           ;[1a1b] 57
                    in        a,($eb)                       ;[1a1c] db eb
                    cp        e                             ;[1a1e] bb
                    jr        nz,$1a3f                      ;[1a1f] 20 1e
                    dec       d                             ;[1a21] 15
                    jr        nz,$1a63                      ;[1a22] 20 3f
                    ld        h,$40                         ;[1a24] 26 40
                    pop       af                            ;[1a26] f1
                    push      af                            ;[1a27] f5
                    push      hl                            ;[1a28] e5
                    ld        a,$77                         ;[1a29] 3e 77
                    call      $18d6                         ;[1a2b] cd d6 18
                    pop       hl                            ;[1a2e] e1
                    pop       af                            ;[1a2f] f1
                    push      af                            ;[1a30] f5
                    push      hl                            ;[1a31] e5
                    ld        a,$69                         ;[1a32] 3e 69
                    call      $18d8                         ;[1a34] cd d8 18
                    pop       hl                            ;[1a37] e1
                    jr        c,$1a46                       ;[1a38] 38 0c
                    dec       a                             ;[1a3a] 3d
                    jr        z,$1a26                       ;[1a3b] 28 e9
                    jr        $1a63                         ;[1a3d] 18 24
                    djnz      $19f5                         ;[1a3f] 10 b4
                    dec       c                             ;[1a41] 0d
                    jr        nz,$19f5                      ;[1a42] 20 b1
                    jr        $1a63                         ;[1a44] 18 1d
                    pop       af                            ;[1a46] f1
                    push      af                            ;[1a47] f5
                    call      $1a68                         ;[1a48] cd 68 1a
                    jr        nc,$1a63                      ;[1a4b] 30 16
                    ld        d,a                           ;[1a4d] 57
                    jr        z,$1a60                       ;[1a4e] 28 10
                    pop       af                            ;[1a50] f1
                    push      af                            ;[1a51] f5
                    ld        a,$50                         ;[1a52] 3e 50
                    ld        de,$0200                      ;[1a54] 11 00 02
                    ld        h,e                           ;[1a57] 63
                    ld        l,e                           ;[1a58] 6b
                    call      $18dc                         ;[1a59] cd dc 18
                    jr        nc,$1a63                      ;[1a5c] 30 05
                    ld        d,$01                         ;[1a5e] 16 01
                    scf                                     ;[1a60] 37
                    jr        $1a64                         ;[1a61] 18 01
                    and       a                             ;[1a63] a7
                    pop       bc                            ;[1a64] c1
                    jp        $1961                         ;[1a65] c3 61 19
                    ld        a,$7a                         ;[1a68] 3e 7a
                    call      $18d6                         ;[1a6a] cd d6 18
                    ret       nc                            ;[1a6d] d0
                    ld        d,$c0                         ;[1a6e] 16 c0
                    in        a,($eb)                       ;[1a70] db eb
                    and       d                             ;[1a72] a2
                    ld        h,a                           ;[1a73] 67
                    in        a,($eb)                       ;[1a74] db eb
                    ld        l,a                           ;[1a76] 6f
                    nop                                     ;[1a77] 00
                    in        a,($eb)                       ;[1a78] db eb
                    ld        e,a                           ;[1a7a] 5f
                    nop                                     ;[1a7b] 00
                    in        a,($eb)                       ;[1a7c] db eb
                    ld        a,h                           ;[1a7e] 7c
                    sub       d                             ;[1a7f] 92
                    scf                                     ;[1a80] 37
                    ret                                     ;[1a81] c9

                    call      $1a68                         ;[1a82] cd 68 1a
                    ret       nc                            ;[1a85] d0
                    ret       z                             ;[1a86] c8
                    and       a                             ;[1a87] a7
                    ret                                     ;[1a88] c9

                    ld        de,$0109                      ;[1a89] 11 09 01
                    scf                                     ;[1a8c] 37
                    ret                                     ;[1a8d] c9

                    cp        $01                           ;[1a8e] fe 01
                    jr        z,$1b04                       ;[1a90] 28 72
                    and       a                             ;[1a92] a7
                    ld        a,$15                         ;[1a93] 3e 15
                    ret       nz                            ;[1a95] c0
                    ld        b,$10                         ;[1a96] 06 10
                    push      bc                            ;[1a98] c5
                    dec       b                             ;[1a99] 05
                    call      $0109                         ;[1a9a] cd 09 01
                    pop       bc                            ;[1a9d] c1
                    call      $1afa                         ;[1a9e] cd fa 1a
                    djnz      $1a98                         ;[1aa1] 10 f5
                    ld        ix,$e46c                      ;[1aa3] dd 21 6c e4
                    ld        b,$14                         ;[1aa7] 06 14
                    push      bc                            ;[1aa9] c5
                    push      ix                            ;[1aaa] dd e5
                    call      $00dc                         ;[1aac] cd dc 00
                    pop       ix                            ;[1aaf] dd e1
                    ld        bc,$0013                      ;[1ab1] 01 13 00
                    add       ix,bc                         ;[1ab4] dd 09
                    pop       bc                            ;[1ab6] c1
                    djnz      $1aa9                         ;[1ab7] 10 f0
                    ld        b,$41                         ;[1ab9] 06 41
                    push      bc                            ;[1abb] c5
                    ld        l,b                           ;[1abc] 68
                    ld        bc,$0000                      ;[1abd] 01 00 00
                    call      $00f7                         ;[1ac0] cd f7 00
                    pop       bc                            ;[1ac3] c1
                    call      $1afa                         ;[1ac4] cd fa 1a
                    jr        z,$1ad5                       ;[1ac7] 28 0c
                    inc       a                             ;[1ac9] 3c
                    jr        nz,$1ad5                      ;[1aca] 20 09
                    push      bc                            ;[1acc] c5
                    ld        l,b                           ;[1acd] 68
                    call      $00f4                         ;[1ace] cd f4 00
                    pop       bc                            ;[1ad1] c1
                    call      $1afa                         ;[1ad2] cd fa 1a
                    inc       b                             ;[1ad5] 04
                    ld        a,b                           ;[1ad6] 78
                    cp        $51                           ;[1ad7] fe 51
                    jr        nz,$1abb                      ;[1ad9] 20 e0
                    ld        b,$41                         ;[1adb] 06 41
                    push      bc                            ;[1add] c5
                    ld        l,b                           ;[1ade] 68
                    call      $00f4                         ;[1adf] cd f4 00
                    pop       bc                            ;[1ae2] c1
                    call      $1afa                         ;[1ae3] cd fa 1a
                    inc       b                             ;[1ae6] 04
                    ld        a,b                           ;[1ae7] 78
                    cp        $51                           ;[1ae8] fe 51
                    jr        nz,$1add                      ;[1aea] 20 f1
                    ld        b,$01                         ;[1aec] 06 01
                    rst       $08                           ;[1aee] cf
                    nop                                     ;[1aef] 00
                    adc       d                             ;[1af0] 8a
                    ld        sp,$0106                      ;[1af1] 31 06 01
                    rst       $08                           ;[1af4] cf
                    nop                                     ;[1af5] 00
                    ld        h,a                           ;[1af6] 67
                    ld        sp,$c937                      ;[1af7] 31 37 c9
                    ret       c                             ;[1afa] d8
                    cp        $1d                           ;[1afb] fe 1d
                    ret       z                             ;[1afd] c8
                    cp        $38                           ;[1afe] fe 38
                    ret       z                             ;[1b00] c8
                    and       a                             ;[1b01] a7
                    pop       hl                            ;[1b02] e1
                    ret                                     ;[1b03] c9

                    ld        hl,$5b8a                      ;[1b04] 21 8a 5b
                    ld        de,$da37                      ;[1b07] 11 37 da
                    ld        bc,$0075                      ;[1b0a] 01 75 00
                    ldir                                    ;[1b0d] ed b0
                    ld        ($da35),sp                    ;[1b0f] ed 73 35 da
                    ld        sp,$5bff                      ;[1b13] 31 ff 5b
                    ld        d,a                           ;[1b16] 57
                    ld        hl,$2007                      ;[1b17] 21 07 20
                    rst       $08                           ;[1b1a] cf
                    nop                                     ;[1b1b] 00
                    dec       e                             ;[1b1c] 1d
                    nop                                     ;[1b1d] 00
                    ld        e,a                           ;[1b1e] 5f
                    inc       hl                            ;[1b1f] 23
                    rst       $08                           ;[1b20] cf
                    nop                                     ;[1b21] 00
                    dec       e                             ;[1b22] 1d
                    nop                                     ;[1b23] 00
                    cpl                                     ;[1b24] 2f
                    cp        e                             ;[1b25] bb
                    jr        z,$1b2a                       ;[1b26] 28 02
                    ld        a,$02                         ;[1b28] 3e 02
                    push      af                            ;[1b2a] f5
                    push      de                            ;[1b2b] d5
                    xor       a                             ;[1b2c] af
                    ld        hl,$dba0                      ;[1b2d] 21 a0 db
                    ld        de,$dba1                      ;[1b30] 11 a1 db
                    ld        bc,$07f9                      ;[1b33] 01 f9 07
                    ld        (hl),a                        ;[1b36] 77
                    ldir                                    ;[1b37] ed b0
                    ld        hl,$e3f7                      ;[1b39] 21 f7 e3
                    ld        de,$e3f8                      ;[1b3c] 11 f8 e3
                    ld        bc,$1119                      ;[1b3f] 01 19 11
                    ld        (hl),a                        ;[1b42] 77
                    ldir                                    ;[1b43] ed b0
                    dec       a                             ;[1b45] 3d
                    ld        c,$2c                         ;[1b46] 0e 2c
                    ld        hl,$e300                      ;[1b48] 21 00 e3
                    ld        de,$e301                      ;[1b4b] 11 01 e3
                    push      bc                            ;[1b4e] c5
                    ld        (hl),a                        ;[1b4f] 77
                    ldir                                    ;[1b50] ed b0
                    pop       bc                            ;[1b52] c1
                    ld        hl,$e36d                      ;[1b53] 21 6d e3
                    ld        de,$e36e                      ;[1b56] 11 6e e3
                    ld        (hl),a                        ;[1b59] 77
                    ldir                                    ;[1b5a] ed b0
                    ld        c,$1f                         ;[1b5c] 0e 1f
                    ld        hl,$e3ca                      ;[1b5e] 21 ca e3
                    ld        de,$e3cb                      ;[1b61] 11 cb e3
                    ld        (hl),a                        ;[1b64] 77
                    ldir                                    ;[1b65] ed b0
                    call      $00a3                         ;[1b67] cd a3 00
                    ld        hl,$2300                      ;[1b6a] 21 00 23
                    ld        de,$2301                      ;[1b6d] 11 01 23
                    ld        bc,$0e2c                      ;[1b70] 01 2c 0e
                    rst       $08                           ;[1b73] cf
                    nop                                     ;[1b74] 00
                    in        a,($04)                       ;[1b75] db 04
                    ld        hl,$2300                      ;[1b77] 21 00 23
                    ld        de,$2301                      ;[1b7a] 11 01 23
                    ld        bc,$002f                      ;[1b7d] 01 2f 00
                    dec       a                             ;[1b80] 3d
                    rst       $08                           ;[1b81] cf
                    nop                                     ;[1b82] 00
                    call      c,$2104                       ;[1b83] dc 04 21
                    ret       p                             ;[1b86] f0
                    scf                                     ;[1b87] 37
                    ld        de,$37f1                      ;[1b88] 11 f1 37
                    ld        bc,$080c                      ;[1b8b] 01 0c 08
                    rst       $08                           ;[1b8e] cf
                    dec       c                             ;[1b8f] 0d
                    in        a,($04)                       ;[1b90] db 04
                    ld        hl,$2300                      ;[1b92] 21 00 23
                    ld        de,$0000                      ;[1b95] 11 00 00
                    rst       $08                           ;[1b98] cf
                    nop                                     ;[1b99] 00
                    ld        h,(hl)                        ;[1b9a] 66
                    ld        bc,$4321                      ;[1b9b] 01 21 43
                    nop                                     ;[1b9e] 00
                    call      $1c03                         ;[1b9f] cd 03 1c
                    pop       af                            ;[1ba2] f1
                    push      hl                            ;[1ba3] e5
                    push      af                            ;[1ba4] f5
                    and       a                             ;[1ba5] a7
                    call      z,$1d0c                       ;[1ba6] cc 0c 1d
                    ld        hl,$2302                      ;[1ba9] 21 02 23
                    ld        de,$024d                      ;[1bac] 11 4d 02
                    rst       $08                           ;[1baf] cf
                    nop                                     ;[1bb0] 00
                    ld        h,(hl)                        ;[1bb1] 66
                    ld        bc,$cdf1                      ;[1bb2] 01 f1 cd
                    exx                                     ;[1bb5] d9
                    inc       e                             ;[1bb6] 1c
                    ld        hl,$044d                      ;[1bb7] 21 4d 04
                    ld        b,$01                         ;[1bba] 06 01
                    call      $1c05                         ;[1bbc] cd 05 1c
                    pop       hl                            ;[1bbf] e1
                    ld        h,$05                         ;[1bc0] 26 05
                    call      $1c03                         ;[1bc2] cd 03 1c
                    ld        hl,$2025                      ;[1bc5] 21 25 20
                    ld        a,$09                         ;[1bc8] 3e 09
                    call      $1cb2                         ;[1bca] cd b2 1c
                    jr        nc,$1be0                      ;[1bcd] 30 11
                    dec       b                             ;[1bcf] 05
                    jr        z,$1be0                       ;[1bd0] 28 0e
                    push      bc                            ;[1bd2] c5
                    ld        de,$202e                      ;[1bd3] 11 2e 20
                    call      $1c2c                         ;[1bd6] cd 2c 1c
                    pop       bc                            ;[1bd9] c1
                    ld        de,$2031                      ;[1bda] 11 31 20
                    call      $1c2c                         ;[1bdd] cd 2c 1c
                    pop       af                            ;[1be0] f1
                    ld        hl,$da37                      ;[1be1] 21 37 da
                    ld        de,$5b8a                      ;[1be4] 11 8a 5b
                    ld        bc,$0075                      ;[1be7] 01 75 00
                    ldir                                    ;[1bea] ed b0
                    ld        sp,($da35)                    ;[1bec] ed 7b 35 da
                    add       $41                           ;[1bf0] c6 41
                    call      $012d                         ;[1bf2] cd 2d 01
                    jr        c,$1bfc                       ;[1bf5] 38 05
                    ld        a,$43                         ;[1bf7] 3e 43
                    call      $012d                         ;[1bf9] cd 2d 01
                    ld        ($5b79),a                     ;[1bfc] 32 79 5b
                    ld        ($5b7a),a                     ;[1bff] 32 7a 5b
                    ret                                     ;[1c02] c9

                    ld        b,$02                         ;[1c03] 06 02
                    push      bc                            ;[1c05] c5
                    ld        bc,$0000                      ;[1c06] 01 00 00
                    push      bc                            ;[1c09] c5
                    push      hl                            ;[1c0a] e5
                    ld        a,h                           ;[1c0b] 7c
                    call      $00f1                         ;[1c0c] cd f1 00
                    pop       hl                            ;[1c0f] e1
                    pop       bc                            ;[1c10] c1
                    jr        nc,$1c1e                      ;[1c11] 30 0b
                    push      bc                            ;[1c13] c5
                    push      hl                            ;[1c14] e5
                    call      $1c84                         ;[1c15] cd 84 1c
                    pop       hl                            ;[1c18] e1
                    pop       bc                            ;[1c19] c1
                    inc       l                             ;[1c1a] 2c
                    inc       bc                            ;[1c1b] 03
                    jr        $1c09                         ;[1c1c] 18 eb
                    cp        $09                           ;[1c1e] fe 09
                    jr        z,$1c1b                       ;[1c20] 28 f9
                    dec       bc                            ;[1c22] 0b
                    cp        $3e                           ;[1c23] fe 3e
                    jr        z,$1c1a                       ;[1c25] 28 f3
                    pop       bc                            ;[1c27] c1
                    inc       h                             ;[1c28] 24
                    djnz      $1c05                         ;[1c29] 10 da
                    ret                                     ;[1c2b] c9

                    ld        hl,$fb6c                      ;[1c2c] 21 6c fb
                    push      bc                            ;[1c2f] c5
                    push      de                            ;[1c30] d5
                    push      hl                            ;[1c31] e5
                    call      $1ccd                         ;[1c32] cd cd 1c
                    jr        nz,$1c7a                      ;[1c35] 20 43
                    inc       hl                            ;[1c37] 23
                    ld        a,(hl)                        ;[1c38] 7e
                    inc       hl                            ;[1c39] 23
                    res       7,a                           ;[1c3a] cb bf
                    cp        $41                           ;[1c3c] fe 41
                    jr        c,$1c7a                       ;[1c3e] 38 3a
                    cp        $51                           ;[1c40] fe 51
                    jr        nc,$1c7a                      ;[1c42] 30 36
                    ld        c,a                           ;[1c44] 4f
                    inc       hl                            ;[1c45] 23
                    inc       hl                            ;[1c46] 23
                    inc       hl                            ;[1c47] 23
                    ld        de,$2034                      ;[1c48] 11 34 20
                    push      hl                            ;[1c4b] e5
                    call      $1ccd                         ;[1c4c] cd cd 1c
                    pop       hl                            ;[1c4f] e1
                    jr        z,$1c5a                       ;[1c50] 28 08
                    ld        de,$2037                      ;[1c52] 11 37 20
                    call      $1ccd                         ;[1c55] cd cd 1c
                    jr        nz,$1c7a                      ;[1c58] 20 20
                    pop       hl                            ;[1c5a] e1
                    push      hl                            ;[1c5b] e5
                    ld        de,$d837                      ;[1c5c] 11 37 d8
                    ld        b,$0b                         ;[1c5f] 06 0b
                    ld        a,(hl)                        ;[1c61] 7e
                    inc       hl                            ;[1c62] 23
                    res       7,a                           ;[1c63] cb bf
                    ld        (de),a                        ;[1c65] 12
                    inc       de                            ;[1c66] 13
                    djnz      $1c61                         ;[1c67] 10 f8
                    ld        a,$ff                         ;[1c69] 3e ff
                    ld        (de),a                        ;[1c6b] 12
                    ld        l,c                           ;[1c6c] 69
                    ld        a,$ff                         ;[1c6d] 3e ff
                    ld        bc,$d82b                      ;[1c6f] 01 2b d8
                    push      hl                            ;[1c72] e5
                    call      $00f1                         ;[1c73] cd f1 00
                    pop       hl                            ;[1c76] e1
                    call      c,$1c84                       ;[1c77] dc 84 1c
                    pop       hl                            ;[1c7a] e1
                    pop       de                            ;[1c7b] d1
                    add       hl,$000d                      ;[1c7c] ed 34 0d 00
                    pop       bc                            ;[1c80] c1
                    djnz      $1c2f                         ;[1c81] 10 ac
                    ret                                     ;[1c83] c9

                    push      hl                            ;[1c84] e5
                    ld        a,l                           ;[1c85] 7d
                    add       $df                           ;[1c86] c6 df
                    ld        bc,$0100                      ;[1c88] 01 00 01
                    ld        h,a                           ;[1c8b] 67
                    ld        l,c                           ;[1c8c] 69
                    ld        de,$e090                      ;[1c8d] 11 90 e0
                    push      de                            ;[1c90] d5
                    ld        a,$09                         ;[1c91] 3e 09
                    rst       $08                           ;[1c93] cf
                    nop                                     ;[1c94] 00
                    ld        l,c                           ;[1c95] 69
                    rlca                                    ;[1c96] 07
                    pop       hl                            ;[1c97] e1
                    ld        a,(hl)                        ;[1c98] 7e
                    cpl                                     ;[1c99] 2f
                    inc       hl                            ;[1c9a] 23
                    cp        (hl)                          ;[1c9b] be
                    inc       hl                            ;[1c9c] 23
                    scf                                     ;[1c9d] 37
                    ccf                                     ;[1c9e] 3f
                    push      hl                            ;[1c9f] e5
                    call      z,$050a                       ;[1ca0] cc 0a 05
                    pop       hl                            ;[1ca3] e1
                    pop       bc                            ;[1ca4] c1
                    ret       c                             ;[1ca5] d8
                    ld        a,c                           ;[1ca6] 79
                    sub       $41                           ;[1ca7] d6 41
                    ld        (hl),$2f                      ;[1ca9] 36 2f
                    inc       hl                            ;[1cab] 23
                    ld        (hl),$ff                      ;[1cac] 36 ff
                    dec       hl                            ;[1cae] 2b
                    jp        $050a                         ;[1caf] c3 0a 05
                    call      $1e4e                         ;[1cb2] cd 4e 1e
                    push      hl                            ;[1cb5] e5
                    ld        hl,$fb5f                      ;[1cb6] 21 5f fb
                    ld        de,$fb60                      ;[1cb9] 11 60 fb
                    ld        bc,$000c                      ;[1cbc] 01 0c 00
                    ld        (hl),b                        ;[1cbf] 70
                    ldir                                    ;[1cc0] ed b0
                    pop       hl                            ;[1cc2] e1
                    ld        de,$fb5f                      ;[1cc3] 11 5f fb
                    ld        bc,$4021                      ;[1cc6] 01 21 40
                    xor       a                             ;[1cc9] af
                    jp        $011e                         ;[1cca] c3 1e 01
                    ld        b,$03                         ;[1ccd] 06 03
                    res       7,(hl)                        ;[1ccf] cb be
                    ld        a,(de)                        ;[1cd1] 1a
                    cp        (hl)                          ;[1cd2] be
                    ret       nz                            ;[1cd3] c0
                    inc       de                            ;[1cd4] 13
                    inc       hl                            ;[1cd5] 23
                    djnz      $1ccf                         ;[1cd6] 10 f7
                    ret                                     ;[1cd8] c9

                    push      af                            ;[1cd9] f5
                    call      $013c                         ;[1cda] cd 3c 01
                    ld        de,$0080                      ;[1cdd] 11 80 00
                    pop       af                            ;[1ce0] f1
                    and       a                             ;[1ce1] a7
                    sbc       hl,de                         ;[1ce2] ed 52
                    jr        z,$1ce7                       ;[1ce4] 28 01
                    xor       a                             ;[1ce6] af
                    ld        hl,$0080                      ;[1ce7] 21 80 00
                    ld        de,$0000                      ;[1cea] 11 00 00
                    push      de                            ;[1ced] d5
                    call      $24d8                         ;[1cee] cd d8 24
                    pop       de                            ;[1cf1] d1
                    call      $05ef                         ;[1cf2] cd ef 05
                    ld        hl,$e458                      ;[1cf5] 21 58 e4
                    ld        de,$e46c                      ;[1cf8] 11 6c e4
                    ld        a,$05                         ;[1cfb] 3e 05
                    call      $1f29                         ;[1cfd] cd 29 1f
                    ld        hl,$e462                      ;[1d00] 21 62 e4
                    ld        de,$e47f                      ;[1d03] 11 7f e4
                    ld        a,$06                         ;[1d06] 3e 06
                    call      $1f29                         ;[1d08] cd 29 1f
                    ret                                     ;[1d0b] c9

                    ld        a,$43                         ;[1d0c] 3e 43
                    cp        l                             ;[1d0e] bd
                    ld        hl,$2106                      ;[1d0f] 21 06 21
                    jp        nc,$1ed2                      ;[1d12] d2 d2 1e
                    ld        hl,$1ffe                      ;[1d15] 21 fe 1f
                    ld        a,$0b                         ;[1d18] 3e 0b
                    call      $1e45                         ;[1d1a] cd 45 1e
                    push      hl                            ;[1d1d] e5
                    ld        a,$10                         ;[1d1e] 3e 10
                    call      $0e37                         ;[1d20] cd 37 0e
                    pop       hl                            ;[1d23] e1
                    jp        nc,$1e09                      ;[1d24] d2 09 1e
                    call      $3e18                         ;[1d27] cd 18 3e
                    inc       bc                            ;[1d2a] 03
                    nop                                     ;[1d2b] 00
                    ld        hl,$d82b                      ;[1d2c] 21 2b d8
                    call      $1e9f                         ;[1d2f] cd 9f 1e
                    ld        hl,$2043                      ;[1d32] 21 43 20
                    ld        a,$0c                         ;[1d35] 3e 0c
                    ld        de,$0ec2                      ;[1d37] 11 c2 0e
                    call      $1dfa                         ;[1d3a] cd fa 1d
                    ld        hl,$c002                      ;[1d3d] 21 02 c0
                    ld        de,$3140                      ;[1d40] 11 40 31
                    ld        bc,$0ec0                      ;[1d43] 01 c0 0e
                    ld        a,$00                         ;[1d46] 3e 00
                    call      $1dec                         ;[1d48] cd ec 1d
                    ld        hl,$c000                      ;[1d4b] 21 00 c0
                    ld        de,$0700                      ;[1d4e] 11 00 07
                    push      hl                            ;[1d51] e5
                    push      de                            ;[1d52] d5
                    call      $1e29                         ;[1d53] cd 29 1e
                    pop       bc                            ;[1d56] c1
                    pop       hl                            ;[1d57] e1
                    push      hl                            ;[1d58] e5
                    ld        de,$2000                      ;[1d59] 11 00 20
                    call      $1dea                         ;[1d5c] cd ea 1d
                    pop       hl                            ;[1d5f] e1
                    ld        de,$0580                      ;[1d60] 11 80 05
                    push      hl                            ;[1d63] e5
                    push      de                            ;[1d64] d5
                    call      $1e29                         ;[1d65] cd 29 1e
                    pop       bc                            ;[1d68] c1
                    pop       hl                            ;[1d69] e1
                    push      hl                            ;[1d6a] e5
                    ld        de,$3180                      ;[1d6b] 11 80 31
                    call      $1dea                         ;[1d6e] cd ea 1d
                    pop       hl                            ;[1d71] e1
                    ld        de,$1400                      ;[1d72] 11 00 14
                    push      hl                            ;[1d75] e5
                    push      de                            ;[1d76] d5
                    call      $1e29                         ;[1d77] cd 29 1e
                    pop       bc                            ;[1d7a] c1
                    pop       hl                            ;[1d7b] e1
                    push      hl                            ;[1d7c] e5
                    ld        de,$2300                      ;[1d7d] 11 00 23
                    ld        a,$0c                         ;[1d80] 3e 0c
                    call      $1dec                         ;[1d82] cd ec 1d
                    pop       hl                            ;[1d85] e1
                    ld        de,$0bf8                      ;[1d86] 11 f8 0b
                    push      hl                            ;[1d89] e5
                    push      de                            ;[1d8a] d5
                    call      $1e29                         ;[1d8b] cd 29 1e
                    pop       bc                            ;[1d8e] c1
                    pop       hl                            ;[1d8f] e1
                    push      hl                            ;[1d90] e5
                    push      bc                            ;[1d91] c5
                    ld        de,$2000                      ;[1d92] 11 00 20
                    ld        a,$0d                         ;[1d95] 3e 0d
                    call      $1dec                         ;[1d97] cd ec 1d
                    pop       de                            ;[1d9a] d1
                    pop       hl                            ;[1d9b] e1
                    push      hl                            ;[1d9c] e5
                    push      de                            ;[1d9d] d5
                    call      $1e29                         ;[1d9e] cd 29 1e
                    pop       bc                            ;[1da1] c1
                    pop       hl                            ;[1da2] e1
                    push      hl                            ;[1da3] e5
                    ld        de,$2bf8                      ;[1da4] 11 f8 2b
                    ld        a,$0d                         ;[1da7] 3e 0d
                    call      $1dec                         ;[1da9] cd ec 1d
                    pop       hl                            ;[1dac] e1
                    ld        de,$1500                      ;[1dad] 11 00 15
                    push      hl                            ;[1db0] e5
                    push      de                            ;[1db1] d5
                    call      $1e29                         ;[1db2] cd 29 1e
                    pop       bc                            ;[1db5] c1
                    pop       hl                            ;[1db6] e1
                    push      hl                            ;[1db7] e5
                    ld        de,$2000                      ;[1db8] 11 00 20
                    ld        a,$0a                         ;[1dbb] 3e 0a
                    call      $1dec                         ;[1dbd] cd ec 1d
                    pop       hl                            ;[1dc0] e1
                    ld        de,$1140                      ;[1dc1] 11 40 11
                    push      de                            ;[1dc4] d5
                    call      $1e29                         ;[1dc5] cd 29 1e
                    call      $1df1                         ;[1dc8] cd f1 1d
                    pop       bc                            ;[1dcb] c1
                    ld        de,$2000                      ;[1dcc] 11 00 20
                    ld        a,$07                         ;[1dcf] 3e 07
                    call      $1dec                         ;[1dd1] cd ec 1d
                    ld        hl,$204f                      ;[1dd4] 21 4f 20
                    ld        a,$07                         ;[1dd7] 3e 07
                    ld        b,$c9                         ;[1dd9] 06 c9
                    ld        de,$0200                      ;[1ddb] 11 00 02
                    call      $1e14                         ;[1dde] cd 14 1e
                    call      $1df1                         ;[1de1] cd f1 1d
                    ld        de,$2700                      ;[1de4] 11 00 27
                    ld        bc,$0200                      ;[1de7] 01 00 02
                    ld        a,$01                         ;[1dea] 3e 01
                    rst       $08                           ;[1dec] cf
                    nop                                     ;[1ded] 00
                    inc       bc                            ;[1dee] 03
                    rlca                                    ;[1def] 07
                    ret                                     ;[1df0] c9

                    ld        b,$00                         ;[1df1] 06 00
                    call      $0109                         ;[1df3] cd 09 01
                    ld        hl,$c000                      ;[1df6] 21 00 c0
                    ret                                     ;[1df9] c9

                    ld        b,$00                         ;[1dfa] 06 00
                    call      $1e14                         ;[1dfc] cd 14 1e
                    ld        de,($c000)                    ;[1dff] ed 5b 00 c0
                    ld        hl,$d82b                      ;[1e03] 21 2b d8
                    jp        c,$1e9f                       ;[1e06] da 9f 1e
                    push      hl                            ;[1e09] e5
                    ld        hl,$20cc                      ;[1e0a] 21 cc 20
                    call      $1eef                         ;[1e0d] cd ef 1e
                    pop       hl                            ;[1e10] e1
                    jp        $1ed2                         ;[1e11] c3 d2 1e
                    push      bc                            ;[1e14] c5
                    push      de                            ;[1e15] d5
                    call      $1e4e                         ;[1e16] cd 4e 1e
                    inc       c                             ;[1e19] 0c
                    ld        de,$0002                      ;[1e1a] 11 02 00
                    call      $0106                         ;[1e1d] cd 06 01
                    pop       de                            ;[1e20] d1
                    ld        hl,$c001                      ;[1e21] 21 01 c0
                    pop       bc                            ;[1e24] c1
                    ld        (hl),b                        ;[1e25] 70
                    dec       hl                            ;[1e26] 2b
                    ld        (hl),b                        ;[1e27] 70
                    ret       nc                            ;[1e28] d0
                    ld        bc,$0007                      ;[1e29] 01 07 00
                    call      $0112                         ;[1e2c] cd 12 01
                    ret       c                             ;[1e2f] d8
                    cp        $19                           ;[1e30] fe 19
                    scf                                     ;[1e32] 37
                    ret       z                             ;[1e33] c8
                    and       a                             ;[1e34] a7
                    ret                                     ;[1e35] c9

                    ld        hl,$1fdf                      ;[1e36] 21 df 1f
                    add       hl,a                          ;[1e39] ed 31
                    bit       0,a                           ;[1e3b] cb 47
                    ld        a,$08                         ;[1e3d] 3e 08
                    jr        z,$1e45                       ;[1e3f] 28 04
                    inc       a                             ;[1e41] 3c
                    ld        hl,$2009                      ;[1e42] 21 09 20
                    push      hl                            ;[1e45] e5
                    ld        hl,$208e                      ;[1e46] 21 8e 20
                    ld        bc,$0011                      ;[1e49] 01 11 00
                    jr        $1e55                         ;[1e4c] 18 07
                    push      hl                            ;[1e4e] e5
                    ld        hl,$2074                      ;[1e4f] 21 74 20
                    ld        bc,$000c                      ;[1e52] 01 0c 00
                    ld        de,$d82b                      ;[1e55] 11 2b d8
                    ldir                                    ;[1e58] ed b0
                    pop       hl                            ;[1e5a] e1
                    ld        c,a                           ;[1e5b] 4f
                    ldir                                    ;[1e5c] ed b0
                    ld        a,$ff                         ;[1e5e] 3e ff
                    ld        (de),a                        ;[1e60] 12
                    ld        hl,$d82b                      ;[1e61] 21 2b d8
                    ret                                     ;[1e64] c9

                    xor       a                             ;[1e65] af
                    call      $0e0b                         ;[1e66] cd 0b 0e
                    cp        $08                           ;[1e69] fe 08
                    jr        z,$1e84                       ;[1e6b] 28 17
                    ld        a,$01                         ;[1e6d] 3e 01
                    call      $0e0b                         ;[1e6f] cd 0b 0e
                    ld        h,a                           ;[1e72] 67
                    ld        a,$0e                         ;[1e73] 3e 0e
                    call      $0e0b                         ;[1e75] cd 0b 0e
                    ld        l,a                           ;[1e78] 6f
                    ld        bc,$310a                      ;[1e79] 01 0a 31
                    and       a                             ;[1e7c] a7
                    sbc       hl,bc                         ;[1e7d] ed 42
                    ld        hl,$20af                      ;[1e7f] 21 af 20
                    jr        c,$1ed2                       ;[1e82] 38 4e
                    ld        hl,$1edb                      ;[1e84] 21 db 1e
                    ld        de,$ed27                      ;[1e87] 11 27 ed
                    ld        bc,$0014                      ;[1e8a] 01 14 00
                    ldir                                    ;[1e8d] ed b0
                    call      $ed27                         ;[1e8f] cd 27 ed
                    ld        hl,$2012                      ;[1e92] 21 12 20
                    jr        z,$1e9f                       ;[1e95] 28 08
                    call      $1eef                         ;[1e97] cd ef 1e
                    ld        hl,$20c3                      ;[1e9a] 21 c3 20
                    jr        $1ed2                         ;[1e9d] 18 33
                    push      hl                            ;[1e9f] e5
                    ld        hl,$0209                      ;[1ea0] 21 09 02
                    and       a                             ;[1ea3] a7
                    sbc       hl,de                         ;[1ea4] ed 52
                    pop       hl                            ;[1ea6] e1
                    ret       z                             ;[1ea7] c8
                    push      hl                            ;[1ea8] e5
                    ld        hl,$20e1                      ;[1ea9] 21 e1 20
                    call      $1eef                         ;[1eac] cd ef 1e
                    ex        de,hl                         ;[1eaf] eb
                    ld        b,$04                         ;[1eb0] 06 04
                    ld        a,h                           ;[1eb2] 7c
                    swapnib                                 ;[1eb3] ed 23
                    and       $0f                           ;[1eb5] e6 0f
                    add       $30                           ;[1eb7] c6 30
                    cp        $3a                           ;[1eb9] fe 3a
                    jr        c,$1ebf                       ;[1ebb] 38 02
                    add       $27                           ;[1ebd] c6 27
                    call      $3e00                         ;[1ebf] cd 00 3e
                    djnz      $1ec4                         ;[1ec2] 10 00
                    add       hl,hl                         ;[1ec4] 29
                    add       hl,hl                         ;[1ec5] 29
                    add       hl,hl                         ;[1ec6] 29
                    add       hl,hl                         ;[1ec7] 29
                    djnz      $1eb2                         ;[1ec8] 10 e8
                    ld        a,$20                         ;[1eca] 3e 20
                    call      $3e00                         ;[1ecc] cd 00 3e
                    djnz      $1ed1                         ;[1ecf] 10 00
                    pop       hl                            ;[1ed1] e1
                    call      $1eef                         ;[1ed2] cd ef 1e
                    ld        a,$02                         ;[1ed5] 3e 02
                    out       ($fe),a                       ;[1ed7] d3 fe
                    jr        $1ed9                         ;[1ed9] 18 fe
                    ld        a,$80                         ;[1edb] 3e 80
                    out       ($e3),a                       ;[1edd] d3 e3
                    ld        hl,($0004)                    ;[1edf] 2a 04 00
                    ld        de,($0006)                    ;[1ee2] ed 5b 06 00
                    xor       a                             ;[1ee6] af
                    out       ($e3),a                       ;[1ee7] d3 e3
                    ld        bc,$5644                      ;[1ee9] 01 44 56
                    sbc       hl,bc                         ;[1eec] ed 42
                    ret                                     ;[1eee] c9

                    push      de                            ;[1eef] d5
                    ld        de,$e090                      ;[1ef0] 11 90 e0
                    ld        bc,$0200                      ;[1ef3] 01 00 02
                    push      de                            ;[1ef6] d5
                    ldir                                    ;[1ef7] ed b0
                    pop       hl                            ;[1ef9] e1
                    call      $3e00                         ;[1efa] cd 00 3e
                    rst       $10                           ;[1efd] d7
                    rlca                                    ;[1efe] 07
                    pop       de                            ;[1eff] d1
                    ret                                     ;[1f00] c9

                    nextreg $8e,$7a                         ;[1f01] ed 91 8e 7a
                    ld        hl,$1f13                      ;[1f05] 21 13 1f
                    ld        de,$ed27                      ;[1f08] 11 27 ed
                    ld        bc,$0016                      ;[1f0b] 01 16 00
                    ldir                                    ;[1f0e] ed b0
                    jp        $ed27                         ;[1f10] c3 27 ed
                    ld        a,$81                         ;[1f13] 3e 81
                    out       ($e3),a                       ;[1f15] d3 e3
                    ld        a,$c9                         ;[1f17] 3e c9
                    ld        ($2009),a                     ;[1f19] 32 09 20
                    ld        a,$80                         ;[1f1c] 3e 80
                    out       ($e3),a                       ;[1f1e] d3 e3
                    ld        a,$c9                         ;[1f20] 3e c9
                    ld        ($3d00),a                     ;[1f22] 32 00 3d
                    xor       a                             ;[1f25] af
                    out       ($e3),a                       ;[1f26] d3 e3
                    ret                                     ;[1f28] c9

                    push      de                            ;[1f29] d5
                    push      hl                            ;[1f2a] e5
                    ld        hl,$1f99                      ;[1f2b] 21 99 1f
                    ld        bc,$0010                      ;[1f2e] 01 10 00
                    ldir                                    ;[1f31] ed b0
                    pop       de                            ;[1f33] d1
                    ex        (sp),ix                       ;[1f34] dd e3
                    push      de                            ;[1f36] d5
                    call      $07d5                         ;[1f37] cd d5 07
                    pop       de                            ;[1f3a] d1
                    ex        (sp),ix                       ;[1f3b] dd e3
                    push      de                            ;[1f3d] d5
                    ld        hl,$1fa4                      ;[1f3e] 21 a4 1f
                    ld        bc,$0008                      ;[1f41] 01 08 00
                    ldir                                    ;[1f44] ed b0
                    ex        de,hl                         ;[1f46] eb
                    pop       bc                            ;[1f47] c1
                    pop       de                            ;[1f48] d1
                    push      de                            ;[1f49] d5
                    push      bc                            ;[1f4a] c5
                    ld        (hl),e                        ;[1f4b] 73
                    inc       hl                            ;[1f4c] 23
                    ld        (hl),d                        ;[1f4d] 72
                    push      af                            ;[1f4e] f5
                    ld        hl,$209f                      ;[1f4f] 21 9f 20
                    call      $00b5                         ;[1f52] cd b5 00
                    pop       bc                            ;[1f55] c1
                    jr        nc,$1f5f                      ;[1f56] 30 07
                    ld        a,($f722)                     ;[1f58] 3a 22 f7
                    cp        $01                           ;[1f5b] fe 01
                    jr        z,$1f79                       ;[1f5d] 28 1a
                    pop       hl                            ;[1f5f] e1
                    ex        (sp),ix                       ;[1f60] dd e3
                    set       7,(ix+$01)                    ;[1f62] dd cb 01 fe
                    ex        (sp),ix                       ;[1f66] dd e3
                    push      hl                            ;[1f68] e5
                    ld        a,b                           ;[1f69] 78
                    ld        hl,$209f                      ;[1f6a] 21 9f 20
                    call      $00b5                         ;[1f6d] cd b5 00
                    jr        nc,$1f91                      ;[1f70] 30 1f
                    ld        a,($f722)                     ;[1f72] 3a 22 f7
                    cp        $01                           ;[1f75] fe 01
                    jr        nz,$1f91                      ;[1f77] 20 18
                    pop       de                            ;[1f79] d1
                    ld        hl,$f732                      ;[1f7a] 21 32 f7
                    ld        bc,$0008                      ;[1f7d] 01 08 00
                    ldir                                    ;[1f80] ed b0
                    ex        (sp),ix                       ;[1f82] dd e3
                    ld        a,(ix+$10)                    ;[1f84] dd 7e 10
                    and       $01                           ;[1f87] e6 01
                    add       $05                           ;[1f89] c6 05
                    call      $0765                         ;[1f8b] cd 65 07
                    pop       ix                            ;[1f8e] dd e1
                    ret                                     ;[1f90] c9

                    xor       a                             ;[1f91] af
                    pop       hl                            ;[1f92] e1
                    ld        (hl),a                        ;[1f93] 77
                    inc       hl                            ;[1f94] 23
                    ld        (hl),a                        ;[1f95] 77
                    pop       hl                            ;[1f96] e1
                    ld        (hl),a                        ;[1f97] 77
                    ret                                     ;[1f98] c9

                    ld        bc,$0000                      ;[1f99] 01 00 00
                    nop                                     ;[1f9c] 00
                    nop                                     ;[1f9d] 00
                    nop                                     ;[1f9e] 00
                    nop                                     ;[1f9f] 00
                    nop                                     ;[1fa0] 00
                    nop                                     ;[1fa1] 00
                    nop                                     ;[1fa2] 00
                    nop                                     ;[1fa3] 00
                    ld        bc,$0200                      ;[1fa4] 01 00 02
                    add       b                             ;[1fa7] 80
                    nop                                     ;[1fa8] 00
                    ld        bc,$0000                      ;[1fa9] 01 00 00
                    ld        c,a                           ;[1fac] 4f
                    ld        ix,$e46c                      ;[1fad] dd 21 6c e4
                    ld        b,$14                         ;[1fb1] 06 14
                    ld        e,$00                         ;[1fb3] 1e 00
                    ld        h,e                           ;[1fb5] 63
                    ld        l,e                           ;[1fb6] 6b
                    ld        a,(ix+$00)                    ;[1fb7] dd 7e 00
                    and       a                             ;[1fba] a7
                    jr        z,$1fda                       ;[1fbb] 28 1d
                    ld        a,(ix+$10)                    ;[1fbd] dd 7e 10
                    and       $01                           ;[1fc0] e6 01
                    cp        c                             ;[1fc2] b9
                    jr        nz,$1fce                      ;[1fc3] 20 09
                    ld        a,(ix+$11)                    ;[1fc5] dd 7e 11
                    or        (ix+$12)                      ;[1fc8] dd b6 12
                    jr        z,$1fce                       ;[1fcb] 28 01
                    inc       e                             ;[1fcd] 1c
                    push      de                            ;[1fce] d5
                    ld        de,$0013                      ;[1fcf] 11 13 00
                    add       ix,de                         ;[1fd2] dd 19
                    pop       de                            ;[1fd4] d1
                    djnz      $1fb7                         ;[1fd5] 10 e0
                    ld        a,e                           ;[1fd7] 7b
                    scf                                     ;[1fd8] 37
                    ret                                     ;[1fd9] c9

                    push      ix                            ;[1fda] dd e5
                    pop       hl                            ;[1fdc] e1
                    jr        $1fce                         ;[1fdd] 18 ef
                    ld        a,d                           ;[1fdf] 7a
                    ld        a,b                           ;[1fe0] 78
                    jr        c,$2013                       ;[1fe1] 38 30
                    ld        l,$72                         ;[1fe3] 2e 72
                    ld        l,a                           ;[1fe5] 6f
                    ld        l,l                           ;[1fe6] 6d
                    ld        a,d                           ;[1fe7] 7a
                    ld        a,b                           ;[1fe8] 78
                    jr        c,$201c                       ;[1fe9] 38 31
                    ld        l,$72                         ;[1feb] 2e 72
                    ld        l,a                           ;[1fed] 6f
                    ld        l,l                           ;[1fee] 6d
                    ld        sp,$3832                      ;[1fef] 31 32 38
                    ld        l,$72                         ;[1ff2] 2e 72
                    ld        l,a                           ;[1ff4] 6f
                    ld        l,l                           ;[1ff5] 6d
                    rst       $38                           ;[1ff6] ff
                    inc       (hl)                          ;[1ff7] 34
                    jr        c,$2028                       ;[1ff8] 38 2e
                    ld        (hl),d                        ;[1ffa] 72
                    ld        l,a                           ;[1ffb] 6f
                    ld        l,l                           ;[1ffc] 6d
                    rst       $38                           ;[1ffd] ff
                    ld        h,l                           ;[1ffe] 65
                    ld        l,(hl)                        ;[1fff] 6e
                    ld        b,c                           ;[2000] 41
                    ld        l,h                           ;[2001] 6c
                    ld        (hl),h                        ;[2002] 74
                    ld        e,d                           ;[2003] 5a
                    ld        e,b                           ;[2004] 58
                    ld        l,$72                         ;[2005] 2e 72
                    ld        l,a                           ;[2007] 6f
                    ld        l,l                           ;[2008] 6d
                    ld        sp,$3832                      ;[2009] 31 32 38
                    dec       l                             ;[200c] 2d
                    ld        ($722e),a                     ;[200d] 32 2e 72
                    ld        l,a                           ;[2010] 6f
                    ld        l,l                           ;[2011] 6d
                    ld        h,l                           ;[2012] 65
                    ld        l,(hl)                        ;[2013] 6e
                    ld        c,(hl)                        ;[2014] 4e
                    ld        a,b                           ;[2015] 78
                    ld        (hl),h                        ;[2016] 74
                    ld        l,l                           ;[2017] 6d
                    ld        l,l                           ;[2018] 6d
                    ld        h,e                           ;[2019] 63
                    ld        l,$72                         ;[201a] 2e 72
                    ld        l,a                           ;[201c] 6f
                    ld        l,l                           ;[201d] 6d
                    rst       $38                           ;[201e] ff
                    ld        c,l                           ;[201f] 4d
                    ld        l,a                           ;[2020] 6f
                    ld        (hl),l                        ;[2021] 75
                    ld        l,(hl)                        ;[2022] 6e
                    ld        (hl),h                        ;[2023] 74
                    dec       a                             ;[2024] 3d
                    ccf                                     ;[2025] 3f
                    ccf                                     ;[2026] 3f
                    ccf                                     ;[2027] 3f
                    dec       l                             ;[2028] 2d
                    ccf                                     ;[2029] 3f
                    ld        l,$3f                         ;[202a] 2e 3f
                    ccf                                     ;[202c] 3f
                    ccf                                     ;[202d] 3f
                    ld        b,h                           ;[202e] 44
                    ld        d,d                           ;[202f] 52
                    ld        d,(hl)                        ;[2030] 56
                    ld        b,e                           ;[2031] 43
                    ld        d,b                           ;[2032] 50
                    ld        c,l                           ;[2033] 4d
                    ld        d,b                           ;[2034] 50
                    inc       sp                            ;[2035] 33
                    ld        b,h                           ;[2036] 44
                    ld        b,h                           ;[2037] 44
                    ld        d,e                           ;[2038] 53
                    ld        c,e                           ;[2039] 4b
                    ld        d,e                           ;[203a] 53
                    ld        d,a                           ;[203b] 57
                    ld        d,b                           ;[203c] 50
                    dec       l                             ;[203d] 2d
                    ccf                                     ;[203e] 3f
                    ld        l,$50                         ;[203f] 2e 50
                    inc       sp                            ;[2041] 33
                    ld        d,e                           ;[2042] 53
                    ld        h,l                           ;[2043] 65
                    ld        l,(hl)                        ;[2044] 6e
                    ld        d,e                           ;[2045] 53
                    ld        a,c                           ;[2046] 79
                    ld        (hl),e                        ;[2047] 73
                    ld        (hl),h                        ;[2048] 74
                    ld        h,l                           ;[2049] 65
                    ld        l,l                           ;[204a] 6d
                    ld        l,$73                         ;[204b] 2e 73
                    ld        a,c                           ;[204d] 79
                    ld        (hl),e                        ;[204e] 73
                    ld        (hl),d                        ;[204f] 72
                    ld        (hl),h                        ;[2050] 74
                    ld        h,e                           ;[2051] 63
                    ld        l,$73                         ;[2052] 2e 73
                    ld        a,c                           ;[2054] 79
                    ld        (hl),e                        ;[2055] 73
                    rst       $28                           ;[2056] ef
                    ld        ($3a63),hl                    ;[2057] 22 63 3a
                    cpl                                     ;[205a] 2f
                    ld        l,(hl)                        ;[205b] 6e
                    ld        h,l                           ;[205c] 65
                    ld        a,b                           ;[205d] 78
                    ld        (hl),h                        ;[205e] 74
                    ld        a,d                           ;[205f] 7a
                    ld        a,b                           ;[2060] 78
                    ld        l,a                           ;[2061] 6f
                    ld        (hl),e                        ;[2062] 73
                    cpl                                     ;[2063] 2f
                    ld        h,c                           ;[2064] 61
                    ld        (hl),l                        ;[2065] 75
                    ld        (hl),h                        ;[2066] 74
                    ld        l,a                           ;[2067] 6f
                    ld        h,l                           ;[2068] 65
                    ld        a,b                           ;[2069] 78
                    ld        h,l                           ;[206a] 65
                    ld        h,e                           ;[206b] 63
                    ld        l,$31                         ;[206c] 2e 31
                    ld        (hl),e                        ;[206e] 73
                    ld        (hl),h                        ;[206f] 74
                    ld        ($ef0d),hl                    ;[2070] 22 0d ef
                    ld        ($3a63),hl                    ;[2073] 22 63 3a
                    cpl                                     ;[2076] 2f
                    ld        l,(hl)                        ;[2077] 6e
                    ld        h,l                           ;[2078] 65
                    ld        a,b                           ;[2079] 78
                    ld        (hl),h                        ;[207a] 74
                    ld        a,d                           ;[207b] 7a
                    ld        a,b                           ;[207c] 78
                    ld        l,a                           ;[207d] 6f
                    ld        (hl),e                        ;[207e] 73
                    cpl                                     ;[207f] 2f
                    ld        h,c                           ;[2080] 61
                    ld        (hl),l                        ;[2081] 75
                    ld        (hl),h                        ;[2082] 74
                    ld        l,a                           ;[2083] 6f
                    ld        h,l                           ;[2084] 65
                    ld        a,b                           ;[2085] 78
                    ld        h,l                           ;[2086] 65
                    ld        h,e                           ;[2087] 63
                    ld        l,$62                         ;[2088] 2e 62
                    ld        h,c                           ;[208a] 61
                    ld        (hl),e                        ;[208b] 73
                    ld        ($630d),hl                    ;[208c] 22 0d 63
                    ld        a,($6d2f)                     ;[208f] 3a 2f 6d
                    ld        h,c                           ;[2092] 61
                    ld        h,e                           ;[2093] 63
                    ld        l,b                           ;[2094] 68
                    ld        l,c                           ;[2095] 69
                    ld        l,(hl)                        ;[2096] 6e
                    ld        h,l                           ;[2097] 65
                    ld        (hl),e                        ;[2098] 73
                    cpl                                     ;[2099] 2f
                    ld        l,(hl)                        ;[209a] 6e
                    ld        h,l                           ;[209b] 65
                    ld        a,b                           ;[209c] 78
                    ld        (hl),h                        ;[209d] 74
                    cpl                                     ;[209e] 2f
                    ld        d,b                           ;[209f] 50
                    ld        c,h                           ;[20a0] 4c
                    ld        d,l                           ;[20a1] 55
                    ld        d,e                           ;[20a2] 53
                    ld        c,c                           ;[20a3] 49
                    ld        b,h                           ;[20a4] 44
                    ld        b,l                           ;[20a5] 45
                    ld        b,h                           ;[20a6] 44
                    ld        c,a                           ;[20a7] 4f
                    ld        d,e                           ;[20a8] 53
                    jr        nz,$20cb                      ;[20a9] 20 20
                    jr        nz,$20cd                      ;[20ab] 20 20
                    jr        nz,$20cf                      ;[20ad] 20 20
                    ld        b,e                           ;[20af] 43
                    ld        l,a                           ;[20b0] 6f
                    ld        (hl),d                        ;[20b1] 72
                    ld        h,l                           ;[20b2] 65
                    jr        nz,$20e8                      ;[20b3] 20 33
                    ld        l,$30                         ;[20b5] 2e 30
                    ld        sp,$312e                      ;[20b7] 31 2e 31
                    jr        nc,$20dc                      ;[20ba] 30 20
                    ld        l,(hl)                        ;[20bc] 6e
                    ld        h,l                           ;[20bd] 65
                    ld        h,l                           ;[20be] 65
                    ld        h,h                           ;[20bf] 64
                    ld        h,l                           ;[20c0] 65
                    ld        h,h                           ;[20c1] 64
                    rst       $38                           ;[20c2] ff
                    jr        nz,$212e                      ;[20c3] 20 69
                    ld        l,(hl)                        ;[20c5] 6e
                    halt                                    ;[20c6] 76
                    ld        h,c                           ;[20c7] 61
                    ld        l,h                           ;[20c8] 6c
                    ld        l,c                           ;[20c9] 69
                    ld        h,h                           ;[20ca] 64
                    rst       $38                           ;[20cb] ff
                    ld        b,l                           ;[20cc] 45
                    ld        (hl),d                        ;[20cd] 72
                    ld        (hl),d                        ;[20ce] 72
                    ld        l,a                           ;[20cf] 6f
                    ld        (hl),d                        ;[20d0] 72
                    jr        nz,$2145                      ;[20d1] 20 72
                    ld        h,l                           ;[20d3] 65
                    ld        h,c                           ;[20d4] 61
                    ld        h,h                           ;[20d5] 64
                    ld        l,c                           ;[20d6] 69
                    ld        l,(hl)                        ;[20d7] 6e
                    ld        h,a                           ;[20d8] 67
                    jr        nz,$2141                      ;[20d9] 20 66
                    ld        l,c                           ;[20db] 69
                    ld        l,h                           ;[20dc] 6c
                    ld        h,l                           ;[20dd] 65
                    ld        a,($ff0d)                     ;[20de] 3a 0d ff
                    ld        d,(hl)                        ;[20e1] 56
                    ld        h,l                           ;[20e2] 65
                    ld        (hl),d                        ;[20e3] 72
                    ld        (hl),e                        ;[20e4] 73
                    ld        l,c                           ;[20e5] 69
                    ld        l,a                           ;[20e6] 6f
                    ld        l,(hl)                        ;[20e7] 6e
                    jr        nz,$2157                      ;[20e8] 20 6d
                    ld        l,c                           ;[20ea] 69
                    ld        (hl),e                        ;[20eb] 73
                    ld        l,l                           ;[20ec] 6d
                    ld        h,c                           ;[20ed] 61
                    ld        (hl),h                        ;[20ee] 74
                    ld        h,e                           ;[20ef] 63
                    ld        l,b                           ;[20f0] 68
                    ld        a,($300d)                     ;[20f1] 3a 0d 30
                    ld        ($3930),a                     ;[20f4] 32 30 39
                    jr        nz,$215e                      ;[20f7] 20 65
                    ld        l,(hl)                        ;[20f9] 6e
                    ld        c,(hl)                        ;[20fa] 4e
                    ld        h,l                           ;[20fb] 65
                    ld        a,b                           ;[20fc] 78
                    ld        (hl),h                        ;[20fd] 74
                    ld        e,d                           ;[20fe] 5a
                    ld        e,b                           ;[20ff] 58
                    ld        l,$72                         ;[2100] 2e 72
                    ld        l,a                           ;[2102] 6f
                    ld        l,l                           ;[2103] 6d
                    dec       c                             ;[2104] 0d
                    rst       $38                           ;[2105] ff
                    ld        b,h                           ;[2106] 44
                    ld        (hl),d                        ;[2107] 72
                    ld        l,c                           ;[2108] 69
                    halt                                    ;[2109] 76
                    ld        h,l                           ;[210a] 65
                    jr        nz,$2150                      ;[210b] 20 43
                    ld        a,($6e20)                     ;[210d] 3a 20 6e
                    ld        l,a                           ;[2110] 6f
                    ld        (hl),h                        ;[2111] 74
                    jr        nz,$217a                      ;[2112] 20 66
                    ld        l,a                           ;[2114] 6f
                    ld        (hl),l                        ;[2115] 75
                    ld        l,(hl)                        ;[2116] 6e
                    ld        h,h                           ;[2117] 64
                    rst       $38                           ;[2118] ff
                    and       $df                           ;[2119] e6 df
                    sub       $41                           ;[211b] d6 41
                    ccf                                     ;[211d] 3f
                    jr        nc,$212f                      ;[211e] 30 0f
                    cp        $10                           ;[2120] fe 10
                    jr        nc,$212f                      ;[2122] 30 0b
                    call      $2132                         ;[2124] cd 32 21
                    push      bc                            ;[2127] c5
                    pop       ix                            ;[2128] dd e1
                    ld        a,(hl)                        ;[212a] 7e
                    inc       hl                            ;[212b] 23
                    or        (hl)                          ;[212c] b6
                    add       $ff                           ;[212d] c6 ff
                    ld        a,$16                         ;[212f] 3e 16
                    ret                                     ;[2131] c9

                    ld        e,a                           ;[2132] 5f
                    ld        d,$30                         ;[2133] 16 30
                    mul       d,e                           ;[2135] ed 30
                    add       de,$e5e8                      ;[2137] ed 35 e8 e5
                    add       a                             ;[213b] 87
                    ld        hl,$e2a0                      ;[213c] 21 a0 e2
                    add       hl,a                          ;[213f] ed 31
                    ld        bc,$e2c0                      ;[2141] 01 c0 e2
                    and       a                             ;[2144] a7
                    ret       z                             ;[2145] c8
                    ld        bc,$e32d                      ;[2146] 01 2d e3
                    cp        $02                           ;[2149] fe 02
                    ret       z                             ;[214b] c8
                    ld        bc,$e39a                      ;[214c] 01 9a e3
                    cp        $18                           ;[214f] fe 18
                    ret       z                             ;[2151] c8
                    ld        b,d                           ;[2152] 42
                    ld        c,e                           ;[2153] 4b
                    ret                                     ;[2154] c9

                    ld        a,d                           ;[2155] 7a
                    cp        $05                           ;[2156] fe 05
                    jr        nc,$2188                      ;[2158] 30 2e
                    ld        hl,$e844                      ;[215a] 21 44 e8
                    cp        $04                           ;[215d] fe 04
                    jr        z,$2165                       ;[215f] 28 04
                    ld        a,$41                         ;[2161] 3e 41
                    and       a                             ;[2163] a7
                    ret                                     ;[2164] c9

                    ld        a,(hl)                        ;[2165] 7e
                    and       a                             ;[2166] a7
                    jp        m,$2161                       ;[2167] fa 61 21
                    ld        a,$3b                         ;[216a] 3e 3b
                    ret       nz                            ;[216c] c0
                    ld        a,e                           ;[216d] 7b
                    add       $41                           ;[216e] c6 41
                    ld        (hl),a                        ;[2170] 77
                    add       hl,$ffe4                      ;[2171] ed 34 e4 ff
                    push      hl                            ;[2175] e5
                    ld        a,e                           ;[2176] 7b
                    call      $2132                         ;[2177] cd 32 21
                    ex        (sp),hl                       ;[217a] e3
                    ld        d,b                           ;[217b] 50
                    ld        e,c                           ;[217c] 59
                    push      de                            ;[217d] d5
                    ld        bc,$0030                      ;[217e] 01 30 00
                    ldir                                    ;[2181] ed b0
                    pop       de                            ;[2183] d1
                    pop       hl                            ;[2184] e1
                    jp        $2226                         ;[2185] c3 26 22
                    push      bc                            ;[2188] c5
                    push      de                            ;[2189] d5
                    ld        a,e                           ;[218a] 7b
                    call      $2132                         ;[218b] cd 32 21
                    pop       de                            ;[218e] d1
                    ex        (sp),hl                       ;[218f] e3
                    push      bc                            ;[2190] c5
                    push      de                            ;[2191] d5
                    ld        b,h                           ;[2192] 44
                    ld        c,l                           ;[2193] 4d
                    ld        a,d                           ;[2194] 7a
                    and       a                             ;[2195] a7
                    jp        p,$21a5                       ;[2196] f2 a5 21
                    ld        a,$03                         ;[2199] 3e 03
                    call      $2364                         ;[219b] cd 64 23
                    jr        c,$21c2                       ;[219e] 38 22
                    and       a                             ;[21a0] a7
                    pop       de                            ;[21a1] d1
                    pop       de                            ;[21a2] d1
                    pop       de                            ;[21a3] d1
                    ret                                     ;[21a4] c9

                    ld        hl,$f712                      ;[21a5] 21 12 f7
                    call      $00c4                         ;[21a8] cd c4 00
                    jr        nc,$21a0                      ;[21ab] 30 f3
                    ld        a,($f722)                     ;[21ad] 3a 22 f7
                    cp        $03                           ;[21b0] fe 03
                    ld        a,$09                         ;[21b2] 3e 09
                    jr        nz,$21a0                      ;[21b4] 20 ea
                    pop       af                            ;[21b6] f1
                    push      af                            ;[21b7] f5
                    push      bc                            ;[21b8] c5
                    call      $0740                         ;[21b9] cd 40 07
                    pop       bc                            ;[21bc] c1
                    jr        nc,$21a0                      ;[21bd] 30 e1
                    ld        hl,$f732                      ;[21bf] 21 32 f7
                    pop       af                            ;[21c2] f1
                    pop       de                            ;[21c3] d1
                    push      de                            ;[21c4] d5
                    push      af                            ;[21c5] f5
                    ld        bc,$001c                      ;[21c6] 01 1c 00
                    ldir                                    ;[21c9] ed b0
                    ex        de,hl                         ;[21cb] eb
                    pop       bc                            ;[21cc] c1
                    ld        a,c                           ;[21cd] 79
                    add       $41                           ;[21ce] c6 41
                    ld        (hl),a                        ;[21d0] 77
                    inc       hl                            ;[21d1] 23
                    ld        a,b                           ;[21d2] 78
                    and       $7f                           ;[21d3] e6 7f
                    ld        (hl),a                        ;[21d5] 77
                    inc       hl                            ;[21d6] 23
                    ld        b,$0c                         ;[21d7] 06 0c
                    ld        (hl),$00                      ;[21d9] 36 00
                    inc       hl                            ;[21db] 23
                    djnz      $21d9                         ;[21dc] 10 fb
                    ld        (hl),$96                      ;[21de] 36 96
                    inc       hl                            ;[21e0] 23
                    ld        (hl),$2e                      ;[21e1] 36 2e
                    inc       hl                            ;[21e3] 23
                    ld        (hl),$98                      ;[21e4] 36 98
                    inc       hl                            ;[21e6] 23
                    ld        (hl),$2e                      ;[21e7] 36 2e
                    inc       hl                            ;[21e9] 23
                    ld        (hl),$e3                      ;[21ea] 36 e3
                    inc       hl                            ;[21ec] 23
                    ld        (hl),$2e                      ;[21ed] 36 2e
                    add       hl,$ffec                      ;[21ef] ed 34 ec ff
                    ld        a,(hl)                        ;[21f3] 7e
                    and       $30                           ;[21f4] e6 30
                    add       hl,$fff4                      ;[21f6] ed 34 f4 ff
                    or        (hl)                          ;[21fa] b6
                    ld        (ix+$05),a                    ;[21fb] dd 77 05
                    add       hl,$0005                      ;[21fe] ed 34 05 00
                    ld        a,(hl)                        ;[2202] 7e
                    ld        (ix+$06),a                    ;[2203] dd 77 06
                    push      ix                            ;[2206] dd e5
                    pop       hl                            ;[2208] e1
                    push      hl                            ;[2209] e5
                    ld        d,c                           ;[220a] 51
                    ld        e,$13                         ;[220b] 1e 13
                    mul       d,e                           ;[220d] ed 30
                    add       de,$376e                      ;[220f] ed 35 6e 37
                    ld        bc,$0013                      ;[2213] 01 13 00
                    rst       $08                           ;[2216] cf
                    inc       c                             ;[2217] 0c
                    inc       b                             ;[2218] dd 04
                    pop       de                            ;[221a] d1
                    pop       hl                            ;[221b] e1
                    push      hl                            ;[221c] e5
                    add       hl,$0028                      ;[221d] ed 34 28 00
                    ld        (hl),e                        ;[2221] 73
                    inc       hl                            ;[2222] 23
                    ld        (hl),d                        ;[2223] 72
                    pop       de                            ;[2224] d1
                    pop       hl                            ;[2225] e1
                    ld        (hl),e                        ;[2226] 73
                    inc       hl                            ;[2227] 23
                    ld        (hl),d                        ;[2228] 72
                    ex        de,hl                         ;[2229] eb
                    ld        de,$f72f                      ;[222a] 11 2f f7
                    push      de                            ;[222d] d5
                    ld        bc,$000f                      ;[222e] 01 0f 00
                    ldir                                    ;[2231] ed b0
                    add       hl,$000b                      ;[2233] ed 34 0b 00
                    ld        a,(hl)                        ;[2237] 7e
                    and       $40                           ;[2238] e6 40
                    inc       hl                            ;[223a] 23
                    or        (hl)                          ;[223b] b6
                    inc       hl                            ;[223c] 23
                    ld        (de),a                        ;[223d] 12
                    inc       de                            ;[223e] 13
                    ldi                                     ;[223f] ed a0
                    pop       hl                            ;[2241] e1
                    ld        bc,$0016                      ;[2242] 01 16 00
                    dec       de                            ;[2245] 1b
                    ld        a,(de)                        ;[2246] 1a
                    sub       $41                           ;[2247] d6 41
                    ld        d,a                           ;[2249] 57
                    ld        e,c                           ;[224a] 59
                    mul       d,e                           ;[224b] ed 30
                    add       de,$3870                      ;[224d] ed 35 70 38
                    ld        c,$11                         ;[2251] 0e 11
                    rst       $08                           ;[2253] cf
                    dec       c                             ;[2254] 0d
                    inc       b                             ;[2255] dd 04
                    scf                                     ;[2257] 37
                    ret                                     ;[2258] c9

                    ld        a,$03                         ;[2259] 3e 03
                    push      bc                            ;[225b] c5
                    ld        hl,$f700                      ;[225c] 21 00 f7
                    ld        de,$f701                      ;[225f] 11 01 f7
                    ld        bc,$0012                      ;[2262] 01 12 00
                    ld        (hl),b                        ;[2265] 70
                    ldir                                    ;[2266] ed b0
                    ld        ($f700),a                     ;[2268] 32 00 f7
                    pop       hl                            ;[226b] e1
                    push      hl                            ;[226c] e5
                    ld        a,(hl)                        ;[226d] 7e
                    cp        $ff                           ;[226e] fe ff
                    jr        z,$2286                       ;[2270] 28 14
                    inc       hl                            ;[2272] 23
                    cp        $2e                           ;[2273] fe 2e
                    jr        nz,$226d                      ;[2275] 20 f6
                    push      hl                            ;[2277] e5
                    ld        a,(hl)                        ;[2278] 7e
                    inc       hl                            ;[2279] 23
                    cp        $ff                           ;[227a] fe ff
                    jr        z,$2285                       ;[227c] 28 07
                    cp        $2e                           ;[227e] fe 2e
                    jr        nz,$2278                      ;[2280] 20 f6
                    pop       af                            ;[2282] f1
                    jr        $2277                         ;[2283] 18 f2
                    pop       hl                            ;[2285] e1
                    ld        b,$03                         ;[2286] 06 03
                    ld        a,(hl)                        ;[2288] 7e
                    cp        $ff                           ;[2289] fe ff
                    jr        z,$229a                       ;[228b] 28 0d
                    inc       hl                            ;[228d] 23
                    cp        $61                           ;[228e] fe 61
                    jr        c,$229c                       ;[2290] 38 0a
                    cp        $7b                           ;[2292] fe 7b
                    jr        nc,$229c                      ;[2294] 30 06
                    and       $df                           ;[2296] e6 df
                    jr        $229c                         ;[2298] 18 02
                    ld        a,$20                         ;[229a] 3e 20
                    ld        (de),a                        ;[229c] 12
                    inc       de                            ;[229d] 13
                    djnz      $2288                         ;[229e] 10 e8
                    call      $05f4                         ;[22a0] cd f4 05
                    pop       hl                            ;[22a3] e1
                    ret       nc                            ;[22a4] d0
                    push      hl                            ;[22a5] e5
                    rst       $08                           ;[22a6] cf
                    nop                                     ;[22a7] 00
                    ld        c,a                           ;[22a8] 4f
                    inc       bc                            ;[22a9] 03
                    pop       hl                            ;[22aa] e1
                    and       $0f                           ;[22ab] e6 0f
                    ld        c,a                           ;[22ad] 4f
                    push      bc                            ;[22ae] c5
                    ld        c,$01                         ;[22af] 0e 01
                    ld        de,$0002                      ;[22b1] 11 02 00
                    call      $0106                         ;[22b4] cd 06 01
                    pop       bc                            ;[22b7] c1
                    ret       nc                            ;[22b8] d0
                    push      bc                            ;[22b9] c5
                    call      $0139                         ;[22ba] cd 39 01
                    pop       bc                            ;[22bd] c1
                    jr        nc,$22e0                      ;[22be] 30 20
                    ld        ($f70b),hl                    ;[22c0] 22 0b f7
                    ld        ($f70d),de                    ;[22c3] ed 53 0d f7
                    push      bc                            ;[22c7] c5
                    ld        a,b                           ;[22c8] 78
                    ld        hl,$f717                      ;[22c9] 21 17 f7
                    ld        de,$0002                      ;[22cc] 11 02 00
                    rst       $08                           ;[22cf] cf
                    nop                                     ;[22d0] 00
                    ld        c,a                           ;[22d1] 4f
                    inc       (hl)                          ;[22d2] 34
                    pop       bc                            ;[22d3] c1
                    push      af                            ;[22d4] f5
                    ld        ($f710),a                     ;[22d5] 32 10 f7
                    or        $80                           ;[22d8] f6 80
                    ld        ($f716),a                     ;[22da] 32 16 f7
                    pop       af                            ;[22dd] f1
                    ld        a,$09                         ;[22de] 3e 09
                    push      af                            ;[22e0] f5
                    push      bc                            ;[22e1] c5
                    push      de                            ;[22e2] d5
                    call      $0109                         ;[22e3] cd 09 01
                    pop       de                            ;[22e6] d1
                    pop       bc                            ;[22e7] c1
                    pop       af                            ;[22e8] f1
                    ret       nc                            ;[22e9] d0
                    dec       e                             ;[22ea] 1d
                    ld        a,$4a                         ;[22eb] 3e 4a
                    ccf                                     ;[22ed] 3f
                    ret       nz                            ;[22ee] c0
                    ld        hl,($f71b)                    ;[22ef] 2a 1b f7
                    ld        a,h                           ;[22f2] 7c
                    or        l                             ;[22f3] b5
                    ld        a,$09                         ;[22f4] 3e 09
                    ret       z                             ;[22f6] c8
                    dec       hl                            ;[22f7] 2b
                    ld        ($f707),hl                    ;[22f8] 22 07 f7
                    ld        a,c                           ;[22fb] 79
                    ld        ($f71b),a                     ;[22fc] 32 1b f7
                    ld        de,($f717)                    ;[22ff] ed 5b 17 f7
                    ld        hl,($f719)                    ;[2303] 2a 19 f7
                    ld        a,($f710)                     ;[2306] 3a 10 f7
                    bit       1,a                           ;[2309] cb 4f
                    jr        nz,$2318                      ;[230b] 20 0b
                    ld        e,d                           ;[230d] 5a
                    ld        d,l                           ;[230e] 55
                    ld        l,h                           ;[230f] 6c
                    ld        h,$00                         ;[2310] 26 00
                    srl       l                             ;[2312] cb 3d
                    rr        d                             ;[2314] cb 1a
                    rr        e                             ;[2316] cb 1b
                    ld        ($f701),de                    ;[2318] ed 53 01 f7
                    ld        ($f703),hl                    ;[231c] 22 03 f7
                    ld        ix,$f71c                      ;[231f] dd 21 1c f7
                    ld        (ix+$10),$80                  ;[2323] dd 36 10 80
                    ld        bc,$0000                      ;[2327] 01 00 00
                    ld        d,b                           ;[232a] 50
                    ld        e,c                           ;[232b] 59
                    rst       $08                           ;[232c] cf
                    nop                                     ;[232d] 00
                    xor       l                             ;[232e] ad
                    inc       de                            ;[232f] 13
                    ld        a,$3c                         ;[2330] 3e 3c
                    ccf                                     ;[2332] 3f
                    ret       nc                            ;[2333] d0
                    ld        ($f711),hl                    ;[2334] 22 11 f7
                    push      hl                            ;[2337] e5
                    ld        ix,$f700                      ;[2338] dd 21 00 f7
                    ld        hl,$f75f                      ;[233c] 21 5f f7
                    ld        bc,$0700                      ;[233f] 01 00 07
                    ld        d,c                           ;[2342] 51
                    ld        e,c                           ;[2343] 59
                    call      $00ac                         ;[2344] cd ac 00
                    ld        hl,$f95f                      ;[2347] 21 5f f9
                    ld        bc,$0700                      ;[234a] 01 00 07
                    ld        de,$0001                      ;[234d] 11 01 00
                    call      c,$00ac                       ;[2350] dc ac 00
                    pop       de                            ;[2353] d1
                    ret       nc                            ;[2354] d0
                    ld        hl,$f716                      ;[2355] 21 16 f7
                    ld        bc,$0006                      ;[2358] 01 06 00
                    rst       $08                           ;[235b] cf
                    nop                                     ;[235c] 00
                    inc       b                             ;[235d] dd 04
                    ld        bc,$f75f                      ;[235f] 01 5f f7
                    scf                                     ;[2362] 37
                    ret                                     ;[2363] c9

                    push      af                            ;[2364] f5
                    call      $240b                         ;[2365] cd 0b 24
                    jr        nz,$23a9                      ;[2368] 20 3f
                    pop       bc                            ;[236a] c1
                    ld        a,$09                         ;[236b] 3e 09
                    scf                                     ;[236d] 37
                    ccf                                     ;[236e] 3f
                    ret       nz                            ;[236f] c0
                    add       hl,$0020                      ;[2370] ed 34 20 00
                    ld        a,(hl)                        ;[2374] 7e
                    cp        b                             ;[2375] b8
                    jr        nz,$236b                      ;[2376] 20 f3
                    add       hl,$ffe4                      ;[2378] ed 34 e4 ff
                    push      hl                            ;[237c] e5
                    push      ix                            ;[237d] dd e5
                    call      $0605                         ;[237f] cd 05 06
                    pop       hl                            ;[2382] e1
                    pop       de                            ;[2383] d1
                    ret       nc                            ;[2384] d0
                    push      de                            ;[2385] d5
                    push      ix                            ;[2386] dd e5
                    pop       de                            ;[2388] d1
                    ld        bc,$0013                      ;[2389] 01 13 00
                    ldir                                    ;[238c] ed b0
                    pop       hl                            ;[238e] e1
                    push      hl                            ;[238f] e5
                    add       hl,$001b                      ;[2390] ed 34 1b 00
                    set       4,(hl)                        ;[2394] cb e6
                    bit       5,(hl)                        ;[2396] cb 6e
                    pop       hl                            ;[2398] e1
                    scf                                     ;[2399] 37
                    ret       nz                            ;[239a] c0
                    add       de,$ffee                      ;[239b] ed 35 ee ff
                    ld        b,$04                         ;[239f] 06 04
                    ex        de,hl                         ;[23a1] eb
                    inc       (hl)                          ;[23a2] 34
                    ex        de,hl                         ;[23a3] eb
                    inc       de                            ;[23a4] 13
                    ret       nz                            ;[23a5] c0
                    djnz      $23a1                         ;[23a6] 10 f9
                    ret                                     ;[23a8] c9

                    pop       af                            ;[23a9] f1
                    cp        $03                           ;[23aa] fe 03
                    jr        nz,$236b                      ;[23ac] 20 bd
                    ld        de,$243f                      ;[23ae] 11 3f 24
                    call      $2426                         ;[23b1] cd 26 24
                    jr        z,$23be                       ;[23b4] 28 08
                    ld        de,$2437                      ;[23b6] 11 37 24
                    call      $2426                         ;[23b9] cd 26 24
                    jr        nz,$236b                      ;[23bc] 20 ad
                    add       hl,$0031                      ;[23be] ed 34 31 00
                    ld        a,(hl)                        ;[23c2] 7e
                    cp        $03                           ;[23c3] fe 03
                    jr        nc,$236b                      ;[23c5] 30 a4
                    add       hl,$00e9                      ;[23c7] ed 34 e9 00
                    ld        a,(hl)                        ;[23cb] 7e
                    and       $c0                           ;[23cc] e6 c0
                    jr        nz,$23e6                      ;[23ce] 20 16
                    add       hl,$00e6                      ;[23d0] ed 34 e6 00
                    ld        d,h                           ;[23d4] 54
                    ld        e,l                           ;[23d5] 5d
                    ld        b,$0a                         ;[23d6] 06 0a
                    ld        a,(de)                        ;[23d8] 1a
                    ld        c,a                           ;[23d9] 4f
                    ld        a,(de)                        ;[23da] 1a
                    inc       de                            ;[23db] 13
                    cp        c                             ;[23dc] b9
                    jr        nz,$23ef                      ;[23dd] 20 10
                    djnz      $23da                         ;[23df] 10 f9
                    ld        hl,$275e                      ;[23e1] 21 5e 27
                    jr        $23ef                         ;[23e4] 18 09
                    rlca                                    ;[23e6] 07
                    ld        hl,$2768                      ;[23e7] 21 68 27
                    jr        nc,$23ef                      ;[23ea] 30 03
                    ld        hl,$2772                      ;[23ec] 21 72 27
                    push      ix                            ;[23ef] dd e5
                    ld        ix,$f72f                      ;[23f1] dd 21 2f f7
                    call      $265e                         ;[23f5] cd 5e 26
                    pop       ix                            ;[23f8] dd e1
                    ld        hl,$3000                      ;[23fa] 21 00 30
                    ld        ($f749),hl                    ;[23fd] 22 49 f7
                    ld        h,$80                         ;[2400] 26 80
                    ld        ($f73a),hl                    ;[2402] 22 3a f7
                    ld        hl,$f72f                      ;[2405] 21 2f f7
                    jp        $237c                         ;[2408] c3 7c 23
                    ld        h,b                           ;[240b] 60
                    ld        l,c                           ;[240c] 69
                    ld        b,$04                         ;[240d] 06 04
                    ld        de,$2433                      ;[240f] 11 33 24
                    call      $2428                         ;[2412] cd 28 24
                    ret       nz                            ;[2415] c0
                    push      hl                            ;[2416] e5
                    ld        a,$1f                         ;[2417] 3e 1f
                    ld        bc,$02ff                      ;[2419] 01 ff 02
                    add       (hl)                          ;[241c] 86
                    inc       hl                            ;[241d] 23
                    dec       c                             ;[241e] 0d
                    jr        nz,$241c                      ;[241f] 20 fb
                    djnz      $241c                         ;[2421] 10 f9
                    cp        (hl)                          ;[2423] be
                    pop       hl                            ;[2424] e1
                    ret                                     ;[2425] c9

                    ld        b,$08                         ;[2426] 06 08
                    push      hl                            ;[2428] e5
                    ld        a,(de)                        ;[2429] 1a
                    cp        (hl)                          ;[242a] be
                    inc       de                            ;[242b] 13
                    inc       hl                            ;[242c] 23
                    jr        nz,$2431                      ;[242d] 20 02
                    djnz      $2429                         ;[242f] 10 f8
                    pop       hl                            ;[2431] e1
                    ret                                     ;[2432] c9

                    ld        d,b                           ;[2433] 50
                    inc       sp                            ;[2434] 33
                    ld        b,h                           ;[2435] 44
                    ld        a,(de)                        ;[2436] 1a
                    ld        c,l                           ;[2437] 4d
                    ld        d,(hl)                        ;[2438] 56
                    jr        nz,$2468                      ;[2439] 20 2d
                    jr        nz,$2480                      ;[243b] 20 43
                    ld        d,b                           ;[243d] 50
                    ld        b,e                           ;[243e] 43
                    ld        b,l                           ;[243f] 45
                    ld        e,b                           ;[2440] 58
                    ld        d,h                           ;[2441] 54
                    ld        b,l                           ;[2442] 45
                    ld        c,(hl)                        ;[2443] 4e
                    ld        b,h                           ;[2444] 44
                    ld        b,l                           ;[2445] 45
                    ld        b,h                           ;[2446] 44
                    push      ix                            ;[2447] dd e5
                    push      af                            ;[2449] f5
                    add       $41                           ;[244a] c6 41
                    push      hl                            ;[244c] e5
                    call      $05ea                         ;[244d] cd ea 05
                    pop       hl                            ;[2450] e1
                    pop       de                            ;[2451] d1
                    pop       ix                            ;[2452] dd e1
                    ret       nc                            ;[2454] d0
                    ld        a,d                           ;[2455] 7a
                    add       $41                           ;[2456] c6 41
                    ld        b,$00                         ;[2458] 06 00
                    ld        (hl),b                        ;[245a] 70
                    inc       hl                            ;[245b] 23
                    ld        (hl),b                        ;[245c] 70
                    ld        hl,$e844                      ;[245d] 21 44 e8
                    cp        (hl)                          ;[2460] be
                    jr        nz,$2464                      ;[2461] 20 01
                    ld        (hl),b                        ;[2463] 70
                    ld        l,(ix+$28)                    ;[2464] dd 6e 28
                    ld        h,(ix+$29)                    ;[2467] dd 66 29
                    ld        a,h                           ;[246a] 7c
                    or        l                             ;[246b] b5
                    scf                                     ;[246c] 37
                    ret       z                             ;[246d] c8
                    ld        (hl),b                        ;[246e] 70
                    ret                                     ;[246f] c9

                    add       $41                           ;[2470] c6 41
                    ld        d,b                           ;[2472] 50
                    ld        e,c                           ;[2473] 59
                    ld        hl,$e844                      ;[2474] 21 44 e8
                    cp        (hl)                          ;[2477] be
                    ld        hl,$27d9                      ;[2478] 21 d9 27
                    ld        bc,$000a                      ;[247b] 01 0a 00
                    jr        nz,$2487                      ;[247e] 20 07
                    ld        a,(hl)                        ;[2480] 7e
                    sub       $30                           ;[2481] d6 30
                    ldir                                    ;[2483] ed b0
                    scf                                     ;[2485] 37
                    ret                                     ;[2486] c9

                    push      ix                            ;[2487] dd e5
                    push      de                            ;[2489] d5
                    sub       $41                           ;[248a] d6 41
                    call      $2132                         ;[248c] cd 32 21
                    push      bc                            ;[248f] c5
                    pop       ix                            ;[2490] dd e1
                    ld        a,$ff                         ;[2492] 3e ff
                    bit       4,(ix+$1b)                    ;[2494] dd cb 1b 66
                    jr        nz,$24d3                      ;[2498] 20 39
                    ld        e,(ix+$28)                    ;[249a] dd 5e 28
                    ld        d,(ix+$29)                    ;[249d] dd 56 29
                    push      de                            ;[24a0] d5
                    pop       ix                            ;[24a1] dd e1
                    ld        a,(ix+$10)                    ;[24a3] dd 7e 10
                    and       $01                           ;[24a6] e6 01
                    add       $05                           ;[24a8] c6 05
                    ld        c,(ix+$11)                    ;[24aa] dd 4e 11
                    ld        b,(ix+$12)                    ;[24ad] dd 46 12
                    push      bc                            ;[24b0] c5
                    push      af                            ;[24b1] f5
                    ld        hl,$f712                      ;[24b2] 21 12 f7
                    call      $00c4                         ;[24b5] cd c4 00
                    pop       hl                            ;[24b8] e1
                    pop       bc                            ;[24b9] c1
                    pop       de                            ;[24ba] d1
                    jr        nc,$24d4                      ;[24bb] 30 17
                    ld        a,h                           ;[24bd] 7c
                    add       $30                           ;[24be] c6 30
                    ld        (de),a                        ;[24c0] 12
                    inc       de                            ;[24c1] 13
                    ld        a,$3e                         ;[24c2] 3e 3e
                    ld        (de),a                        ;[24c4] 12
                    inc       de                            ;[24c5] 13
                    push      bc                            ;[24c6] c5
                    ld        a,h                           ;[24c7] 7c
                    ld        hl,$f712                      ;[24c8] 21 12 f7
                    ld        bc,$0010                      ;[24cb] 01 10 00
                    ldir                                    ;[24ce] ed b0
                    ex        de,hl                         ;[24d0] eb
                    ld        (hl),$ff                      ;[24d1] 36 ff
                    pop       bc                            ;[24d3] c1
                    pop       ix                            ;[24d4] dd e1
                    scf                                     ;[24d6] 37
                    ret                                     ;[24d7] c9

                    push      af                            ;[24d8] f5
                    push      hl                            ;[24d9] e5
                    ld        hl,$254b                      ;[24da] 21 4b 25
                    ld        de,$e843                      ;[24dd] 11 43 e8
                    ld        bc,$0015                      ;[24e0] 01 15 00
                    ldir                                    ;[24e3] ed b0
                    pop       hl                            ;[24e5] e1
                    pop       af                            ;[24e6] f1
                    push      hl                            ;[24e7] e5
                    ld        ix,$e828                      ;[24e8] dd 21 28 e8
                    ld        hl,$2543                      ;[24ec] 21 43 25
                    ld        de,$e3ef                      ;[24ef] 11 ef e3
                    ld        bc,$0008                      ;[24f2] 01 08 00
                    ldir                                    ;[24f5] ed b0
                    pop       de                            ;[24f7] d1
                    push      af                            ;[24f8] f5
                    ld        a,e                           ;[24f9] 7b
                    cp        $04                           ;[24fa] fe 04
                    jr        c,$2532                       ;[24fc] 38 34
                    add       d                             ;[24fe] 82
                    ld        ($e3f1),a                     ;[24ff] 32 f1 e3
                    ld        a,d                           ;[2502] 7a
                    ld        ($e3f4),a                     ;[2503] 32 f4 e3
                    pop       af                            ;[2506] f1
                    and       a                             ;[2507] a7
                    jr        nz,$2515                      ;[2508] 20 0b
                    ld        a,d                           ;[250a] 7a
                    push      de                            ;[250b] d5
                    rst       $08                           ;[250c] cf
                    dec       c                             ;[250d] 0d
                    dec       l                             ;[250e] 2d
                    ld        (hl),$d1                      ;[250f] 36 d1
                    inc       d                             ;[2511] 14
                    dec       e                             ;[2512] 1d
                    jr        nz,$250a                      ;[2513] 20 f5
                    ld        hl,$e3ef                      ;[2515] 21 ef e3
                    call      $26a0                         ;[2518] cd a0 26
                    ld        hl,$e3f1                      ;[251b] 21 f1 e3
                    ld        de,$3e76                      ;[251e] 11 76 3e
                    ld        bc,$0001                      ;[2521] 01 01 00
                    rst       $08                           ;[2524] cf
                    dec       c                             ;[2525] 0d
                    inc       b                             ;[2526] dd 04
                    bit       7,(ix+$1c)                    ;[2528] dd cb 1c 7e
                    scf                                     ;[252c] 37
                    ret       z                             ;[252d] c8
                    inc       (ix+$1c)                      ;[252e] dd 34 1c
                    ret                                     ;[2531] c9

                    pop       af                            ;[2532] f1
                    ld        l,(ix+$1c)                    ;[2533] dd 6e 1c
                    push      ix                            ;[2536] dd e5
                    call      $00f4                         ;[2538] cd f4 00
                    pop       ix                            ;[253b] dd e1
                    ld        (ix+$1c),$ff                  ;[253d] dd 36 1c ff
                    scf                                     ;[2541] 37
                    ret                                     ;[2542] c9

                    nop                                     ;[2543] 00
                    nop                                     ;[2544] 00
                    nop                                     ;[2545] 00
                    ld        bc,$0002                      ;[2546] 01 02 00
                    inc       bc                            ;[2549] 03
                    nop                                     ;[254a] 00
                    ex        af,af'                        ;[254b] 08
                    nop                                     ;[254c] 00
                    rst       $38                           ;[254d] ff
                    nop                                     ;[254e] 00
                    nop                                     ;[254f] 00
                    nop                                     ;[2550] 00
                    nop                                     ;[2551] 00
                    nop                                     ;[2552] 00
                    nop                                     ;[2553] 00
                    nop                                     ;[2554] 00
                    nop                                     ;[2555] 00
                    nop                                     ;[2556] 00
                    nop                                     ;[2557] 00
                    nop                                     ;[2558] 00
                    nop                                     ;[2559] 00
                    sub       (hl)                          ;[255a] 96
                    ld        l,$96                         ;[255b] 2e 96
                    ld        l,$96                         ;[255d] 2e 96
                    ld        l,$37                         ;[255f] 2e 37
                    ret                                     ;[2561] c9

                    xor       a                             ;[2562] af
                    ld        b,$01                         ;[2563] 06 01
                    sub       b                             ;[2565] 90
                    ld        a,$00                         ;[2566] 3e 00
                    ld        b,a                           ;[2568] 47
                    ld        c,a                           ;[2569] 4f
                    ld        de,$0101                      ;[256a] 11 01 01
                    ld        hl,$0069                      ;[256d] 21 69 00
                    scf                                     ;[2570] 37
                    ret                                     ;[2571] c9

                    ld        a,$41                         ;[2572] 3e 41
                    ld        b,$07                         ;[2574] 06 07
                    ld        hl,$e090                      ;[2576] 21 90 e0
                    push      hl                            ;[2579] e5
                    rst       $08                           ;[257a] cf
                    dec       c                             ;[257b] 0d
                    and       l                             ;[257c] a5
                    inc       (hl)                          ;[257d] 34
                    pop       hl                            ;[257e] e1
                    ret       nc                            ;[257f] d0
                    push      hl                            ;[2580] e5
                    xor       a                             ;[2581] af
                    ld        b,a                           ;[2582] 47
                    ld        e,$02                         ;[2583] 1e 02
                    add       (hl)                          ;[2585] 86
                    inc       hl                            ;[2586] 23
                    djnz      $2585                         ;[2587] 10 fc
                    dec       e                             ;[2589] 1d
                    jr        nz,$2585                      ;[258a] 20 f9
                    pop       hl                            ;[258c] e1
                    xor       $03                           ;[258d] ee 03
                    ld        a,$23                         ;[258f] 3e 23
                    ret       nz                            ;[2591] c0
                    di                                      ;[2592] f3
                    ld        a,$06                         ;[2593] 3e 06
                    nextreg $54,a                           ;[2595] ed 92 54
                    inc       a                             ;[2598] 3c
                    nextreg $55,a                           ;[2599] ed 92 55
                    ld        de,$be00                      ;[259c] 11 00 be
                    ld        bc,$0200                      ;[259f] 01 00 02
                    ldir                                    ;[25a2] ed b0
                    nextreg $8e,$3a                         ;[25a4] ed 91 8e 3a
                    ld        sp,$fe00                      ;[25a8] 31 00 fe
                    ld        a,$03                         ;[25ab] 3e 03
                    ld        ($5b5c),a                     ;[25ad] 32 5c 5b
                    inc       a                             ;[25b0] 3c
                    ld        ($5b67),a                     ;[25b1] 32 67 5b
                    call      $0e14                         ;[25b4] cd 14 0e
                    ld        hl,$25ca                      ;[25b7] 21 ca 25
                    ld        de,$fdfb                      ;[25ba] 11 fb fd
                    ld        bc,$0005                      ;[25bd] 01 05 00
                    ldir                                    ;[25c0] ed b0
                    ld        bc,$1ffd                      ;[25c2] 01 fd 1f
                    ld        a,$07                         ;[25c5] 3e 07
                    jp        $fdfb                         ;[25c7] c3 fb fd
                    out       (c),a                         ;[25ca] ed 79
                    jp        $fe10                         ;[25cc] c3 10 fe
                    ld        a,$3a                         ;[25cf] 3e 3a
                    and       a                             ;[25d1] a7
                    ret                                     ;[25d2] c9

                    scf                                     ;[25d3] 37
                    ret                                     ;[25d4] c9

                    ld        hl,$0000                      ;[25d5] 21 00 00
                    ret                                     ;[25d8] c9

                    ld        a,(ix+$19)                    ;[25d9] dd 7e 19
                    or        $06                           ;[25dc] f6 06
                    call      $2611                         ;[25de] cd 11 26
                    ld        a,e                           ;[25e1] 7b
                    add       (ix+$14)                      ;[25e2] dd 86 14
                    ld        e,a                           ;[25e5] 5f
                    push      de                            ;[25e6] d5
                    ld        hl,($e419)                    ;[25e7] 2a 19 e4
                    ld        a,h                           ;[25ea] 7c
                    or        l                             ;[25eb] b5
                    call      nz,$2792                      ;[25ec] c4 92 27
                    ld        a,e                           ;[25ef] 7b
                    ld        ($e40a),a                     ;[25f0] 32 0a e4
                    ld        l,(ix+$0f)                    ;[25f3] dd 6e 0f
                    ld        h,e                           ;[25f6] 63
                    ld        ($e40b),hl                    ;[25f7] 22 0b e4
                    ld        a,(ix+$17)                    ;[25fa] dd 7e 17
                    ld        ($e40d),a                     ;[25fd] 32 0d e4
                    ld        h,b                           ;[2600] 60
                    ld        l,d                           ;[2601] 6a
                    ld        ($e408),hl                    ;[2602] 22 08 e4
                    ld        a,$09                         ;[2605] 3e 09
                    ld        ($e405),a                     ;[2607] 32 05 e4
                    ld        hl,$e40e                      ;[260a] 21 0e e4
                    ld        (hl),$ff                      ;[260d] 36 ff
                    pop       de                            ;[260f] d1
                    ret                                     ;[2610] c9

                    ld        ($e401),hl                    ;[2611] 22 01 e4
                    ld        l,a                           ;[2614] 6f
                    ld        a,b                           ;[2615] 78
                    ld        ($e400),a                     ;[2616] 32 00 e4
                    call      $262a                         ;[2619] cd 2a 26
                    ld        h,c                           ;[261c] 61
                    ld        ($e406),hl                    ;[261d] 22 06 e4
                    ld        l,(ix+$15)                    ;[2620] dd 6e 15
                    ld        h,(ix+$16)                    ;[2623] dd 66 16
                    ld        ($e403),hl                    ;[2626] 22 03 e4
                    ret                                     ;[2629] c9

                    ld        a,(ix+$11)                    ;[262a] dd 7e 11
                    and       $7f                           ;[262d] e6 7f
                    ld        b,$00                         ;[262f] 06 00
                    ret       z                             ;[2631] c8
                    dec       a                             ;[2632] 3d
                    jr        nz,$263d                      ;[2633] 20 08
                    ld        a,d                           ;[2635] 7a
                    rra                                     ;[2636] 1f
                    ld        d,a                           ;[2637] 57
                    ld        a,b                           ;[2638] 78
                    rla                                     ;[2639] 17
                    ld        b,a                           ;[263a] 47
                    jr        $2649                         ;[263b] 18 0c
                    ld        a,d                           ;[263d] 7a
                    sub       (ix+$12)                      ;[263e] dd 96 12
                    jr        c,$2649                       ;[2641] 38 06
                    sub       (ix+$12)                      ;[2643] dd 96 12
                    cpl                                     ;[2646] 2f
                    ld        d,a                           ;[2647] 57
                    inc       b                             ;[2648] 04
                    ld        a,b                           ;[2649] 78
                    add       a                             ;[264a] 87
                    add       a                             ;[264b] 87
                    or        c                             ;[264c] b1
                    ld        c,a                           ;[264d] 4f
                    ret                                     ;[264e] c9

                    or        a                             ;[264f] b7
                    jr        nz,$2655                      ;[2650] 20 03
                    ld        hl,$0000                      ;[2652] 21 00 00
                    ld        de,($e419)                    ;[2655] ed 5b 19 e4
                    ld        ($e419),hl                    ;[2659] 22 19 e4
                    ex        de,hl                         ;[265c] eb
                    ret                                     ;[265d] c9

                    push      hl                            ;[265e] e5
                    push      bc                            ;[265f] c5
                    ld        a,(hl)                        ;[2660] 7e
                    ld        b,$41                         ;[2661] 06 41
                    dec       a                             ;[2663] 3d
                    jr        z,$266d                       ;[2664] 28 07
                    ld        b,$c1                         ;[2666] 06 c1
                    dec       a                             ;[2668] 3d
                    jr        z,$266d                       ;[2669] 28 02
                    ld        b,$01                         ;[266b] 06 01
                    ld        (ix+$14),b                    ;[266d] dd 70 14
                    inc       hl                            ;[2670] 23
                    ld        a,(hl)                        ;[2671] 7e
                    ld        (ix+$11),a                    ;[2672] dd 77 11
                    inc       hl                            ;[2675] 23
                    ld        a,(hl)                        ;[2676] 7e
                    ld        (ix+$12),a                    ;[2677] dd 77 12
                    inc       hl                            ;[267a] 23
                    ld        a,(hl)                        ;[267b] 7e
                    ld        (ix+$13),a                    ;[267c] dd 77 13
                    inc       hl                            ;[267f] 23
                    ld        b,(hl)                        ;[2680] 46
                    inc       hl                            ;[2681] 23
                    inc       hl                            ;[2682] 23
                    inc       hl                            ;[2683] 23
                    inc       hl                            ;[2684] 23
                    ld        a,(hl)                        ;[2685] 7e
                    ld        (ix+$17),a                    ;[2686] dd 77 17
                    inc       hl                            ;[2689] 23
                    ld        a,(hl)                        ;[268a] 7e
                    ld        (ix+$18),a                    ;[268b] dd 77 18
                    ld        hl,$0080                      ;[268e] 21 80 00
                    call      $279d                         ;[2691] cd 9d 27
                    ld        (ix+$15),l                    ;[2694] dd 75 15
                    ld        (ix+$16),h                    ;[2697] dd 74 16
                    ld        (ix+$19),$60                  ;[269a] dd 36 19 60
                    pop       bc                            ;[269e] c1
                    pop       hl                            ;[269f] e1
                    push      bc                            ;[26a0] c5
                    push      hl                            ;[26a1] e5
                    ex        de,hl                         ;[26a2] eb
                    ld        hl,$0004                      ;[26a3] 21 04 00
                    add       hl,de                         ;[26a6] 19
                    ld        a,(hl)                        ;[26a7] 7e
                    ld        (ix+$0f),a                    ;[26a8] dd 77 0f
                    push      af                            ;[26ab] f5
                    call      $2793                         ;[26ac] cd 93 27
                    ld        (ix+$10),a                    ;[26af] dd 77 10
                    dec       hl                            ;[26b2] 2b
                    ld        l,(hl)                        ;[26b3] 6e
                    ld        h,$00                         ;[26b4] 26 00
                    pop       bc                            ;[26b6] c1
                    call      $279d                         ;[26b7] cd 9d 27
                    ld        (ix+$00),l                    ;[26ba] dd 75 00
                    ld        (ix+$01),h                    ;[26bd] dd 74 01
                    ld        hl,$0006                      ;[26c0] 21 06 00
                    add       hl,de                         ;[26c3] 19
                    ld        a,(hl)                        ;[26c4] 7e
                    ld        (ix+$02),a                    ;[26c5] dd 77 02
                    ld        c,a                           ;[26c8] 4f
                    push      hl                            ;[26c9] e5
                    call      $2793                         ;[26ca] cd 93 27
                    ld        (ix+$03),a                    ;[26cd] dd 77 03
                    dec       hl                            ;[26d0] 2b
                    ld        e,(hl)                        ;[26d1] 5e
                    ld        (ix+$0d),e                    ;[26d2] dd 73 0d
                    ld        (ix+$0e),$00                  ;[26d5] dd 36 0e 00
                    dec       hl                            ;[26d9] 2b
                    dec       hl                            ;[26da] 2b
                    ld        b,(hl)                        ;[26db] 46
                    dec       hl                            ;[26dc] 2b
                    ld        d,(hl)                        ;[26dd] 56
                    dec       hl                            ;[26de] 2b
                    ld        a,(hl)                        ;[26df] 7e
                    ld        l,d                           ;[26e0] 6a
                    ld        h,$00                         ;[26e1] 26 00
                    ld        d,h                           ;[26e3] 54
                    and       $7f                           ;[26e4] e6 7f
                    jr        z,$26e9                       ;[26e6] 28 01
                    add       hl,hl                         ;[26e8] 29
                    sbc       hl,de                         ;[26e9] ed 52
                    ex        de,hl                         ;[26eb] eb
                    ld        hl,$0000                      ;[26ec] 21 00 00
                    add       hl,de                         ;[26ef] 19
                    djnz      $26ef                         ;[26f0] 10 fd
                    ld        a,c                           ;[26f2] 79
                    sub       (ix+$0f)                      ;[26f3] dd 96 0f
                    ld        b,a                           ;[26f6] 47
                    call      $27a4                         ;[26f7] cd a4 27
                    dec       hl                            ;[26fa] 2b
                    ld        (ix+$05),l                    ;[26fb] dd 75 05
                    ld        (ix+$06),h                    ;[26fe] dd 74 06
                    ld        b,$03                         ;[2701] 06 03
                    ld        a,h                           ;[2703] 7c
                    or        a                             ;[2704] b7
                    jr        z,$2708                       ;[2705] 28 01
                    inc       b                             ;[2707] 04
                    ld        a,c                           ;[2708] 79
                    sub       b                             ;[2709] 90
                    call      $2793                         ;[270a] cd 93 27
                    ld        (ix+$04),a                    ;[270d] dd 77 04
                    pop       de                            ;[2710] d1
                    push      hl                            ;[2711] e5
                    ld        b,$02                         ;[2712] 06 02
                    call      $27a4                         ;[2714] cd a4 27
                    inc       hl                            ;[2717] 23
                    inc       hl                            ;[2718] 23
                    ex        (sp),hl                       ;[2719] e3
                    inc       de                            ;[271a] 13
                    ld        a,(de)                        ;[271b] 1a
                    or        a                             ;[271c] b7
                    jr        nz,$2727                      ;[271d] 20 08
                    add       hl,hl                         ;[271f] 29
                    ld        a,h                           ;[2720] 7c
                    inc       a                             ;[2721] 3c
                    cp        $02                           ;[2722] fe 02
                    jr        nc,$2727                      ;[2724] 30 01
                    inc       a                             ;[2726] 3c
                    ld        b,a                           ;[2727] 47
                    ld        hl,$0000                      ;[2728] 21 00 00
                    scf                                     ;[272b] 37
                    rr        h                             ;[272c] cb 1c
                    rr        l                             ;[272e] cb 1d
                    djnz      $272b                         ;[2730] 10 f9
                    ld        (ix+$09),h                    ;[2732] dd 74 09
                    ld        (ix+$0a),l                    ;[2735] dd 75 0a
                    ld        h,$00                         ;[2738] 26 00
                    ld        l,a                           ;[273a] 6f
                    ld        b,c                           ;[273b] 41
                    inc       b                             ;[273c] 04
                    inc       b                             ;[273d] 04
                    call      $279d                         ;[273e] cd 9d 27
                    push      hl                            ;[2741] e5
                    dec       hl                            ;[2742] 2b
                    ld        (ix+$07),l                    ;[2743] dd 75 07
                    ld        (ix+$08),h                    ;[2746] dd 74 08
                    ld        b,$02                         ;[2749] 06 02
                    call      $27a4                         ;[274b] cd a4 27
                    inc       hl                            ;[274e] 23
                    ld        (ix+$0b),l                    ;[274f] dd 75 0b
                    ld        (ix+$0c),h                    ;[2752] dd 74 0c
                    pop       hl                            ;[2755] e1
                    add       hl,hl                         ;[2756] 29
                    add       hl,hl                         ;[2757] 29
                    pop       de                            ;[2758] d1
                    pop       bc                            ;[2759] c1
                    ld        a,(bc)                        ;[275a] 0a
                    scf                                     ;[275b] 37
                    pop       bc                            ;[275c] c1
                    ret                                     ;[275d] c9

                    nop                                     ;[275e] 00
                    nop                                     ;[275f] 00
                    jr        z,$276b                       ;[2760] 28 09
                    ld        (bc),a                        ;[2762] 02
                    ld        bc,$0203                      ;[2763] 01 03 02
                    ld        hl,($0152)                    ;[2766] 2a 52 01
                    nop                                     ;[2769] 00
                    jr        z,$2775                       ;[276a] 28 09
                    ld        (bc),a                        ;[276c] 02
                    ld        (bc),a                        ;[276d] 02
                    inc       bc                            ;[276e] 03
                    ld        (bc),a                        ;[276f] 02
                    ld        hl,($0252)                    ;[2770] 2a 52 02
                    nop                                     ;[2773] 00
                    jr        z,$277f                       ;[2774] 28 09
                    ld        (bc),a                        ;[2776] 02
                    nop                                     ;[2777] 00
                    inc       bc                            ;[2778] 03
                    ld        (bc),a                        ;[2779] 02
                    ld        hl,($0352)                    ;[277a] 2a 52 03
                    add       c                             ;[277d] 81
                    ld        d,b                           ;[277e] 50
                    add       hl,bc                         ;[277f] 09
                    ld        (bc),a                        ;[2780] 02
                    ld        bc,$0404                      ;[2781] 01 04 04
                    ld        hl,($c552)                    ;[2784] 2a 52 c5
                    ld        c,$01                         ;[2787] 0e 01
                    call      $27ae                         ;[2789] cd ae 27
                    pop       bc                            ;[278c] c1
                    and       $60                           ;[278d] e6 60
                    ret       z                             ;[278f] c8
                    scf                                     ;[2790] 37
                    ret                                     ;[2791] c9

                    jp        (hl)                          ;[2792] e9
                    or        a                             ;[2793] b7
                    ret       z                             ;[2794] c8
                    ld        b,a                           ;[2795] 47
                    ld        a,$01                         ;[2796] 3e 01
                    add       a                             ;[2798] 87
                    djnz      $2798                         ;[2799] 10 fd
                    dec       a                             ;[279b] 3d
                    ret                                     ;[279c] c9

                    ld        a,b                           ;[279d] 78
                    or        a                             ;[279e] b7
                    ret       z                             ;[279f] c8
                    add       hl,hl                         ;[27a0] 29
                    djnz      $27a0                         ;[27a1] 10 fd
                    ret                                     ;[27a3] c9

                    ld        a,b                           ;[27a4] 78
                    or        a                             ;[27a5] b7
                    ret       z                             ;[27a6] c8
                    srl       h                             ;[27a7] cb 3c
                    rr        l                             ;[27a9] cb 1d
                    djnz      $27a7                         ;[27ab] 10 fa
                    ret                                     ;[27ad] c9

                    ld        a,c                           ;[27ae] 79
                    and       $01                           ;[27af] e6 01
                    ld        c,a                           ;[27b1] 4f
                    ld        a,($e2db)                     ;[27b2] 3a db e2
                    jr        z,$27ba                       ;[27b5] 28 03
                    ld        a,($e348)                     ;[27b7] 3a 48 e3
                    and       $20                           ;[27ba] e6 20
                    or        c                             ;[27bc] b1
                    scf                                     ;[27bd] 37
                    ret                                     ;[27be] c9

                    call      $25d9                         ;[27bf] cd d9 25
                    push      de                            ;[27c2] d5
                    push      bc                            ;[27c3] c5
                    ld        hl,$e400                      ;[27c4] 21 00 e4
                    rst       $08                           ;[27c7] cf
                    inc       c                             ;[27c8] 0c
                    call      z,$c130                       ;[27c9] cc 30 c1
                    pop       de                            ;[27cc] d1
                    inc       hl                            ;[27cd] 23
                    ld        a,(hl)                        ;[27ce] 7e
                    xor       c                             ;[27cf] a9
                    scf                                     ;[27d0] 37
                    ret       z                             ;[27d1] c8
                    xor       a                             ;[27d2] af
                    ret                                     ;[27d3] c9

                    rst       $08                           ;[27d4] cf
                    inc       c                             ;[27d5] 0c
                    call      z,$c930                       ;[27d6] cc 30 c9
                    inc       (hl)                          ;[27d9] 34
                    ld        a,$52                         ;[27da] 3e 52
                    ld        b,c                           ;[27dc] 41
                    ld        c,l                           ;[27dd] 4d
                    ld        h,h                           ;[27de] 64
                    ld        l,c                           ;[27df] 69
                    ld        (hl),e                        ;[27e0] 73
                    ld        l,e                           ;[27e1] 6b
                    rst       $38                           ;[27e2] ff
                    pop       ix                            ;[27e3] dd e1
                    push      hl                            ;[27e5] e5
                    push      de                            ;[27e6] d5
                    ld        hl,($5c3d)                    ;[27e7] 2a 3d 5c
                    push      hl                            ;[27ea] e5
                    ld        hl,($5b6c)                    ;[27eb] 2a 6c 5b
                    push      hl                            ;[27ee] e5
                    ld        hl,($5b58)                    ;[27ef] 2a 58 5b
                    push      hl                            ;[27f2] e5
                    ld        h,$3e                         ;[27f3] 26 3e
                    push      hl                            ;[27f5] e5
                    push      bc                            ;[27f6] c5
                    ld        a,$2f                         ;[27f7] 3e 2f
                    call      $3f00                         ;[27f9] cd 00 3f
                    jr        nz,$2838                      ;[27fc] 20 3a
                    pop       bc                            ;[27fe] c1
                    ld        hl,$0013                      ;[27ff] 21 13 00
                    add       hl,sp                         ;[2802] 39
                    ld        e,(hl)                        ;[2803] 5e
                    inc       hl                            ;[2804] 23
                    ld        d,(hl)                        ;[2805] 56
                    inc       hl                            ;[2806] 23
                    ld        a,(hl)                        ;[2807] 7e
                    inc       hl                            ;[2808] 23
                    ld        h,(hl)                        ;[2809] 66
                    ld        l,a                           ;[280a] 6f
                    jp        (ix)                          ;[280b] dd e9
                    push      hl                            ;[280d] e5
                    push      de                            ;[280e] d5
                    push      bc                            ;[280f] c5
                    push      af                            ;[2810] f5
                    ld        hl,$000b                      ;[2811] 21 0b 00
                    add       hl,sp                         ;[2814] 39
                    call      $3f00                         ;[2815] cd 00 3f
                    xor       b                             ;[2818] a8
                    ld        a,($2323)                     ;[2819] 3a 23 23
                    ld        e,(hl)                        ;[281c] 5e
                    inc       hl                            ;[281d] 23
                    ld        d,(hl)                        ;[281e] 56
                    inc       hl                            ;[281f] 23
                    ld        ($5b58),de                    ;[2820] ed 53 58 5b
                    ld        e,(hl)                        ;[2824] 5e
                    inc       hl                            ;[2825] 23
                    ld        d,(hl)                        ;[2826] 56
                    inc       hl                            ;[2827] 23
                    ld        ($5b6c),de                    ;[2828] ed 53 6c 5b
                    ld        e,(hl)                        ;[282c] 5e
                    inc       hl                            ;[282d] 23
                    ld        d,(hl)                        ;[282e] 56
                    inc       hl                            ;[282f] 23
                    ld        ($5c3d),de                    ;[2830] ed 53 3d 5c
                    add       hl,$0004                      ;[2834] ed 34 04 00
                    pop       af                            ;[2838] f1
                    pop       bc                            ;[2839] c1
                    pop       de                            ;[283a] d1
                    ex        (sp),hl                       ;[283b] e3
                    exx                                     ;[283c] d9
                    pop       hl                            ;[283d] e1
                    pop       ix                            ;[283e] dd e1
                    ld        sp,hl                         ;[2840] f9
                    exx                                     ;[2841] d9
                    set       7,(iy+$01)                    ;[2842] fd cb 01 fe
                    jp        (ix)                          ;[2846] dd e9
                    push      hl                            ;[2848] e5
                    call      $021d                         ;[2849] cd 1d 02
                    pop       hl                            ;[284c] e1
                    ret       nc                            ;[284d] d0
                    call      $291b                         ;[284e] cd 1b 29
                    ret       nz                            ;[2851] c0
                    ld        de,$f700                      ;[2852] 11 00 f7
                    call      $28a5                         ;[2855] cd a5 28
                    call      c,$021d                       ;[2858] dc 1d 02
                    scf                                     ;[285b] 37
                    ret                                     ;[285c] c9

                    push      hl                            ;[285d] e5
                    push      de                            ;[285e] d5
                    ld        de,$f700                      ;[285f] 11 00 f7
                    call      $28ab                         ;[2862] cd ab 28
                    pop       hl                            ;[2865] e1
                    push      hl                            ;[2866] e5
                    push      de                            ;[2867] d5
                    ld        de,$fb00                      ;[2868] 11 00 fb
                    call      c,$28ab                       ;[286b] dc ab 28
                    pop       hl                            ;[286e] e1
                    dec       hl                            ;[286f] 2b
                    ld        a,(hl)                        ;[2870] 7e
                    cp        $2f                           ;[2871] fe 2f
                    jr        nz,$2882                      ;[2873] 20 0d
                    ld        (hl),$ff                      ;[2875] 36 ff
                    ex        de,hl                         ;[2877] eb
                    dec       hl                            ;[2878] 2b
                    ld        a,(hl)                        ;[2879] 7e
                    cp        $7d                           ;[287a] fe 7d
                    jr        nz,$2882                      ;[287c] 20 04
                    dec       hl                            ;[287e] 2b
                    dec       hl                            ;[287f] 2b
                    ld        (hl),$ff                      ;[2880] 36 ff
                    pop       de                            ;[2882] d1
                    pop       hl                            ;[2883] e1
                    push      af                            ;[2884] f5
                    call      $0598                         ;[2885] cd 98 05
                    pop       bc                            ;[2888] c1
                    ret       nc                            ;[2889] d0
                    call      $291b                         ;[288a] cd 1b 29
                    ret       nz                            ;[288d] c0
                    push      bc                            ;[288e] c5
                    ld        hl,$fb00                      ;[288f] 21 00 fb
                    call      $2928                         ;[2892] cd 28 29
                    pop       af                            ;[2895] f1
                    push      hl                            ;[2896] e5
                    ld        a,$03                         ;[2897] 3e 03
                    call      z,$01b1                       ;[2899] cc b1 01
                    pop       de                            ;[289c] d1
                    ld        hl,$f700                      ;[289d] 21 00 f7
                    call      $0598                         ;[28a0] cd 98 05
                    scf                                     ;[28a3] 37
                    ret                                     ;[28a4] c9

                    ld        de,$f700                      ;[28a5] 11 00 f7
                    and       a                             ;[28a8] a7
                    jr        $28c4                         ;[28a9] 18 19
                    push      de                            ;[28ab] d5
                    push      hl                            ;[28ac] e5
                    rst       $08                           ;[28ad] cf
                    nop                                     ;[28ae] 00
                    ld        c,a                           ;[28af] 4f
                    inc       bc                            ;[28b0] 03
                    rst       $08                           ;[28b1] cf
                    nop                                     ;[28b2] 00
                    jr        $28b7                         ;[28b3] 18 02
                    pop       bc                            ;[28b5] c1
                    pop       de                            ;[28b6] d1
                    ret       nc                            ;[28b7] d0
                    push      de                            ;[28b8] d5
                    push      bc                            ;[28b9] c5
                    scf                                     ;[28ba] 37
                    jp        p,$28c2                       ;[28bb] f2 c2 28
                    rst       $08                           ;[28be] cf
                    nop                                     ;[28bf] 00
                    inc       sp                            ;[28c0] 33
                    inc       e                             ;[28c1] 1c
                    pop       hl                            ;[28c2] e1
                    pop       de                            ;[28c3] d1
                    push      de                            ;[28c4] d5
                    push      af                            ;[28c5] f5
                    push      hl                            ;[28c6] e5
                    ld        hl,$2956                      ;[28c7] 21 56 29
                    ld        bc,$0015                      ;[28ca] 01 15 00
                    ldir                                    ;[28cd] ed b0
                    pop       hl                            ;[28cf] e1
                    push      de                            ;[28d0] d5
                    push      hl                            ;[28d1] e5
                    ld        a,(hl)                        ;[28d2] 7e
                    ld        (de),a                        ;[28d3] 12
                    inc       hl                            ;[28d4] 23
                    inc       de                            ;[28d5] 13
                    cp        $3a                           ;[28d6] fe 3a
                    jr        z,$28e2                       ;[28d8] 28 08
                    cp        $2f                           ;[28da] fe 2f
                    jr        z,$28e2                       ;[28dc] 28 04
                    cp        $5c                           ;[28de] fe 5c
                    jr        nz,$28e4                      ;[28e0] 20 02
                    pop       bc                            ;[28e2] c1
                    push      hl                            ;[28e3] e5
                    inc       a                             ;[28e4] 3c
                    jr        nz,$28d2                      ;[28e5] 20 eb
                    pop       hl                            ;[28e7] e1
                    ex        (sp),hl                       ;[28e8] e3
                    push      hl                            ;[28e9] e5
                    ld        a,$01                         ;[28ea] 3e 01
                    call      $01b1                         ;[28ec] cd b1 01
                    pop       hl                            ;[28ef] e1
                    pop       de                            ;[28f0] d1
                    jr        nc,$2917                      ;[28f1] 30 24
                    ld        a,(hl)                        ;[28f3] 7e
                    cp        $3a                           ;[28f4] fe 3a
                    jr        nz,$28fa                      ;[28f6] 20 02
                    ld        (hl),$5f                      ;[28f8] 36 5f
                    inc       hl                            ;[28fa] 23
                    inc       a                             ;[28fb] 3c
                    jr        nz,$28f3                      ;[28fc] 20 f5
                    dec       hl                            ;[28fe] 2b
                    pop       af                            ;[28ff] f1
                    push      af                            ;[2900] f5
                    jr        c,$2916                       ;[2901] 38 13
                    ld        a,(de)                        ;[2903] 1a
                    ld        (hl),a                        ;[2904] 77
                    inc       hl                            ;[2905] 23
                    inc       de                            ;[2906] 13
                    inc       a                             ;[2907] 3c
                    jr        nz,$2903                      ;[2908] 20 f9
                    dec       hl                            ;[290a] 2b
                    ld        (hl),$2e                      ;[290b] 36 2e
                    inc       hl                            ;[290d] 23
                    ld        (hl),$7b                      ;[290e] 36 7b
                    inc       hl                            ;[2910] 23
                    ld        (hl),$7d                      ;[2911] 36 7d
                    inc       hl                            ;[2913] 23
                    ld        (hl),$ff                      ;[2914] 36 ff
                    scf                                     ;[2916] 37
                    ex        de,hl                         ;[2917] eb
                    pop       bc                            ;[2918] c1
                    pop       hl                            ;[2919] e1
                    ret                                     ;[291a] c9

                    push      hl                            ;[291b] e5
                    ld        hl,$314a                      ;[291c] 21 4a 31
                    rst       $08                           ;[291f] cf
                    rlca                                    ;[2920] 07
                    dec       e                             ;[2921] 1d
                    nop                                     ;[2922] 00
                    pop       hl                            ;[2923] e1
                    and       $04                           ;[2924] e6 04
                    scf                                     ;[2926] 37
                    ret                                     ;[2927] c9

                    call      $291b                         ;[2928] cd 1b 29
                    ret       nz                            ;[292b] c0
                    push      hl                            ;[292c] e5
                    inc       hl                            ;[292d] 23
                    inc       hl                            ;[292e] 23
                    inc       hl                            ;[292f] 23
                    ld        a,(hl)                        ;[2930] 7e
                    cp        $2f                           ;[2931] fe 2f
                    jr        z,$2940                       ;[2933] 28 0b
                    cp        $5c                           ;[2935] fe 5c
                    jr        z,$2940                       ;[2937] 28 07
                    inc       hl                            ;[2939] 23
                    inc       a                             ;[293a] 3c
                    jr        nz,$2930                      ;[293b] 20 f3
                    pop       hl                            ;[293d] e1
                    scf                                     ;[293e] 37
                    ret                                     ;[293f] c9

                    ld        (hl),$ff                      ;[2940] 36 ff
                    ex        (sp),hl                       ;[2942] e3
                    push      hl                            ;[2943] e5
                    ld        a,$02                         ;[2944] 3e 02
                    call      $01b1                         ;[2946] cd b1 01
                    pop       hl                            ;[2949] e1
                    ex        (sp),hl                       ;[294a] e3
                    ld        (hl),$2f                      ;[294b] 36 2f
                    jr        c,$2939                       ;[294d] 38 ea
                    cp        $18                           ;[294f] fe 18
                    jr        z,$2939                       ;[2951] 28 e6
                    pop       de                            ;[2953] d1
                    and       a                             ;[2954] a7
                    ret                                     ;[2955] c9

                    ld        b,e                           ;[2956] 43
                    ld        a,($4e2f)                     ;[2957] 3a 2f 4e
                    ld        b,l                           ;[295a] 45
                    ld        e,b                           ;[295b] 58
                    ld        d,h                           ;[295c] 54
                    ld        e,d                           ;[295d] 5a
                    ld        e,b                           ;[295e] 58
                    ld        c,a                           ;[295f] 4f
                    ld        d,e                           ;[2960] 53
                    cpl                                     ;[2961] 2f
                    ld        c,l                           ;[2962] 4d
                    ld        b,l                           ;[2963] 45
                    ld        d,h                           ;[2964] 54
                    ld        b,c                           ;[2965] 41
                    ld        b,h                           ;[2966] 44
                    ld        b,c                           ;[2967] 41
                    ld        d,h                           ;[2968] 54
                    ld        b,c                           ;[2969] 41
                    cpl                                     ;[296a] 2f
                    nop                                     ;[296b] 00
                    nop                                     ;[296c] 00
                    nop                                     ;[296d] 00
                    nop                                     ;[296e] 00
                    nop                                     ;[296f] 00
                    nop                                     ;[2970] 00
                    nop                                     ;[2971] 00
                    nop                                     ;[2972] 00
                    nop                                     ;[2973] 00
                    nop                                     ;[2974] 00
                    nop                                     ;[2975] 00
                    nop                                     ;[2976] 00
                    nop                                     ;[2977] 00
                    nop                                     ;[2978] 00
                    nop                                     ;[2979] 00
                    nop                                     ;[297a] 00
                    nop                                     ;[297b] 00
                    nop                                     ;[297c] 00
                    nop                                     ;[297d] 00
                    nop                                     ;[297e] 00
                    nop                                     ;[297f] 00
                    nop                                     ;[2980] 00
                    nop                                     ;[2981] 00
                    nop                                     ;[2982] 00
                    nop                                     ;[2983] 00
                    nop                                     ;[2984] 00
                    nop                                     ;[2985] 00
                    nop                                     ;[2986] 00
                    nop                                     ;[2987] 00
                    nop                                     ;[2988] 00
                    nop                                     ;[2989] 00
                    nop                                     ;[298a] 00
                    nop                                     ;[298b] 00
                    nop                                     ;[298c] 00
                    nop                                     ;[298d] 00
                    nop                                     ;[298e] 00
                    nop                                     ;[298f] 00
                    nop                                     ;[2990] 00
                    nop                                     ;[2991] 00
                    nop                                     ;[2992] 00
                    nop                                     ;[2993] 00
                    nop                                     ;[2994] 00
                    nop                                     ;[2995] 00
                    nop                                     ;[2996] 00
                    nop                                     ;[2997] 00
                    nop                                     ;[2998] 00
                    nop                                     ;[2999] 00
                    nop                                     ;[299a] 00
                    nop                                     ;[299b] 00
                    nop                                     ;[299c] 00
                    nop                                     ;[299d] 00
                    nop                                     ;[299e] 00
                    nop                                     ;[299f] 00
                    nop                                     ;[29a0] 00
                    nop                                     ;[29a1] 00
                    nop                                     ;[29a2] 00
                    nop                                     ;[29a3] 00
                    nop                                     ;[29a4] 00
                    nop                                     ;[29a5] 00
                    nop                                     ;[29a6] 00
                    nop                                     ;[29a7] 00
                    nop                                     ;[29a8] 00
                    nop                                     ;[29a9] 00
                    nop                                     ;[29aa] 00
                    nop                                     ;[29ab] 00
                    nop                                     ;[29ac] 00
                    nop                                     ;[29ad] 00
                    nop                                     ;[29ae] 00
                    nop                                     ;[29af] 00
                    nop                                     ;[29b0] 00
                    nop                                     ;[29b1] 00
                    nop                                     ;[29b2] 00
                    nop                                     ;[29b3] 00
                    nop                                     ;[29b4] 00
                    nop                                     ;[29b5] 00
                    nop                                     ;[29b6] 00
                    nop                                     ;[29b7] 00
                    nop                                     ;[29b8] 00
                    nop                                     ;[29b9] 00
                    nop                                     ;[29ba] 00
                    nop                                     ;[29bb] 00
                    nop                                     ;[29bc] 00
                    nop                                     ;[29bd] 00
                    nop                                     ;[29be] 00
                    nop                                     ;[29bf] 00
                    nop                                     ;[29c0] 00
                    nop                                     ;[29c1] 00
                    nop                                     ;[29c2] 00
                    nop                                     ;[29c3] 00
                    nop                                     ;[29c4] 00
                    nop                                     ;[29c5] 00
                    nop                                     ;[29c6] 00
                    nop                                     ;[29c7] 00
                    nop                                     ;[29c8] 00
                    nop                                     ;[29c9] 00
                    nop                                     ;[29ca] 00
                    nop                                     ;[29cb] 00
                    nop                                     ;[29cc] 00
                    nop                                     ;[29cd] 00
                    nop                                     ;[29ce] 00
                    nop                                     ;[29cf] 00
                    nop                                     ;[29d0] 00
                    nop                                     ;[29d1] 00
                    nop                                     ;[29d2] 00
                    nop                                     ;[29d3] 00
                    nop                                     ;[29d4] 00
                    nop                                     ;[29d5] 00
                    nop                                     ;[29d6] 00
                    nop                                     ;[29d7] 00
                    nop                                     ;[29d8] 00
                    nop                                     ;[29d9] 00
                    nop                                     ;[29da] 00
                    nop                                     ;[29db] 00
                    nop                                     ;[29dc] 00
                    nop                                     ;[29dd] 00
                    nop                                     ;[29de] 00
                    nop                                     ;[29df] 00
                    nop                                     ;[29e0] 00
                    nop                                     ;[29e1] 00
                    nop                                     ;[29e2] 00
                    nop                                     ;[29e3] 00
                    nop                                     ;[29e4] 00
                    nop                                     ;[29e5] 00
                    nop                                     ;[29e6] 00
                    nop                                     ;[29e7] 00
                    nop                                     ;[29e8] 00
                    nop                                     ;[29e9] 00
                    nop                                     ;[29ea] 00
                    nop                                     ;[29eb] 00
                    nop                                     ;[29ec] 00
                    nop                                     ;[29ed] 00
                    nop                                     ;[29ee] 00
                    nop                                     ;[29ef] 00
                    nop                                     ;[29f0] 00
                    nop                                     ;[29f1] 00
                    nop                                     ;[29f2] 00
                    nop                                     ;[29f3] 00
                    nop                                     ;[29f4] 00
                    nop                                     ;[29f5] 00
                    nop                                     ;[29f6] 00
                    nop                                     ;[29f7] 00
                    nop                                     ;[29f8] 00
                    nop                                     ;[29f9] 00
                    nop                                     ;[29fa] 00
                    nop                                     ;[29fb] 00
                    nop                                     ;[29fc] 00
                    nop                                     ;[29fd] 00
                    nop                                     ;[29fe] 00
                    nop                                     ;[29ff] 00
                    nop                                     ;[2a00] 00
                    nop                                     ;[2a01] 00
                    nop                                     ;[2a02] 00
                    nop                                     ;[2a03] 00
                    nop                                     ;[2a04] 00
                    nop                                     ;[2a05] 00
                    nop                                     ;[2a06] 00
                    nop                                     ;[2a07] 00
                    nop                                     ;[2a08] 00
                    nop                                     ;[2a09] 00
                    nop                                     ;[2a0a] 00
                    nop                                     ;[2a0b] 00
                    nop                                     ;[2a0c] 00
                    nop                                     ;[2a0d] 00
                    nop                                     ;[2a0e] 00
                    nop                                     ;[2a0f] 00
                    nop                                     ;[2a10] 00
                    nop                                     ;[2a11] 00
                    nop                                     ;[2a12] 00
                    nop                                     ;[2a13] 00
                    nop                                     ;[2a14] 00
                    nop                                     ;[2a15] 00
                    nop                                     ;[2a16] 00
                    nop                                     ;[2a17] 00
                    nop                                     ;[2a18] 00
                    nop                                     ;[2a19] 00
                    nop                                     ;[2a1a] 00
                    nop                                     ;[2a1b] 00
                    nop                                     ;[2a1c] 00
                    nop                                     ;[2a1d] 00
                    nop                                     ;[2a1e] 00
                    nop                                     ;[2a1f] 00
                    nop                                     ;[2a20] 00
                    nop                                     ;[2a21] 00
                    nop                                     ;[2a22] 00
                    nop                                     ;[2a23] 00
                    nop                                     ;[2a24] 00
                    nop                                     ;[2a25] 00
                    nop                                     ;[2a26] 00
                    nop                                     ;[2a27] 00
                    nop                                     ;[2a28] 00
                    nop                                     ;[2a29] 00
                    nop                                     ;[2a2a] 00
                    nop                                     ;[2a2b] 00
                    nop                                     ;[2a2c] 00
                    nop                                     ;[2a2d] 00
                    nop                                     ;[2a2e] 00
                    nop                                     ;[2a2f] 00
                    nop                                     ;[2a30] 00
                    nop                                     ;[2a31] 00
                    nop                                     ;[2a32] 00
                    nop                                     ;[2a33] 00
                    nop                                     ;[2a34] 00
                    nop                                     ;[2a35] 00
                    nop                                     ;[2a36] 00
                    nop                                     ;[2a37] 00
                    nop                                     ;[2a38] 00
                    nop                                     ;[2a39] 00
                    nop                                     ;[2a3a] 00
                    nop                                     ;[2a3b] 00
                    nop                                     ;[2a3c] 00
                    nop                                     ;[2a3d] 00
                    nop                                     ;[2a3e] 00
                    nop                                     ;[2a3f] 00
                    nop                                     ;[2a40] 00
                    nop                                     ;[2a41] 00
                    nop                                     ;[2a42] 00
                    nop                                     ;[2a43] 00
                    nop                                     ;[2a44] 00
                    nop                                     ;[2a45] 00
                    nop                                     ;[2a46] 00
                    nop                                     ;[2a47] 00
                    nop                                     ;[2a48] 00
                    nop                                     ;[2a49] 00
                    nop                                     ;[2a4a] 00
                    nop                                     ;[2a4b] 00
                    nop                                     ;[2a4c] 00
                    nop                                     ;[2a4d] 00
                    nop                                     ;[2a4e] 00
                    nop                                     ;[2a4f] 00
                    nop                                     ;[2a50] 00
                    nop                                     ;[2a51] 00
                    nop                                     ;[2a52] 00
                    nop                                     ;[2a53] 00
                    nop                                     ;[2a54] 00
                    nop                                     ;[2a55] 00
                    nop                                     ;[2a56] 00
                    nop                                     ;[2a57] 00
                    nop                                     ;[2a58] 00
                    nop                                     ;[2a59] 00
                    nop                                     ;[2a5a] 00
                    nop                                     ;[2a5b] 00
                    nop                                     ;[2a5c] 00
                    nop                                     ;[2a5d] 00
                    nop                                     ;[2a5e] 00
                    nop                                     ;[2a5f] 00
                    nop                                     ;[2a60] 00
                    nop                                     ;[2a61] 00
                    nop                                     ;[2a62] 00
                    nop                                     ;[2a63] 00
                    nop                                     ;[2a64] 00
                    nop                                     ;[2a65] 00
                    nop                                     ;[2a66] 00
                    nop                                     ;[2a67] 00
                    nop                                     ;[2a68] 00
                    nop                                     ;[2a69] 00
                    nop                                     ;[2a6a] 00
                    nop                                     ;[2a6b] 00
                    nop                                     ;[2a6c] 00
                    nop                                     ;[2a6d] 00
                    nop                                     ;[2a6e] 00
                    nop                                     ;[2a6f] 00
                    nop                                     ;[2a70] 00
                    nop                                     ;[2a71] 00
                    nop                                     ;[2a72] 00
                    nop                                     ;[2a73] 00
                    nop                                     ;[2a74] 00
                    nop                                     ;[2a75] 00
                    nop                                     ;[2a76] 00
                    nop                                     ;[2a77] 00
                    nop                                     ;[2a78] 00
                    nop                                     ;[2a79] 00
                    nop                                     ;[2a7a] 00
                    nop                                     ;[2a7b] 00
                    nop                                     ;[2a7c] 00
                    nop                                     ;[2a7d] 00
                    nop                                     ;[2a7e] 00
                    nop                                     ;[2a7f] 00
                    nop                                     ;[2a80] 00
                    nop                                     ;[2a81] 00
                    nop                                     ;[2a82] 00
                    nop                                     ;[2a83] 00
                    nop                                     ;[2a84] 00
                    nop                                     ;[2a85] 00
                    nop                                     ;[2a86] 00
                    nop                                     ;[2a87] 00
                    nop                                     ;[2a88] 00
                    nop                                     ;[2a89] 00
                    nop                                     ;[2a8a] 00
                    nop                                     ;[2a8b] 00
                    nop                                     ;[2a8c] 00
                    nop                                     ;[2a8d] 00
                    nop                                     ;[2a8e] 00
                    nop                                     ;[2a8f] 00
                    nop                                     ;[2a90] 00
                    nop                                     ;[2a91] 00
                    nop                                     ;[2a92] 00
                    nop                                     ;[2a93] 00
                    nop                                     ;[2a94] 00
                    nop                                     ;[2a95] 00
                    nop                                     ;[2a96] 00
                    nop                                     ;[2a97] 00
                    nop                                     ;[2a98] 00
                    nop                                     ;[2a99] 00
                    nop                                     ;[2a9a] 00
                    nop                                     ;[2a9b] 00
                    nop                                     ;[2a9c] 00
                    nop                                     ;[2a9d] 00
                    nop                                     ;[2a9e] 00
                    nop                                     ;[2a9f] 00
                    nop                                     ;[2aa0] 00
                    nop                                     ;[2aa1] 00
                    nop                                     ;[2aa2] 00
                    nop                                     ;[2aa3] 00
                    nop                                     ;[2aa4] 00
                    nop                                     ;[2aa5] 00
                    nop                                     ;[2aa6] 00
                    nop                                     ;[2aa7] 00
                    nop                                     ;[2aa8] 00
                    nop                                     ;[2aa9] 00
                    nop                                     ;[2aaa] 00
                    nop                                     ;[2aab] 00
                    nop                                     ;[2aac] 00
                    nop                                     ;[2aad] 00
                    nop                                     ;[2aae] 00
                    nop                                     ;[2aaf] 00
                    nop                                     ;[2ab0] 00
                    nop                                     ;[2ab1] 00
                    nop                                     ;[2ab2] 00
                    nop                                     ;[2ab3] 00
                    nop                                     ;[2ab4] 00
                    nop                                     ;[2ab5] 00
                    nop                                     ;[2ab6] 00
                    nop                                     ;[2ab7] 00
                    nop                                     ;[2ab8] 00
                    nop                                     ;[2ab9] 00
                    nop                                     ;[2aba] 00
                    nop                                     ;[2abb] 00
                    nop                                     ;[2abc] 00
                    nop                                     ;[2abd] 00
                    nop                                     ;[2abe] 00
                    nop                                     ;[2abf] 00
                    nop                                     ;[2ac0] 00
                    nop                                     ;[2ac1] 00
                    nop                                     ;[2ac2] 00
                    nop                                     ;[2ac3] 00
                    nop                                     ;[2ac4] 00
                    nop                                     ;[2ac5] 00
                    nop                                     ;[2ac6] 00
                    nop                                     ;[2ac7] 00
                    nop                                     ;[2ac8] 00
                    nop                                     ;[2ac9] 00
                    nop                                     ;[2aca] 00
                    nop                                     ;[2acb] 00
                    nop                                     ;[2acc] 00
                    nop                                     ;[2acd] 00
                    nop                                     ;[2ace] 00
                    nop                                     ;[2acf] 00
                    nop                                     ;[2ad0] 00
                    nop                                     ;[2ad1] 00
                    nop                                     ;[2ad2] 00
                    nop                                     ;[2ad3] 00
                    nop                                     ;[2ad4] 00
                    nop                                     ;[2ad5] 00
                    nop                                     ;[2ad6] 00
                    nop                                     ;[2ad7] 00
                    nop                                     ;[2ad8] 00
                    nop                                     ;[2ad9] 00
                    nop                                     ;[2ada] 00
                    nop                                     ;[2adb] 00
                    nop                                     ;[2adc] 00
                    nop                                     ;[2add] 00
                    nop                                     ;[2ade] 00
                    nop                                     ;[2adf] 00
                    nop                                     ;[2ae0] 00
                    nop                                     ;[2ae1] 00
                    nop                                     ;[2ae2] 00
                    nop                                     ;[2ae3] 00
                    nop                                     ;[2ae4] 00
                    nop                                     ;[2ae5] 00
                    nop                                     ;[2ae6] 00
                    nop                                     ;[2ae7] 00
                    nop                                     ;[2ae8] 00
                    nop                                     ;[2ae9] 00
                    nop                                     ;[2aea] 00
                    nop                                     ;[2aeb] 00
                    nop                                     ;[2aec] 00
                    nop                                     ;[2aed] 00
                    nop                                     ;[2aee] 00
                    nop                                     ;[2aef] 00
                    nop                                     ;[2af0] 00
                    nop                                     ;[2af1] 00
                    nop                                     ;[2af2] 00
                    nop                                     ;[2af3] 00
                    nop                                     ;[2af4] 00
                    nop                                     ;[2af5] 00
                    nop                                     ;[2af6] 00
                    nop                                     ;[2af7] 00
                    nop                                     ;[2af8] 00
                    nop                                     ;[2af9] 00
                    nop                                     ;[2afa] 00
                    nop                                     ;[2afb] 00
                    nop                                     ;[2afc] 00
                    nop                                     ;[2afd] 00
                    nop                                     ;[2afe] 00
                    nop                                     ;[2aff] 00
                    nop                                     ;[2b00] 00
                    nop                                     ;[2b01] 00
                    nop                                     ;[2b02] 00
                    nop                                     ;[2b03] 00
                    nop                                     ;[2b04] 00
                    nop                                     ;[2b05] 00
                    nop                                     ;[2b06] 00
                    nop                                     ;[2b07] 00
                    nop                                     ;[2b08] 00
                    nop                                     ;[2b09] 00
                    nop                                     ;[2b0a] 00
                    nop                                     ;[2b0b] 00
                    nop                                     ;[2b0c] 00
                    nop                                     ;[2b0d] 00
                    nop                                     ;[2b0e] 00
                    nop                                     ;[2b0f] 00
                    nop                                     ;[2b10] 00
                    nop                                     ;[2b11] 00
                    nop                                     ;[2b12] 00
                    nop                                     ;[2b13] 00
                    nop                                     ;[2b14] 00
                    nop                                     ;[2b15] 00
                    nop                                     ;[2b16] 00
                    nop                                     ;[2b17] 00
                    nop                                     ;[2b18] 00
                    nop                                     ;[2b19] 00
                    nop                                     ;[2b1a] 00
                    nop                                     ;[2b1b] 00
                    nop                                     ;[2b1c] 00
                    nop                                     ;[2b1d] 00
                    nop                                     ;[2b1e] 00
                    nop                                     ;[2b1f] 00
                    nop                                     ;[2b20] 00
                    nop                                     ;[2b21] 00
                    nop                                     ;[2b22] 00
                    nop                                     ;[2b23] 00
                    nop                                     ;[2b24] 00
                    nop                                     ;[2b25] 00
                    nop                                     ;[2b26] 00
                    nop                                     ;[2b27] 00
                    nop                                     ;[2b28] 00
                    nop                                     ;[2b29] 00
                    nop                                     ;[2b2a] 00
                    nop                                     ;[2b2b] 00
                    nop                                     ;[2b2c] 00
                    nop                                     ;[2b2d] 00
                    nop                                     ;[2b2e] 00
                    nop                                     ;[2b2f] 00
                    nop                                     ;[2b30] 00
                    nop                                     ;[2b31] 00
                    nop                                     ;[2b32] 00
                    nop                                     ;[2b33] 00
                    nop                                     ;[2b34] 00
                    nop                                     ;[2b35] 00
                    nop                                     ;[2b36] 00
                    nop                                     ;[2b37] 00
                    nop                                     ;[2b38] 00
                    nop                                     ;[2b39] 00
                    nop                                     ;[2b3a] 00
                    nop                                     ;[2b3b] 00
                    nop                                     ;[2b3c] 00
                    nop                                     ;[2b3d] 00
                    nop                                     ;[2b3e] 00
                    nop                                     ;[2b3f] 00
                    nop                                     ;[2b40] 00
                    nop                                     ;[2b41] 00
                    nop                                     ;[2b42] 00
                    nop                                     ;[2b43] 00
                    nop                                     ;[2b44] 00
                    nop                                     ;[2b45] 00
                    nop                                     ;[2b46] 00
                    nop                                     ;[2b47] 00
                    nop                                     ;[2b48] 00
                    nop                                     ;[2b49] 00
                    nop                                     ;[2b4a] 00
                    nop                                     ;[2b4b] 00
                    nop                                     ;[2b4c] 00
                    nop                                     ;[2b4d] 00
                    nop                                     ;[2b4e] 00
                    nop                                     ;[2b4f] 00
                    nop                                     ;[2b50] 00
                    nop                                     ;[2b51] 00
                    nop                                     ;[2b52] 00
                    nop                                     ;[2b53] 00
                    nop                                     ;[2b54] 00
                    nop                                     ;[2b55] 00
                    nop                                     ;[2b56] 00
                    nop                                     ;[2b57] 00
                    nop                                     ;[2b58] 00
                    nop                                     ;[2b59] 00
                    nop                                     ;[2b5a] 00
                    nop                                     ;[2b5b] 00
                    nop                                     ;[2b5c] 00
                    nop                                     ;[2b5d] 00
                    nop                                     ;[2b5e] 00
                    nop                                     ;[2b5f] 00
                    nop                                     ;[2b60] 00
                    nop                                     ;[2b61] 00
                    nop                                     ;[2b62] 00
                    nop                                     ;[2b63] 00
                    nop                                     ;[2b64] 00
                    nop                                     ;[2b65] 00
                    nop                                     ;[2b66] 00
                    nop                                     ;[2b67] 00
                    nop                                     ;[2b68] 00
                    nop                                     ;[2b69] 00
                    nop                                     ;[2b6a] 00
                    nop                                     ;[2b6b] 00
                    nop                                     ;[2b6c] 00
                    nop                                     ;[2b6d] 00
                    nop                                     ;[2b6e] 00
                    nop                                     ;[2b6f] 00
                    nop                                     ;[2b70] 00
                    nop                                     ;[2b71] 00
                    nop                                     ;[2b72] 00
                    nop                                     ;[2b73] 00
                    nop                                     ;[2b74] 00
                    nop                                     ;[2b75] 00
                    nop                                     ;[2b76] 00
                    nop                                     ;[2b77] 00
                    nop                                     ;[2b78] 00
                    nop                                     ;[2b79] 00
                    nop                                     ;[2b7a] 00
                    nop                                     ;[2b7b] 00
                    nop                                     ;[2b7c] 00
                    nop                                     ;[2b7d] 00
                    nop                                     ;[2b7e] 00
                    nop                                     ;[2b7f] 00
                    nop                                     ;[2b80] 00
                    nop                                     ;[2b81] 00
                    nop                                     ;[2b82] 00
                    nop                                     ;[2b83] 00
                    nop                                     ;[2b84] 00
                    nop                                     ;[2b85] 00
                    nop                                     ;[2b86] 00
                    nop                                     ;[2b87] 00
                    nop                                     ;[2b88] 00
                    nop                                     ;[2b89] 00
                    nop                                     ;[2b8a] 00
                    nop                                     ;[2b8b] 00
                    nop                                     ;[2b8c] 00
                    nop                                     ;[2b8d] 00
                    nop                                     ;[2b8e] 00
                    nop                                     ;[2b8f] 00
                    nop                                     ;[2b90] 00
                    nop                                     ;[2b91] 00
                    nop                                     ;[2b92] 00
                    nop                                     ;[2b93] 00
                    nop                                     ;[2b94] 00
                    nop                                     ;[2b95] 00
                    nop                                     ;[2b96] 00
                    nop                                     ;[2b97] 00
                    nop                                     ;[2b98] 00
                    nop                                     ;[2b99] 00
                    nop                                     ;[2b9a] 00
                    nop                                     ;[2b9b] 00
                    nop                                     ;[2b9c] 00
                    nop                                     ;[2b9d] 00
                    nop                                     ;[2b9e] 00
                    nop                                     ;[2b9f] 00
                    nop                                     ;[2ba0] 00
                    nop                                     ;[2ba1] 00
                    nop                                     ;[2ba2] 00
                    nop                                     ;[2ba3] 00
                    nop                                     ;[2ba4] 00
                    nop                                     ;[2ba5] 00
                    nop                                     ;[2ba6] 00
                    nop                                     ;[2ba7] 00
                    nop                                     ;[2ba8] 00
                    nop                                     ;[2ba9] 00
                    nop                                     ;[2baa] 00
                    nop                                     ;[2bab] 00
                    nop                                     ;[2bac] 00
                    nop                                     ;[2bad] 00
                    nop                                     ;[2bae] 00
                    nop                                     ;[2baf] 00
                    nop                                     ;[2bb0] 00
                    nop                                     ;[2bb1] 00
                    nop                                     ;[2bb2] 00
                    nop                                     ;[2bb3] 00
                    nop                                     ;[2bb4] 00
                    nop                                     ;[2bb5] 00
                    nop                                     ;[2bb6] 00
                    nop                                     ;[2bb7] 00
                    nop                                     ;[2bb8] 00
                    nop                                     ;[2bb9] 00
                    nop                                     ;[2bba] 00
                    nop                                     ;[2bbb] 00
                    nop                                     ;[2bbc] 00
                    nop                                     ;[2bbd] 00
                    nop                                     ;[2bbe] 00
                    nop                                     ;[2bbf] 00
                    nop                                     ;[2bc0] 00
                    nop                                     ;[2bc1] 00
                    nop                                     ;[2bc2] 00
                    nop                                     ;[2bc3] 00
                    nop                                     ;[2bc4] 00
                    nop                                     ;[2bc5] 00
                    nop                                     ;[2bc6] 00
                    nop                                     ;[2bc7] 00
                    nop                                     ;[2bc8] 00
                    nop                                     ;[2bc9] 00
                    nop                                     ;[2bca] 00
                    nop                                     ;[2bcb] 00
                    nop                                     ;[2bcc] 00
                    nop                                     ;[2bcd] 00
                    nop                                     ;[2bce] 00
                    nop                                     ;[2bcf] 00
                    nop                                     ;[2bd0] 00
                    nop                                     ;[2bd1] 00
                    nop                                     ;[2bd2] 00
                    nop                                     ;[2bd3] 00
                    nop                                     ;[2bd4] 00
                    nop                                     ;[2bd5] 00
                    nop                                     ;[2bd6] 00
                    nop                                     ;[2bd7] 00
                    nop                                     ;[2bd8] 00
                    nop                                     ;[2bd9] 00
                    nop                                     ;[2bda] 00
                    nop                                     ;[2bdb] 00
                    nop                                     ;[2bdc] 00
                    nop                                     ;[2bdd] 00
                    nop                                     ;[2bde] 00
                    nop                                     ;[2bdf] 00
                    nop                                     ;[2be0] 00
                    nop                                     ;[2be1] 00
                    nop                                     ;[2be2] 00
                    nop                                     ;[2be3] 00
                    nop                                     ;[2be4] 00
                    nop                                     ;[2be5] 00
                    nop                                     ;[2be6] 00
                    nop                                     ;[2be7] 00
                    nop                                     ;[2be8] 00
                    nop                                     ;[2be9] 00
                    nop                                     ;[2bea] 00
                    nop                                     ;[2beb] 00
                    nop                                     ;[2bec] 00
                    nop                                     ;[2bed] 00
                    nop                                     ;[2bee] 00
                    nop                                     ;[2bef] 00
                    nop                                     ;[2bf0] 00
                    nop                                     ;[2bf1] 00
                    nop                                     ;[2bf2] 00
                    nop                                     ;[2bf3] 00
                    nop                                     ;[2bf4] 00
                    nop                                     ;[2bf5] 00
                    nop                                     ;[2bf6] 00
                    nop                                     ;[2bf7] 00
                    nop                                     ;[2bf8] 00
                    nop                                     ;[2bf9] 00
                    nop                                     ;[2bfa] 00
                    nop                                     ;[2bfb] 00
                    nop                                     ;[2bfc] 00
                    nop                                     ;[2bfd] 00
                    nop                                     ;[2bfe] 00
                    nop                                     ;[2bff] 00
                    nop                                     ;[2c00] 00
                    nop                                     ;[2c01] 00
                    nop                                     ;[2c02] 00
                    nop                                     ;[2c03] 00
                    nop                                     ;[2c04] 00
                    nop                                     ;[2c05] 00
                    nop                                     ;[2c06] 00
                    nop                                     ;[2c07] 00
                    nop                                     ;[2c08] 00
                    nop                                     ;[2c09] 00
                    nop                                     ;[2c0a] 00
                    nop                                     ;[2c0b] 00
                    nop                                     ;[2c0c] 00
                    nop                                     ;[2c0d] 00
                    nop                                     ;[2c0e] 00
                    nop                                     ;[2c0f] 00
                    nop                                     ;[2c10] 00
                    nop                                     ;[2c11] 00
                    nop                                     ;[2c12] 00
                    nop                                     ;[2c13] 00
                    nop                                     ;[2c14] 00
                    nop                                     ;[2c15] 00
                    nop                                     ;[2c16] 00
                    nop                                     ;[2c17] 00
                    nop                                     ;[2c18] 00
                    nop                                     ;[2c19] 00
                    nop                                     ;[2c1a] 00
                    nop                                     ;[2c1b] 00
                    nop                                     ;[2c1c] 00
                    nop                                     ;[2c1d] 00
                    nop                                     ;[2c1e] 00
                    nop                                     ;[2c1f] 00
                    nop                                     ;[2c20] 00
                    nop                                     ;[2c21] 00
                    nop                                     ;[2c22] 00
                    nop                                     ;[2c23] 00
                    nop                                     ;[2c24] 00
                    nop                                     ;[2c25] 00
                    nop                                     ;[2c26] 00
                    nop                                     ;[2c27] 00
                    nop                                     ;[2c28] 00
                    nop                                     ;[2c29] 00
                    nop                                     ;[2c2a] 00
                    nop                                     ;[2c2b] 00
                    nop                                     ;[2c2c] 00
                    nop                                     ;[2c2d] 00
                    nop                                     ;[2c2e] 00
                    nop                                     ;[2c2f] 00
                    nop                                     ;[2c30] 00
                    nop                                     ;[2c31] 00
                    nop                                     ;[2c32] 00
                    nop                                     ;[2c33] 00
                    nop                                     ;[2c34] 00
                    nop                                     ;[2c35] 00
                    nop                                     ;[2c36] 00
                    nop                                     ;[2c37] 00
                    nop                                     ;[2c38] 00
                    nop                                     ;[2c39] 00
                    nop                                     ;[2c3a] 00
                    nop                                     ;[2c3b] 00
                    nop                                     ;[2c3c] 00
                    nop                                     ;[2c3d] 00
                    nop                                     ;[2c3e] 00
                    nop                                     ;[2c3f] 00
                    nop                                     ;[2c40] 00
                    nop                                     ;[2c41] 00
                    nop                                     ;[2c42] 00
                    nop                                     ;[2c43] 00
                    nop                                     ;[2c44] 00
                    nop                                     ;[2c45] 00
                    nop                                     ;[2c46] 00
                    nop                                     ;[2c47] 00
                    nop                                     ;[2c48] 00
                    nop                                     ;[2c49] 00
                    nop                                     ;[2c4a] 00
                    nop                                     ;[2c4b] 00
                    nop                                     ;[2c4c] 00
                    nop                                     ;[2c4d] 00
                    nop                                     ;[2c4e] 00
                    nop                                     ;[2c4f] 00
                    nop                                     ;[2c50] 00
                    nop                                     ;[2c51] 00
                    nop                                     ;[2c52] 00
                    nop                                     ;[2c53] 00
                    nop                                     ;[2c54] 00
                    nop                                     ;[2c55] 00
                    nop                                     ;[2c56] 00
                    nop                                     ;[2c57] 00
                    nop                                     ;[2c58] 00
                    nop                                     ;[2c59] 00
                    nop                                     ;[2c5a] 00
                    nop                                     ;[2c5b] 00
                    nop                                     ;[2c5c] 00
                    nop                                     ;[2c5d] 00
                    nop                                     ;[2c5e] 00
                    nop                                     ;[2c5f] 00
                    nop                                     ;[2c60] 00
                    nop                                     ;[2c61] 00
                    nop                                     ;[2c62] 00
                    nop                                     ;[2c63] 00
                    nop                                     ;[2c64] 00
                    nop                                     ;[2c65] 00
                    nop                                     ;[2c66] 00
                    nop                                     ;[2c67] 00
                    nop                                     ;[2c68] 00
                    nop                                     ;[2c69] 00
                    nop                                     ;[2c6a] 00
                    nop                                     ;[2c6b] 00
                    nop                                     ;[2c6c] 00
                    nop                                     ;[2c6d] 00
                    nop                                     ;[2c6e] 00
                    nop                                     ;[2c6f] 00
                    nop                                     ;[2c70] 00
                    nop                                     ;[2c71] 00
                    nop                                     ;[2c72] 00
                    nop                                     ;[2c73] 00
                    nop                                     ;[2c74] 00
                    nop                                     ;[2c75] 00
                    nop                                     ;[2c76] 00
                    nop                                     ;[2c77] 00
                    nop                                     ;[2c78] 00
                    nop                                     ;[2c79] 00
                    nop                                     ;[2c7a] 00
                    nop                                     ;[2c7b] 00
                    nop                                     ;[2c7c] 00
                    nop                                     ;[2c7d] 00
                    nop                                     ;[2c7e] 00
                    nop                                     ;[2c7f] 00
                    nop                                     ;[2c80] 00
                    nop                                     ;[2c81] 00
                    nop                                     ;[2c82] 00
                    nop                                     ;[2c83] 00
                    nop                                     ;[2c84] 00
                    nop                                     ;[2c85] 00
                    nop                                     ;[2c86] 00
                    nop                                     ;[2c87] 00
                    nop                                     ;[2c88] 00
                    nop                                     ;[2c89] 00
                    nop                                     ;[2c8a] 00
                    nop                                     ;[2c8b] 00
                    nop                                     ;[2c8c] 00
                    nop                                     ;[2c8d] 00
                    nop                                     ;[2c8e] 00
                    nop                                     ;[2c8f] 00
                    nop                                     ;[2c90] 00
                    nop                                     ;[2c91] 00
                    nop                                     ;[2c92] 00
                    nop                                     ;[2c93] 00
                    nop                                     ;[2c94] 00
                    nop                                     ;[2c95] 00
                    nop                                     ;[2c96] 00
                    nop                                     ;[2c97] 00
                    nop                                     ;[2c98] 00
                    nop                                     ;[2c99] 00
                    nop                                     ;[2c9a] 00
                    nop                                     ;[2c9b] 00
                    nop                                     ;[2c9c] 00
                    nop                                     ;[2c9d] 00
                    nop                                     ;[2c9e] 00
                    nop                                     ;[2c9f] 00
                    nop                                     ;[2ca0] 00
                    nop                                     ;[2ca1] 00
                    nop                                     ;[2ca2] 00
                    nop                                     ;[2ca3] 00
                    nop                                     ;[2ca4] 00
                    nop                                     ;[2ca5] 00
                    nop                                     ;[2ca6] 00
                    nop                                     ;[2ca7] 00
                    nop                                     ;[2ca8] 00
                    nop                                     ;[2ca9] 00
                    nop                                     ;[2caa] 00
                    nop                                     ;[2cab] 00
                    nop                                     ;[2cac] 00
                    nop                                     ;[2cad] 00
                    nop                                     ;[2cae] 00
                    nop                                     ;[2caf] 00
                    nop                                     ;[2cb0] 00
                    nop                                     ;[2cb1] 00
                    nop                                     ;[2cb2] 00
                    nop                                     ;[2cb3] 00
                    nop                                     ;[2cb4] 00
                    nop                                     ;[2cb5] 00
                    nop                                     ;[2cb6] 00
                    nop                                     ;[2cb7] 00
                    nop                                     ;[2cb8] 00
                    nop                                     ;[2cb9] 00
                    nop                                     ;[2cba] 00
                    nop                                     ;[2cbb] 00
                    nop                                     ;[2cbc] 00
                    nop                                     ;[2cbd] 00
                    nop                                     ;[2cbe] 00
                    nop                                     ;[2cbf] 00
                    nop                                     ;[2cc0] 00
                    nop                                     ;[2cc1] 00
                    nop                                     ;[2cc2] 00
                    nop                                     ;[2cc3] 00
                    nop                                     ;[2cc4] 00
                    nop                                     ;[2cc5] 00
                    nop                                     ;[2cc6] 00
                    nop                                     ;[2cc7] 00
                    nop                                     ;[2cc8] 00
                    nop                                     ;[2cc9] 00
                    nop                                     ;[2cca] 00
                    nop                                     ;[2ccb] 00
                    nop                                     ;[2ccc] 00
                    nop                                     ;[2ccd] 00
                    nop                                     ;[2cce] 00
                    nop                                     ;[2ccf] 00
                    nop                                     ;[2cd0] 00
                    nop                                     ;[2cd1] 00
                    nop                                     ;[2cd2] 00
                    nop                                     ;[2cd3] 00
                    nop                                     ;[2cd4] 00
                    nop                                     ;[2cd5] 00
                    nop                                     ;[2cd6] 00
                    nop                                     ;[2cd7] 00
                    nop                                     ;[2cd8] 00
                    nop                                     ;[2cd9] 00
                    nop                                     ;[2cda] 00
                    nop                                     ;[2cdb] 00
                    nop                                     ;[2cdc] 00
                    nop                                     ;[2cdd] 00
                    nop                                     ;[2cde] 00
                    nop                                     ;[2cdf] 00
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
                    nextreg $8e,$03                         ;[3cfc] ed 91 8e 03
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
                    nextreg $8e,$00                         ;[3e13] ed 91 8e 00
                    ret                                     ;[3e17] c9

                    ld        ($5b54),bc                    ;[3e18] ed 43 54 5b
                    ex        (sp),hl                       ;[3e1c] e3
                    ld        c,(hl)                        ;[3e1d] 4e
                    inc       hl                            ;[3e1e] 23
                    ld        b,(hl)                        ;[3e1f] 46
                    inc       hl                            ;[3e20] 23
                    ex        (sp),hl                       ;[3e21] e3
                    push    $3e13                           ;[3e22] ed 8a 3e 13
                    push    $007b                           ;[3e26] ed 8a 00 7b
                    push      bc                            ;[3e2a] c5
                    push    $007b                           ;[3e2b] ed 8a 00 7b
                    jp        $3e0f                         ;[3e2f] c3 0f 3e
                    ld        ($5b54),bc                    ;[3e32] ed 43 54 5b
                    ex        (sp),hl                       ;[3e36] e3
                    ld        c,(hl)                        ;[3e37] 4e
                    inc       hl                            ;[3e38] 23
                    ld        b,(hl)                        ;[3e39] 46
                    inc       hl                            ;[3e3a] 23
                    ex        (sp),hl                       ;[3e3b] e3
                    push    $3f13                           ;[3e3c] ed 8a 3f 13
                    push    $007b                           ;[3e40] ed 8a 00 7b
                    push      bc                            ;[3e44] c5
                    push    $007b                           ;[3e45] ed 8a 00 7b
                    jp        $3f0f                         ;[3e49] c3 0f 3f
                    push      hl                            ;[3e4c] e5
                    push      bc                            ;[3e4d] c5
                    ld        l,$56                         ;[3e4e] 2e 56
                    ld        bc,$243b                      ;[3e50] 01 3b 24
                    out       (c),l                         ;[3e53] ed 69
                    inc       b                             ;[3e55] 04
                    in        l,(c)                         ;[3e56] ed 68
                    add       a                             ;[3e58] 87
                    nextreg $56,a                           ;[3e59] ed 92 56
                    inc       a                             ;[3e5c] 3c
                    nextreg $57,a                           ;[3e5d] ed 92 57
                    ld        a,l                           ;[3e60] 7d
                    srl       a                             ;[3e61] cb 3f
                    pop       bc                            ;[3e63] c1
                    pop       hl                            ;[3e64] e1
                    ret                                     ;[3e65] c9

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
                    ld        ($5b54),bc                    ;[3f00] ed 43 54 5b
                    ex        (sp),hl                       ;[3f04] e3
                    ld        c,(hl)                        ;[3f05] 4e
                    inc       hl                            ;[3f06] 23
                    ld        b,(hl)                        ;[3f07] 46
                    inc       hl                            ;[3f08] 23
                    ex        (sp),hl                       ;[3f09] e3
                    push    $3f13                           ;[3f0a] ed 8a 3f 13
                    push      bc                            ;[3f0e] c5
                    ld        bc,($5b54)                    ;[3f0f] ed 4b 54 5b
                    nextreg $8e,$01                         ;[3f13] ed 91 8e 01
                    ret                                     ;[3f17] c9

                    ld        ($5b52),hl                    ;[3f18] 22 52 5b
                    push      af                            ;[3f1b] f5
                    pop       hl                            ;[3f1c] e1
                    ld        ($5b56),hl                    ;[3f1d] 22 56 5b
                    pop       hl                            ;[3f20] e1
                    push      de                            ;[3f21] d5
                    ld        a,(hl)                        ;[3f22] 7e
                    inc       hl                            ;[3f23] 23
                    ld        e,(hl)                        ;[3f24] 5e
                    inc       hl                            ;[3f25] 23
                    ld        d,(hl)                        ;[3f26] 56
                    inc       hl                            ;[3f27] 23
                    ex        (sp),hl                       ;[3f28] e3
                    ex        de,hl                         ;[3f29] eb
                    push      hl                            ;[3f2a] e5
                    ld        h,a                           ;[3f2b] 67
                    ld        a,i                           ;[3f2c] ed 57
                    ld        a,h                           ;[3f2e] 7c
                    pop       hl                            ;[3f2f] e1
                    di                                      ;[3f30] f3
                    push      af                            ;[3f31] f5
                    push      iy                            ;[3f32] fd e5
                    push      af                            ;[3f34] f5
                    xor       a                             ;[3f35] af
                    out       ($e3),a                       ;[3f36] d3 e3
                    pop       af                            ;[3f38] f1
                    push    $0448                           ;[3f39] ed 8a 04 48
                    jp        $3cfc                         ;[3f3d] c3 fc 3c
                    pop       iy                            ;[3f40] fd e1
                    ex        (sp),hl                       ;[3f42] e3
                    push      af                            ;[3f43] f5
                    bit       2,l                           ;[3f44] cb 55
                    jr        z,$3f49                       ;[3f46] 28 01
                    ei                                      ;[3f48] fb
                    pop       af                            ;[3f49] f1
                    pop       hl                            ;[3f4a] e1
                    ret                                     ;[3f4b] c9

                    nop                                     ;[3f4c] 00
                    nop                                     ;[3f4d] 00
                    nop                                     ;[3f4e] 00
                    nop                                     ;[3f4f] 00
                    nop                                     ;[3f50] 00
                    nop                                     ;[3f51] 00
                    nop                                     ;[3f52] 00
                    nop                                     ;[3f53] 00
                    nop                                     ;[3f54] 00
                    nop                                     ;[3f55] 00
                    nop                                     ;[3f56] 00
                    nop                                     ;[3f57] 00
                    nop                                     ;[3f58] 00
                    nop                                     ;[3f59] 00
                    nop                                     ;[3f5a] 00
                    nop                                     ;[3f5b] 00
                    nop                                     ;[3f5c] 00
                    nop                                     ;[3f5d] 00
                    nop                                     ;[3f5e] 00
                    nop                                     ;[3f5f] 00
                    nop                                     ;[3f60] 00
                    nop                                     ;[3f61] 00
                    nop                                     ;[3f62] 00
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
                    nop                                     ;[3ff0] 00
                    nop                                     ;[3ff1] 00
                    nop                                     ;[3ff2] 00
                    nop                                     ;[3ff3] 00
                    nop                                     ;[3ff4] 00
                    nop                                     ;[3ff5] 00
                    nop                                     ;[3ff6] 00
                    nop                                     ;[3ff7] 00
                    nop                                     ;[3ff8] 00
                    nop                                     ;[3ff9] 00
                    nop                                     ;[3ffa] 00
                    nop                                     ;[3ffb] 00
                    nop                                     ;[3ffc] 00
                    nop                                     ;[3ffd] 00
                    nop                                     ;[3ffe] 00
                    nop                                     ;[3fff] 00
