                    di                                      ;[0000] f3
                    xor       a                             ;[0001] af
                    ld        bc,$243b                      ;[0002] 01 3b 24
                    jp        $3be8                         ;[0005] c3 e8 3b
                    ld        hl,($5c5d)                    ;[0008] 2a 5d 5c
                    ld        ($5c5f),hl                    ;[000b] 22 5f 5c
                    jr        $0053                         ;[000e] 18 43
                    jp        $15f2                         ;[0010] c3 f2 15
                    jp        (ix)                          ;[0013] dd e9
                    rst       $38                           ;[0015] ff
                    rst       $38                           ;[0016] ff
                    rst       $38                           ;[0017] ff
                    jp        $1555                         ;[0018] c3 55 15
                    ld        a,(hl)                        ;[001b] 7e
                    call      $007d                         ;[001c] cd 7d 00
                    ret       nc                            ;[001f] d0
                    jp        $155b                         ;[0020] c3 5b 15
                    jr        $001c                         ;[0023] 18 f7
                    rst       $38                           ;[0025] ff
                    rst       $38                           ;[0026] ff
                    rst       $38                           ;[0027] ff
                    jp        $335b                         ;[0028] c3 5b 33
                    rst       $38                           ;[002b] ff
                    rst       $38                           ;[002c] ff
                    rst       $38                           ;[002d] ff
                    rst       $38                           ;[002e] ff
                    rst       $38                           ;[002f] ff
                    push      bc                            ;[0030] c5
                    ld        hl,($5c61)                    ;[0031] 2a 61 5c
                    push      hl                            ;[0034] e5
                    jp        $169e                         ;[0035] c3 9e 16
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
                    call      $386e                         ;[004a] cd 6e 38
                    pop       de                            ;[004d] d1
                    pop       bc                            ;[004e] c1
                    pop       hl                            ;[004f] e1
                    pop       af                            ;[0050] f1
                    ei                                      ;[0051] fb
                    ret                                     ;[0052] c9

                    pop       hl                            ;[0053] e1
                    ld        l,(hl)                        ;[0054] 6e
                    ld        (iy+$00),l                    ;[0055] fd 75 00
                    ld        sp,($5c3d)                    ;[0058] ed 7b 3d 5c
                    jp        $16c5                         ;[005c] c3 c5 16
                    rst       $38                           ;[005f] ff
                    rst       $38                           ;[0060] ff
                    rst       $38                           ;[0061] ff
                    rst       $38                           ;[0062] ff
                    rst       $38                           ;[0063] ff
                    rst       $38                           ;[0064] ff
                    rst       $38                           ;[0065] ff
                    push      af                            ;[0066] f5
                    add       hl,bc                         ;[0067] 09
                    ld        b,h                           ;[0068] 44
                    ld        c,l                           ;[0069] 4d
                    ex        de,hl                         ;[006a] eb
                    pop       de                            ;[006b] d1
                    ldir                                    ;[006c] ed b0
                    ret                                     ;[006e] c9

                    nop                                     ;[006f] 00
                    nop                                     ;[0070] 00
                    nop                                     ;[0071] 00
                    nop                                     ;[0072] 00
                    nop                                     ;[0073] 00
                    ld        hl,($5c5d)                    ;[0074] 2a 5d 5c
                    inc       hl                            ;[0077] 23
                    ld        ($5c5d),hl                    ;[0078] 22 5d 5c
                    ld        a,(hl)                        ;[007b] 7e
                    ret                                     ;[007c] c9

                    cp        $21                           ;[007d] fe 21
                    ret       nc                            ;[007f] d0
                    cp        $0d                           ;[0080] fe 0d
                    ret       z                             ;[0082] c8
                    cp        $10                           ;[0083] fe 10
                    ret       c                             ;[0085] d8
                    cp        $18                           ;[0086] fe 18
                    ccf                                     ;[0088] 3f
                    ret       c                             ;[0089] d8
                    inc       hl                            ;[008a] 23
                    cp        $16                           ;[008b] fe 16
                    jr        c,$0090                       ;[008d] 38 01
                    inc       hl                            ;[008f] 23
                    scf                                     ;[0090] 37
                    ld        ($5c5d),hl                    ;[0091] 22 5d 5c
                    ret                                     ;[0094] c9

                    cp        a                             ;[0095] bf
                    ld        d,d                           ;[0096] 52
                    ld        c,(hl)                        ;[0097] 4e
                    call      nz,$4e49                      ;[0098] c4 49 4e
                    ld        c,e                           ;[009b] 4b
                    ld        b,l                           ;[009c] 45
                    ld        e,c                           ;[009d] 59
                    and       h                             ;[009e] a4
                    ld        d,b                           ;[009f] 50
                    ret                                     ;[00a0] c9

                    ld        b,(hl)                        ;[00a1] 46
                    adc       $50                           ;[00a2] ce 50
                    ld        c,a                           ;[00a4] 4f
                    ld        c,c                           ;[00a5] 49
                    ld        c,(hl)                        ;[00a6] 4e
                    call      nc,$4353                      ;[00a7] d4 53 43
                    ld        d,d                           ;[00aa] 52
                    ld        b,l                           ;[00ab] 45
                    ld        b,l                           ;[00ac] 45
                    ld        c,(hl)                        ;[00ad] 4e
                    and       h                             ;[00ae] a4
                    ld        b,c                           ;[00af] 41
                    ld        d,h                           ;[00b0] 54
                    ld        d,h                           ;[00b1] 54
                    jp        nc,$d441                      ;[00b2] d2 41 d4
                    ld        d,h                           ;[00b5] 54
                    ld        b,c                           ;[00b6] 41
                    jp        nz,$4156                      ;[00b7] c2 56 41
                    ld        c,h                           ;[00ba] 4c
                    and       h                             ;[00bb] a4
                    ld        b,e                           ;[00bc] 43
                    ld        c,a                           ;[00bd] 4f
                    ld        b,h                           ;[00be] 44
                    push      bc                            ;[00bf] c5
                    ld        d,(hl)                        ;[00c0] 56
                    ld        b,c                           ;[00c1] 41
                    call      z,$454c                       ;[00c2] cc 4c 45
                    adc       $53                           ;[00c5] ce 53
                    ld        c,c                           ;[00c7] 49
                    adc       $43                           ;[00c8] ce 43
                    ld        c,a                           ;[00ca] 4f
                    out       ($54),a                       ;[00cb] d3 54
                    ld        b,c                           ;[00cd] 41
                    adc       $41                           ;[00ce] ce 41
                    ld        d,e                           ;[00d0] 53
                    adc       $41                           ;[00d1] ce 41
                    ld        b,e                           ;[00d3] 43
                    out       ($41),a                       ;[00d4] d3 41
                    ld        d,h                           ;[00d6] 54
                    adc       $4c                           ;[00d7] ce 4c
                    adc       $45                           ;[00d9] ce 45
                    ld        e,b                           ;[00db] 58
                    ret       nc                            ;[00dc] d0
                    ld        c,c                           ;[00dd] 49
                    ld        c,(hl)                        ;[00de] 4e
                    call      nc,$5153                      ;[00df] d4 53 51
                    jp        nc,$4753                      ;[00e2] d2 53 47
                    adc       $41                           ;[00e5] ce 41
                    ld        b,d                           ;[00e7] 42
                    out       ($50),a                       ;[00e8] d3 50
                    ld        b,l                           ;[00ea] 45
                    ld        b,l                           ;[00eb] 45
                    bit       1,c                           ;[00ec] cb 49
                    adc       $55                           ;[00ee] ce 55
                    ld        d,e                           ;[00f0] 53
                    jp        nc,$5453                      ;[00f1] d2 53 54
                    ld        d,d                           ;[00f4] 52
                    and       h                             ;[00f5] a4
                    ld        b,e                           ;[00f6] 43
                    ld        c,b                           ;[00f7] 48
                    ld        d,d                           ;[00f8] 52
                    and       h                             ;[00f9] a4
                    ld        c,(hl)                        ;[00fa] 4e
                    ld        c,a                           ;[00fb] 4f
                    call      nc,$4942                      ;[00fc] d4 42 49
                    adc       $4f                           ;[00ff] ce 4f
                    jp        nc,$4e41                      ;[0101] d2 41 4e
                    call      nz,$bd3c                      ;[0104] c4 3c bd
                    ld        a,$bd                         ;[0107] 3e bd
                    inc       a                             ;[0109] 3c
                    cp        (hl)                          ;[010a] be
                    ld        c,h                           ;[010b] 4c
                    ld        c,c                           ;[010c] 49
                    ld        c,(hl)                        ;[010d] 4e
                    push      bc                            ;[010e] c5
                    ld        d,h                           ;[010f] 54
                    ld        c,b                           ;[0110] 48
                    ld        b,l                           ;[0111] 45
                    adc       $54                           ;[0112] ce 54
                    rst       $08                           ;[0114] cf
                    ld        d,e                           ;[0115] 53
                    ld        d,h                           ;[0116] 54
                    ld        b,l                           ;[0117] 45
                    ret       nc                            ;[0118] d0
                    ld        b,h                           ;[0119] 44
                    ld        b,l                           ;[011a] 45
                    ld        b,(hl)                        ;[011b] 46
                    jr        nz,$0164                      ;[011c] 20 46
                    adc       $43                           ;[011e] ce 43
                    ld        b,c                           ;[0120] 41
                    call      nc,$4f46                      ;[0121] d4 46 4f
                    ld        d,d                           ;[0124] 52
                    ld        c,l                           ;[0125] 4d
                    ld        b,c                           ;[0126] 41
                    call      nc,$4f4d                      ;[0127] d4 4d 4f
                    ld        d,(hl)                        ;[012a] 56
                    push      bc                            ;[012b] c5
                    ld        b,l                           ;[012c] 45
                    ld        d,d                           ;[012d] 52
                    ld        b,c                           ;[012e] 41
                    ld        d,e                           ;[012f] 53
                    push      bc                            ;[0130] c5
                    ld        c,a                           ;[0131] 4f
                    ld        d,b                           ;[0132] 50
                    ld        b,l                           ;[0133] 45
                    ld        c,(hl)                        ;[0134] 4e
                    jr        nz,$00da                      ;[0135] 20 a3
                    ld        b,e                           ;[0137] 43
                    ld        c,h                           ;[0138] 4c
                    ld        c,a                           ;[0139] 4f
                    ld        d,e                           ;[013a] 53
                    ld        b,l                           ;[013b] 45
                    jr        nz,$00e1                      ;[013c] 20 a3
                    ld        c,l                           ;[013e] 4d
                    ld        b,l                           ;[013f] 45
                    ld        d,d                           ;[0140] 52
                    ld        b,a                           ;[0141] 47
                    push      bc                            ;[0142] c5
                    ld        d,(hl)                        ;[0143] 56
                    ld        b,l                           ;[0144] 45
                    ld        d,d                           ;[0145] 52
                    ld        c,c                           ;[0146] 49
                    ld        b,(hl)                        ;[0147] 46
                    exx                                     ;[0148] d9
                    ld        b,d                           ;[0149] 42
                    ld        b,l                           ;[014a] 45
                    ld        b,l                           ;[014b] 45
                    ret       nc                            ;[014c] d0
                    ld        b,e                           ;[014d] 43
                    ld        c,c                           ;[014e] 49
                    ld        d,d                           ;[014f] 52
                    ld        b,e                           ;[0150] 43
                    ld        c,h                           ;[0151] 4c
                    push      bc                            ;[0152] c5
                    ld        c,c                           ;[0153] 49
                    ld        c,(hl)                        ;[0154] 4e
                    bit       2,b                           ;[0155] cb 50
                    ld        b,c                           ;[0157] 41
                    ld        d,b                           ;[0158] 50
                    ld        b,l                           ;[0159] 45
                    jp        nc,$4c46                      ;[015a] d2 46 4c
                    ld        b,c                           ;[015d] 41
                    ld        d,e                           ;[015e] 53
                    ret       z                             ;[015f] c8
                    ld        b,d                           ;[0160] 42
                    ld        d,d                           ;[0161] 52
                    ld        c,c                           ;[0162] 49
                    ld        b,a                           ;[0163] 47
                    ld        c,b                           ;[0164] 48
                    call      nc,$4e49                      ;[0165] d4 49 4e
                    ld        d,(hl)                        ;[0168] 56
                    ld        b,l                           ;[0169] 45
                    ld        d,d                           ;[016a] 52
                    ld        d,e                           ;[016b] 53
                    push      bc                            ;[016c] c5
                    ld        c,a                           ;[016d] 4f
                    ld        d,(hl)                        ;[016e] 56
                    ld        b,l                           ;[016f] 45
                    jp        nc,$554f                      ;[0170] d2 4f 55
                    call      nc,$504c                      ;[0173] d4 4c 50
                    ld        d,d                           ;[0176] 52
                    ld        c,c                           ;[0177] 49
                    ld        c,(hl)                        ;[0178] 4e
                    call      nc,$4c4c                      ;[0179] d4 4c 4c
                    ld        c,c                           ;[017c] 49
                    ld        d,e                           ;[017d] 53
                    call      nc,$5453                      ;[017e] d4 53 54
                    ld        c,a                           ;[0181] 4f
                    ret       nc                            ;[0182] d0
                    ld        d,d                           ;[0183] 52
                    ld        b,l                           ;[0184] 45
                    ld        b,c                           ;[0185] 41
                    call      nz,$4144                      ;[0186] c4 44 41
                    ld        d,h                           ;[0189] 54
                    pop       bc                            ;[018a] c1
                    ld        d,d                           ;[018b] 52
                    ld        b,l                           ;[018c] 45
                    ld        d,e                           ;[018d] 53
                    ld        d,h                           ;[018e] 54
                    ld        c,a                           ;[018f] 4f
                    ld        d,d                           ;[0190] 52
                    push      bc                            ;[0191] c5
                    ld        c,(hl)                        ;[0192] 4e
                    ld        b,l                           ;[0193] 45
                    rst       $10                           ;[0194] d7
                    ld        b,d                           ;[0195] 42
                    ld        c,a                           ;[0196] 4f
                    ld        d,d                           ;[0197] 52
                    ld        b,h                           ;[0198] 44
                    ld        b,l                           ;[0199] 45
                    jp        nc,$4f43                      ;[019a] d2 43 4f
                    ld        c,(hl)                        ;[019d] 4e
                    ld        d,h                           ;[019e] 54
                    ld        c,c                           ;[019f] 49
                    ld        c,(hl)                        ;[01a0] 4e
                    ld        d,l                           ;[01a1] 55
                    push      bc                            ;[01a2] c5
                    ld        b,h                           ;[01a3] 44
                    ld        c,c                           ;[01a4] 49
                    call      $4552                         ;[01a5] cd 52 45
                    call      $4f46                         ;[01a8] cd 46 4f
                    jp        nc,$4f47                      ;[01ab] d2 47 4f
                    jr        nz,$0204                      ;[01ae] 20 54
                    rst       $08                           ;[01b0] cf
                    ld        b,a                           ;[01b1] 47
                    ld        c,a                           ;[01b2] 4f
                    jr        nz,$0208                      ;[01b3] 20 53
                    ld        d,l                           ;[01b5] 55
                    jp        nz,$4e49                      ;[01b6] c2 49 4e
                    ld        d,b                           ;[01b9] 50
                    ld        d,l                           ;[01ba] 55
                    call      nc,$4f4c                      ;[01bb] d4 4c 4f
                    ld        b,c                           ;[01be] 41
                    call      nz,$494c                      ;[01bf] c4 4c 49
                    ld        d,e                           ;[01c2] 53
                    call      nc,$454c                      ;[01c3] d4 4c 45
                    call      nc,$4150                      ;[01c6] d4 50 41
                    ld        d,l                           ;[01c9] 55
                    ld        d,e                           ;[01ca] 53
                    push      bc                            ;[01cb] c5
                    ld        c,(hl)                        ;[01cc] 4e
                    ld        b,l                           ;[01cd] 45
                    ld        e,b                           ;[01ce] 58
                    call      nc,$4f50                      ;[01cf] d4 50 4f
                    ld        c,e                           ;[01d2] 4b
                    push      bc                            ;[01d3] c5
                    ld        d,b                           ;[01d4] 50
                    ld        d,d                           ;[01d5] 52
                    ld        c,c                           ;[01d6] 49
                    ld        c,(hl)                        ;[01d7] 4e
                    call      nc,$4c50                      ;[01d8] d4 50 4c
                    ld        c,a                           ;[01db] 4f
                    call      nc,$5552                      ;[01dc] d4 52 55
                    adc       $53                           ;[01df] ce 53
                    ld        b,c                           ;[01e1] 41
                    ld        d,(hl)                        ;[01e2] 56
                    push      bc                            ;[01e3] c5
                    ld        d,d                           ;[01e4] 52
                    ld        b,c                           ;[01e5] 41
                    ld        c,(hl)                        ;[01e6] 4e
                    ld        b,h                           ;[01e7] 44
                    ld        c,a                           ;[01e8] 4f
                    ld        c,l                           ;[01e9] 4d
                    ld        c,c                           ;[01ea] 49
                    ld        e,d                           ;[01eb] 5a
                    push      bc                            ;[01ec] c5
                    ld        c,c                           ;[01ed] 49
                    add       $43                           ;[01ee] c6 43
                    ld        c,h                           ;[01f0] 4c
                    out       ($44),a                       ;[01f1] d3 44
                    ld        d,d                           ;[01f3] 52
                    ld        b,c                           ;[01f4] 41
                    rst       $10                           ;[01f5] d7
                    ld        b,e                           ;[01f6] 43
                    ld        c,h                           ;[01f7] 4c
                    ld        b,l                           ;[01f8] 45
                    ld        b,c                           ;[01f9] 41
                    jp        nc,$4552                      ;[01fa] d2 52 45
                    ld        d,h                           ;[01fd] 54
                    ld        d,l                           ;[01fe] 55
                    ld        d,d                           ;[01ff] 52
                    adc       $43                           ;[0200] ce 43
                    ld        c,a                           ;[0202] 4f
                    ld        d,b                           ;[0203] 50
                    exx                                     ;[0204] d9
                    ld        b,d                           ;[0205] 42
                    ld        c,b                           ;[0206] 48
                    ld        e,c                           ;[0207] 59
                    ld        (hl),$35                      ;[0208] 36 35
                    ld        d,h                           ;[020a] 54
                    ld        b,a                           ;[020b] 47
                    ld        d,(hl)                        ;[020c] 56
                    ld        c,(hl)                        ;[020d] 4e
                    ld        c,d                           ;[020e] 4a
                    ld        d,l                           ;[020f] 55
                    scf                                     ;[0210] 37
                    inc       (hl)                          ;[0211] 34
                    ld        d,d                           ;[0212] 52
                    ld        b,(hl)                        ;[0213] 46
                    ld        b,e                           ;[0214] 43
                    ld        c,l                           ;[0215] 4d
                    ld        c,e                           ;[0216] 4b
                    ld        c,c                           ;[0217] 49
                    jr        c,$024d                       ;[0218] 38 33
                    ld        b,l                           ;[021a] 45
                    ld        b,h                           ;[021b] 44
                    ld        e,b                           ;[021c] 58
                    ld        c,$4c                         ;[021d] 0e 4c
                    ld        c,a                           ;[021f] 4f
                    add       hl,sp                         ;[0220] 39
                    ld        ($5357),a                     ;[0221] 32 57 53
                    ld        e,d                           ;[0224] 5a
                    jr        nz,$0234                      ;[0225] 20 0d
                    ld        d,b                           ;[0227] 50
                    jr        nc,$025b                      ;[0228] 30 31
                    ld        d,c                           ;[022a] 51
                    ld        b,c                           ;[022b] 41
                    ex        (sp),hl                       ;[022c] e3
                    call      nz,$e4e0                      ;[022d] c4 e0 e4
                    or        h                             ;[0230] b4
                    cp        h                             ;[0231] bc
                    cp        l                             ;[0232] bd
                    cp        e                             ;[0233] bb
                    xor       a                             ;[0234] af
                    or        b                             ;[0235] b0
                    or        c                             ;[0236] b1
                    ret       nz                            ;[0237] c0
                    and       a                             ;[0238] a7
                    and       (hl)                          ;[0239] a6
                    cp        (hl)                          ;[023a] be
                    xor       l                             ;[023b] ad
                    or        d                             ;[023c] b2
                    cp        d                             ;[023d] ba
                    push      hl                            ;[023e] e5
                    and       l                             ;[023f] a5
                    jp        nz,$b3e1                      ;[0240] c2 e1 b3
                    cp        c                             ;[0243] b9
                    pop       bc                            ;[0244] c1
                    cp        b                             ;[0245] b8
                    ld        a,(hl)                        ;[0246] 7e
                    call      c,$5cda                       ;[0247] dc da 5c
                    or        a                             ;[024a] b7
                    ld        a,e                           ;[024b] 7b
                    ld        a,l                           ;[024c] 7d
                    ret       c                             ;[024d] d8
                    cp        a                             ;[024e] bf
                    xor       (hl)                          ;[024f] ae
                    xor       d                             ;[0250] aa
                    xor       e                             ;[0251] ab
                    sbc       $df                           ;[0252] dd de df
                    ld        a,a                           ;[0255] 7f
                    or        l                             ;[0256] b5
                    sub       $7c                           ;[0257] d6 7c
                    push      de                            ;[0259] d5
                    ld        e,l                           ;[025a] 5d
                    in        a,($b6)                       ;[025b] db b6
                    exx                                     ;[025d] d9
                    ld        e,e                           ;[025e] 5b
                    rst       $10                           ;[025f] d7
                    inc       c                             ;[0260] 0c
                    rlca                                    ;[0261] 07
                    ld        b,$04                         ;[0262] 06 04
                    dec       b                             ;[0264] 05
                    ex        af,af'                        ;[0265] 08
                    ld        a,(bc)                        ;[0266] 0a
                    dec       bc                            ;[0267] 0b
                    add       hl,bc                         ;[0268] 09
                    rrca                                    ;[0269] 0f
                    jp        po,$3f2a                      ;[026a] e2 2a 3f
                    call      $ccc8                         ;[026d] cd c8 cc
                    bit       3,(hl)                        ;[0270] cb 5e
                    xor       h                             ;[0272] ac
                    dec       l                             ;[0273] 2d
                    dec       hl                            ;[0274] 2b
                    dec       a                             ;[0275] 3d
                    ld        l,$2c                         ;[0276] 2e 2c
                    dec       sp                            ;[0278] 3b
                    ld        ($3cc7),hl                    ;[0279] 22 c7 3c
                    jp        $c53e                         ;[027c] c3 3e c5
                    cpl                                     ;[027f] 2f
                    ret                                     ;[0280] c9

                    ld        h,b                           ;[0281] 60
                    add       $3a                           ;[0282] c6 3a
                    ret       nc                            ;[0284] d0
                    adc       $a8                           ;[0285] ce a8
                    jp        z,$d4d3                       ;[0287] ca d3 d4
                    pop       de                            ;[028a] d1
                    jp        nc,$cfa9                      ;[028b] d2 a9 cf
                    ld        l,$2f                         ;[028e] 2e 2f
                    ld        de,$ffff                      ;[0290] 11 ff ff
                    ld        bc,$fefe                      ;[0293] 01 fe fe
                    in        a,(c)                         ;[0296] ed 78
                    cpl                                     ;[0298] 2f
                    and       $1f                           ;[0299] e6 1f
                    jr        z,$02ab                       ;[029b] 28 0e
                    ld        h,a                           ;[029d] 67
                    ld        a,l                           ;[029e] 7d
                    inc       d                             ;[029f] 14
                    ret       nz                            ;[02a0] c0
                    sub       $08                           ;[02a1] d6 08
                    srl       h                             ;[02a3] cb 3c
                    jr        nc,$02a1                      ;[02a5] 30 fa
                    ld        d,e                           ;[02a7] 53
                    ld        e,a                           ;[02a8] 5f
                    jr        nz,$029f                      ;[02a9] 20 f4
                    dec       l                             ;[02ab] 2d
                    rlc       b                             ;[02ac] cb 00
                    jr        c,$0296                       ;[02ae] 38 e6
                    ld        a,d                           ;[02b0] 7a
                    inc       a                             ;[02b1] 3c
                    ret       z                             ;[02b2] c8
                    cp        $28                           ;[02b3] fe 28
                    ret       z                             ;[02b5] c8
                    cp        $19                           ;[02b6] fe 19
                    ret       z                             ;[02b8] c8
                    ld        a,e                           ;[02b9] 7b
                    ld        e,d                           ;[02ba] 5a
                    ld        d,a                           ;[02bb] 57
                    cp        $18                           ;[02bc] fe 18
                    ret                                     ;[02be] c9

                    call      $028e                         ;[02bf] cd 8e 02
                    ret       nz                            ;[02c2] c0
                    ld        hl,$5c00                      ;[02c3] 21 00 5c
                    bit       7,(hl)                        ;[02c6] cb 7e
                    jr        nz,$02d1                      ;[02c8] 20 07
                    inc       hl                            ;[02ca] 23
                    dec       (hl)                          ;[02cb] 35
                    dec       hl                            ;[02cc] 2b
                    jr        nz,$02d1                      ;[02cd] 20 02
                    ld        (hl),$ff                      ;[02cf] 36 ff
                    ld        a,l                           ;[02d1] 7d
                    ld        hl,$5c04                      ;[02d2] 21 04 5c
                    cp        l                             ;[02d5] bd
                    jr        nz,$02c6                      ;[02d6] 20 ee
                    call      $031e                         ;[02d8] cd 1e 03
                    ret       nc                            ;[02db] d0
                    ld        hl,$5c00                      ;[02dc] 21 00 5c
                    cp        (hl)                          ;[02df] be
                    jr        z,$0310                       ;[02e0] 28 2e
                    ex        de,hl                         ;[02e2] eb
                    ld        hl,$5c04                      ;[02e3] 21 04 5c
                    cp        (hl)                          ;[02e6] be
                    jr        z,$0310                       ;[02e7] 28 27
                    bit       7,(hl)                        ;[02e9] cb 7e
                    jr        nz,$02f1                      ;[02eb] 20 04
                    ex        de,hl                         ;[02ed] eb
                    bit       7,(hl)                        ;[02ee] cb 7e
                    ret       z                             ;[02f0] c8
                    ld        e,a                           ;[02f1] 5f
                    ld        (hl),a                        ;[02f2] 77
                    inc       hl                            ;[02f3] 23
                    ld        (hl),$05                      ;[02f4] 36 05
                    inc       hl                            ;[02f6] 23
                    ld        a,($5c09)                     ;[02f7] 3a 09 5c
                    ld        (hl),a                        ;[02fa] 77
                    inc       hl                            ;[02fb] 23
                    ld        c,(iy+$07)                    ;[02fc] fd 4e 07
                    ld        d,(iy+$01)                    ;[02ff] fd 56 01
                    push      hl                            ;[0302] e5
                    call      $0333                         ;[0303] cd 33 03
                    pop       hl                            ;[0306] e1
                    ld        (hl),a                        ;[0307] 77
                    ld        ($5c08),a                     ;[0308] 32 08 5c
                    set       5,(iy+$01)                    ;[030b] fd cb 01 ee
                    ret                                     ;[030f] c9

                    inc       hl                            ;[0310] 23
                    ld        (hl),$05                      ;[0311] 36 05
                    inc       hl                            ;[0313] 23
                    dec       (hl)                          ;[0314] 35
                    ret       nz                            ;[0315] c0
                    ld        a,($5c0a)                     ;[0316] 3a 0a 5c
                    ld        (hl),a                        ;[0319] 77
                    inc       hl                            ;[031a] 23
                    ld        a,(hl)                        ;[031b] 7e
                    jr        $0308                         ;[031c] 18 ea
                    ld        b,d                           ;[031e] 42
                    ld        d,$00                         ;[031f] 16 00
                    ld        a,e                           ;[0321] 7b
                    cp        $27                           ;[0322] fe 27
                    ret       nc                            ;[0324] d0
                    cp        $18                           ;[0325] fe 18
                    jr        nz,$032c                      ;[0327] 20 03
                    bit       7,b                           ;[0329] cb 78
                    ret       nz                            ;[032b] c0
                    ld        hl,$0205                      ;[032c] 21 05 02
                    add       hl,de                         ;[032f] 19
                    ld        a,(hl)                        ;[0330] 7e
                    scf                                     ;[0331] 37
                    ret                                     ;[0332] c9

                    ld        a,e                           ;[0333] 7b
                    cp        $3a                           ;[0334] fe 3a
                    jr        c,$0367                       ;[0336] 38 2f
                    dec       c                             ;[0338] 0d
                    jp        m,$034f                       ;[0339] fa 4f 03
                    jr        z,$0341                       ;[033c] 28 03
                    add       $4f                           ;[033e] c6 4f
                    ret                                     ;[0340] c9

                    ld        hl,$01eb                      ;[0341] 21 eb 01
                    inc       b                             ;[0344] 04
                    jr        z,$034a                       ;[0345] 28 03
                    ld        hl,$0205                      ;[0347] 21 05 02
                    ld        d,$00                         ;[034a] 16 00
                    add       hl,de                         ;[034c] 19
                    ld        a,(hl)                        ;[034d] 7e
                    ret                                     ;[034e] c9

                    ld        hl,$0229                      ;[034f] 21 29 02
                    bit       0,b                           ;[0352] cb 40
                    jr        z,$034a                       ;[0354] 28 f4
                    bit       3,d                           ;[0356] cb 5a
                    jr        z,$0364                       ;[0358] 28 0a
                    bit       3,(iy+$30)                    ;[035a] fd cb 30 5e
                    ret       nz                            ;[035e] c0
                    inc       b                             ;[035f] 04
                    ret       nz                            ;[0360] c0
                    add       $20                           ;[0361] c6 20
                    ret                                     ;[0363] c9

                    add       $a5                           ;[0364] c6 a5
                    ret                                     ;[0366] c9

                    cp        $30                           ;[0367] fe 30
                    ret       c                             ;[0369] d8
                    dec       c                             ;[036a] 0d
                    jp        m,$039d                       ;[036b] fa 9d 03
                    jr        nz,$0389                      ;[036e] 20 19
                    ld        hl,$0254                      ;[0370] 21 54 02
                    bit       5,b                           ;[0373] cb 68
                    jr        z,$034a                       ;[0375] 28 d3
                    cp        $38                           ;[0377] fe 38
                    jr        nc,$0382                      ;[0379] 30 07
                    sub       $20                           ;[037b] d6 20
                    inc       b                             ;[037d] 04
                    ret       z                             ;[037e] c8
                    add       $08                           ;[037f] c6 08
                    ret                                     ;[0381] c9

                    sub       $36                           ;[0382] d6 36
                    inc       b                             ;[0384] 04
                    ret       z                             ;[0385] c8
                    add       $fe                           ;[0386] c6 fe
                    ret                                     ;[0388] c9

                    ld        hl,$0230                      ;[0389] 21 30 02
                    cp        $39                           ;[038c] fe 39
                    jr        z,$034a                       ;[038e] 28 ba
                    cp        $30                           ;[0390] fe 30
                    jr        z,$034a                       ;[0392] 28 b6
                    and       $07                           ;[0394] e6 07
                    add       $80                           ;[0396] c6 80
                    inc       b                             ;[0398] 04
                    ret       z                             ;[0399] c8
                    xor       $0f                           ;[039a] ee 0f
                    ret                                     ;[039c] c9

                    inc       b                             ;[039d] 04
                    ret       z                             ;[039e] c8
                    bit       5,b                           ;[039f] cb 68
                    ld        hl,$0230                      ;[03a1] 21 30 02
                    jr        nz,$034a                      ;[03a4] 20 a4
                    sub       $10                           ;[03a6] d6 10
                    cp        $22                           ;[03a8] fe 22
                    jr        z,$03b2                       ;[03aa] 28 06
                    cp        $20                           ;[03ac] fe 20
                    ret       nz                            ;[03ae] c0
                    ld        a,$5f                         ;[03af] 3e 5f
                    ret                                     ;[03b1] c9

                    ld        a,$40                         ;[03b2] 3e 40
                    ret                                     ;[03b4] c9

                    di                                      ;[03b5] f3
                    ld        a,l                           ;[03b6] 7d
                    srl       l                             ;[03b7] cb 3d
                    srl       l                             ;[03b9] cb 3d
                    cpl                                     ;[03bb] 2f
                    and       $03                           ;[03bc] e6 03
                    ld        c,a                           ;[03be] 4f
                    ld        b,$00                         ;[03bf] 06 00
                    ld        ix,$03d1                      ;[03c1] dd 21 d1 03
                    add       ix,bc                         ;[03c5] dd 09
                    ld        a,($5c48)                     ;[03c7] 3a 48 5c
                    and       $38                           ;[03ca] e6 38
                    rrca                                    ;[03cc] 0f
                    rrca                                    ;[03cd] 0f
                    rrca                                    ;[03ce] 0f
                    or        $08                           ;[03cf] f6 08
                    nop                                     ;[03d1] 00
                    nop                                     ;[03d2] 00
                    nop                                     ;[03d3] 00
                    inc       b                             ;[03d4] 04
                    inc       c                             ;[03d5] 0c
                    dec       c                             ;[03d6] 0d
                    jr        nz,$03d6                      ;[03d7] 20 fd
                    ld        c,$3f                         ;[03d9] 0e 3f
                    dec       b                             ;[03db] 05
                    jp        nz,$03d6                      ;[03dc] c2 d6 03
                    xor       $10                           ;[03df] ee 10
                    out       ($fe),a                       ;[03e1] d3 fe
                    ld        b,h                           ;[03e3] 44
                    ld        c,a                           ;[03e4] 4f
                    bit       4,a                           ;[03e5] cb 67
                    jr        nz,$03f2                      ;[03e7] 20 09
                    ld        a,d                           ;[03e9] 7a
                    or        e                             ;[03ea] b3
                    jr        z,$03f6                       ;[03eb] 28 09
                    ld        a,c                           ;[03ed] 79
                    ld        c,l                           ;[03ee] 4d
                    dec       de                            ;[03ef] 1b
                    jp        (ix)                          ;[03f0] dd e9
                    ld        c,l                           ;[03f2] 4d
                    inc       c                             ;[03f3] 0c
                    jp        (ix)                          ;[03f4] dd e9
                    ei                                      ;[03f6] fb
                    ret                                     ;[03f7] c9

                    rst       $28                           ;[03f8] ef
                    ld        sp,$c027                      ;[03f9] 31 27 c0
                    inc       bc                            ;[03fc] 03
                    inc       (hl)                          ;[03fd] 34
                    call      pe,$986c                      ;[03fe] ec 6c 98
                    rra                                     ;[0401] 1f
                    push      af                            ;[0402] f5
                    inc       b                             ;[0403] 04
                    and       c                             ;[0404] a1
                    rrca                                    ;[0405] 0f
                    jr        c,$0429                       ;[0406] 38 21
                    sub       d                             ;[0408] 92
                    ld        e,h                           ;[0409] 5c
                    ld        a,(hl)                        ;[040a] 7e
                    and       a                             ;[040b] a7
                    jr        nz,$046c                      ;[040c] 20 5e
                    inc       hl                            ;[040e] 23
                    ld        c,(hl)                        ;[040f] 4e
                    inc       hl                            ;[0410] 23
                    ld        b,(hl)                        ;[0411] 46
                    ld        a,b                           ;[0412] 78
                    rla                                     ;[0413] 17
                    sbc       a                             ;[0414] 9f
                    cp        c                             ;[0415] b9
                    jr        nz,$046c                      ;[0416] 20 54
                    inc       hl                            ;[0418] 23
                    cp        (hl)                          ;[0419] be
                    jr        nz,$046c                      ;[041a] 20 50
                    ld        a,b                           ;[041c] 78
                    add       $3c                           ;[041d] c6 3c
                    jp        p,$0425                       ;[041f] f2 25 04
                    jp        po,$046c                      ;[0422] e2 6c 04
                    ld        b,$fa                         ;[0425] 06 fa
                    inc       b                             ;[0427] 04
                    sub       $0c                           ;[0428] d6 0c
                    jr        nc,$0427                      ;[042a] 30 fb
                    add       $0c                           ;[042c] c6 0c
                    push      bc                            ;[042e] c5
                    ld        hl,$046e                      ;[042f] 21 6e 04
                    call      $3406                         ;[0432] cd 06 34
                    call      $33b4                         ;[0435] cd b4 33
                    rst       $28                           ;[0438] ef
                    inc       b                             ;[0439] 04
                    jr        c,$042d                       ;[043a] 38 f1
                    add       (hl)                          ;[043c] 86
                    ld        (hl),a                        ;[043d] 77
                    rst       $28                           ;[043e] ef
                    ret       nz                            ;[043f] c0
                    ld        (bc),a                        ;[0440] 02
                    ld        sp,$cd38                      ;[0441] 31 38 cd
                    sub       h                             ;[0444] 94
                    ld        e,$fe                         ;[0445] 1e fe
                    dec       bc                            ;[0447] 0b
                    jr        nc,$046c                      ;[0448] 30 22
                    rst       $28                           ;[044a] ef
                    ret       po                            ;[044b] e0
                    inc       b                             ;[044c] 04
                    ret       po                            ;[044d] e0
                    inc       (hl)                          ;[044e] 34
                    add       b                             ;[044f] 80
                    ld        b,e                           ;[0450] 43
                    ld        d,l                           ;[0451] 55
                    sbc       a                             ;[0452] 9f
                    add       b                             ;[0453] 80
                    ld        bc,$3405                      ;[0454] 01 05 34
                    dec       (hl)                          ;[0457] 35
                    ld        (hl),c                        ;[0458] 71
                    inc       bc                            ;[0459] 03
                    jr        c,$0429                       ;[045a] 38 cd
                    sbc       c                             ;[045c] 99
                    ld        e,$c5                         ;[045d] 1e c5
                    call      $1e99                         ;[045f] cd 99 1e
                    pop       hl                            ;[0462] e1
                    ld        d,b                           ;[0463] 50
                    ld        e,c                           ;[0464] 59
                    ld        a,d                           ;[0465] 7a
                    or        e                             ;[0466] b3
                    ret       z                             ;[0467] c8
                    dec       de                            ;[0468] 1b
                    jp        $03b5                         ;[0469] c3 b5 03
                    rst       $08                           ;[046c] cf
                    ld        a,(bc)                        ;[046d] 0a
                    adc       c                             ;[046e] 89
                    ld        (bc),a                        ;[046f] 02
                    ret       nc                            ;[0470] d0
                    ld        (de),a                        ;[0471] 12
                    add       (hl)                          ;[0472] 86
                    adc       c                             ;[0473] 89
                    ld        a,(bc)                        ;[0474] 0a
                    sub       a                             ;[0475] 97
                    ld        h,b                           ;[0476] 60
                    ld        (hl),l                        ;[0477] 75
                    adc       c                             ;[0478] 89
                    ld        (de),a                        ;[0479] 12
                    push      de                            ;[047a] d5
                    rla                                     ;[047b] 17
                    rra                                     ;[047c] 1f
                    adc       c                             ;[047d] 89
                    dec       de                            ;[047e] 1b
                    sub       b                             ;[047f] 90
                    ld        b,c                           ;[0480] 41
                    ld        (bc),a                        ;[0481] 02
                    adc       c                             ;[0482] 89
                    inc       h                             ;[0483] 24
                    ret       nc                            ;[0484] d0
                    ld        d,e                           ;[0485] 53
                    jp        z,$2e89                       ;[0486] ca 89 2e
                    sbc       l                             ;[0489] 9d
                    ld        (hl),$b1                      ;[048a] 36 b1
                    adc       c                             ;[048c] 89
                    jr        c,$048e                       ;[048d] 38 ff
                    ld        c,c                           ;[048f] 49
                    ld        a,$89                         ;[0490] 3e 89
                    ld        b,e                           ;[0492] 43
                    rst       $38                           ;[0493] ff
                    ld        l,d                           ;[0494] 6a
                    ld        (hl),e                        ;[0495] 73
                    adc       c                             ;[0496] 89
                    ld        c,a                           ;[0497] 4f
                    and       a                             ;[0498] a7
                    nop                                     ;[0499] 00
                    ld        d,h                           ;[049a] 54
                    adc       c                             ;[049b] 89
                    ld        e,h                           ;[049c] 5c
                    nop                                     ;[049d] 00
                    nop                                     ;[049e] 00
                    nop                                     ;[049f] 00
                    adc       c                             ;[04a0] 89
                    ld        l,c                           ;[04a1] 69
                    inc       d                             ;[04a2] 14
                    or        $24                           ;[04a3] f6 24
                    adc       c                             ;[04a5] 89
                    halt                                    ;[04a6] 76
                    pop       af                            ;[04a7] f1
                    djnz      $04af                         ;[04a8] 10 05
                    ld        bc,$3e75                      ;[04aa] 01 75 3e
                    call      $32c5                         ;[04ad] cd c5 32
                    jp        nz,$0554                      ;[04b0] c2 54 05
                    rst       $08                           ;[04b3] cf
                    inc       c                             ;[04b4] 0c
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
                    ld        hl,$053f                      ;[04c2] 21 3f 05
                    push      hl                            ;[04c5] e5
                    ld        hl,$1f80                      ;[04c6] 21 80 1f
                    bit       7,a                           ;[04c9] cb 7f
                    jr        z,$04d0                       ;[04cb] 28 03
                    ld        hl,$0c98                      ;[04cd] 21 98 0c
                    ex        af,af'                        ;[04d0] 08
                    inc       de                            ;[04d1] 13
                    dec       ix                            ;[04d2] dd 2b
                    di                                      ;[04d4] f3
                    ld        a,$02                         ;[04d5] 3e 02
                    ld        b,a                           ;[04d7] 47
                    djnz      $04d8                         ;[04d8] 10 fe
                    out       ($fe),a                       ;[04da] d3 fe
                    xor       $0f                           ;[04dc] ee 0f
                    ld        b,$a4                         ;[04de] 06 a4
                    dec       l                             ;[04e0] 2d
                    jr        nz,$04d8                      ;[04e1] 20 f5
                    dec       b                             ;[04e3] 05
                    dec       h                             ;[04e4] 25
                    jp        p,$04d8                       ;[04e5] f2 d8 04
                    ld        b,$2f                         ;[04e8] 06 2f
                    djnz      $04ea                         ;[04ea] 10 fe
                    out       ($fe),a                       ;[04ec] d3 fe
                    ld        a,$0d                         ;[04ee] 3e 0d
                    ld        b,$37                         ;[04f0] 06 37
                    djnz      $04f2                         ;[04f2] 10 fe
                    out       ($fe),a                       ;[04f4] d3 fe
                    ld        bc,$3b0e                      ;[04f6] 01 0e 3b
                    ex        af,af'                        ;[04f9] 08
                    ld        l,a                           ;[04fa] 6f
                    jp        $0507                         ;[04fb] c3 07 05
                    ld        a,d                           ;[04fe] 7a
                    or        e                             ;[04ff] b3
                    jr        z,$050e                       ;[0500] 28 0c
                    ld        l,(ix+$00)                    ;[0502] dd 6e 00
                    ld        a,h                           ;[0505] 7c
                    xor       l                             ;[0506] ad
                    ld        h,a                           ;[0507] 67
                    ld        a,$01                         ;[0508] 3e 01
                    scf                                     ;[050a] 37
                    jp        $0525                         ;[050b] c3 25 05
                    ld        l,h                           ;[050e] 6c
                    jr        $0505                         ;[050f] 18 f4
                    ld        a,c                           ;[0511] 79
                    bit       7,b                           ;[0512] cb 78
                    djnz      $0514                         ;[0514] 10 fe
                    jr        nc,$051c                      ;[0516] 30 04
                    ld        b,$42                         ;[0518] 06 42
                    djnz      $051a                         ;[051a] 10 fe
                    out       ($fe),a                       ;[051c] d3 fe
                    ld        b,$3e                         ;[051e] 06 3e
                    jr        nz,$0511                      ;[0520] 20 ef
                    dec       b                             ;[0522] 05
                    xor       a                             ;[0523] af
                    inc       a                             ;[0524] 3c
                    rl        l                             ;[0525] cb 15
                    jp        nz,$0514                      ;[0527] c2 14 05
                    dec       de                            ;[052a] 1b
                    inc       ix                            ;[052b] dd 23
                    ld        b,$31                         ;[052d] 06 31
                    ld        a,$7f                         ;[052f] 3e 7f
                    in        a,($fe)                       ;[0531] db fe
                    rra                                     ;[0533] 1f
                    ret       nc                            ;[0534] d0
                    ld        a,d                           ;[0535] 7a
                    inc       a                             ;[0536] 3c
                    jp        nz,$04fe                      ;[0537] c2 fe 04
                    ld        b,$3b                         ;[053a] 06 3b
                    djnz      $053c                         ;[053c] 10 fe
                    ret                                     ;[053e] c9

                    push      af                            ;[053f] f5
                    ld        a,($5c48)                     ;[0540] 3a 48 5c
                    and       $38                           ;[0543] e6 38
                    rrca                                    ;[0545] 0f
                    rrca                                    ;[0546] 0f
                    rrca                                    ;[0547] 0f
                    out       ($fe),a                       ;[0548] d3 fe
                    ld        a,$7f                         ;[054a] 3e 7f
                    in        a,($fe)                       ;[054c] db fe
                    rra                                     ;[054e] 1f
                    ei                                      ;[054f] fb
                    jp        nc,$04aa                      ;[0550] d2 aa 04
                    nop                                     ;[0553] 00
                    pop       af                            ;[0554] f1
                    ret                                     ;[0555] c9

                    inc       d                             ;[0556] 14
                    ex        af,af'                        ;[0557] 08
                    dec       d                             ;[0558] 15
                    di                                      ;[0559] f3
                    ld        a,$0f                         ;[055a] 3e 0f
                    out       ($fe),a                       ;[055c] d3 fe
                    ld        hl,$053f                      ;[055e] 21 3f 05
                    push      hl                            ;[0561] e5
                    in        a,($fe)                       ;[0562] db fe
                    rra                                     ;[0564] 1f
                    and       $20                           ;[0565] e6 20
                    or        $02                           ;[0567] f6 02
                    ld        c,a                           ;[0569] 4f
                    cp        a                             ;[056a] bf
                    ret       nz                            ;[056b] c0
                    call      $05e7                         ;[056c] cd e7 05
                    jr        nc,$056b                      ;[056f] 30 fa
                    ld        hl,$0415                      ;[0571] 21 15 04
                    djnz      $0574                         ;[0574] 10 fe
                    dec       hl                            ;[0576] 2b
                    ld        a,h                           ;[0577] 7c
                    or        l                             ;[0578] b5
                    jr        nz,$0574                      ;[0579] 20 f9
                    call      $05e3                         ;[057b] cd e3 05
                    jr        nc,$056b                      ;[057e] 30 eb
                    ld        b,$9c                         ;[0580] 06 9c
                    call      $05e3                         ;[0582] cd e3 05
                    jr        nc,$056b                      ;[0585] 30 e4
                    ld        a,$c6                         ;[0587] 3e c6
                    cp        b                             ;[0589] b8
                    jr        nc,$056c                      ;[058a] 30 e0
                    inc       h                             ;[058c] 24
                    jr        nz,$0580                      ;[058d] 20 f1
                    ld        b,$c9                         ;[058f] 06 c9
                    call      $05e7                         ;[0591] cd e7 05
                    jr        nc,$056b                      ;[0594] 30 d5
                    ld        a,b                           ;[0596] 78
                    cp        $d4                           ;[0597] fe d4
                    jr        nc,$058f                      ;[0599] 30 f4
                    call      $05e7                         ;[059b] cd e7 05
                    ret       nc                            ;[059e] d0
                    ld        a,c                           ;[059f] 79
                    xor       $03                           ;[05a0] ee 03
                    ld        c,a                           ;[05a2] 4f
                    ld        h,$00                         ;[05a3] 26 00
                    ld        b,$b0                         ;[05a5] 06 b0
                    jr        $05c8                         ;[05a7] 18 1f
                    ex        af,af'                        ;[05a9] 08
                    jr        nz,$05b3                      ;[05aa] 20 07
                    jr        nc,$05bd                      ;[05ac] 30 0f
                    ld        (ix+$00),l                    ;[05ae] dd 75 00
                    jr        $05c2                         ;[05b1] 18 0f
                    rl        c                             ;[05b3] cb 11
                    xor       l                             ;[05b5] ad
                    ret       nz                            ;[05b6] c0
                    ld        a,c                           ;[05b7] 79
                    rra                                     ;[05b8] 1f
                    ld        c,a                           ;[05b9] 4f
                    inc       de                            ;[05ba] 13
                    jr        $05c4                         ;[05bb] 18 07
                    ld        a,(ix+$00)                    ;[05bd] dd 7e 00
                    xor       l                             ;[05c0] ad
                    ret       nz                            ;[05c1] c0
                    inc       ix                            ;[05c2] dd 23
                    dec       de                            ;[05c4] 1b
                    ex        af,af'                        ;[05c5] 08
                    ld        b,$b2                         ;[05c6] 06 b2
                    ld        l,$01                         ;[05c8] 2e 01
                    call      $05e3                         ;[05ca] cd e3 05
                    ret       nc                            ;[05cd] d0
                    ld        a,$cb                         ;[05ce] 3e cb
                    cp        b                             ;[05d0] b8
                    rl        l                             ;[05d1] cb 15
                    ld        b,$b0                         ;[05d3] 06 b0
                    jp        nc,$05ca                      ;[05d5] d2 ca 05
                    ld        a,h                           ;[05d8] 7c
                    xor       l                             ;[05d9] ad
                    ld        h,a                           ;[05da] 67
                    ld        a,d                           ;[05db] 7a
                    or        e                             ;[05dc] b3
                    jr        nz,$05a9                      ;[05dd] 20 ca
                    ld        a,h                           ;[05df] 7c
                    cp        $01                           ;[05e0] fe 01
                    ret                                     ;[05e2] c9

                    call      $05e7                         ;[05e3] cd e7 05
                    ret       nc                            ;[05e6] d0
                    ld        a,$16                         ;[05e7] 3e 16
                    dec       a                             ;[05e9] 3d
                    jr        nz,$05e9                      ;[05ea] 20 fd
                    and       a                             ;[05ec] a7
                    inc       b                             ;[05ed] 04
                    ret       z                             ;[05ee] c8
                    ld        a,$7f                         ;[05ef] 3e 7f
                    in        a,($fe)                       ;[05f1] db fe
                    rra                                     ;[05f3] 1f
                    ret       nc                            ;[05f4] d0
                    xor       c                             ;[05f5] a9
                    and       $20                           ;[05f6] e6 20
                    jr        z,$05ed                       ;[05f8] 28 f3
                    ld        a,c                           ;[05fa] 79
                    cpl                                     ;[05fb] 2f
                    ld        c,a                           ;[05fc] 4f
                    and       $07                           ;[05fd] e6 07
                    or        $08                           ;[05ff] f6 08
                    out       ($fe),a                       ;[0601] d3 fe
                    scf                                     ;[0603] 37
                    ret                                     ;[0604] c9

                    ld        bc,$2400                      ;[0605] 01 00 24
                    ld        a,$87                         ;[0608] 3e 87
                    jp        $26ec                         ;[060a] c3 ec 26
                    ld        bc,$2846                      ;[060d] 01 46 28
                    ld        a,$87                         ;[0610] 3e 87
                    jp        $26ec                         ;[0612] c3 ec 26
                    nop                                     ;[0615] 00
                    nop                                     ;[0616] 00
                    nop                                     ;[0617] 00
                    nop                                     ;[0618] 00
                    nop                                     ;[0619] 00
                    adc       d                             ;[061a] 8a
                    inc       e                             ;[061b] 1c
                    adc       d                             ;[061c] 8a
                    inc       e                             ;[061d] 1c
                    adc       d                             ;[061e] 8a
                    inc       e                             ;[061f] 1c
                    adc       d                             ;[0620] 8a
                    inc       e                             ;[0621] 1c
                    adc       d                             ;[0622] 8a
                    inc       e                             ;[0623] 1c
                    adc       d                             ;[0624] 8a
                    inc       e                             ;[0625] 1c
                    adc       d                             ;[0626] 8a
                    inc       e                             ;[0627] 1c
                    adc       d                             ;[0628] 8a
                    inc       e                             ;[0629] 1c
                    adc       d                             ;[062a] 8a
                    inc       e                             ;[062b] 1c
                    adc       d                             ;[062c] 8a
                    inc       e                             ;[062d] 1c
                    adc       d                             ;[062e] 8a
                    inc       e                             ;[062f] 1c
                    adc       d                             ;[0630] 8a
                    inc       e                             ;[0631] 1c
                    adc       d                             ;[0632] 8a
                    inc       e                             ;[0633] 1c
                    adc       d                             ;[0634] 8a
                    inc       e                             ;[0635] 1c
                    adc       d                             ;[0636] 8a
                    inc       e                             ;[0637] 1c
                    adc       d                             ;[0638] 8a
                    inc       e                             ;[0639] 1c
                    adc       d                             ;[063a] 8a
                    inc       e                             ;[063b] 1c
                    adc       d                             ;[063c] 8a
                    inc       e                             ;[063d] 1c
                    adc       d                             ;[063e] 8a
                    inc       e                             ;[063f] 1c
                    adc       d                             ;[0640] 8a
                    inc       e                             ;[0641] 1c
                    nop                                     ;[0642] 00
                    ex        af,af'                        ;[0643] 08
                    or        e                             ;[0644] b3
                    dec       h                             ;[0645] 25
                    adc       d                             ;[0646] 8a
                    inc       e                             ;[0647] 1c
                    adc       l                             ;[0648] 8d
                    ld        h,$8a                         ;[0649] 26 8a
                    inc       e                             ;[064b] 1c
                    adc       d                             ;[064c] 8a
                    inc       e                             ;[064d] 1c
                    adc       d                             ;[064e] 8a
                    inc       e                             ;[064f] 1c
                    ret       pe                            ;[0650] e8
                    dec       h                             ;[0651] 25
                    adc       d                             ;[0652] 8a
                    inc       e                             ;[0653] 1c
                    adc       d                             ;[0654] 8a
                    inc       e                             ;[0655] 1c
                    xor       a                             ;[0656] af
                    dec       h                             ;[0657] 25
                    adc       d                             ;[0658] 8a
                    inc       e                             ;[0659] 1c
                    ld        sp,$8d2a                      ;[065a] 31 2a 8d
                    ld        h,$8a                         ;[065d] 26 8a
                    inc       e                             ;[065f] 1c
                    adc       l                             ;[0660] 8d
                    ld        h,$8d                         ;[0661] 26 8d
                    ld        h,$8d                         ;[0663] 26 8d
                    ld        h,$8d                         ;[0665] 26 8d
                    ld        h,$8d                         ;[0667] 26 8d
                    ld        h,$8d                         ;[0669] 26 8d
                    ld        h,$8d                         ;[066b] 26 8d
                    ld        h,$8d                         ;[066d] 26 8d
                    ld        h,$8d                         ;[066f] 26 8d
                    ld        h,$8d                         ;[0671] 26 8d
                    ld        h,$8a                         ;[0673] 26 8a
                    inc       e                             ;[0675] 1c
                    adc       d                             ;[0676] 8a
                    inc       e                             ;[0677] 1c
                    adc       d                             ;[0678] 8a
                    inc       e                             ;[0679] 1c
                    adc       d                             ;[067a] 8a
                    inc       e                             ;[067b] 1c
                    adc       d                             ;[067c] 8a
                    inc       e                             ;[067d] 1c
                    adc       d                             ;[067e] 8a
                    inc       e                             ;[067f] 1c
                    adc       l                             ;[0680] 8d
                    ld        h,$c9                         ;[0681] 26 c9
                    ld        h,$c9                         ;[0683] 26 c9
                    ld        h,$c9                         ;[0685] 26 c9
                    ld        h,$c9                         ;[0687] 26 c9
                    ld        h,$c9                         ;[0689] 26 c9
                    ld        h,$c9                         ;[068b] 26 c9
                    ld        h,$c9                         ;[068d] 26 c9
                    ld        h,$c9                         ;[068f] 26 c9
                    ld        h,$c9                         ;[0691] 26 c9
                    ld        h,$c9                         ;[0693] 26 c9
                    ld        h,$c9                         ;[0695] 26 c9
                    ld        h,$c9                         ;[0697] 26 c9
                    ld        h,$c9                         ;[0699] 26 c9
                    ld        h,$c9                         ;[069b] 26 c9
                    ld        h,$c9                         ;[069d] 26 c9
                    ld        h,$c9                         ;[069f] 26 c9
                    ld        h,$c9                         ;[06a1] 26 c9
                    ld        h,$c9                         ;[06a3] 26 c9
                    ld        h,$c9                         ;[06a5] 26 c9
                    ld        h,$c9                         ;[06a7] 26 c9
                    ld        h,$c9                         ;[06a9] 26 c9
                    ld        h,$c9                         ;[06ab] 26 c9
                    ld        h,$c9                         ;[06ad] 26 c9
                    ld        h,$c9                         ;[06af] 26 c9
                    ld        h,$c9                         ;[06b1] 26 c9
                    ld        h,$c9                         ;[06b3] 26 c9
                    ld        h,$8a                         ;[06b5] 26 8a
                    inc       e                             ;[06b7] 1c
                    adc       d                             ;[06b8] 8a
                    inc       e                             ;[06b9] 1c
                    adc       d                             ;[06ba] 8a
                    inc       e                             ;[06bb] 1c
                    adc       d                             ;[06bc] 8a
                    inc       e                             ;[06bd] 1c
                    adc       d                             ;[06be] 8a
                    inc       e                             ;[06bf] 1c
                    adc       d                             ;[06c0] 8a
                    inc       e                             ;[06c1] 1c
                    ret                                     ;[06c2] c9

                    ld        h,$c9                         ;[06c3] 26 c9
                    ld        h,$c9                         ;[06c5] 26 c9
                    ld        h,$c9                         ;[06c7] 26 c9
                    ld        h,$c9                         ;[06c9] 26 c9
                    ld        h,$c9                         ;[06cb] 26 c9
                    ld        h,$c9                         ;[06cd] 26 c9
                    ld        h,$c9                         ;[06cf] 26 c9
                    ld        h,$c9                         ;[06d1] 26 c9
                    ld        h,$c9                         ;[06d3] 26 c9
                    ld        h,$c9                         ;[06d5] 26 c9
                    ld        h,$c9                         ;[06d7] 26 c9
                    ld        h,$c9                         ;[06d9] 26 c9
                    ld        h,$c9                         ;[06db] 26 c9
                    ld        h,$c9                         ;[06dd] 26 c9
                    ld        h,$c9                         ;[06df] 26 c9
                    ld        h,$c9                         ;[06e1] 26 c9
                    ld        h,$c9                         ;[06e3] 26 c9
                    ld        h,$c9                         ;[06e5] 26 c9
                    ld        h,$c9                         ;[06e7] 26 c9
                    ld        h,$c9                         ;[06e9] 26 c9
                    ld        h,$c9                         ;[06eb] 26 c9
                    ld        h,$c9                         ;[06ed] 26 c9
                    ld        h,$c9                         ;[06ef] 26 c9
                    ld        h,$c9                         ;[06f1] 26 c9
                    ld        h,$c9                         ;[06f3] 26 c9
                    ld        h,$b8                         ;[06f5] 26 b8
                    ld        a,($25a5)                     ;[06f7] 3a a5 25
                    adc       d                             ;[06fa] 8a
                    inc       e                             ;[06fb] 1c
                    adc       d                             ;[06fc] 8a
                    inc       e                             ;[06fd] 1c
                    adc       d                             ;[06fe] 8a
                    inc       e                             ;[06ff] 1c
                    adc       d                             ;[0700] 8a
                    inc       e                             ;[0701] 1c
                    ld        e,l                           ;[0702] 5d
                    dec       e                             ;[0703] 1d
                    adc       d                             ;[0704] 8a
                    inc       e                             ;[0705] 1c
                    adc       d                             ;[0706] 8a
                    inc       e                             ;[0707] 1c
                    adc       d                             ;[0708] 8a
                    inc       e                             ;[0709] 1c
                    adc       d                             ;[070a] 8a
                    inc       e                             ;[070b] 1c
                    adc       d                             ;[070c] 8a
                    inc       e                             ;[070d] 1c
                    ld        (bc),a                        ;[070e] 02
                    ld        a,($0805)                     ;[070f] 3a 05 08
                    adc       d                             ;[0712] 8a
                    inc       e                             ;[0713] 1c
                    dec       b                             ;[0714] 05
                    ex        af,af'                        ;[0715] 08
                    adc       d                             ;[0716] 8a
                    inc       e                             ;[0717] 1c
                    adc       d                             ;[0718] 8a
                    inc       e                             ;[0719] 1c
                    adc       d                             ;[071a] 8a
                    inc       e                             ;[071b] 1c
                    adc       d                             ;[071c] 8a
                    inc       e                             ;[071d] 1c
                    ld        e,d                           ;[071e] 5a
                    djnz      $06ab                         ;[071f] 10 8a
                    inc       e                             ;[0721] 1c
                    adc       d                             ;[0722] 8a
                    inc       e                             ;[0723] 1c
                    adc       d                             ;[0724] 8a
                    inc       e                             ;[0725] 1c
                    adc       d                             ;[0726] 8a
                    inc       e                             ;[0727] 1c
                    adc       d                             ;[0728] 8a
                    inc       e                             ;[0729] 1c
                    adc       d                             ;[072a] 8a
                    inc       e                             ;[072b] 1c
                    adc       d                             ;[072c] 8a
                    inc       e                             ;[072d] 1c
                    adc       d                             ;[072e] 8a
                    inc       e                             ;[072f] 1c
                    adc       d                             ;[0730] 8a
                    inc       e                             ;[0731] 1c
                    adc       d                             ;[0732] 8a
                    inc       e                             ;[0733] 1c
                    ld        d,e                           ;[0734] 53
                    add       hl,sp                         ;[0735] 39
                    adc       d                             ;[0736] 8a
                    inc       e                             ;[0737] 1c
                    adc       d                             ;[0738] 8a
                    inc       e                             ;[0739] 1c
                    adc       d                             ;[073a] 8a
                    inc       e                             ;[073b] 1c
                    ld        a,(hl)                        ;[073c] 7e
                    add       hl,sp                         ;[073d] 39
                    adc       d                             ;[073e] 8a
                    inc       e                             ;[073f] 1c
                    adc       d                             ;[0740] 8a
                    inc       e                             ;[0741] 1c
                    adc       d                             ;[0742] 8a
                    inc       e                             ;[0743] 1c
                    adc       d                             ;[0744] 8a
                    inc       e                             ;[0745] 1c
                    adc       d                             ;[0746] 8a
                    inc       e                             ;[0747] 1c
                    adc       d                             ;[0748] 8a
                    inc       e                             ;[0749] 1c
                    ret       m                             ;[074a] f8
                    dec       h                             ;[074b] 25
                    ld        d,c                           ;[074c] 51
                    ld        h,$44                         ;[074d] 26 44
                    ld        h,$98                         ;[074f] 26 98
                    ex        af,af'                        ;[0751] 08
                    add       a                             ;[0752] 87
                    ex        af,af'                        ;[0753] 08
                    ld        (hl),h                        ;[0754] 74
                    ex        af,af'                        ;[0755] 08
                    ld        a,(hl)                        ;[0756] 7e
                    ex        af,af'                        ;[0757] 08
                    adc       d                             ;[0758] 8a
                    inc       e                             ;[0759] 1c
                    adc       d                             ;[075a] 8a
                    inc       e                             ;[075b] 1c
                    scf                                     ;[075c] 37
                    ld        hl,($0816)                    ;[075d] 2a 16 08
                    ld        d,$08                         ;[0760] 16 08
                    ld        d,$08                         ;[0762] 16 08
                    ld        hl,($2a08)                    ;[0764] 2a 08 2a
                    ex        af,af'                        ;[0767] 08
                    ld        hl,($2a08)                    ;[0768] 2a 08 2a
                    ex        af,af'                        ;[076b] 08
                    ld        hl,($2a08)                    ;[076c] 2a 08 2a
                    ex        af,af'                        ;[076f] 08
                    ld        hl,($2a08)                    ;[0770] 2a 08 2a
                    ex        af,af'                        ;[0773] 08
                    ld        hl,($2a08)                    ;[0774] 2a 08 2a
                    ex        af,af'                        ;[0777] 08
                    ld        hl,($2a08)                    ;[0778] 2a 08 2a
                    ex        af,af'                        ;[077b] 08
                    ld        hl,($2408)                    ;[077c] 2a 08 24
                    ex        af,af'                        ;[077f] 08
                    or        e                             ;[0780] b3
                    rla                                     ;[0781] 17
                    ld        a,(de)                        ;[0782] 1a
                    ex        af,af'                        ;[0783] 08
                    jr        nz,$078e                      ;[0784] 20 08
                    inc       (hl)                          ;[0786] 34
                    ex        af,af'                        ;[0787] 08
                    adc       l                             ;[0788] 8d
                    ld        h,$8a                         ;[0789] 26 8a
                    inc       e                             ;[078b] 1c
                    adc       d                             ;[078c] 8a
                    inc       e                             ;[078d] 1c
                    adc       d                             ;[078e] 8a
                    inc       e                             ;[078f] 1c
                    adc       d                             ;[0790] 8a
                    inc       e                             ;[0791] 1c
                    adc       d                             ;[0792] 8a
                    inc       e                             ;[0793] 1c
                    adc       d                             ;[0794] 8a
                    inc       e                             ;[0795] 1c
                    adc       d                             ;[0796] 8a
                    inc       e                             ;[0797] 1c
                    adc       d                             ;[0798] 8a
                    inc       e                             ;[0799] 1c
                    adc       d                             ;[079a] 8a
                    inc       e                             ;[079b] 1c
                    adc       d                             ;[079c] 8a
                    inc       e                             ;[079d] 1c
                    adc       d                             ;[079e] 8a
                    inc       e                             ;[079f] 1c
                    adc       d                             ;[07a0] 8a
                    inc       e                             ;[07a1] 1c
                    adc       d                             ;[07a2] 8a
                    inc       e                             ;[07a3] 1c
                    adc       d                             ;[07a4] 8a
                    inc       e                             ;[07a5] 1c
                    adc       d                             ;[07a6] 8a
                    inc       e                             ;[07a7] 1c
                    adc       d                             ;[07a8] 8a
                    inc       e                             ;[07a9] 1c
                    adc       d                             ;[07aa] 8a
                    inc       e                             ;[07ab] 1c
                    adc       d                             ;[07ac] 8a
                    inc       e                             ;[07ad] 1c
                    adc       d                             ;[07ae] 8a
                    inc       e                             ;[07af] 1c
                    adc       d                             ;[07b0] 8a
                    inc       e                             ;[07b1] 1c
                    adc       d                             ;[07b2] 8a
                    inc       e                             ;[07b3] 1c
                    adc       d                             ;[07b4] 8a
                    inc       e                             ;[07b5] 1c
                    adc       d                             ;[07b6] 8a
                    inc       e                             ;[07b7] 1c
                    adc       d                             ;[07b8] 8a
                    inc       e                             ;[07b9] 1c
                    adc       d                             ;[07ba] 8a
                    inc       e                             ;[07bb] 1c
                    adc       d                             ;[07bc] 8a
                    inc       e                             ;[07bd] 1c
                    adc       d                             ;[07be] 8a
                    inc       e                             ;[07bf] 1c
                    adc       d                             ;[07c0] 8a
                    inc       e                             ;[07c1] 1c
                    adc       d                             ;[07c2] 8a
                    inc       e                             ;[07c3] 1c
                    adc       d                             ;[07c4] 8a
                    inc       e                             ;[07c5] 1c
                    adc       d                             ;[07c6] 8a
                    inc       e                             ;[07c7] 1c
                    add       hl,sp                         ;[07c8] 39
                    ex        af,af'                        ;[07c9] 08
                    adc       d                             ;[07ca] 8a
                    inc       e                             ;[07cb] 1c
                    adc       d                             ;[07cc] 8a
                    inc       e                             ;[07cd] 1c
                    adc       d                             ;[07ce] 8a
                    inc       e                             ;[07cf] 1c
                    adc       d                             ;[07d0] 8a
                    inc       e                             ;[07d1] 1c
                    and       l                             ;[07d2] a5
                    ex        af,af'                        ;[07d3] 08
                    adc       d                             ;[07d4] 8a
                    inc       e                             ;[07d5] 1c
                    adc       d                             ;[07d6] 8a
                    inc       e                             ;[07d7] 1c
                    adc       d                             ;[07d8] 8a
                    inc       e                             ;[07d9] 1c
                    adc       d                             ;[07da] 8a
                    inc       e                             ;[07db] 1c
                    ld        (de),a                        ;[07dc] 12
                    ex        af,af'                        ;[07dd] 08
                    adc       d                             ;[07de] 8a
                    inc       e                             ;[07df] 1c
                    adc       d                             ;[07e0] 8a
                    inc       e                             ;[07e1] 1c
                    adc       d                             ;[07e2] 8a
                    inc       e                             ;[07e3] 1c
                    adc       d                             ;[07e4] 8a
                    inc       e                             ;[07e5] 1c
                    add       hl,bc                         ;[07e6] 09
                    ex        af,af'                        ;[07e7] 08
                    adc       d                             ;[07e8] 8a
                    inc       e                             ;[07e9] 1c
                    adc       d                             ;[07ea] 8a
                    inc       e                             ;[07eb] 1c
                    adc       d                             ;[07ec] 8a
                    inc       e                             ;[07ed] 1c
                    adc       d                             ;[07ee] 8a
                    inc       e                             ;[07ef] 1c
                    adc       d                             ;[07f0] 8a
                    inc       e                             ;[07f1] 1c
                    adc       d                             ;[07f2] 8a
                    inc       e                             ;[07f3] 1c
                    adc       d                             ;[07f4] 8a
                    inc       e                             ;[07f5] 1c
                    adc       d                             ;[07f6] 8a
                    inc       e                             ;[07f7] 1c
                    adc       d                             ;[07f8] 8a
                    inc       e                             ;[07f9] 1c
                    adc       d                             ;[07fa] 8a
                    inc       e                             ;[07fb] 1c
                    adc       d                             ;[07fc] 8a
                    inc       e                             ;[07fd] 1c
                    adc       d                             ;[07fe] 8a
                    inc       e                             ;[07ff] 1c
                    ld        bc,$04f2                      ;[0800] 01 f2 04
                    jr        $082f                         ;[0803] 18 2a
                    add       $69                           ;[0805] c6 69
                    jr        $082c                         ;[0807] 18 23
                    ld        c,$f5                         ;[0809] 0e f5
                    rst       $20                           ;[080b] e7
                    cp        $23                           ;[080c] fe 23
                    jr        z,$082d                       ;[080e] 28 1d
                    rst       $08                           ;[0810] cf
                    dec       bc                            ;[0811] 0b
                    ld        c,$f8                         ;[0812] 0e f8
                    jr        $082d                         ;[0814] 18 17
                    add       $ed                           ;[0816] c6 ed
                    jr        $082c                         ;[0818] 18 12
                    call      $27d4                         ;[081a] cd d4 27
                    jp        c,$0970                       ;[081d] da 70 09
                    add       $ad                           ;[0820] c6 ad
                    jr        $082c                         ;[0822] 18 08
                    call      $27d4                         ;[0824] cd d4 27
                    jp        c,$20c1                       ;[0827] da c1 20
                    add       $2d                           ;[082a] c6 2d
                    ld        c,a                           ;[082c] 4f
                    ld        b,$10                         ;[082d] 06 10
                    push      bc                            ;[082f] c5
                    rst       $20                           ;[0830] e7
                    jp        $2504                         ;[0831] c3 04 25
                    ld        bc,$04f0                      ;[0834] 01 f0 04
                    jr        $082f                         ;[0837] 18 f6
                    call      $2530                         ;[0839] cd 30 25
                    jr        z,$0871                       ;[083c] 28 33
                    ld        hl,($5c57)                    ;[083e] 2a 57 5c
                    ld        a,($5b78)                     ;[0841] 3a 78 5b
                    push      af                            ;[0844] f5
                    push      hl                            ;[0845] e5
                    ld        hl,($5c5f)                    ;[0846] 2a 5f 5c
                    push      hl                            ;[0849] e5
                    ld        hl,$3503                      ;[084a] 21 03 35
                    call      $32cd                         ;[084d] cd cd 32
                    push      af                            ;[0850] f5
                    call      c,$2bf1                       ;[0851] dc f1 2b
                    pop       af                            ;[0854] f1
                    ld        bc,$0000                      ;[0855] 01 00 00
                    jr        nc,$0862                      ;[0858] 30 08
                    inc       bc                            ;[085a] 03
                    bit       6,(iy+$01)                    ;[085b] fd cb 01 76
                    jr        nz,$0862                      ;[085f] 20 01
                    inc       bc                            ;[0861] 03
                    call      $2d2f                         ;[0862] cd 2f 2d
                    pop       hl                            ;[0865] e1
                    ld        ($5c5f),hl                    ;[0866] 22 5f 5c
                    pop       hl                            ;[0869] e1
                    pop       af                            ;[086a] f1
                    ld        ($5b78),a                     ;[086b] 32 78 5b
                    ld        ($5c57),hl                    ;[086e] 22 57 5c
                    jp        $264d                         ;[0871] c3 4d 26
                    call      $2522                         ;[0874] cd 22 25
                    call      nz,$2535                      ;[0877] c4 35 25
                    rst       $20                           ;[087a] e7
                    jp        $25db                         ;[087b] c3 db 25
                    call      $2522                         ;[087e] cd 22 25
                    call      nz,$2580                      ;[0881] c4 80 25
                    rst       $20                           ;[0884] e7
                    jr        $0895                         ;[0885] 18 0e
                    rst       $20                           ;[0887] e7
                    cp        $23                           ;[0888] fe 23
                    ld        c,$f4                         ;[088a] 0e f4
                    jr        z,$082d                       ;[088c] 28 9f
                    call      $2523                         ;[088e] cd 23 25
                    call      nz,$22cb                      ;[0891] c4 cb 22
                    rst       $20                           ;[0894] e7
                    jp        $26c3                         ;[0895] c3 c3 26
                    ld        a,($5b77)                     ;[0898] 3a 77 5b
                    ld        d,a                           ;[089b] 57
                    ld        hl,$1d2a                      ;[089c] 21 2a 1d
                    call      $32cd                         ;[089f] cd cd 32
                    jp        $2712                         ;[08a2] c3 12 27
                    rst       $20                           ;[08a5] e7
                    cp        $23                           ;[08a6] fe 23
                    ld        c,$f6                         ;[08a8] 0e f6
                    jr        z,$082d                       ;[08aa] 28 81
                    cp        $28                           ;[08ac] fe 28
                    jp        nz,$11f2                      ;[08ae] c2 f2 11
                    rst       $20                           ;[08b1] e7
                    call      $28bb                         ;[08b2] cd bb 28
                    jp        c,$1c2e                       ;[08b5] da 2e 1c
                    jr        nz,$08ae                      ;[08b8] 20 f4
                    push      hl                            ;[08ba] e5
                    push      bc                            ;[08bb] c5
                    rst       $20                           ;[08bc] e7
                    cp        $29                           ;[08bd] fe 29
                    jr        nz,$08ae                      ;[08bf] 20 ed
                    rst       $20                           ;[08c1] e7
                    cp        $2c                           ;[08c2] fe 2c
                    ld        a,$3a                         ;[08c4] 3e 3a
                    call      z,$155e                       ;[08c6] cc 5e 15
                    call      $1ce2                         ;[08c9] cd e2 1c
                    rst       $18                           ;[08cc] df
                    cp        $29                           ;[08cd] fe 29
                    jr        nz,$08ae                      ;[08cf] 20 dd
                    rst       $20                           ;[08d1] e7
                    pop       bc                            ;[08d2] c1
                    pop       hl                            ;[08d3] e1
                    call      $2530                         ;[08d4] cd 30 25
                    jr        z,$0895                       ;[08d7] 28 bc
                    push      hl                            ;[08d9] e5
                    push      bc                            ;[08da] c5
                    call      $1e94                         ;[08db] cd 94 1e
                    pop       hl                            ;[08de] e1
                    bit       7,h                           ;[08df] cb 7c
                    pop       hl                            ;[08e1] e1
                    inc       hl                            ;[08e2] 23
                    ld        c,$01                         ;[08e3] 0e 01
                    jr        z,$08eb                       ;[08e5] 28 04
                    inc       hl                            ;[08e7] 23
                    inc       hl                            ;[08e8] 23
                    ld        c,(hl)                        ;[08e9] 4e
                    inc       hl                            ;[08ea] 23
                    and       a                             ;[08eb] a7
                    jr        z,$08f9                       ;[08ec] 28 0b
                    dec       a                             ;[08ee] 3d
                    cp        c                             ;[08ef] b9
                    jp        nc,$2a25                      ;[08f0] d2 25 2a
                    add       a                             ;[08f3] 87
                    add       hl,a                          ;[08f4] ed 31
                    ld        c,(hl)                        ;[08f6] 4e
                    inc       hl                            ;[08f7] 23
                    ld        b,(hl)                        ;[08f8] 46
                    jp        $217e                         ;[08f9] c3 7e 21
                    ex        af,af'                        ;[08fc] 08
                    bit       5,h                           ;[08fd] cb 6c
                    jr        nz,$0911                      ;[08ff] 20 10
                    dec       a                             ;[0901] 3d
                    nextreg $51,a                           ;[0902] ed 92 51
                    inc       a                             ;[0905] 3c
                    ex        af,af'                        ;[0906] 08
                    set       5,h                           ;[0907] cb ec
                    ld        a,(hl)                        ;[0909] 7e
                    res       5,h                           ;[090a] cb ac
                    nextreg $51,$ff                         ;[090c] ed 91 51 ff
                    ret                                     ;[0910] c9

                    nextreg $51,a                           ;[0911] ed 92 51
                    ex        af,af'                        ;[0914] 08
                    ld        a,(hl)                        ;[0915] 7e
                    jr        $090c                         ;[0916] 18 f4
                    ld        hl,$5c92                      ;[0918] 21 92 5c
                    push      hl                            ;[091b] e5
                    ld        (hl),$2a                      ;[091c] 36 2a
                    inc       hl                            ;[091e] 23
                    ld        (hl),e                        ;[091f] 73
                    inc       hl                            ;[0920] 23
                    ld        (hl),d                        ;[0921] 72
                    inc       hl                            ;[0922] 23
                    ld        (hl),$c9                      ;[0923] 36 c9
                    ret                                     ;[0925] c9

                    rst       $28                           ;[0926] ef
                    ret       nz                            ;[0927] c0
                    ld        (bc),a                        ;[0928] 02
                    ld        sp,$e01e                      ;[0929] 31 1e e0
                    inc       b                             ;[092c] 04
                    jr        c,$08fc                       ;[092d] 38 cd
                    and       d                             ;[092f] a2
                    dec       l                             ;[0930] 2d
                    jp        c,$1e9f                       ;[0931] da 9f 1e
                    ex        af,af'                        ;[0934] 08
                    exx                                     ;[0935] d9
                    call      $2bf1                         ;[0936] cd f1 2b
                    ex        de,hl                         ;[0939] eb
                    ld        d,b                           ;[093a] 50
                    ld        e,c                           ;[093b] 59
                    ex        af,af'                        ;[093c] 08
                    jr        z,$0941                       ;[093d] 28 02
                    add       hl,bc                         ;[093f] 09
                    dec       hl                            ;[0940] 2b
                    exx                                     ;[0941] d9
                    ex        af,af'                        ;[0942] 08
                    rst       $30                           ;[0943] f7
                    call      $2ab2                         ;[0944] cd b2 2a
                    ld        a,b                           ;[0947] 78
                    or        c                             ;[0948] b1
                    jp        z,$35bf                       ;[0949] ca bf 35
                    exx                                     ;[094c] d9
                    ld        a,d                           ;[094d] 7a
                    or        e                             ;[094e] b3
                    jr        nz,$095f                      ;[094f] 20 0e
                    ex        af,af'                        ;[0951] 08
                    ld        d,b                           ;[0952] 50
                    ld        e,c                           ;[0953] 59
                    jr        z,$0959                       ;[0954] 28 03
                    add       hl,bc                         ;[0956] 09
                    jr        $0960                         ;[0957] 18 07
                    ex        af,af'                        ;[0959] 08
                    sbc       hl,bc                         ;[095a] ed 42
                    ex        af,af'                        ;[095c] 08
                    jr        $0960                         ;[095d] 18 01
                    ex        af,af'                        ;[095f] 08
                    ld        a,(hl)                        ;[0960] 7e
                    inc       hl                            ;[0961] 23
                    dec       de                            ;[0962] 1b
                    jr        z,$0967                       ;[0963] 28 02
                    dec       hl                            ;[0965] 2b
                    dec       hl                            ;[0966] 2b
                    exx                                     ;[0967] d9
                    ld        (de),a                        ;[0968] 12
                    inc       de                            ;[0969] 13
                    dec       bc                            ;[096a] 0b
                    ex        af,af'                        ;[096b] 08
                    jr        $0947                         ;[096c] 18 d9
                    rst       $38                           ;[096e] ff
                    ret                                     ;[096f] c9

                    ld        ix,$361f                      ;[0970] dd 21 1f 36
                    jp        z,$179d                       ;[0974] ca 9d 17
                    bit       6,(hl)                        ;[0977] cb 76
                    jp        z,$1c8a                       ;[0979] ca 8a 1c
                    call      $1c82                         ;[097c] cd 82 1c
                    cp        $2c                           ;[097f] fe 2c
                    ld        a,$3a                         ;[0981] 3e 3a
                    call      z,$155e                       ;[0983] cc 5e 15
                    call      $1ce2                         ;[0986] cd e2 1c
                    rst       $18                           ;[0989] df
                    cp        $29                           ;[098a] fe 29
                    jp        nz,$1c8a                      ;[098c] c2 8a 1c
                    rst       $20                           ;[098f] e7
                    call      $2530                         ;[0990] cd 30 25
                    jp        z,$25db                       ;[0993] ca db 25
                    call      $1e94                         ;[0996] cd 94 1e
                    push      af                            ;[0999] f5
                    call      $1e94                         ;[099a] cd 94 1e
                    cp        $25                           ;[099d] fe 25
                    jp        nc,$24f9                      ;[099f] d2 f9 24
                    cp        $02                           ;[09a2] fe 02
                    jp        c,$24f9                       ;[09a4] da f9 24
                    call      $2d28                         ;[09a7] cd 28 2d
                    rst       $28                           ;[09aa] ef
                    pop       bc                            ;[09ab] c1
                    ld        (bc),a                        ;[09ac] 02
                    jp        $c42a                         ;[09ad] c3 2a c4
                    daa                                     ;[09b0] 27
                    ld        sp,$01e4                      ;[09b1] 31 e4 01
                    inc       bc                            ;[09b4] 03
                    ld        bc,$c338                      ;[09b5] 01 38 c3
                    call      $ff1d                         ;[09b8] cd 1d ff
                    rst       $38                           ;[09bb] ff
                    rst       $38                           ;[09bc] ff
                    rst       $38                           ;[09bd] ff
                    rst       $38                           ;[09be] ff
                    rst       $38                           ;[09bf] ff
                    xor       (hl)                          ;[09c0] ae
                    dec       c                             ;[09c1] 0d
                    ld        d,b                           ;[09c2] 50
                    ld        (hl),d                        ;[09c3] 72
                    ld        l,a                           ;[09c4] 6f
                    ld        h,a                           ;[09c5] 67
                    ld        (hl),d                        ;[09c6] 72
                    ld        h,c                           ;[09c7] 61
                    ld        l,l                           ;[09c8] 6d
                    ld        a,($0da0)                     ;[09c9] 3a a0 0d
                    ld        c,(hl)                        ;[09cc] 4e
                    ld        (hl),l                        ;[09cd] 75
                    ld        l,l                           ;[09ce] 6d
                    ld        h,d                           ;[09cf] 62
                    ld        h,l                           ;[09d0] 65
                    ld        (hl),d                        ;[09d1] 72
                    jr        nz,$0a35                      ;[09d2] 20 61
                    ld        (hl),d                        ;[09d4] 72
                    ld        (hl),d                        ;[09d5] 72
                    ld        h,c                           ;[09d6] 61
                    ld        a,c                           ;[09d7] 79
                    ld        a,($0da0)                     ;[09d8] 3a a0 0d
                    ld        b,e                           ;[09db] 43
                    ld        l,b                           ;[09dc] 68
                    ld        h,c                           ;[09dd] 61
                    ld        (hl),d                        ;[09de] 72
                    ld        h,c                           ;[09df] 61
                    ld        h,e                           ;[09e0] 63
                    ld        (hl),h                        ;[09e1] 74
                    ld        h,l                           ;[09e2] 65
                    ld        (hl),d                        ;[09e3] 72
                    jr        nz,$0a47                      ;[09e4] 20 61
                    ld        (hl),d                        ;[09e6] 72
                    ld        (hl),d                        ;[09e7] 72
                    ld        h,c                           ;[09e8] 61
                    ld        a,c                           ;[09e9] 79
                    ld        a,($0da0)                     ;[09ea] 3a a0 0d
                    ld        b,d                           ;[09ed] 42
                    ld        a,c                           ;[09ee] 79
                    ld        (hl),h                        ;[09ef] 74
                    ld        h,l                           ;[09f0] 65
                    ld        (hl),e                        ;[09f1] 73
                    ld        a,($cda0)                     ;[09f2] 3a a0 cd
                    inc       bc                            ;[09f5] 03
                    dec       bc                            ;[09f6] 0b
                    cp        $20                           ;[09f7] fe 20
                    jp        nc,$0ad9                      ;[09f9] d2 d9 0a
                    cp        $06                           ;[09fc] fe 06
                    jr        c,$0a69                       ;[09fe] 38 69
                    cp        $18                           ;[0a00] fe 18
                    jr        nc,$0a69                      ;[0a02] 30 65
                    ld        hl,$0a0b                      ;[0a04] 21 0b 0a
                    ld        e,a                           ;[0a07] 5f
                    ld        d,$00                         ;[0a08] 16 00
                    add       hl,de                         ;[0a0a] 19
                    ld        e,(hl)                        ;[0a0b] 5e
                    add       hl,de                         ;[0a0c] 19
                    push      hl                            ;[0a0d] e5
                    jp        $0b03                         ;[0a0e] c3 03 0b
                    ld        c,(hl)                        ;[0a11] 4e
                    ld        d,a                           ;[0a12] 57
                    djnz      $0a3e                         ;[0a13] 10 29
                    ld        d,h                           ;[0a15] 54
                    ld        d,e                           ;[0a16] 53
                    ld        d,d                           ;[0a17] 52
                    scf                                     ;[0a18] 37
                    ld        d,b                           ;[0a19] 50
                    ld        c,a                           ;[0a1a] 4f
                    ld        e,a                           ;[0a1b] 5f
                    ld        e,(hl)                        ;[0a1c] 5e
                    ld        e,l                           ;[0a1d] 5d
                    ld        e,h                           ;[0a1e] 5c
                    ld        e,e                           ;[0a1f] 5b
                    ld        e,d                           ;[0a20] 5a
                    ld        d,h                           ;[0a21] 54
                    ld        d,e                           ;[0a22] 53
                    inc       c                             ;[0a23] 0c
                    ld        a,$22                         ;[0a24] 3e 22
                    cp        c                             ;[0a26] b9
                    jr        nz,$0a3a                      ;[0a27] 20 11
                    bit       1,(iy+$01)                    ;[0a29] fd cb 01 4e
                    jr        nz,$0a38                      ;[0a2d] 20 09
                    inc       b                             ;[0a2f] 04
                    ld        c,$02                         ;[0a30] 0e 02
                    ld        a,$19                         ;[0a32] 3e 19
                    cp        b                             ;[0a34] b8
                    jr        nz,$0a3a                      ;[0a35] 20 03
                    dec       b                             ;[0a37] 05
                    ld        c,$21                         ;[0a38] 0e 21
                    jp        $0dd9                         ;[0a3a] c3 d9 0d
                    ld        a,($5c91)                     ;[0a3d] 3a 91 5c
                    push      af                            ;[0a40] f5
                    ld        (iy+$57),$01                  ;[0a41] fd 36 57 01
                    ld        a,$20                         ;[0a45] 3e 20
                    call      $0ad9                         ;[0a47] cd d9 0a
                    pop       af                            ;[0a4a] f1
                    ld        ($5c91),a                     ;[0a4b] 32 91 5c
                    ret                                     ;[0a4e] c9

                    bit       1,(iy+$01)                    ;[0a4f] fd cb 01 4e
                    jp        nz,$0ecd                      ;[0a53] c2 cd 0e
                    ld        c,$21                         ;[0a56] 0e 21
                    call      $0c55                         ;[0a58] cd 55 0c
                    dec       b                             ;[0a5b] 05
                    jp        $0dd9                         ;[0a5c] c3 d9 0d
                    call      $0b03                         ;[0a5f] cd 03 0b
                    ld        a,c                           ;[0a62] 79
                    dec       a                             ;[0a63] 3d
                    dec       a                             ;[0a64] 3d
                    and       $10                           ;[0a65] e6 10
                    jr        $0ac3                         ;[0a67] 18 5a
                    ld        a,$3f                         ;[0a69] 3e 3f
                    jr        $0ad9                         ;[0a6b] 18 6c
                    ld        de,$0a87                      ;[0a6d] 11 87 0a
                    ld        ($5c0f),a                     ;[0a70] 32 0f 5c
                    jr        $0a80                         ;[0a73] 18 0b
                    ld        de,$0a6d                      ;[0a75] 11 6d 0a
                    jr        $0a7d                         ;[0a78] 18 03
                    ld        de,$0a87                      ;[0a7a] 11 87 0a
                    ld        ($5c0e),a                     ;[0a7d] 32 0e 5c
                    ld        hl,($5c51)                    ;[0a80] 2a 51 5c
                    ld        (hl),e                        ;[0a83] 73
                    inc       hl                            ;[0a84] 23
                    ld        (hl),d                        ;[0a85] 72
                    ret                                     ;[0a86] c9

                    ld        de,$09f4                      ;[0a87] 11 f4 09
                    call      $0a80                         ;[0a8a] cd 80 0a
                    ld        hl,($5c0e)                    ;[0a8d] 2a 0e 5c
                    ld        d,a                           ;[0a90] 57
                    ld        a,l                           ;[0a91] 7d
                    cp        $16                           ;[0a92] fe 16
                    jp        c,$2211                       ;[0a94] da 11 22
                    jr        nz,$0ac2                      ;[0a97] 20 29
                    ld        b,h                           ;[0a99] 44
                    ld        c,d                           ;[0a9a] 4a
                    ld        a,$1f                         ;[0a9b] 3e 1f
                    sub       c                             ;[0a9d] 91
                    jr        c,$0aac                       ;[0a9e] 38 0c
                    add       $02                           ;[0aa0] c6 02
                    ld        c,a                           ;[0aa2] 4f
                    bit       1,(iy+$01)                    ;[0aa3] fd cb 01 4e
                    jr        nz,$0abf                      ;[0aa7] 20 16
                    ld        a,$16                         ;[0aa9] 3e 16
                    sub       b                             ;[0aab] 90
                    jp        c,$1e9f                       ;[0aac] da 9f 1e
                    inc       a                             ;[0aaf] 3c
                    ld        b,a                           ;[0ab0] 47
                    inc       b                             ;[0ab1] 04
                    bit       0,(iy+$02)                    ;[0ab2] fd cb 02 46
                    jp        nz,$0c55                      ;[0ab6] c2 55 0c
                    cp        (iy+$31)                      ;[0ab9] fd be 31
                    jp        c,$0c86                       ;[0abc] da 86 0c
                    jp        $0dd9                         ;[0abf] c3 d9 0d
                    ld        a,h                           ;[0ac2] 7c
                    call      $0b03                         ;[0ac3] cd 03 0b
                    add       c                             ;[0ac6] 81
                    dec       a                             ;[0ac7] 3d
                    and       $1f                           ;[0ac8] e6 1f
                    ret       z                             ;[0aca] c8
                    ld        d,a                           ;[0acb] 57
                    set       0,(iy+$01)                    ;[0acc] fd cb 01 c6
                    ld        a,$20                         ;[0ad0] 3e 20
                    call      $0c3b                         ;[0ad2] cd 3b 0c
                    dec       d                             ;[0ad5] 15
                    jr        nz,$0ad0                      ;[0ad6] 20 f8
                    ret                                     ;[0ad8] c9

                    call      $0b24                         ;[0ad9] cd 24 0b
                    bit       1,(iy+$01)                    ;[0adc] fd cb 01 4e
                    jr        nz,$0afc                      ;[0ae0] 20 1a
                    bit       0,(iy+$02)                    ;[0ae2] fd cb 02 46
                    jr        nz,$0af0                      ;[0ae6] 20 08
                    ld        ($5c88),bc                    ;[0ae8] ed 43 88 5c
                    ld        ($5c84),hl                    ;[0aec] 22 84 5c
                    ret                                     ;[0aef] c9

                    ld        ($5c8a),bc                    ;[0af0] ed 43 8a 5c
                    ld        ($5c82),bc                    ;[0af4] ed 43 82 5c
                    ld        ($5c86),hl                    ;[0af8] 22 86 5c
                    ret                                     ;[0afb] c9

                    ld        (iy+$45),c                    ;[0afc] fd 71 45
                    ld        ($5c80),hl                    ;[0aff] 22 80 5c
                    ret                                     ;[0b02] c9

                    bit       1,(iy+$01)                    ;[0b03] fd cb 01 4e
                    jr        nz,$0b1d                      ;[0b07] 20 14
                    ld        bc,($5c88)                    ;[0b09] ed 4b 88 5c
                    ld        hl,($5c84)                    ;[0b0d] 2a 84 5c
                    bit       0,(iy+$02)                    ;[0b10] fd cb 02 46
                    ret       z                             ;[0b14] c8
                    ld        bc,($5c8a)                    ;[0b15] ed 4b 8a 5c
                    ld        hl,($5c86)                    ;[0b19] 2a 86 5c
                    ret                                     ;[0b1c] c9

                    ld        c,(iy+$45)                    ;[0b1d] fd 4e 45
                    ld        hl,($5c80)                    ;[0b20] 2a 80 5c
                    ret                                     ;[0b23] c9

                    cp        $80                           ;[0b24] fe 80
                    jr        c,$0b65                       ;[0b26] 38 3d
                    cp        $90                           ;[0b28] fe 90
                    jr        nc,$0b52                      ;[0b2a] 30 26
                    ld        b,a                           ;[0b2c] 47
                    call      $0b38                         ;[0b2d] cd 38 0b
                    call      $0b03                         ;[0b30] cd 03 0b
                    ld        de,$5c92                      ;[0b33] 11 92 5c
                    jr        $0b7f                         ;[0b36] 18 47
                    ld        hl,$5c92                      ;[0b38] 21 92 5c
                    call      $0b3e                         ;[0b3b] cd 3e 0b
                    rr        b                             ;[0b3e] cb 18
                    sbc       a                             ;[0b40] 9f
                    and       $0f                           ;[0b41] e6 0f
                    ld        c,a                           ;[0b43] 4f
                    rr        b                             ;[0b44] cb 18
                    sbc       a                             ;[0b46] 9f
                    and       $f0                           ;[0b47] e6 f0
                    or        c                             ;[0b49] b1
                    ld        c,$04                         ;[0b4a] 0e 04
                    ld        (hl),a                        ;[0b4c] 77
                    inc       hl                            ;[0b4d] 23
                    dec       c                             ;[0b4e] 0d
                    jr        nz,$0b4c                      ;[0b4f] 20 fb
                    ret                                     ;[0b51] c9

                    jp        $387e                         ;[0b52] c3 7e 38
                    nop                                     ;[0b55] 00
                    add       $15                           ;[0b56] c6 15
                    push      bc                            ;[0b58] c5
                    ld        bc,($5c7b)                    ;[0b59] ed 4b 7b 5c
                    jr        $0b6a                         ;[0b5d] 18 0b
                    call      $0c10                         ;[0b5f] cd 10 0c
                    jp        $0b03                         ;[0b62] c3 03 0b
                    push      bc                            ;[0b65] c5
                    ld        bc,($5c36)                    ;[0b66] ed 4b 36 5c
                    ex        de,hl                         ;[0b6a] eb
                    ld        hl,$5c3b                      ;[0b6b] 21 3b 5c
                    res       0,(hl)                        ;[0b6e] cb 86
                    cp        $20                           ;[0b70] fe 20
                    jr        nz,$0b76                      ;[0b72] 20 02
                    set       0,(hl)                        ;[0b74] cb c6
                    ld        h,$00                         ;[0b76] 26 00
                    ld        l,a                           ;[0b78] 6f
                    add       hl,hl                         ;[0b79] 29
                    add       hl,hl                         ;[0b7a] 29
                    add       hl,hl                         ;[0b7b] 29
                    add       hl,bc                         ;[0b7c] 09
                    pop       bc                            ;[0b7d] c1
                    ex        de,hl                         ;[0b7e] eb
                    ld        a,c                           ;[0b7f] 79
                    dec       a                             ;[0b80] 3d
                    ld        a,$21                         ;[0b81] 3e 21
                    jr        nz,$0b93                      ;[0b83] 20 0e
                    dec       b                             ;[0b85] 05
                    ld        c,a                           ;[0b86] 4f
                    bit       1,(iy+$01)                    ;[0b87] fd cb 01 4e
                    jr        z,$0b93                       ;[0b8b] 28 06
                    push      de                            ;[0b8d] d5
                    call      $0ecd                         ;[0b8e] cd cd 0e
                    pop       de                            ;[0b91] d1
                    ld        a,c                           ;[0b92] 79
                    cp        c                             ;[0b93] b9
                    push      de                            ;[0b94] d5
                    call      z,$0c55                       ;[0b95] cc 55 0c
                    pop       de                            ;[0b98] d1
                    push      bc                            ;[0b99] c5
                    push      hl                            ;[0b9a] e5
                    ld        a,($5c91)                     ;[0b9b] 3a 91 5c
                    ld        b,$ff                         ;[0b9e] 06 ff
                    rra                                     ;[0ba0] 1f
                    jr        c,$0ba4                       ;[0ba1] 38 01
                    inc       b                             ;[0ba3] 04
                    rra                                     ;[0ba4] 1f
                    rra                                     ;[0ba5] 1f
                    sbc       a                             ;[0ba6] 9f
                    ld        c,a                           ;[0ba7] 4f
                    ld        a,$08                         ;[0ba8] 3e 08
                    and       a                             ;[0baa] a7
                    bit       1,(iy+$01)                    ;[0bab] fd cb 01 4e
                    jr        z,$0bb6                       ;[0baf] 28 05
                    set       1,(iy+$30)                    ;[0bb1] fd cb 30 ce
                    scf                                     ;[0bb5] 37
                    ex        de,hl                         ;[0bb6] eb
                    ex        af,af'                        ;[0bb7] 08
                    ld        a,(de)                        ;[0bb8] 1a
                    and       b                             ;[0bb9] a0
                    xor       (hl)                          ;[0bba] ae
                    xor       c                             ;[0bbb] a9
                    ld        (de),a                        ;[0bbc] 12
                    ex        af,af'                        ;[0bbd] 08
                    jr        c,$0bd3                       ;[0bbe] 38 13
                    inc       d                             ;[0bc0] 14
                    inc       hl                            ;[0bc1] 23
                    dec       a                             ;[0bc2] 3d
                    jr        nz,$0bb7                      ;[0bc3] 20 f2
                    ex        de,hl                         ;[0bc5] eb
                    dec       h                             ;[0bc6] 25
                    bit       1,(iy+$01)                    ;[0bc7] fd cb 01 4e
                    call      z,$0bdb                       ;[0bcb] cc db 0b
                    pop       hl                            ;[0bce] e1
                    pop       bc                            ;[0bcf] c1
                    dec       c                             ;[0bd0] 0d
                    inc       hl                            ;[0bd1] 23
                    ret                                     ;[0bd2] c9

                    ex        af,af'                        ;[0bd3] 08
                    ld        a,$20                         ;[0bd4] 3e 20
                    add       e                             ;[0bd6] 83
                    ld        e,a                           ;[0bd7] 5f
                    ex        af,af'                        ;[0bd8] 08
                    jr        $0bc1                         ;[0bd9] 18 e6
                    ld        a,h                           ;[0bdb] 7c
                    rrca                                    ;[0bdc] 0f
                    rrca                                    ;[0bdd] 0f
                    rrca                                    ;[0bde] 0f
                    and       $03                           ;[0bdf] e6 03
                    or        $58                           ;[0be1] f6 58
                    ld        h,a                           ;[0be3] 67
                    ld        de,($5c8f)                    ;[0be4] ed 5b 8f 5c
                    ld        a,(hl)                        ;[0be8] 7e
                    xor       e                             ;[0be9] ab
                    and       d                             ;[0bea] a2
                    xor       e                             ;[0beb] ab
                    bit       6,(iy+$57)                    ;[0bec] fd cb 57 76
                    jr        z,$0bfa                       ;[0bf0] 28 08
                    and       $c7                           ;[0bf2] e6 c7
                    bit       2,a                           ;[0bf4] cb 57
                    jr        nz,$0bfa                      ;[0bf6] 20 02
                    xor       $38                           ;[0bf8] ee 38
                    bit       4,(iy+$57)                    ;[0bfa] fd cb 57 66
                    jr        z,$0c08                       ;[0bfe] 28 08
                    and       $f8                           ;[0c00] e6 f8
                    bit       5,a                           ;[0c02] cb 6f
                    jr        nz,$0c08                      ;[0c04] 20 02
                    xor       $07                           ;[0c06] ee 07
                    ld        (hl),a                        ;[0c08] 77
                    ret                                     ;[0c09] c9

                    push      hl                            ;[0c0a] e5
                    ld        h,$00                         ;[0c0b] 26 00
                    ex        (sp),hl                       ;[0c0d] e3
                    jr        $0c14                         ;[0c0e] 18 04
                    ld        de,$0095                      ;[0c10] 11 95 00
                    push      af                            ;[0c13] f5
                    call      $0c41                         ;[0c14] cd 41 0c
                    jr        c,$0c22                       ;[0c17] 38 09
                    ld        a,$20                         ;[0c19] 3e 20
                    bit       0,(iy+$01)                    ;[0c1b] fd cb 01 46
                    call      z,$0c3b                       ;[0c1f] cc 3b 0c
                    ld        a,(de)                        ;[0c22] 1a
                    and       $7f                           ;[0c23] e6 7f
                    call      $0c3b                         ;[0c25] cd 3b 0c
                    ld        a,(de)                        ;[0c28] 1a
                    inc       de                            ;[0c29] 13
                    add       a                             ;[0c2a] 87
                    jr        nc,$0c22                      ;[0c2b] 30 f5
                    pop       de                            ;[0c2d] d1
                    cp        $48                           ;[0c2e] fe 48
                    jr        z,$0c35                       ;[0c30] 28 03
                    cp        $82                           ;[0c32] fe 82
                    ret       c                             ;[0c34] d8
                    ld        a,d                           ;[0c35] 7a
                    cp        $03                           ;[0c36] fe 03
                    ret       c                             ;[0c38] d8
                    ld        a,$20                         ;[0c39] 3e 20
                    push      de                            ;[0c3b] d5
                    exx                                     ;[0c3c] d9
                    rst       $10                           ;[0c3d] d7
                    exx                                     ;[0c3e] d9
                    pop       de                            ;[0c3f] d1
                    ret                                     ;[0c40] c9

                    push      af                            ;[0c41] f5
                    ex        de,hl                         ;[0c42] eb
                    inc       a                             ;[0c43] 3c
                    bit       7,(hl)                        ;[0c44] cb 7e
                    inc       hl                            ;[0c46] 23
                    jr        z,$0c44                       ;[0c47] 28 fb
                    dec       a                             ;[0c49] 3d
                    jr        nz,$0c44                      ;[0c4a] 20 f8
                    ex        de,hl                         ;[0c4c] eb
                    pop       af                            ;[0c4d] f1
                    cp        $20                           ;[0c4e] fe 20
                    ret       c                             ;[0c50] d8
                    ld        a,(de)                        ;[0c51] 1a
                    sub       $41                           ;[0c52] d6 41
                    ret                                     ;[0c54] c9

                    bit       1,(iy+$01)                    ;[0c55] fd cb 01 4e
                    ret       nz                            ;[0c59] c0
                    ld        de,$0dd9                      ;[0c5a] 11 d9 0d
                    push      de                            ;[0c5d] d5
                    ld        a,b                           ;[0c5e] 78
                    bit       0,(iy+$02)                    ;[0c5f] fd cb 02 46
                    jp        nz,$0d02                      ;[0c63] c2 02 0d
                    cp        (iy+$31)                      ;[0c66] fd be 31
                    jr        c,$0c86                       ;[0c69] 38 1b
                    ret       nz                            ;[0c6b] c0
                    jr        $0c88                         ;[0c6c] 18 1a
                    push      bc                            ;[0c6e] c5
                    push      de                            ;[0c6f] d5
                    call      $15d4                         ;[0c70] cd d4 15
                    push      af                            ;[0c73] f5
                    exx                                     ;[0c74] d9
                    call      $0d6e                         ;[0c75] cd 6e 0d
                    exx                                     ;[0c78] d9
                    pop       af                            ;[0c79] f1
                    pop       de                            ;[0c7a] d1
                    ld        bc,$3e75                      ;[0c7b] 01 75 3e
                    call      $32c5                         ;[0c7e] cd c5 32
                    pop       bc                            ;[0c81] c1
                    ret       z                             ;[0c82] c8
                    xor       a                             ;[0c83] af
                    ret                                     ;[0c84] c9

                    nop                                     ;[0c85] 00
                    rst       $08                           ;[0c86] cf
                    inc       b                             ;[0c87] 04
                    dec       (iy+$52)                      ;[0c88] fd 35 52
                    jr        nz,$0cd2                      ;[0c8b] 20 45
                    ld        a,$18                         ;[0c8d] 3e 18
                    sub       b                             ;[0c8f] 90
                    ld        ($5c8c),a                     ;[0c90] 32 8c 5c
                    ld        hl,($5c8f)                    ;[0c93] 2a 8f 5c
                    push      hl                            ;[0c96] e5
                    ld        a,($5c91)                     ;[0c97] 3a 91 5c
                    push      af                            ;[0c9a] f5
                    ld        a,$fd                         ;[0c9b] 3e fd
                    call      $1601                         ;[0c9d] cd 01 16
                    xor       a                             ;[0ca0] af
                    ld        de,$0cf8                      ;[0ca1] 11 f8 0c
                    call      $0c0a                         ;[0ca4] cd 0a 0c
                    nop                                     ;[0ca7] 00
                    nop                                     ;[0ca8] 00
                    nop                                     ;[0ca9] 00
                    nop                                     ;[0caa] 00
                    ld        hl,$5c3b                      ;[0cab] 21 3b 5c
                    set       3,(hl)                        ;[0cae] cb de
                    res       5,(hl)                        ;[0cb0] cb ae
                    exx                                     ;[0cb2] d9
                    call      $0c6e                         ;[0cb3] cd 6e 0c
                    exx                                     ;[0cb6] d9
                    cp        $20                           ;[0cb7] fe 20
                    jr        z,$0d00                       ;[0cb9] 28 45
                    cp        $e2                           ;[0cbb] fe e2
                    jr        z,$0d00                       ;[0cbd] 28 41
                    or        $20                           ;[0cbf] f6 20
                    cp        $6e                           ;[0cc1] fe 6e
                    jr        z,$0d00                       ;[0cc3] 28 3b
                    ld        a,$fe                         ;[0cc5] 3e fe
                    call      $1601                         ;[0cc7] cd 01 16
                    pop       af                            ;[0cca] f1
                    ld        ($5c91),a                     ;[0ccb] 32 91 5c
                    pop       hl                            ;[0cce] e1
                    ld        ($5c8f),hl                    ;[0ccf] 22 8f 5c
                    call      $0dfe                         ;[0cd2] cd fe 0d
                    ld        b,(iy+$31)                    ;[0cd5] fd 46 31
                    inc       b                             ;[0cd8] 04
                    ld        c,$21                         ;[0cd9] 0e 21
                    push      bc                            ;[0cdb] c5
                    call      $0e9b                         ;[0cdc] cd 9b 0e
                    ld        a,h                           ;[0cdf] 7c
                    rrca                                    ;[0ce0] 0f
                    rrca                                    ;[0ce1] 0f
                    rrca                                    ;[0ce2] 0f
                    and       $03                           ;[0ce3] e6 03
                    or        $58                           ;[0ce5] f6 58
                    ld        h,a                           ;[0ce7] 67
                    ld        de,$5ae0                      ;[0ce8] 11 e0 5a
                    ld        a,(de)                        ;[0ceb] 1a
                    ld        c,(hl)                        ;[0cec] 4e
                    ld        b,$20                         ;[0ced] 06 20
                    ex        de,hl                         ;[0cef] eb
                    ld        (de),a                        ;[0cf0] 12
                    ld        (hl),c                        ;[0cf1] 71
                    inc       de                            ;[0cf2] 13
                    inc       hl                            ;[0cf3] 23
                    djnz      $0cf0                         ;[0cf4] 10 fa
                    pop       bc                            ;[0cf6] c1
                    ret                                     ;[0cf7] c9

                    add       b                             ;[0cf8] 80
                    ld        (hl),e                        ;[0cf9] 73
                    ld        h,e                           ;[0cfa] 63
                    ld        (hl),d                        ;[0cfb] 72
                    ld        l,a                           ;[0cfc] 6f
                    ld        l,h                           ;[0cfd] 6c
                    ld        l,h                           ;[0cfe] 6c
                    cp        a                             ;[0cff] bf
                    rst       $08                           ;[0d00] cf
                    inc       c                             ;[0d01] 0c
                    cp        $02                           ;[0d02] fe 02
                    jr        c,$0c86                       ;[0d04] 38 80
                    add       (iy+$31)                      ;[0d06] fd 86 31
                    sub       $19                           ;[0d09] d6 19
                    ret       nc                            ;[0d0b] d0
                    neg                                     ;[0d0c] ed 44
                    push      bc                            ;[0d0e] c5
                    ld        b,a                           ;[0d0f] 47
                    ld        hl,($5c8f)                    ;[0d10] 2a 8f 5c
                    push      hl                            ;[0d13] e5
                    ld        hl,($5c91)                    ;[0d14] 2a 91 5c
                    push      hl                            ;[0d17] e5
                    call      $0d4d                         ;[0d18] cd 4d 0d
                    ld        a,b                           ;[0d1b] 78
                    push      af                            ;[0d1c] f5
                    ld        hl,$5c6b                      ;[0d1d] 21 6b 5c
                    ld        b,(hl)                        ;[0d20] 46
                    ld        a,b                           ;[0d21] 78
                    inc       a                             ;[0d22] 3c
                    ld        (hl),a                        ;[0d23] 77
                    ld        hl,$5c89                      ;[0d24] 21 89 5c
                    cp        (hl)                          ;[0d27] be
                    jr        c,$0d2d                       ;[0d28] 38 03
                    inc       (hl)                          ;[0d2a] 34
                    ld        b,$18                         ;[0d2b] 06 18
                    call      $0e00                         ;[0d2d] cd 00 0e
                    pop       af                            ;[0d30] f1
                    dec       a                             ;[0d31] 3d
                    jr        nz,$0d1c                      ;[0d32] 20 e8
                    pop       hl                            ;[0d34] e1
                    ld        (iy+$57),l                    ;[0d35] fd 75 57
                    pop       hl                            ;[0d38] e1
                    ld        ($5c8f),hl                    ;[0d39] 22 8f 5c
                    ld        bc,($5c88)                    ;[0d3c] ed 4b 88 5c
                    res       0,(iy+$02)                    ;[0d40] fd cb 02 86
                    call      $0dd9                         ;[0d44] cd d9 0d
                    set       0,(iy+$02)                    ;[0d47] fd cb 02 c6
                    pop       bc                            ;[0d4b] c1
                    ret                                     ;[0d4c] c9

                    xor       a                             ;[0d4d] af
                    ld        hl,($5c8d)                    ;[0d4e] 2a 8d 5c
                    bit       0,(iy+$02)                    ;[0d51] fd cb 02 46
                    jr        z,$0d5b                       ;[0d55] 28 04
                    ld        h,a                           ;[0d57] 67
                    ld        l,(iy+$0e)                    ;[0d58] fd 6e 0e
                    ld        ($5c8f),hl                    ;[0d5b] 22 8f 5c
                    ld        hl,$5c91                      ;[0d5e] 21 91 5c
                    jr        nz,$0d65                      ;[0d61] 20 02
                    ld        a,(hl)                        ;[0d63] 7e
                    rrca                                    ;[0d64] 0f
                    xor       (hl)                          ;[0d65] ae
                    and       $55                           ;[0d66] e6 55
                    xor       (hl)                          ;[0d68] ae
                    ld        (hl),a                        ;[0d69] 77
                    ret                                     ;[0d6a] c9

                    call      $0daf                         ;[0d6b] cd af 0d
                    ld        hl,$5c3c                      ;[0d6e] 21 3c 5c
                    res       5,(hl)                        ;[0d71] cb ae
                    set       0,(hl)                        ;[0d73] cb c6
                    call      $0d4d                         ;[0d75] cd 4d 0d
                    ld        b,(iy+$31)                    ;[0d78] fd 46 31
                    call      $0e44                         ;[0d7b] cd 44 0e
                    ld        hl,$5ac0                      ;[0d7e] 21 c0 5a
                    ld        a,($5c8d)                     ;[0d81] 3a 8d 5c
                    dec       b                             ;[0d84] 05
                    jr        $0d8e                         ;[0d85] 18 07
                    ld        c,$20                         ;[0d87] 0e 20
                    dec       hl                            ;[0d89] 2b
                    ld        (hl),a                        ;[0d8a] 77
                    dec       c                             ;[0d8b] 0d
                    jr        nz,$0d89                      ;[0d8c] 20 fb
                    djnz      $0d87                         ;[0d8e] 10 f7
                    ld        (iy+$31),$02                  ;[0d90] fd 36 31 02
                    ld        a,$fd                         ;[0d94] 3e fd
                    call      $1601                         ;[0d96] cd 01 16
                    ld        hl,($5c51)                    ;[0d99] 2a 51 5c
                    ld        de,$09f4                      ;[0d9c] 11 f4 09
                    and       a                             ;[0d9f] a7
                    ld        (hl),e                        ;[0da0] 73
                    inc       hl                            ;[0da1] 23
                    ld        (hl),d                        ;[0da2] 72
                    inc       hl                            ;[0da3] 23
                    ld        de,$10a8                      ;[0da4] 11 a8 10
                    ccf                                     ;[0da7] 3f
                    jr        c,$0da0                       ;[0da8] 38 f6
                    ld        bc,$1721                      ;[0daa] 01 21 17
                    jr        $0dd9                         ;[0dad] 18 2a
                    ld        hl,$0000                      ;[0daf] 21 00 00
                    ld        ($5c7d),hl                    ;[0db2] 22 7d 5c
                    res       0,(iy+$30)                    ;[0db5] fd cb 30 86
                    call      $0d94                         ;[0db9] cd 94 0d
                    ld        a,$fe                         ;[0dbc] 3e fe
                    call      $1601                         ;[0dbe] cd 01 16
                    call      $0d4d                         ;[0dc1] cd 4d 0d
                    ld        b,$18                         ;[0dc4] 06 18
                    call      $0e44                         ;[0dc6] cd 44 0e
                    ld        hl,($5c51)                    ;[0dc9] 2a 51 5c
                    ld        de,$09f4                      ;[0dcc] 11 f4 09
                    ld        (hl),e                        ;[0dcf] 73
                    inc       hl                            ;[0dd0] 23
                    ld        (hl),d                        ;[0dd1] 72
                    ld        (iy+$52),$01                  ;[0dd2] fd 36 52 01
                    ld        bc,$1821                      ;[0dd6] 01 21 18
                    ld        hl,$5b00                      ;[0dd9] 21 00 5b
                    bit       1,(iy+$01)                    ;[0ddc] fd cb 01 4e
                    jr        nz,$0df4                      ;[0de0] 20 12
                    ld        a,b                           ;[0de2] 78
                    bit       0,(iy+$02)                    ;[0de3] fd cb 02 46
                    jr        z,$0dee                       ;[0de7] 28 05
                    add       (iy+$31)                      ;[0de9] fd 86 31
                    sub       $18                           ;[0dec] d6 18
                    push      bc                            ;[0dee] c5
                    ld        b,a                           ;[0def] 47
                    call      $0e9b                         ;[0df0] cd 9b 0e
                    pop       bc                            ;[0df3] c1
                    ld        a,$21                         ;[0df4] 3e 21
                    sub       c                             ;[0df6] 91
                    ld        e,a                           ;[0df7] 5f
                    ld        d,$00                         ;[0df8] 16 00
                    add       hl,de                         ;[0dfa] 19
                    jp        $0adc                         ;[0dfb] c3 dc 0a
                    ld        b,$17                         ;[0dfe] 06 17
                    call      $0e9b                         ;[0e00] cd 9b 0e
                    ld        c,$08                         ;[0e03] 0e 08
                    push      bc                            ;[0e05] c5
                    push      hl                            ;[0e06] e5
                    ld        a,b                           ;[0e07] 78
                    and       $07                           ;[0e08] e6 07
                    ld        a,b                           ;[0e0a] 78
                    jr        nz,$0e19                      ;[0e0b] 20 0c
                    ex        de,hl                         ;[0e0d] eb
                    ld        hl,$f8e0                      ;[0e0e] 21 e0 f8
                    add       hl,de                         ;[0e11] 19
                    ex        de,hl                         ;[0e12] eb
                    ld        bc,$0020                      ;[0e13] 01 20 00
                    dec       a                             ;[0e16] 3d
                    ldir                                    ;[0e17] ed b0
                    ex        de,hl                         ;[0e19] eb
                    ld        hl,$ffe0                      ;[0e1a] 21 e0 ff
                    add       hl,de                         ;[0e1d] 19
                    ex        de,hl                         ;[0e1e] eb
                    ld        b,a                           ;[0e1f] 47
                    and       $07                           ;[0e20] e6 07
                    rrca                                    ;[0e22] 0f
                    rrca                                    ;[0e23] 0f
                    rrca                                    ;[0e24] 0f
                    ld        c,a                           ;[0e25] 4f
                    ld        a,b                           ;[0e26] 78
                    ld        b,$00                         ;[0e27] 06 00
                    ldir                                    ;[0e29] ed b0
                    ld        b,$07                         ;[0e2b] 06 07
                    add       hl,bc                         ;[0e2d] 09
                    and       $f8                           ;[0e2e] e6 f8
                    jr        nz,$0e0d                      ;[0e30] 20 db
                    pop       hl                            ;[0e32] e1
                    inc       h                             ;[0e33] 24
                    pop       bc                            ;[0e34] c1
                    dec       c                             ;[0e35] 0d
                    jr        nz,$0e05                      ;[0e36] 20 cd
                    call      $0e88                         ;[0e38] cd 88 0e
                    ld        hl,$ffe0                      ;[0e3b] 21 e0 ff
                    add       hl,de                         ;[0e3e] 19
                    ex        de,hl                         ;[0e3f] eb
                    ldir                                    ;[0e40] ed b0
                    ld        b,$01                         ;[0e42] 06 01
                    push      bc                            ;[0e44] c5
                    call      $0e9b                         ;[0e45] cd 9b 0e
                    ld        c,$08                         ;[0e48] 0e 08
                    push      bc                            ;[0e4a] c5
                    push      hl                            ;[0e4b] e5
                    ld        a,b                           ;[0e4c] 78
                    and       $07                           ;[0e4d] e6 07
                    rrca                                    ;[0e4f] 0f
                    rrca                                    ;[0e50] 0f
                    rrca                                    ;[0e51] 0f
                    ld        c,a                           ;[0e52] 4f
                    ld        a,b                           ;[0e53] 78
                    ld        b,$00                         ;[0e54] 06 00
                    dec       c                             ;[0e56] 0d
                    ld        d,h                           ;[0e57] 54
                    ld        e,l                           ;[0e58] 5d
                    ld        (hl),$00                      ;[0e59] 36 00
                    inc       de                            ;[0e5b] 13
                    ldir                                    ;[0e5c] ed b0
                    ld        de,$0701                      ;[0e5e] 11 01 07
                    add       hl,de                         ;[0e61] 19
                    dec       a                             ;[0e62] 3d
                    and       $f8                           ;[0e63] e6 f8
                    ld        b,a                           ;[0e65] 47
                    jr        nz,$0e4d                      ;[0e66] 20 e5
                    pop       hl                            ;[0e68] e1
                    inc       h                             ;[0e69] 24
                    pop       bc                            ;[0e6a] c1
                    dec       c                             ;[0e6b] 0d
                    jr        nz,$0e4a                      ;[0e6c] 20 dc
                    call      $0e88                         ;[0e6e] cd 88 0e
                    ld        h,d                           ;[0e71] 62
                    ld        l,e                           ;[0e72] 6b
                    inc       de                            ;[0e73] 13
                    ld        a,($5c8d)                     ;[0e74] 3a 8d 5c
                    bit       0,(iy+$02)                    ;[0e77] fd cb 02 46
                    jr        z,$0e80                       ;[0e7b] 28 03
                    ld        a,($5c48)                     ;[0e7d] 3a 48 5c
                    ld        (hl),a                        ;[0e80] 77
                    dec       bc                            ;[0e81] 0b
                    ldir                                    ;[0e82] ed b0
                    pop       bc                            ;[0e84] c1
                    ld        c,$21                         ;[0e85] 0e 21
                    ret                                     ;[0e87] c9

                    ld        a,h                           ;[0e88] 7c
                    rrca                                    ;[0e89] 0f
                    rrca                                    ;[0e8a] 0f
                    rrca                                    ;[0e8b] 0f
                    dec       a                             ;[0e8c] 3d
                    or        $50                           ;[0e8d] f6 50
                    ld        h,a                           ;[0e8f] 67
                    ex        de,hl                         ;[0e90] eb
                    ld        h,c                           ;[0e91] 61
                    ld        l,b                           ;[0e92] 68
                    add       hl,hl                         ;[0e93] 29
                    add       hl,hl                         ;[0e94] 29
                    add       hl,hl                         ;[0e95] 29
                    add       hl,hl                         ;[0e96] 29
                    add       hl,hl                         ;[0e97] 29
                    ld        b,h                           ;[0e98] 44
                    ld        c,l                           ;[0e99] 4d
                    ret                                     ;[0e9a] c9

                    ld        a,$18                         ;[0e9b] 3e 18
                    sub       b                             ;[0e9d] 90
                    ld        d,a                           ;[0e9e] 57
                    rrca                                    ;[0e9f] 0f
                    rrca                                    ;[0ea0] 0f
                    rrca                                    ;[0ea1] 0f
                    and       $e0                           ;[0ea2] e6 e0
                    ld        l,a                           ;[0ea4] 6f
                    ld        a,d                           ;[0ea5] 7a
                    and       $18                           ;[0ea6] e6 18
                    or        $40                           ;[0ea8] f6 40
                    ld        h,a                           ;[0eaa] 67
                    ret                                     ;[0eab] c9

                    ld        bc,$f650                      ;[0eac] 01 50 f6
                    rst       $08                           ;[0eaf] cf
                    sub       d                             ;[0eb0] 92
                    ret                                     ;[0eb1] c9

                    push      hl                            ;[0eb2] e5
                    push      bc                            ;[0eb3] c5
                    call      $0ef4                         ;[0eb4] cd f4 0e
                    pop       bc                            ;[0eb7] c1
                    pop       hl                            ;[0eb8] e1
                    inc       h                             ;[0eb9] 24
                    ld        a,h                           ;[0eba] 7c
                    and       $07                           ;[0ebb] e6 07
                    jr        nz,$0ec9                      ;[0ebd] 20 0a
                    ld        a,l                           ;[0ebf] 7d
                    add       $20                           ;[0ec0] c6 20
                    ld        l,a                           ;[0ec2] 6f
                    ccf                                     ;[0ec3] 3f
                    sbc       a                             ;[0ec4] 9f
                    and       $f8                           ;[0ec5] e6 f8
                    add       h                             ;[0ec7] 84
                    ld        h,a                           ;[0ec8] 67
                    djnz      $0eb2                         ;[0ec9] 10 e7
                    jr        $0eda                         ;[0ecb] 18 0d
                    ld        e,a                           ;[0ecd] 5f
                    sub       $a5                           ;[0ece] d6 a5
                    jp        nc,$0c10                      ;[0ed0] d2 10 0c
                    ld        bc,$fb50                      ;[0ed3] 01 50 fb
                    rst       $08                           ;[0ed6] cf
                    sub       d                             ;[0ed7] 92
                    ret                                     ;[0ed8] c9

                    ld        sp,hl                         ;[0ed9] f9
                    ld        a,$04                         ;[0eda] 3e 04
                    out       ($fb),a                       ;[0edc] d3 fb
                    ei                                      ;[0ede] fb
                    ld        hl,$5b00                      ;[0edf] 21 00 5b
                    ld        (iy+$46),l                    ;[0ee2] fd 75 46
                    xor       a                             ;[0ee5] af
                    ld        b,a                           ;[0ee6] 47
                    ld        (hl),a                        ;[0ee7] 77
                    inc       hl                            ;[0ee8] 23
                    djnz      $0ee7                         ;[0ee9] 10 fc
                    res       1,(iy+$30)                    ;[0eeb] fd cb 30 8e
                    ld        c,$21                         ;[0eef] 0e 21
                    jp        $0dd9                         ;[0ef1] c3 d9 0d
                    ld        a,b                           ;[0ef4] 78
                    cp        $03                           ;[0ef5] fe 03
                    sbc       a                             ;[0ef7] 9f
                    and       $02                           ;[0ef8] e6 02
                    out       ($fb),a                       ;[0efa] d3 fb
                    ld        d,a                           ;[0efc] 57
                    call      $1f54                         ;[0efd] cd 54 1f
                    jr        c,$0f0c                       ;[0f00] 38 0a
                    ld        a,$04                         ;[0f02] 3e 04
                    out       ($fb),a                       ;[0f04] d3 fb
                    ei                                      ;[0f06] fb
                    call      $0edf                         ;[0f07] cd df 0e
                    rst       $08                           ;[0f0a] cf
                    inc       c                             ;[0f0b] 0c
                    in        a,($fb)                       ;[0f0c] db fb
                    add       a                             ;[0f0e] 87
                    ret       m                             ;[0f0f] f8
                    jr        nc,$0efd                      ;[0f10] 30 eb
                    ld        c,$20                         ;[0f12] 0e 20
                    ld        e,(hl)                        ;[0f14] 5e
                    inc       hl                            ;[0f15] 23
                    ld        b,$08                         ;[0f16] 06 08
                    rl        d                             ;[0f18] cb 12
                    rl        e                             ;[0f1a] cb 13
                    rr        d                             ;[0f1c] cb 1a
                    in        a,($fb)                       ;[0f1e] db fb
                    rra                                     ;[0f20] 1f
                    jr        nc,$0f1e                      ;[0f21] 30 fb
                    ld        a,d                           ;[0f23] 7a
                    out       ($fb),a                       ;[0f24] d3 fb
                    djnz      $0f18                         ;[0f26] 10 f0
                    dec       c                             ;[0f28] 0d
                    jr        nz,$0f14                      ;[0f29] 20 e9
                    ret                                     ;[0f2b] c9

                    ld        hl,($5c3d)                    ;[0f2c] 2a 3d 5c
                    push      hl                            ;[0f2f] e5
                    ld        hl,$107f                      ;[0f30] 21 7f 10
                    push      hl                            ;[0f33] e5
                    ld        ($5c3d),sp                    ;[0f34] ed 73 3d 5c
                    call      $15d4                         ;[0f38] cd d4 15
                    push      af                            ;[0f3b] f5
                    ld        d,$00                         ;[0f3c] 16 00
                    ld        e,(iy-$01)                    ;[0f3e] fd 5e ff
                    ld        hl,$00c8                      ;[0f41] 21 c8 00
                    call      $03b5                         ;[0f44] cd b5 03
                    pop       af                            ;[0f47] f1
                    ld        hl,$0f38                      ;[0f48] 21 38 0f
                    push      hl                            ;[0f4b] e5
                    cp        $18                           ;[0f4c] fe 18
                    jr        nc,$0f81                      ;[0f4e] 30 31
                    cp        $07                           ;[0f50] fe 07
                    jr        c,$0f81                       ;[0f52] 38 2d
                    cp        $10                           ;[0f54] fe 10
                    jr        c,$0f92                       ;[0f56] 38 3a
                    ld        bc,$0002                      ;[0f58] 01 02 00
                    ld        d,a                           ;[0f5b] 57
                    cp        $16                           ;[0f5c] fe 16
                    jr        c,$0f6c                       ;[0f5e] 38 0c
                    inc       bc                            ;[0f60] 03
                    bit       7,(iy+$37)                    ;[0f61] fd cb 37 7e
                    jp        z,$101e                       ;[0f65] ca 1e 10
                    call      $15d4                         ;[0f68] cd d4 15
                    ld        e,a                           ;[0f6b] 5f
                    call      $15d4                         ;[0f6c] cd d4 15
                    push      de                            ;[0f6f] d5
                    ld        hl,($5c5b)                    ;[0f70] 2a 5b 5c
                    res       0,(iy+$07)                    ;[0f73] fd cb 07 86
                    call      $1655                         ;[0f77] cd 55 16
                    pop       bc                            ;[0f7a] c1
                    inc       hl                            ;[0f7b] 23
                    ld        (hl),b                        ;[0f7c] 70
                    inc       hl                            ;[0f7d] 23
                    ld        (hl),c                        ;[0f7e] 71
                    jr        $0f8b                         ;[0f7f] 18 0a
                    res       0,(iy+$07)                    ;[0f81] fd cb 07 86
                    ld        hl,($5c5b)                    ;[0f85] 2a 5b 5c
                    call      $1652                         ;[0f88] cd 52 16
                    ld        (de),a                        ;[0f8b] 12
                    inc       de                            ;[0f8c] 13
                    ld        ($5c5b),de                    ;[0f8d] ed 53 5b 5c
                    ret                                     ;[0f91] c9

                    ld        e,a                           ;[0f92] 5f
                    ld        d,$00                         ;[0f93] 16 00
                    ld        hl,$0f99                      ;[0f95] 21 99 0f
                    add       hl,de                         ;[0f98] 19
                    ld        e,(hl)                        ;[0f99] 5e
                    add       hl,de                         ;[0f9a] 19
                    push      hl                            ;[0f9b] e5
                    ld        hl,($5c5b)                    ;[0f9c] 2a 5b 5c
                    ret                                     ;[0f9f] c9

                    add       hl,bc                         ;[0fa0] 09
                    ld        h,(hl)                        ;[0fa1] 66
                    ld        l,d                           ;[0fa2] 6a
                    ld        d,b                           ;[0fa3] 50
                    or        l                             ;[0fa4] b5
                    ld        (hl),b                        ;[0fa5] 70
                    ld        a,(hl)                        ;[0fa6] 7e
                    or        d                             ;[0fa7] b2
                    or        c                             ;[0fa8] b1
                    ld        hl,($5c49)                    ;[0fa9] 2a 49 5c
                    bit       5,(iy+$37)                    ;[0fac] fd cb 37 6e
                    jp        $1097                         ;[0fb0] c3 97 10
                    cp        $28                           ;[0fb3] fe 28
                    ld        a,$00                         ;[0fb5] 3e 00
                    jr        nz,$0fc9                      ;[0fb7] 20 10
                    rst       $20                           ;[0fb9] e7
                    call      $1c82                         ;[0fba] cd 82 1c
                    cp        $29                           ;[0fbd] fe 29
                    jp        nz,$11f2                      ;[0fbf] c2 f2 11
                    rst       $20                           ;[0fc2] e7
                    call      $2530                         ;[0fc3] cd 30 25
                    call      nz,$1e94                      ;[0fc6] c4 94 1e
                    call      $2530                         ;[0fc9] cd 30 25
                    jp        z,$26c3                       ;[0fcc] ca c3 26
                    cp        $04                           ;[0fcf] fe 04
                    jp        nc,$24f9                      ;[0fd1] d2 f9 24
                    ld        hl,$3140                      ;[0fd4] 21 40 31
                    add       a                             ;[0fd7] 87
                    add       hl,a                          ;[0fd8] ed 31
                    call      $2705                         ;[0fda] cd 05 27
                    ld        d,a                           ;[0fdd] 57
                    jr        z,$0fde                       ;[0fde] 28 fe
                    ld        (bc),a                        ;[0fe0] 02
                    jr        z,$0fe5                       ;[0fe1] 28 02
                    ld        d,$00                         ;[0fe3] 16 00
                    and       a                             ;[0fe5] a7
                    jr        nz,$0fee                      ;[0fe6] 20 06
                    ld        a,e                           ;[0fe8] 7b
                    inc       a                             ;[0fe9] 3c
                    jr        nz,$0fee                      ;[0fea] 20 02
                    ld        e,$64                         ;[0fec] 1e 64
                    ld        b,d                           ;[0fee] 42
                    ld        c,e                           ;[0fef] 4b
                    jp        $217e                         ;[0ff0] c3 7e 21
                    jr        $1001                         ;[0ff3] 18 0c
                    scf                                     ;[0ff5] 37
                    jr        nz,$0ffe                      ;[0ff6] 20 06
                    push      de                            ;[0ff8] d5
                    call      $1c81                         ;[0ff9] cd 81 1c
                    pop       de                            ;[0ffc] d1
                    and       a                             ;[0ffd] a7
                    rl        d                             ;[0ffe] cb 12
                    ret                                     ;[1000] c9

                    ld        (iy+$00),$10                  ;[1001] fd 36 00 10
                    jr        $1024                         ;[1005] 18 1d
                    call      $1031                         ;[1007] cd 31 10
                    jr        $1011                         ;[100a] 18 05
                    ld        a,(hl)                        ;[100c] 7e
                    cp        $0d                           ;[100d] fe 0d
                    ret       z                             ;[100f] c8
                    inc       hl                            ;[1010] 23
                    ld        ($5c5b),hl                    ;[1011] 22 5b 5c
                    ret                                     ;[1014] c9

                    call      $1031                         ;[1015] cd 31 10
                    ld        bc,$0001                      ;[1018] 01 01 00
                    jp        $19e8                         ;[101b] c3 e8 19
                    call      $15d4                         ;[101e] cd d4 15
                    call      $15d4                         ;[1021] cd d4 15
                    pop       hl                            ;[1024] e1
                    pop       hl                            ;[1025] e1
                    pop       hl                            ;[1026] e1
                    ld        ($5c3d),hl                    ;[1027] 22 3d 5c
                    bit       7,(iy+$00)                    ;[102a] fd cb 00 7e
                    ret       nz                            ;[102e] c0
                    ld        sp,hl                         ;[102f] f9
                    ret                                     ;[1030] c9

                    scf                                     ;[1031] 37
                    call      $1195                         ;[1032] cd 95 11
                    sbc       hl,de                         ;[1035] ed 52
                    add       hl,de                         ;[1037] 19
                    inc       hl                            ;[1038] 23
                    pop       bc                            ;[1039] c1
                    ret       c                             ;[103a] d8
                    push      bc                            ;[103b] c5
                    ld        b,h                           ;[103c] 44
                    ld        c,l                           ;[103d] 4d
                    ld        h,d                           ;[103e] 62
                    ld        l,e                           ;[103f] 6b
                    inc       hl                            ;[1040] 23
                    ld        a,(de)                        ;[1041] 1a
                    and       $f0                           ;[1042] e6 f0
                    cp        $10                           ;[1044] fe 10
                    jr        nz,$1051                      ;[1046] 20 09
                    inc       hl                            ;[1048] 23
                    ld        a,(de)                        ;[1049] 1a
                    sub       $17                           ;[104a] d6 17
                    adc       $00                           ;[104c] ce 00
                    jr        nz,$1051                      ;[104e] 20 01
                    inc       hl                            ;[1050] 23
                    and       a                             ;[1051] a7
                    sbc       hl,bc                         ;[1052] ed 42
                    add       hl,bc                         ;[1054] 09
                    ex        de,hl                         ;[1055] eb
                    jr        c,$103e                       ;[1056] 38 e6
                    ret                                     ;[1058] c9

                    ret                                     ;[1059] c9

                    rst       $20                           ;[105a] e7
                    cp        $24                           ;[105b] fe 24
                    jp        nz,$0fb3                      ;[105d] c2 b3 0f
                    ld        de,$2089                      ;[1060] 11 89 20
                    rst       $20                           ;[1063] e7
                    call      $2530                         ;[1064] cd 30 25
                    call      nz,$3622                      ;[1067] c4 22 36
                    res       6,(iy+$01)                    ;[106a] fd cb 01 b6
                    jp        $2712                         ;[106e] c3 12 27
                    call      $1e94                         ;[1071] cd 94 1e
                    ld        bc,$243b                      ;[1074] 01 3b 24
                    out       (c),a                         ;[1077] ed 79
                    inc       b                             ;[1079] 04
                    in        a,(c)                         ;[107a] ed 78
                    jp        $2d28                         ;[107c] c3 28 2d
                    bit       4,(iy+$30)                    ;[107f] fd cb 30 66
                    jr        z,$1026                       ;[1083] 28 a1
                    ld        (iy+$00),$ff                  ;[1085] fd 36 00 ff
                    ld        d,$00                         ;[1089] 16 00
                    ld        e,(iy-$02)                    ;[108b] fd 5e fe
                    ld        hl,$1a90                      ;[108e] 21 90 1a
                    call      $03b5                         ;[1091] cd b5 03
                    jp        $0f30                         ;[1094] c3 30 0f
                    push      hl                            ;[1097] e5
                    call      $1190                         ;[1098] cd 90 11
                    dec       hl                            ;[109b] 2b
                    call      $19e5                         ;[109c] cd e5 19
                    ld        ($5c5b),hl                    ;[109f] 22 5b 5c
                    ld        (iy+$07),$00                  ;[10a2] fd 36 07 00
                    pop       hl                            ;[10a6] e1
                    ret                                     ;[10a7] c9

                    jp        $10af                         ;[10a8] c3 af 10
                    nop                                     ;[10ab] 00
                    nop                                     ;[10ac] 00
                    nop                                     ;[10ad] 00
                    nop                                     ;[10ae] 00
                    and       a                             ;[10af] a7
                    bit       5,(iy+$01)                    ;[10b0] fd cb 01 6e
                    ret       z                             ;[10b4] c8
                    ld        a,($5c08)                     ;[10b5] 3a 08 5c
                    res       5,(iy+$01)                    ;[10b8] fd cb 01 ae
                    push      af                            ;[10bc] f5
                    bit       5,(iy+$02)                    ;[10bd] fd cb 02 6e
                    call      nz,$0d6e                      ;[10c1] c4 6e 0d
                    pop       af                            ;[10c4] f1
                    cp        $20                           ;[10c5] fe 20
                    jr        nc,$111b                      ;[10c7] 30 52
                    cp        $10                           ;[10c9] fe 10
                    jr        nc,$10fa                      ;[10cb] 30 2d
                    cp        $06                           ;[10cd] fe 06
                    jr        nc,$10db                      ;[10cf] 30 0a
                    ld        b,a                           ;[10d1] 47
                    and       $01                           ;[10d2] e6 01
                    ld        c,a                           ;[10d4] 4f
                    ld        a,b                           ;[10d5] 78
                    rra                                     ;[10d6] 1f
                    add       $12                           ;[10d7] c6 12
                    jr        $1105                         ;[10d9] 18 2a
                    jr        nz,$10e6                      ;[10db] 20 09
                    ld        hl,$5c6a                      ;[10dd] 21 6a 5c
                    ld        a,$08                         ;[10e0] 3e 08
                    xor       (hl)                          ;[10e2] ae
                    ld        (hl),a                        ;[10e3] 77
                    jr        $10f4                         ;[10e4] 18 0e
                    cp        $0e                           ;[10e6] fe 0e
                    ret       c                             ;[10e8] d8
                    sub       $0d                           ;[10e9] d6 0d
                    ld        hl,$5c41                      ;[10eb] 21 41 5c
                    cp        (hl)                          ;[10ee] be
                    ld        (hl),a                        ;[10ef] 77
                    jr        nz,$10f4                      ;[10f0] 20 02
                    ld        (hl),$00                      ;[10f2] 36 00
                    set       3,(iy+$02)                    ;[10f4] fd cb 02 de
                    cp        a                             ;[10f8] bf
                    ret                                     ;[10f9] c9

                    ld        b,a                           ;[10fa] 47
                    and       $07                           ;[10fb] e6 07
                    ld        c,a                           ;[10fd] 4f
                    ld        a,$10                         ;[10fe] 3e 10
                    bit       3,b                           ;[1100] cb 58
                    jr        nz,$1105                      ;[1102] 20 01
                    inc       a                             ;[1104] 3c
                    ld        (iy-$2d),c                    ;[1105] fd 71 d3
                    ld        de,$110d                      ;[1108] 11 0d 11
                    jr        $1113                         ;[110b] 18 06
                    ld        a,($5c0d)                     ;[110d] 3a 0d 5c
                    ld        de,$10a8                      ;[1110] 11 a8 10
                    ld        hl,($5c4f)                    ;[1113] 2a 4f 5c
                    inc       hl                            ;[1116] 23
                    inc       hl                            ;[1117] 23
                    ld        (hl),e                        ;[1118] 73
                    inc       hl                            ;[1119] 23
                    ld        (hl),d                        ;[111a] 72
                    scf                                     ;[111b] 37
                    ret                                     ;[111c] c9

                    call      $0d4d                         ;[111d] cd 4d 0d
                    res       3,(iy+$02)                    ;[1120] fd cb 02 9e
                    res       5,(iy+$02)                    ;[1124] fd cb 02 ae
                    ld        hl,($5c8a)                    ;[1128] 2a 8a 5c
                    push      hl                            ;[112b] e5
                    ld        hl,($5c3d)                    ;[112c] 2a 3d 5c
                    push      hl                            ;[112f] e5
                    ld        hl,$1167                      ;[1130] 21 67 11
                    push      hl                            ;[1133] e5
                    ld        ($5c3d),sp                    ;[1134] ed 73 3d 5c
                    ld        hl,($5c82)                    ;[1138] 2a 82 5c
                    push      hl                            ;[113b] e5
                    scf                                     ;[113c] 37
                    call      $1195                         ;[113d] cd 95 11
                    ex        de,hl                         ;[1140] eb
                    call      $187d                         ;[1141] cd 7d 18
                    ex        de,hl                         ;[1144] eb
                    call      $18e1                         ;[1145] cd e1 18
                    ld        hl,($5c8a)                    ;[1148] 2a 8a 5c
                    ex        (sp),hl                       ;[114b] e3
                    ex        de,hl                         ;[114c] eb
                    call      $0d4d                         ;[114d] cd 4d 0d
                    ld        a,($5c8b)                     ;[1150] 3a 8b 5c
                    sub       d                             ;[1153] 92
                    jr        c,$117c                       ;[1154] 38 26
                    jr        nz,$115e                      ;[1156] 20 06
                    ld        a,e                           ;[1158] 7b
                    sub       (iy+$50)                      ;[1159] fd 96 50
                    jr        nc,$117c                      ;[115c] 30 1e
                    ld        a,$20                         ;[115e] 3e 20
                    push      de                            ;[1160] d5
                    call      $09f4                         ;[1161] cd f4 09
                    pop       de                            ;[1164] d1
                    jr        $1150                         ;[1165] 18 e9
                    ld        d,$00                         ;[1167] 16 00
                    ld        e,(iy-$02)                    ;[1169] fd 5e fe
                    ld        hl,$1a90                      ;[116c] 21 90 1a
                    call      $03b5                         ;[116f] cd b5 03
                    ld        (iy+$00),$ff                  ;[1172] fd 36 00 ff
                    ld        de,($5c8a)                    ;[1176] ed 5b 8a 5c
                    jr        $117e                         ;[117a] 18 02
                    pop       de                            ;[117c] d1
                    pop       hl                            ;[117d] e1
                    pop       hl                            ;[117e] e1
                    ld        ($5c3d),hl                    ;[117f] 22 3d 5c
                    pop       bc                            ;[1182] c1
                    push      de                            ;[1183] d5
                    call      $0dd9                         ;[1184] cd d9 0d
                    pop       hl                            ;[1187] e1
                    ld        ($5c82),hl                    ;[1188] 22 82 5c
                    ld        (iy+$26),$00                  ;[118b] fd 36 26 00
                    ret                                     ;[118f] c9

                    ld        hl,($5c61)                    ;[1190] 2a 61 5c
                    dec       hl                            ;[1193] 2b
                    and       a                             ;[1194] a7
                    ld        de,($5c61)                    ;[1195] ed 5b 61 5c
                    ret       c                             ;[1199] d8
                    ld        hl,($5c63)                    ;[119a] 2a 63 5c
                    ret                                     ;[119d] c9

                    nop                                     ;[119e] 00
                    nop                                     ;[119f] 00
                    nop                                     ;[11a0] 00
                    nop                                     ;[11a1] 00
                    nop                                     ;[11a2] 00
                    nop                                     ;[11a3] 00
                    nop                                     ;[11a4] 00
                    nop                                     ;[11a5] 00
                    nop                                     ;[11a6] 00
                    ld        a,(hl)                        ;[11a7] 7e
                    cp        $0e                           ;[11a8] fe 0e
                    ld        bc,$0006                      ;[11aa] 01 06 00
                    call      z,$19e8                       ;[11ad] cc e8 19
                    ld        a,(hl)                        ;[11b0] 7e
                    inc       hl                            ;[11b1] 23
                    cp        $0d                           ;[11b2] fe 0d
                    jr        nz,$11a7                      ;[11b4] 20 f1
                    ret                                     ;[11b6] c9

                    dec       c                             ;[11b7] 0d
                    jr        z,$1225                       ;[11b8] 28 6b
                    bit       6,(iy+$01)                    ;[11ba] fd cb 01 76
                    jr        z,$11f2                       ;[11be] 28 32
                    rst       $20                           ;[11c0] e7
                    cp        $28                           ;[11c1] fe 28
                    jr        nz,$11f2                      ;[11c3] 20 2d
                    rst       $20                           ;[11c5] e7
                    call      $2530                         ;[11c6] cd 30 25
                    jr        nz,$11f4                      ;[11c9] 20 29
                    call      $24fb                         ;[11cb] cd fb 24
                    cp        $2c                           ;[11ce] fe 2c
                    jr        nz,$11f2                      ;[11d0] 20 20
                    ld        de,($5c3b)                    ;[11d2] ed 5b 3b 5c
                    ld        d,$01                         ;[11d6] 16 01
                    rst       $20                           ;[11d8] e7
                    push      de                            ;[11d9] d5
                    call      $24fb                         ;[11da] cd fb 24
                    pop       de                            ;[11dd] d1
                    inc       d                             ;[11de] 14
                    jr        z,$11f2                       ;[11df] 28 11
                    ld        a,($5c3b)                     ;[11e1] 3a 3b 5c
                    xor       e                             ;[11e4] ab
                    and       $40                           ;[11e5] e6 40
                    jr        nz,$11f2                      ;[11e7] 20 09
                    rst       $18                           ;[11e9] df
                    cp        $2c                           ;[11ea] fe 2c
                    jr        z,$11d8                       ;[11ec] 28 ea
                    cp        $29                           ;[11ee] fe 29
                    jr        z,$1221                       ;[11f0] 28 2f
                    rst       $08                           ;[11f2] cf
                    dec       bc                            ;[11f3] 0b
                    call      $2da2                         ;[11f4] cd a2 2d
                    jr        c,$1200                       ;[11f7] 38 07
                    jr        nz,$1200                      ;[11f9] 20 05
                    inc       b                             ;[11fb] 04
                    dec       b                             ;[11fc] 05
                    ld        b,c                           ;[11fd] 41
                    jr        z,$1202                       ;[11fe] 28 02
                    ld        b,$ff                         ;[1200] 06 ff
                    inc       b                             ;[1202] 04
                    rst       $18                           ;[1203] df
                    jr        $1209                         ;[1204] 18 03
                    call      $134f                         ;[1206] cd 4f 13
                    djnz      $1206                         ;[1209] 10 fb
                    ld        ($5c5d),hl                    ;[120b] 22 5d 5c
                    call      $24fb                         ;[120e] cd fb 24
                    cp        $29                           ;[1211] fe 29
                    jr        z,$1221                       ;[1213] 28 0c
                    rst       $20                           ;[1215] e7
                    ld        b,$02                         ;[1216] 06 02
                    call      $134f                         ;[1218] cd 4f 13
                    djnz      $1216                         ;[121b] 10 f9
                    ld        ($5c5d),de                    ;[121d] ed 53 5d 5c
                    rst       $20                           ;[1221] e7
                    jp        $2713                         ;[1222] c3 13 27
                    bit       6,(iy+$01)                    ;[1225] fd cb 01 76
                    jr        nz,$11f2                      ;[1229] 20 c7
                    ld        d,$00                         ;[122b] 16 00
                    rst       $20                           ;[122d] e7
                    ld        hl,$1347                      ;[122e] 21 47 13
                    ld        bc,$0008                      ;[1231] 01 08 00
                    cpir                                    ;[1234] ed b1
                    jr        nz,$1253                      ;[1236] 20 1b
                    ld        e,c                           ;[1238] 59
                    setae                                   ;[1239] ed 95
                    or        d                             ;[123b] b2
                    ld        d,a                           ;[123c] 57
                    rra                                     ;[123d] 1f
                    jr        nc,$122d                      ;[123e] 30 ed
                    push      de                            ;[1240] d5
                    rst       $20                           ;[1241] e7
                    call      $1c8c                         ;[1242] cd 8c 1c
                    cp        $2c                           ;[1245] fe 2c
                    jr        nz,$11f2                      ;[1247] 20 a9
                    rst       $20                           ;[1249] e7
                    call      $1c8c                         ;[124a] cd 8c 1c
                    cp        $29                           ;[124d] fe 29
                    jr        nz,$11f2                      ;[124f] 20 a1
                    rst       $20                           ;[1251] e7
                    pop       de                            ;[1252] d1
                    cp        $5d                           ;[1253] fe 5d
                    jr        nz,$11f2                      ;[1255] 20 9b
                    rst       $20                           ;[1257] e7
                    call      $2530                         ;[1258] cd 30 25
                    jp        z,$2713                       ;[125b] ca 13 27
                    ld        a,d                           ;[125e] 7a
                    add       a                             ;[125f] 87
                    jr        nc,$1264                      ;[1260] 30 02
                    or        $c0                           ;[1262] f6 c0
                    push      af                            ;[1264] f5
                    bit       1,a                           ;[1265] cb 4f
                    jr        z,$1284                       ;[1267] 28 1b
                    call      $2bf1                         ;[1269] cd f1 2b
                    ld        a,b                           ;[126c] 78
                    and       a                             ;[126d] a7
                    ld        a,c                           ;[126e] 79
                    jr        nz,$1282                      ;[126f] 20 11
                    push      de                            ;[1271] d5
                    push      af                            ;[1272] f5
                    call      $2bf1                         ;[1273] cd f1 2b
                    pop       af                            ;[1276] f1
                    pop       hl                            ;[1277] e1
                    inc       b                             ;[1278] 04
                    dec       b                             ;[1279] 05
                    ld        b,a                           ;[127a] 47
                    jr        nz,$1282                      ;[127b] 20 05
                    ld        a,c                           ;[127d] 79
                    and       a                             ;[127e] a7
                    exx                                     ;[127f] d9
                    jr        nz,$1284                      ;[1280] 20 02
                    rst       $08                           ;[1282] cf
                    add       hl,bc                         ;[1283] 09
                    call      $2bf1                         ;[1284] cd f1 2b
                    pop       hl                            ;[1287] e1
                    ld        a,b                           ;[1288] 78
                    or        c                             ;[1289] b1
                    jp        z,$25db                       ;[128a] ca db 25
                    bit       7,h                           ;[128d] cb 7c
                    jr        z,$129a                       ;[128f] 28 09
                    ld        a,(de)                        ;[1291] 1a
                    cp        $21                           ;[1292] fe 21
                    jr        nc,$129a                      ;[1294] 30 04
                    inc       de                            ;[1296] 13
                    dec       bc                            ;[1297] 0b
                    jr        $1288                         ;[1298] 18 ee
                    push      de                            ;[129a] d5
                    bit       6,h                           ;[129b] cb 74
                    jr        z,$12bd                       ;[129d] 28 1e
                    ld        l,$ff                         ;[129f] 2e ff
                    bit       5,h                           ;[12a1] cb 6c
                    jr        z,$12a7                       ;[12a3] 28 02
                    ld        l,$7f                         ;[12a5] 2e 7f
                    ex        de,hl                         ;[12a7] eb
                    add       hl,bc                         ;[12a8] 09
                    ex        de,hl                         ;[12a9] eb
                    dec       de                            ;[12aa] 1b
                    ld        a,(de)                        ;[12ab] 1a
                    and       l                             ;[12ac] a5
                    cp        $21                           ;[12ad] fe 21
                    jr        nc,$12bd                      ;[12af] 30 0c
                    dec       de                            ;[12b1] 1b
                    dec       bc                            ;[12b2] 0b
                    ld        l,$ff                         ;[12b3] 2e ff
                    res       5,h                           ;[12b5] cb ac
                    ld        a,b                           ;[12b7] 78
                    or        c                             ;[12b8] b1
                    jr        nz,$12ab                      ;[12b9] 20 f0
                    jr        $1287                         ;[12bb] 18 ca
                    push      hl                            ;[12bd] e5
                    rst       $30                           ;[12be] f7
                    pop       af                            ;[12bf] f1
                    pop       hl                            ;[12c0] e1
                    push      de                            ;[12c1] d5
                    push      bc                            ;[12c2] c5
                    add       hl,bc                         ;[12c3] 09
                    ex        de,hl                         ;[12c4] eb
                    add       hl,bc                         ;[12c5] 09
                    ex        de,hl                         ;[12c6] eb
                    ex        af,af'                        ;[12c7] 08
                    dec       de                            ;[12c8] 1b
                    dec       hl                            ;[12c9] 2b
                    ld        a,b                           ;[12ca] 78
                    or        c                             ;[12cb] b1
                    jr        z,$133f                       ;[12cc] 28 71
                    ld        a,(hl)                        ;[12ce] 7e
                    ex        af,af'                        ;[12cf] 08
                    bit       5,a                           ;[12d0] cb 6f
                    jr        z,$12da                       ;[12d2] 28 06
                    res       5,a                           ;[12d4] cb af
                    ex        af,af'                        ;[12d6] 08
                    and       $7f                           ;[12d7] e6 7f
                    ex        af,af'                        ;[12d9] 08
                    bit       4,a                           ;[12da] cb 67
                    jr        z,$12ec                       ;[12dc] 28 0e
                    ex        af,af'                        ;[12de] 08
                    cp        $61                           ;[12df] fe 61
                    jr        c,$12eb                       ;[12e1] 38 08
                    cp        $7b                           ;[12e3] fe 7b
                    jr        nc,$12eb                      ;[12e5] 30 04
                    and       $df                           ;[12e7] e6 df
                    jr        $12fb                         ;[12e9] 18 10
                    ex        af,af'                        ;[12eb] 08
                    bit       3,a                           ;[12ec] cb 5f
                    jr        z,$12fc                       ;[12ee] 28 0c
                    ex        af,af'                        ;[12f0] 08
                    cp        $41                           ;[12f1] fe 41
                    jr        c,$12fb                       ;[12f3] 38 06
                    cp        $5b                           ;[12f5] fe 5b
                    jr        nc,$12fb                      ;[12f7] 30 02
                    or        $20                           ;[12f9] f6 20
                    ex        af,af'                        ;[12fb] 08
                    bit       1,a                           ;[12fc] cb 4f
                    jr        z,$1323                       ;[12fe] 28 23
                    ex        af,af'                        ;[1300] 08
                    exx                                     ;[1301] d9
                    push      af                            ;[1302] f5
                    push      hl                            ;[1303] e5
                    push      de                            ;[1304] d5
                    push      bc                            ;[1305] c5
                    ex        de,hl                         ;[1306] eb
                    ld        b,$00                         ;[1307] 06 00
                    cpir                                    ;[1309] ed b1
                    ld        a,c                           ;[130b] 79
                    pop       bc                            ;[130c] c1
                    pop       de                            ;[130d] d1
                    pop       hl                            ;[130e] e1
                    jr        nz,$1320                      ;[130f] 20 0f
                    sub       c                             ;[1311] 91
                    neg                                     ;[1312] ed 44
                    dec       a                             ;[1314] 3d
                    cp        b                             ;[1315] b8
                    jr        nc,$1332                      ;[1316] 30 1a
                    inc       sp                            ;[1318] 33
                    inc       sp                            ;[1319] 33
                    push      hl                            ;[131a] e5
                    add       hl,a                          ;[131b] ed 31
                    ld        a,(hl)                        ;[131d] 7e
                    pop       hl                            ;[131e] e1
                    push      af                            ;[131f] f5
                    pop       af                            ;[1320] f1
                    exx                                     ;[1321] d9
                    ex        af,af'                        ;[1322] 08
                    bit       2,a                           ;[1323] cb 57
                    jr        z,$132d                       ;[1325] 28 06
                    res       2,a                           ;[1327] cb 97
                    ex        af,af'                        ;[1329] 08
                    or        $80                           ;[132a] f6 80
                    ex        af,af'                        ;[132c] 08
                    ex        af,af'                        ;[132d] 08
                    ld        (de),a                        ;[132e] 12
                    dec       bc                            ;[132f] 0b
                    jr        $12c8                         ;[1330] 18 96
                    pop       af                            ;[1332] f1
                    exx                                     ;[1333] d9
                    pop       af                            ;[1334] f1
                    ex        (sp),hl                       ;[1335] e3
                    inc       hl                            ;[1336] 23
                    ex        (sp),hl                       ;[1337] e3
                    push      af                            ;[1338] f5
                    ex        (sp),hl                       ;[1339] e3
                    dec       hl                            ;[133a] 2b
                    ex        (sp),hl                       ;[133b] e3
                    inc       de                            ;[133c] 13
                    jr        $132f                         ;[133d] 18 f0
                    pop       bc                            ;[133f] c1
                    pop       de                            ;[1340] d1
                    call      $2ab2                         ;[1341] cd b2 2a
                    jp        $2712                         ;[1344] c3 12 27
                    jr        z,$13a7                       ;[1347] 28 5e
                    dec       l                             ;[1349] 2d
                    dec       hl                            ;[134a] 2b
                    ld        a,(hl)                        ;[134b] 7e
                    ld        a,$3c                         ;[134c] 3e 3c
                    ret                                     ;[134e] c9

                    ld        d,h                           ;[134f] 54
                    ld        e,l                           ;[1350] 5d
                    ld        c,$00                         ;[1351] 0e 00
                    ld        a,(hl)                        ;[1353] 7e
                    inc       hl                            ;[1354] 23
                    cp        $22                           ;[1355] fe 22
                    jr        z,$136e                       ;[1357] 28 15
                    cp        $0e                           ;[1359] fe 0e
                    jr        z,$1376                       ;[135b] 28 19
                    cp        $28                           ;[135d] fe 28
                    jr        z,$137c                       ;[135f] 28 1b
                    cp        $29                           ;[1361] fe 29
                    jr        z,$1382                       ;[1363] 28 1d
                    cp        $2c                           ;[1365] fe 2c
                    jr        nz,$1353                      ;[1367] 20 ea
                    inc       c                             ;[1369] 0c
                    dec       c                             ;[136a] 0d
                    jr        nz,$1353                      ;[136b] 20 e6
                    ret                                     ;[136d] c9

                    ld        a,(hl)                        ;[136e] 7e
                    inc       hl                            ;[136f] 23
                    cp        $22                           ;[1370] fe 22
                    jr        nz,$136e                      ;[1372] 20 fa
                    jr        $1353                         ;[1374] 18 dd
                    add       hl,$0005                      ;[1376] ed 34 05 00
                    jr        $1353                         ;[137a] 18 d7
                    inc       c                             ;[137c] 0c
                    jp        p,$1353                       ;[137d] f2 53 13
                    rst       $08                           ;[1380] cf
                    rra                                     ;[1381] 1f
                    dec       c                             ;[1382] 0d
                    jp        p,$1353                       ;[1383] f2 53 13
                    ex        de,hl                         ;[1386] eb
                    dec       de                            ;[1387] 1b
                    ld        b,$01                         ;[1388] 06 01
                    ret                                     ;[138a] c9

                    nop                                     ;[138b] 00
                    nop                                     ;[138c] 00
                    nop                                     ;[138d] 00
                    nop                                     ;[138e] 00
                    nop                                     ;[138f] 00
                    nop                                     ;[1390] 00
                    add       b                             ;[1391] 80
                    ld        c,a                           ;[1392] 4f
                    bit       1,(hl)                        ;[1393] cb 4e
                    ld        b,l                           ;[1395] 45
                    ld        e,b                           ;[1396] 58
                    ld        d,h                           ;[1397] 54
                    jr        nz,$1411                      ;[1398] 20 77
                    ld        l,c                           ;[139a] 69
                    ld        (hl),h                        ;[139b] 74
                    ld        l,b                           ;[139c] 68
                    ld        l,a                           ;[139d] 6f
                    ld        (hl),l                        ;[139e] 75
                    ld        (hl),h                        ;[139f] 74
                    jr        nz,$13e8                      ;[13a0] 20 46
                    ld        c,a                           ;[13a2] 4f
                    jp        nc,$6156                      ;[13a3] d2 56 61
                    ld        (hl),d                        ;[13a6] 72
                    ld        l,c                           ;[13a7] 69
                    ld        h,c                           ;[13a8] 61
                    ld        h,d                           ;[13a9] 62
                    ld        l,h                           ;[13aa] 6c
                    ld        h,l                           ;[13ab] 65
                    jr        nz,$141c                      ;[13ac] 20 6e
                    ld        l,a                           ;[13ae] 6f
                    ld        (hl),h                        ;[13af] 74
                    jr        nz,$1418                      ;[13b0] 20 66
                    ld        l,a                           ;[13b2] 6f
                    ld        (hl),l                        ;[13b3] 75
                    ld        l,(hl)                        ;[13b4] 6e
                    call      po,$7553                      ;[13b5] e4 53 75
                    ld        h,d                           ;[13b8] 62
                    ld        (hl),e                        ;[13b9] 73
                    ld        h,e                           ;[13ba] 63
                    ld        (hl),d                        ;[13bb] 72
                    ld        l,c                           ;[13bc] 69
                    ld        (hl),b                        ;[13bd] 70
                    ld        (hl),h                        ;[13be] 74
                    jr        nz,$1438                      ;[13bf] 20 77
                    ld        (hl),d                        ;[13c1] 72
                    ld        l,a                           ;[13c2] 6f
                    ld        l,(hl)                        ;[13c3] 6e
                    rst       $20                           ;[13c4] e7
                    ld        c,a                           ;[13c5] 4f
                    ld        (hl),l                        ;[13c6] 75
                    ld        (hl),h                        ;[13c7] 74
                    jr        nz,$1439                      ;[13c8] 20 6f
                    ld        h,(hl)                        ;[13ca] 66
                    jr        nz,$143a                      ;[13cb] 20 6d
                    ld        h,l                           ;[13cd] 65
                    ld        l,l                           ;[13ce] 6d
                    ld        l,a                           ;[13cf] 6f
                    ld        (hl),d                        ;[13d0] 72
                    ld        sp,hl                         ;[13d1] f9
                    ld        c,a                           ;[13d2] 4f
                    ld        (hl),l                        ;[13d3] 75
                    ld        (hl),h                        ;[13d4] 74
                    jr        nz,$1446                      ;[13d5] 20 6f
                    ld        h,(hl)                        ;[13d7] 66
                    jr        nz,$144d                      ;[13d8] 20 73
                    ld        h,e                           ;[13da] 63
                    ld        (hl),d                        ;[13db] 72
                    ld        h,l                           ;[13dc] 65
                    ld        h,l                           ;[13dd] 65
                    xor       $4e                           ;[13de] ee 4e
                    ld        (hl),l                        ;[13e0] 75
                    ld        l,l                           ;[13e1] 6d
                    ld        h,d                           ;[13e2] 62
                    ld        h,l                           ;[13e3] 65
                    ld        (hl),d                        ;[13e4] 72
                    jr        nz,$145b                      ;[13e5] 20 74
                    ld        l,a                           ;[13e7] 6f
                    ld        l,a                           ;[13e8] 6f
                    jr        nz,$144d                      ;[13e9] 20 62
                    ld        l,c                           ;[13eb] 69
                    rst       $20                           ;[13ec] e7
                    ld        d,d                           ;[13ed] 52
                    ld        b,l                           ;[13ee] 45
                    ld        d,h                           ;[13ef] 54
                    ld        d,l                           ;[13f0] 55
                    ld        d,d                           ;[13f1] 52
                    ld        c,(hl)                        ;[13f2] 4e
                    jr        nz,$146c                      ;[13f3] 20 77
                    ld        l,c                           ;[13f5] 69
                    ld        (hl),h                        ;[13f6] 74
                    ld        l,b                           ;[13f7] 68
                    ld        l,a                           ;[13f8] 6f
                    ld        (hl),l                        ;[13f9] 75
                    ld        (hl),h                        ;[13fa] 74
                    jr        nz,$1444                      ;[13fb] 20 47
                    ld        c,a                           ;[13fd] 4f
                    ld        d,e                           ;[13fe] 53
                    ld        d,l                           ;[13ff] 55
                    jp        nz,$6e45                      ;[1400] c2 45 6e
                    ld        h,h                           ;[1403] 64
                    jr        nz,$1475                      ;[1404] 20 6f
                    ld        h,(hl)                        ;[1406] 66
                    jr        nz,$146f                      ;[1407] 20 66
                    ld        l,c                           ;[1409] 69
                    ld        l,h                           ;[140a] 6c
                    push      hl                            ;[140b] e5
                    ld        d,e                           ;[140c] 53
                    ld        d,h                           ;[140d] 54
                    ld        c,a                           ;[140e] 4f
                    ld        d,b                           ;[140f] 50
                    jr        nz,$1485                      ;[1410] 20 73
                    ld        (hl),h                        ;[1412] 74
                    ld        h,c                           ;[1413] 61
                    ld        (hl),h                        ;[1414] 74
                    ld        h,l                           ;[1415] 65
                    ld        l,l                           ;[1416] 6d
                    ld        h,l                           ;[1417] 65
                    ld        l,(hl)                        ;[1418] 6e
                    call      p,$6e49                       ;[1419] f4 49 6e
                    halt                                    ;[141c] 76
                    ld        h,c                           ;[141d] 61
                    ld        l,h                           ;[141e] 6c
                    ld        l,c                           ;[141f] 69
                    ld        h,h                           ;[1420] 64
                    jr        nz,$1484                      ;[1421] 20 61
                    ld        (hl),d                        ;[1423] 72
                    ld        h,a                           ;[1424] 67
                    ld        (hl),l                        ;[1425] 75
                    ld        l,l                           ;[1426] 6d
                    ld        h,l                           ;[1427] 65
                    ld        l,(hl)                        ;[1428] 6e
                    call      p,$6e49                       ;[1429] f4 49 6e
                    ld        (hl),h                        ;[142c] 74
                    ld        h,l                           ;[142d] 65
                    ld        h,a                           ;[142e] 67
                    ld        h,l                           ;[142f] 65
                    ld        (hl),d                        ;[1430] 72
                    jr        nz,$14a2                      ;[1431] 20 6f
                    ld        (hl),l                        ;[1433] 75
                    ld        (hl),h                        ;[1434] 74
                    jr        nz,$14a6                      ;[1435] 20 6f
                    ld        h,(hl)                        ;[1437] 66
                    jr        nz,$14ac                      ;[1438] 20 72
                    ld        h,c                           ;[143a] 61
                    ld        l,(hl)                        ;[143b] 6e
                    ld        h,a                           ;[143c] 67
                    push      hl                            ;[143d] e5
                    ld        c,(hl)                        ;[143e] 4e
                    ld        l,a                           ;[143f] 6f
                    ld        l,(hl)                        ;[1440] 6e
                    ld        (hl),e                        ;[1441] 73
                    ld        h,l                           ;[1442] 65
                    ld        l,(hl)                        ;[1443] 6e
                    ld        (hl),e                        ;[1444] 73
                    ld        h,l                           ;[1445] 65
                    jr        nz,$14b1                      ;[1446] 20 69
                    ld        l,(hl)                        ;[1448] 6e
                    jr        nz,$148d                      ;[1449] 20 42
                    ld        b,c                           ;[144b] 41
                    ld        d,e                           ;[144c] 53
                    ld        c,c                           ;[144d] 49
                    jp        $5242                         ;[144e] c3 42 52
                    ld        b,l                           ;[1451] 45
                    ld        b,c                           ;[1452] 41
                    ld        c,e                           ;[1453] 4b
                    jr        nz,$1483                      ;[1454] 20 2d
                    jr        nz,$149b                      ;[1456] 20 43
                    ld        c,a                           ;[1458] 4f
                    ld        c,(hl)                        ;[1459] 4e
                    ld        d,h                           ;[145a] 54
                    jr        nz,$14cf                      ;[145b] 20 72
                    ld        h,l                           ;[145d] 65
                    ld        (hl),b                        ;[145e] 70
                    ld        h,l                           ;[145f] 65
                    ld        h,c                           ;[1460] 61
                    ld        (hl),h                        ;[1461] 74
                    di                                      ;[1462] f3
                    ld        c,a                           ;[1463] 4f
                    ld        (hl),l                        ;[1464] 75
                    ld        (hl),h                        ;[1465] 74
                    jr        nz,$14d7                      ;[1466] 20 6f
                    ld        h,(hl)                        ;[1468] 66
                    jr        nz,$14af                      ;[1469] 20 44
                    ld        b,c                           ;[146b] 41
                    ld        d,h                           ;[146c] 54
                    pop       bc                            ;[146d] c1
                    ld        c,c                           ;[146e] 49
                    ld        l,(hl)                        ;[146f] 6e
                    halt                                    ;[1470] 76
                    ld        h,c                           ;[1471] 61
                    ld        l,h                           ;[1472] 6c
                    ld        l,c                           ;[1473] 69
                    ld        h,h                           ;[1474] 64
                    jr        nz,$14dd                      ;[1475] 20 66
                    ld        l,c                           ;[1477] 69
                    ld        l,h                           ;[1478] 6c
                    ld        h,l                           ;[1479] 65
                    jr        nz,$14ea                      ;[147a] 20 6e
                    ld        h,c                           ;[147c] 61
                    ld        l,l                           ;[147d] 6d
                    push      hl                            ;[147e] e5
                    ld        c,(hl)                        ;[147f] 4e
                    ld        l,a                           ;[1480] 6f
                    jr        nz,$14f5                      ;[1481] 20 72
                    ld        l,a                           ;[1483] 6f
                    ld        l,a                           ;[1484] 6f
                    ld        l,l                           ;[1485] 6d
                    jr        nz,$14ee                      ;[1486] 20 66
                    ld        l,a                           ;[1488] 6f
                    ld        (hl),d                        ;[1489] 72
                    jr        nz,$14f8                      ;[148a] 20 6c
                    ld        l,c                           ;[148c] 69
                    ld        l,(hl)                        ;[148d] 6e
                    push      hl                            ;[148e] e5
                    ld        d,e                           ;[148f] 53
                    ld        d,h                           ;[1490] 54
                    ld        c,a                           ;[1491] 4f
                    ld        d,b                           ;[1492] 50
                    jr        nz,$14fe                      ;[1493] 20 69
                    ld        l,(hl)                        ;[1495] 6e
                    jr        nz,$14e1                      ;[1496] 20 49
                    ld        c,(hl)                        ;[1498] 4e
                    ld        d,b                           ;[1499] 50
                    ld        d,l                           ;[149a] 55
                    call      nc,$4f46                      ;[149b] d4 46 4f
                    ld        d,d                           ;[149e] 52
                    jr        nz,$1518                      ;[149f] 20 77
                    ld        l,c                           ;[14a1] 69
                    ld        (hl),h                        ;[14a2] 74
                    ld        l,b                           ;[14a3] 68
                    ld        l,a                           ;[14a4] 6f
                    ld        (hl),l                        ;[14a5] 75
                    ld        (hl),h                        ;[14a6] 74
                    jr        nz,$14f7                      ;[14a7] 20 4e
                    ld        b,l                           ;[14a9] 45
                    ld        e,b                           ;[14aa] 58
                    call      nc,$6e49                      ;[14ab] d4 49 6e
                    halt                                    ;[14ae] 76
                    ld        h,c                           ;[14af] 61
                    ld        l,h                           ;[14b0] 6c
                    ld        l,c                           ;[14b1] 69
                    ld        h,h                           ;[14b2] 64
                    jr        nz,$14fe                      ;[14b3] 20 49
                    cpl                                     ;[14b5] 2f
                    ld        c,a                           ;[14b6] 4f
                    jr        nz,$151d                      ;[14b7] 20 64
                    ld        h,l                           ;[14b9] 65
                    halt                                    ;[14ba] 76
                    ld        l,c                           ;[14bb] 69
                    ld        h,e                           ;[14bc] 63
                    push      hl                            ;[14bd] e5
                    ld        c,c                           ;[14be] 49
                    ld        l,(hl)                        ;[14bf] 6e
                    halt                                    ;[14c0] 76
                    ld        h,c                           ;[14c1] 61
                    ld        l,h                           ;[14c2] 6c
                    ld        l,c                           ;[14c3] 69
                    ld        h,h                           ;[14c4] 64
                    jr        nz,$152a                      ;[14c5] 20 63
                    ld        l,a                           ;[14c7] 6f
                    ld        l,h                           ;[14c8] 6c
                    ld        l,a                           ;[14c9] 6f
                    ld        (hl),l                        ;[14ca] 75
                    jp        p,$5242                       ;[14cb] f2 42 52
                    ld        b,l                           ;[14ce] 45
                    ld        b,c                           ;[14cf] 41
                    ld        c,e                           ;[14d0] 4b
                    jr        nz,$153c                      ;[14d1] 20 69
                    ld        l,(hl)                        ;[14d3] 6e
                    ld        (hl),h                        ;[14d4] 74
                    ld        l,a                           ;[14d5] 6f
                    jr        nz,$1548                      ;[14d6] 20 70
                    ld        (hl),d                        ;[14d8] 72
                    ld        l,a                           ;[14d9] 6f
                    ld        h,a                           ;[14da] 67
                    ld        (hl),d                        ;[14db] 72
                    ld        h,c                           ;[14dc] 61
                    sbc       hl,de                         ;[14dd] ed 52
                    ld        b,c                           ;[14df] 41
                    ld        c,l                           ;[14e0] 4d
                    ld        d,h                           ;[14e1] 54
                    ld        c,a                           ;[14e2] 4f
                    ld        d,b                           ;[14e3] 50
                    jr        nz,$1554                      ;[14e4] 20 6e
                    ld        l,a                           ;[14e6] 6f
                    jr        nz,$1550                      ;[14e7] 20 67
                    ld        l,a                           ;[14e9] 6f
                    ld        l,a                           ;[14ea] 6f
                    call      po,$7453                      ;[14eb] e4 53 74
                    ld        h,c                           ;[14ee] 61
                    ld        (hl),h                        ;[14ef] 74
                    ld        h,l                           ;[14f0] 65
                    ld        l,l                           ;[14f1] 6d
                    ld        h,l                           ;[14f2] 65
                    ld        l,(hl)                        ;[14f3] 6e
                    ld        (hl),h                        ;[14f4] 74
                    jr        nz,$1563                      ;[14f5] 20 6c
                    ld        l,a                           ;[14f7] 6f
                    ld        (hl),e                        ;[14f8] 73
                    call      p,$6e49                       ;[14f9] f4 49 6e
                    halt                                    ;[14fc] 76
                    ld        h,c                           ;[14fd] 61
                    ld        l,h                           ;[14fe] 6c
                    ld        l,c                           ;[14ff] 69
                    ld        h,h                           ;[1500] 64
                    jr        nz,$1576                      ;[1501] 20 73
                    ld        (hl),h                        ;[1503] 74
                    ld        (hl),d                        ;[1504] 72
                    ld        h,l                           ;[1505] 65
                    ld        h,c                           ;[1506] 61
                    im        0                             ;[1507] ed 46
                    ld        c,(hl)                        ;[1509] 4e
                    jr        nz,$1583                      ;[150a] 20 77
                    ld        l,c                           ;[150c] 69
                    ld        (hl),h                        ;[150d] 74
                    ld        l,b                           ;[150e] 68
                    ld        l,a                           ;[150f] 6f
                    ld        (hl),l                        ;[1510] 75
                    ld        (hl),h                        ;[1511] 74
                    jr        nz,$1558                      ;[1512] 20 44
                    ld        b,l                           ;[1514] 45
                    add       $50                           ;[1515] c6 50
                    ld        h,c                           ;[1517] 61
                    ld        (hl),d                        ;[1518] 72
                    ld        h,c                           ;[1519] 61
                    ld        l,l                           ;[151a] 6d
                    ld        h,l                           ;[151b] 65
                    ld        (hl),h                        ;[151c] 74
                    ld        h,l                           ;[151d] 65
                    ld        (hl),d                        ;[151e] 72
                    jr        nz,$1586                      ;[151f] 20 65
                    ld        (hl),d                        ;[1521] 72
                    ld        (hl),d                        ;[1522] 72
                    ld        l,a                           ;[1523] 6f
                    jp        p,$6154                       ;[1524] f2 54 61
                    ld        (hl),b                        ;[1527] 70
                    ld        h,l                           ;[1528] 65
                    jr        nz,$1597                      ;[1529] 20 6c
                    ld        l,a                           ;[152b] 6f
                    ld        h,c                           ;[152c] 61
                    ld        h,h                           ;[152d] 64
                    ld        l,c                           ;[152e] 69
                    ld        l,(hl)                        ;[152f] 6e
                    ld        h,a                           ;[1530] 67
                    jr        nz,$1598                      ;[1531] 20 65
                    ld        (hl),d                        ;[1533] 72
                    ld        (hl),d                        ;[1534] 72
                    ld        l,a                           ;[1535] 6f
                    jp        p,$a02c                       ;[1536] f2 2c a0
                    ld        a,a                           ;[1539] 7f
                    jr        nz,$156d                      ;[153a] 20 31
                    add       hl,sp                         ;[153c] 39
                    jr        c,$1571                       ;[153d] 38 32
                    jr        nz,$1594                      ;[153f] 20 53
                    ld        l,c                           ;[1541] 69
                    ld        l,(hl)                        ;[1542] 6e
                    ld        h,e                           ;[1543] 63
                    ld        l,h                           ;[1544] 6c
                    ld        h,c                           ;[1545] 61
                    ld        l,c                           ;[1546] 69
                    ld        (hl),d                        ;[1547] 72
                    jr        nz,$159c                      ;[1548] 20 52
                    ld        h,l                           ;[154a] 65
                    ld        (hl),e                        ;[154b] 73
                    ld        h,l                           ;[154c] 65
                    ld        h,c                           ;[154d] 61
                    ld        (hl),d                        ;[154e] 72
                    ld        h,e                           ;[154f] 63
                    ld        l,b                           ;[1550] 68
                    jr        nz,$159f                      ;[1551] 20 4c
                    ld        (hl),h                        ;[1553] 74
                    call      po,$5d2a                      ;[1554] e4 2a 5d
                    ld        e,h                           ;[1557] 5c
                    jp        $1562                         ;[1558] c3 62 15
                    ld        hl,($5c5d)                    ;[155b] 2a 5d 5c
                    inc       hl                            ;[155e] 23
                    ld        ($5c5d),hl                    ;[155f] 22 5d 5c
                    ld        a,(hl)                        ;[1562] 7e
                    cp        $21                           ;[1563] fe 21
                    ret       nc                            ;[1565] d0
                    cp        $0d                           ;[1566] fe 0d
                    ret       z                             ;[1568] c8
                    inc       hl                            ;[1569] 23
                    cp        $18                           ;[156a] fe 18
                    jr        nc,$155f                      ;[156c] 30 f1
                    cp        $10                           ;[156e] fe 10
                    jr        c,$155f                       ;[1570] 38 ed
                    cp        $16                           ;[1572] fe 16
                    jr        c,$155e                       ;[1574] 38 e8
                    inc       hl                            ;[1576] 23
                    jr        $155e                         ;[1577] 18 e5
                    scf                                     ;[1579] 37
                    inc       d                             ;[157a] 14
                    dec       d                             ;[157b] 15
                    jr        z,$1581                       ;[157c] 28 03
                    inc       h                             ;[157e] 24
                    dec       h                             ;[157f] 25
                    ret       nz                            ;[1580] c0
                    ld        b,d                           ;[1581] 42
                    ld        c,e                           ;[1582] 4b
                    ld        d,h                           ;[1583] 54
                    mul       d,e                           ;[1584] ed 30
                    inc       d                             ;[1586] 14
                    dec       d                             ;[1587] 15
                    ret       nz                            ;[1588] c0
                    ld        a,e                           ;[1589] 7b
                    ld        d,b                           ;[158a] 50
                    ld        e,l                           ;[158b] 5d
                    mul       d,e                           ;[158c] ed 30
                    inc       d                             ;[158e] 14
                    dec       d                             ;[158f] 15
                    ret       nz                            ;[1590] c0
                    add       e                             ;[1591] 83
                    ret       c                             ;[1592] d8
                    ld        d,l                           ;[1593] 55
                    ld        e,c                           ;[1594] 59
                    mul       d,e                           ;[1595] ed 30
                    ld        h,a                           ;[1597] 67
                    ld        l,$00                         ;[1598] 2e 00
                    add       hl,de                         ;[159a] 19
                    ret       c                             ;[159b] d8
                    ld        a,h                           ;[159c] 7c
                    or        l                             ;[159d] b5
                    ret                                     ;[159e] c9

                    call      $2530                         ;[159f] cd 30 25
                    ret       z                             ;[15a2] c8
                    push      bc                            ;[15a3] c5
                    push      de                            ;[15a4] d5
                    call      $1579                         ;[15a5] cd 79 15
                    pop       de                            ;[15a8] d1
                    pop       bc                            ;[15a9] c1
                    ret       nc                            ;[15aa] d0
                    jp        $1f15                         ;[15ab] c3 15 1f
                    rst       $38                           ;[15ae] ff
                    call      p,$a809                       ;[15af] f4 09 a8
                    djnz      $15ff                         ;[15b2] 10 4b
                    call      p,$c409                       ;[15b4] f4 09 c4
                    dec       d                             ;[15b7] 15
                    ld        d,e                           ;[15b8] 53
                    add       c                             ;[15b9] 81
                    rrca                                    ;[15ba] 0f
                    call      nz,$5215                      ;[15bb] c4 15 52
                    call      $c40e                         ;[15be] cd 0e c4
                    dec       d                             ;[15c1] 15
                    ld        d,b                           ;[15c2] 50
                    add       b                             ;[15c3] 80
                    rst       $08                           ;[15c4] cf
                    ld        (de),a                        ;[15c5] 12
                    ld        bc,$0600                      ;[15c6] 01 00 06
                    nop                                     ;[15c9] 00
                    dec       bc                            ;[15ca] 0b
                    nop                                     ;[15cb] 00
                    ld        bc,$0100                      ;[15cc] 01 00 01
                    nop                                     ;[15cf] 00
                    ld        b,$00                         ;[15d0] 06 00
                    djnz      $15d4                         ;[15d2] 10 00
                    bit       5,(iy+$02)                    ;[15d4] fd cb 02 6e
                    jr        nz,$15de                      ;[15d8] 20 04
                    set       3,(iy+$02)                    ;[15da] fd cb 02 de
                    call      $15e6                         ;[15de] cd e6 15
                    ret       c                             ;[15e1] d8
                    jr        z,$15de                       ;[15e2] 28 fa
                    rst       $08                           ;[15e4] cf
                    rlca                                    ;[15e5] 07
                    exx                                     ;[15e6] d9
                    push      hl                            ;[15e7] e5
                    ld        hl,($5c51)                    ;[15e8] 2a 51 5c
                    inc       hl                            ;[15eb] 23
                    inc       hl                            ;[15ec] 23
                    jr        $15f7                         ;[15ed] 18 08
                    ld        e,$30                         ;[15ef] 1e 30
                    add       e                             ;[15f1] 83
                    exx                                     ;[15f2] d9
                    push      hl                            ;[15f3] e5
                    ld        hl,($5c51)                    ;[15f4] 2a 51 5c
                    ld        e,(hl)                        ;[15f7] 5e
                    inc       hl                            ;[15f8] 23
                    ld        d,(hl)                        ;[15f9] 56
                    ex        de,hl                         ;[15fa] eb
                    call      $162c                         ;[15fb] cd 2c 16
                    pop       hl                            ;[15fe] e1
                    exx                                     ;[15ff] d9
                    ret                                     ;[1600] c9

                    add       a                             ;[1601] 87
                    add       $16                           ;[1602] c6 16
                    ld        l,a                           ;[1604] 6f
                    ld        h,$5c                         ;[1605] 26 5c
                    ld        e,(hl)                        ;[1607] 5e
                    inc       hl                            ;[1608] 23
                    ld        d,(hl)                        ;[1609] 56
                    ld        a,d                           ;[160a] 7a
                    or        e                             ;[160b] b3
                    jr        nz,$1610                      ;[160c] 20 02
                    rst       $08                           ;[160e] cf
                    rla                                     ;[160f] 17
                    dec       de                            ;[1610] 1b
                    ld        hl,($5c4f)                    ;[1611] 2a 4f 5c
                    add       hl,de                         ;[1614] 19
                    ld        ($5c51),hl                    ;[1615] 22 51 5c
                    res       4,(iy+$30)                    ;[1618] fd cb 30 a6
                    inc       hl                            ;[161c] 23
                    inc       hl                            ;[161d] 23
                    inc       hl                            ;[161e] 23
                    inc       hl                            ;[161f] 23
                    ld        c,(hl)                        ;[1620] 4e
                    ld        hl,$162d                      ;[1621] 21 2d 16
                    call      $16dc                         ;[1624] cd dc 16
                    ret       nc                            ;[1627] d0
                    ld        d,$00                         ;[1628] 16 00
                    ld        e,(hl)                        ;[162a] 5e
                    add       hl,de                         ;[162b] 19
                    jp        (hl)                          ;[162c] e9
                    ld        c,e                           ;[162d] 4b
                    ld        b,$53                         ;[162e] 06 53
                    ld        (de),a                        ;[1630] 12
                    ld        d,b                           ;[1631] 50
                    dec       de                            ;[1632] 1b
                    nop                                     ;[1633] 00
                    set       0,(iy+$02)                    ;[1634] fd cb 02 c6
                    res       5,(iy+$01)                    ;[1638] fd cb 01 ae
                    set       4,(iy+$30)                    ;[163c] fd cb 30 e6
                    jr        $1646                         ;[1640] 18 04
                    res       0,(iy+$02)                    ;[1642] fd cb 02 86
                    res       1,(iy+$01)                    ;[1646] fd cb 01 8e
                    jp        $0d4d                         ;[164a] c3 4d 0d
                    set       1,(iy+$01)                    ;[164d] fd cb 01 ce
                    ret                                     ;[1651] c9

                    ld        bc,$0001                      ;[1652] 01 01 00
                    push      hl                            ;[1655] e5
                    call      $1f05                         ;[1656] cd 05 1f
                    pop       hl                            ;[1659] e1
                    call      $3b86                         ;[165a] cd 86 3b
                    ld        hl,($5c65)                    ;[165d] 2a 65 5c
                    ex        de,hl                         ;[1660] eb
                    lddr                                    ;[1661] ed b8
                    ret                                     ;[1663] c9

                    jp        $3b86                         ;[1664] c3 86 3b
                    call      $2aee                         ;[1667] cd ee 2a
                    ex        (sp),hl                       ;[166a] e3
                    call      $159f                         ;[166b] cd 9f 15
                    pop       bc                            ;[166e] c1
                    add       hl,bc                         ;[166f] 09
                    inc       hl                            ;[1670] 23
                    ld        b,d                           ;[1671] 42
                    ld        c,e                           ;[1672] 4b
                    ex        de,hl                         ;[1673] eb
                    call      $2ab1                         ;[1674] cd b1 2a
                    rst       $18                           ;[1677] df
                    cp        $29                           ;[1678] fe 29
                    jr        z,$1684                       ;[167a] 28 08
                    cp        $2c                           ;[167c] fe 2c
                    jp        nz,$2a25                      ;[167e] c2 25 2a
                    call      $2a52                         ;[1681] cd 52 2a
                    rst       $20                           ;[1684] e7
                    cp        $28                           ;[1685] fe 28
                    jr        z,$1681                       ;[1687] 28 f8
                    res       6,(iy+$01)                    ;[1689] fd cb 01 b6
                    ret                                     ;[168d] c9

                    rst       $38                           ;[168e] ff
                    nop                                     ;[168f] 00
                    nop                                     ;[1690] 00
                    ex        de,hl                         ;[1691] eb
                    ld        de,$168f                      ;[1692] 11 8f 16
                    ld        a,(hl)                        ;[1695] 7e
                    and       $c0                           ;[1696] e6 c0
                    jr        nz,$1691                      ;[1698] 20 f7
                    ld        d,(hl)                        ;[169a] 56
                    inc       hl                            ;[169b] 23
                    ld        e,(hl)                        ;[169c] 5e
                    ret                                     ;[169d] c9

                    ld        hl,($5c63)                    ;[169e] 2a 63 5c
                    dec       hl                            ;[16a1] 2b
                    call      $1655                         ;[16a2] cd 55 16
                    inc       hl                            ;[16a5] 23
                    inc       hl                            ;[16a6] 23
                    pop       bc                            ;[16a7] c1
                    ld        ($5c61),bc                    ;[16a8] ed 43 61 5c
                    pop       bc                            ;[16ac] c1
                    ex        de,hl                         ;[16ad] eb
                    inc       hl                            ;[16ae] 23
                    ret                                     ;[16af] c9

                    ld        hl,($5c59)                    ;[16b0] 2a 59 5c
                    ld        (hl),$0d                      ;[16b3] 36 0d
                    ld        ($5c5b),hl                    ;[16b5] 22 5b 5c
                    inc       hl                            ;[16b8] 23
                    ld        (hl),$80                      ;[16b9] 36 80
                    inc       hl                            ;[16bb] 23
                    ld        ($5c61),hl                    ;[16bc] 22 61 5c
                    ld        hl,($5c61)                    ;[16bf] 2a 61 5c
                    ld        ($5c63),hl                    ;[16c2] 22 63 5c
                    ld        hl,($5c63)                    ;[16c5] 2a 63 5c
                    ld        ($5c65),hl                    ;[16c8] 22 65 5c
                    push      hl                            ;[16cb] e5
                    ld        hl,$5c92                      ;[16cc] 21 92 5c
                    ld        ($5c68),hl                    ;[16cf] 22 68 5c
                    pop       hl                            ;[16d2] e1
                    ret                                     ;[16d3] c9

                    ld        de,($5c59)                    ;[16d4] ed 5b 59 5c
                    jp        $19e5                         ;[16d8] c3 e5 19
                    inc       hl                            ;[16db] 23
                    ld        a,(hl)                        ;[16dc] 7e
                    and       a                             ;[16dd] a7
                    ret       z                             ;[16de] c8
                    cp        c                             ;[16df] b9
                    inc       hl                            ;[16e0] 23
                    jr        nz,$16db                      ;[16e1] 20 f8
                    scf                                     ;[16e3] 37
                    ret                                     ;[16e4] c9

                    call      $171e                         ;[16e5] cd 1e 17
                    call      $1701                         ;[16e8] cd 01 17
                    ld        bc,$0000                      ;[16eb] 01 00 00
                    ld        de,$a3e2                      ;[16ee] 11 e2 a3
                    ex        de,hl                         ;[16f1] eb
                    add       hl,de                         ;[16f2] 19
                    jr        c,$16fc                       ;[16f3] 38 07
                    ld        bc,$15d4                      ;[16f5] 01 d4 15
                    add       hl,bc                         ;[16f8] 09
                    ld        c,(hl)                        ;[16f9] 4e
                    inc       hl                            ;[16fa] 23
                    ld        b,(hl)                        ;[16fb] 46
                    ex        de,hl                         ;[16fc] eb
                    ld        (hl),c                        ;[16fd] 71
                    inc       hl                            ;[16fe] 23
                    ld        (hl),b                        ;[16ff] 70
                    ret                                     ;[1700] c9

                    push      hl                            ;[1701] e5
                    ld        hl,($5c4f)                    ;[1702] 2a 4f 5c
                    add       hl,bc                         ;[1705] 09
                    inc       hl                            ;[1706] 23
                    inc       hl                            ;[1707] 23
                    inc       hl                            ;[1708] 23
                    ld        c,(hl)                        ;[1709] 4e
                    ex        de,hl                         ;[170a] eb
                    ld        hl,$1716                      ;[170b] 21 16 17
                    call      $16dc                         ;[170e] cd dc 16
                    ld        c,(hl)                        ;[1711] 4e
                    ld        b,$00                         ;[1712] 06 00
                    add       hl,bc                         ;[1714] 09
                    jp        (hl)                          ;[1715] e9
                    ld        c,e                           ;[1716] 4b
                    dec       b                             ;[1717] 05
                    ld        d,e                           ;[1718] 53
                    inc       bc                            ;[1719] 03
                    ld        d,b                           ;[171a] 50
                    ld        bc,$c9e1                      ;[171b] 01 e1 c9
                    call      $1e94                         ;[171e] cd 94 1e
                    cp        $10                           ;[1721] fe 10
                    jr        c,$1727                       ;[1723] 38 02
                    rst       $08                           ;[1725] cf
                    rla                                     ;[1726] 17
                    add       $03                           ;[1727] c6 03
                    rlca                                    ;[1729] 07
                    ld        hl,$5c10                      ;[172a] 21 10 5c
                    ld        c,a                           ;[172d] 4f
                    ld        b,$00                         ;[172e] 06 00
                    add       hl,bc                         ;[1730] 09
                    ld        c,(hl)                        ;[1731] 4e
                    inc       hl                            ;[1732] 23
                    ld        b,(hl)                        ;[1733] 46
                    dec       hl                            ;[1734] 2b
                    ret                                     ;[1735] c9

                    rst       $28                           ;[1736] ef
                    ld        bc,$cd38                      ;[1737] 01 38 cd
                    ld        e,$17                         ;[173a] 1e 17
                    ld        a,b                           ;[173c] 78
                    or        c                             ;[173d] b1
                    jr        z,$1756                       ;[173e] 28 16
                    ex        de,hl                         ;[1740] eb
                    ld        hl,($5c4f)                    ;[1741] 2a 4f 5c
                    add       hl,bc                         ;[1744] 09
                    inc       hl                            ;[1745] 23
                    inc       hl                            ;[1746] 23
                    inc       hl                            ;[1747] 23
                    ld        a,(hl)                        ;[1748] 7e
                    ex        de,hl                         ;[1749] eb
                    cp        $4b                           ;[174a] fe 4b
                    jr        z,$1756                       ;[174c] 28 08
                    cp        $53                           ;[174e] fe 53
                    jr        z,$1756                       ;[1750] 28 04
                    cp        $50                           ;[1752] fe 50
                    jr        nz,$1725                      ;[1754] 20 cf
                    call      $175d                         ;[1756] cd 5d 17
                    ld        (hl),e                        ;[1759] 73
                    inc       hl                            ;[175a] 23
                    ld        (hl),d                        ;[175b] 72
                    ret                                     ;[175c] c9

                    push      hl                            ;[175d] e5
                    call      $2bf1                         ;[175e] cd f1 2b
                    ld        a,b                           ;[1761] 78
                    or        c                             ;[1762] b1
                    jr        nz,$1767                      ;[1763] 20 02
                    rst       $08                           ;[1765] cf
                    ld        c,$c5                         ;[1766] 0e c5
                    ld        a,(de)                        ;[1768] 1a
                    and       $df                           ;[1769] e6 df
                    ld        c,a                           ;[176b] 4f
                    ld        hl,$177a                      ;[176c] 21 7a 17
                    call      $16dc                         ;[176f] cd dc 16
                    jr        nc,$1765                      ;[1772] 30 f1
                    ld        c,(hl)                        ;[1774] 4e
                    ld        b,$00                         ;[1775] 06 00
                    add       hl,bc                         ;[1777] 09
                    pop       bc                            ;[1778] c1
                    jp        (hl)                          ;[1779] e9
                    ld        c,e                           ;[177a] 4b
                    ld        b,$53                         ;[177b] 06 53
                    ex        af,af'                        ;[177d] 08
                    ld        d,b                           ;[177e] 50
                    ld        a,(bc)                        ;[177f] 0a
                    nop                                     ;[1780] 00
                    ld        e,$01                         ;[1781] 1e 01
                    jr        $178b                         ;[1783] 18 06
                    ld        e,$06                         ;[1785] 1e 06
                    jr        $178b                         ;[1787] 18 02
                    ld        e,$10                         ;[1789] 1e 10
                    dec       bc                            ;[178b] 0b
                    ld        a,b                           ;[178c] 78
                    or        c                             ;[178d] b1
                    jr        nz,$1765                      ;[178e] 20 d5
                    ld        d,a                           ;[1790] 57
                    pop       hl                            ;[1791] e1
                    ret                                     ;[1792] c9

                    ld        a,$ff                         ;[1793] 3e ff
                    bit       6,(hl)                        ;[1795] cb 76
                    set       6,(hl)                        ;[1797] cb f6
                    jr        z,$17a7                       ;[1799] 28 0c
                    rst       $08                           ;[179b] cf
                    dec       bc                            ;[179c] 0b
                    ld        a,$bf                         ;[179d] 3e bf
                    jr        $17a3                         ;[179f] 18 02
                    ld        a,$ff                         ;[17a1] 3e ff
                    bit       6,(hl)                        ;[17a3] cb 76
                    jr        z,$179b                       ;[17a5] 28 f4
                    and       (hl)                          ;[17a7] a6
                    ld        (hl),a                        ;[17a8] 77
                    call      m,$0013                       ;[17a9] fc 13 00
                    jp        $2712                         ;[17ac] c3 12 27
                    ld        a,d                           ;[17af] 7a
                    and       a                             ;[17b0] a7
                    jr        $17b5                         ;[17b1] 18 02
                    scf                                     ;[17b3] 37
                    sbc       a                             ;[17b4] 9f
                    push      af                            ;[17b5] f5
                    push      de                            ;[17b6] d5
                    call      $27d4                         ;[17b7] cd d4 27
                    pop       de                            ;[17ba] d1
                    jr        c,$17e9                       ;[17bb] 38 2c
                    push      hl                            ;[17bd] e5
                    rst       $20                           ;[17be] e7
                    cp        $24                           ;[17bf] fe 24
                    pop       hl                            ;[17c1] e1
                    jr        z,$17d5                       ;[17c2] 28 11
                    ld        ($5c5d),hl                    ;[17c4] 22 5d 5c
                    pop       af                            ;[17c7] f1
                    jr        nc,$17cf                      ;[17c8] 30 05
                    ld        c,$ed                         ;[17ca] 0e ed
                    jp        $082d                         ;[17cc] c3 2d 08
                    ld        e,$c0                         ;[17cf] 1e c0
                    ld        d,a                           ;[17d1] 57
                    jp        $3977                         ;[17d2] c3 77 39
                    call      $27d4                         ;[17d5] cd d4 27
                    jr        c,$17fa                       ;[17d8] 38 20
                    pop       af                            ;[17da] f1
                    ld        c,$77                         ;[17db] 0e 77
                    jr        c,$17cc                       ;[17dd] 38 ed
                    ld        e,$24                         ;[17df] 1e 24
                    ld        d,a                           ;[17e1] 57
                    ld        bc,$1040                      ;[17e2] 01 40 10
                    push      de                            ;[17e5] d5
                    jp        $082f                         ;[17e6] c3 2f 08
                    pop       de                            ;[17e9] d1
                    jr        nz,$1802                      ;[17ea] 20 16
                    ld        ix,$2188                      ;[17ec] dd 21 88 21
                    bit       6,(hl)                        ;[17f0] cb 76
                    jr        nz,$17a1                      ;[17f2] 20 ad
                    ld        ix,$34bc                      ;[17f4] dd 21 bc 34
                    jr        $1793                         ;[17f8] 18 99
                    ld        ix,$2184                      ;[17fa] dd 21 84 21
                    pop       de                            ;[17fe] d1
                    jr        z,$179d                       ;[17ff] 28 9c
                    and       a                             ;[1801] a7
                    push      de                            ;[1802] d5
                    push      af                            ;[1803] f5
                    bit       6,(hl)                        ;[1804] cb 76
                    jp        z,$1c8a                       ;[1806] ca 8a 1c
                    call      $24fb                         ;[1809] cd fb 24
                    pop       af                            ;[180c] f1
                    ld        hl,$5c3b                      ;[180d] 21 3b 5c
                    bit       6,(hl)                        ;[1810] cb 76
                    push      af                            ;[1812] f5
                    jr        nz,$181c                      ;[1813] 20 07
                    bit       7,(hl)                        ;[1815] cb 7e
                    call      nz,$2bf1                      ;[1817] c4 f1 2b
                    jr        $1821                         ;[181a] 18 05
                    bit       7,(hl)                        ;[181c] cb 7e
                    call      nz,$1e99                      ;[181e] c4 99 1e
                    push      bc                            ;[1821] c5
                    push      de                            ;[1822] d5
                    bit       7,(iy+$01)                    ;[1823] fd cb 01 7e
                    call      nz,$1e99                      ;[1827] c4 99 1e
                    push      bc                            ;[182a] c5
                    ld        c,$00                         ;[182b] 0e 00
                    rst       $18                           ;[182d] df
                    cp        $2c                           ;[182e] fe 2c
                    jr        nz,$184b                      ;[1830] 20 19
                    rst       $20                           ;[1832] e7
                    bit       4,c                           ;[1833] cb 61
                    jp        nz,$1c8a                      ;[1835] c2 8a 1c
                    push      bc                            ;[1838] c5
                    push      de                            ;[1839] d5
                    call      $24fb                         ;[183a] cd fb 24
                    pop       de                            ;[183d] d1
                    pop       bc                            ;[183e] c1
                    inc       c                             ;[183f] 0c
                    ld        a,($5c3b)                     ;[1840] 3a 3b 5c
                    add       a                             ;[1843] 87
                    add       a                             ;[1844] 87
                    rr        d                             ;[1845] cb 1a
                    rr        e                             ;[1847] cb 1b
                    jr        $182d                         ;[1849] 18 e2
                    cp        $29                           ;[184b] fe 29
                    jp        nz,$1c8a                      ;[184d] c2 8a 1c
                    rst       $20                           ;[1850] e7
                    ex        de,hl                         ;[1851] eb
                    ld        a,c                           ;[1852] 79
                    exx                                     ;[1853] d9
                    pop       hl                            ;[1854] e1
                    exx                                     ;[1855] d9
                    pop       de                            ;[1856] d1
                    pop       bc                            ;[1857] c1
                    exx                                     ;[1858] d9
                    ld        c,a                           ;[1859] 4f
                    pop       af                            ;[185a] f1
                    ld        a,c                           ;[185b] 79
                    pop       de                            ;[185c] d1
                    call      $2199                         ;[185d] cd 99 21
                    jp        $2712                         ;[1860] c3 12 27
                    rst       $38                           ;[1863] ff
                    rst       $38                           ;[1864] ff
                    rst       $38                           ;[1865] ff
                    rst       $38                           ;[1866] ff
                    rst       $38                           ;[1867] ff
                    rst       $38                           ;[1868] ff
                    rst       $38                           ;[1869] ff
                    rst       $38                           ;[186a] ff
                    rst       $38                           ;[186b] ff
                    rst       $38                           ;[186c] ff
                    rst       $38                           ;[186d] ff
                    rst       $38                           ;[186e] ff
                    rst       $38                           ;[186f] ff
                    rst       $38                           ;[1870] ff
                    rst       $38                           ;[1871] ff
                    rst       $38                           ;[1872] ff
                    rst       $38                           ;[1873] ff
                    rst       $38                           ;[1874] ff
                    rst       $38                           ;[1875] ff
                    rst       $38                           ;[1876] ff
                    rst       $38                           ;[1877] ff
                    rst       $38                           ;[1878] ff
                    rst       $38                           ;[1879] ff
                    rst       $38                           ;[187a] ff
                    rst       $38                           ;[187b] ff
                    rst       $38                           ;[187c] ff
                    set       0,(iy+$01)                    ;[187d] fd cb 01 c6
                    push      de                            ;[1881] d5
                    ex        de,hl                         ;[1882] eb
                    res       2,(iy+$30)                    ;[1883] fd cb 30 96
                    ld        hl,$5c3b                      ;[1887] 21 3b 5c
                    res       2,(hl)                        ;[188a] cb 96
                    bit       5,(iy+$37)                    ;[188c] fd cb 37 6e
                    jr        z,$1894                       ;[1890] 28 02
                    set       2,(hl)                        ;[1892] cb d6
                    ld        hl,($5c5f)                    ;[1894] 2a 5f 5c
                    and       a                             ;[1897] a7
                    sbc       hl,de                         ;[1898] ed 52
                    jr        nz,$18a1                      ;[189a] 20 05
                    ld        a,$3f                         ;[189c] 3e 3f
                    call      $18c1                         ;[189e] cd c1 18
                    call      $18e1                         ;[18a1] cd e1 18
                    ex        de,hl                         ;[18a4] eb
                    ld        a,(hl)                        ;[18a5] 7e
                    call      $18b6                         ;[18a6] cd b6 18
                    inc       hl                            ;[18a9] 23
                    cp        $0d                           ;[18aa] fe 0d
                    jr        z,$18b4                       ;[18ac] 28 06
                    ex        de,hl                         ;[18ae] eb
                    call      $1937                         ;[18af] cd 37 19
                    jr        $1894                         ;[18b2] 18 e0
                    pop       de                            ;[18b4] d1
                    ret                                     ;[18b5] c9

                    cp        $0e                           ;[18b6] fe 0e
                    ret       nz                            ;[18b8] c0
                    inc       hl                            ;[18b9] 23
                    inc       hl                            ;[18ba] 23
                    inc       hl                            ;[18bb] 23
                    inc       hl                            ;[18bc] 23
                    inc       hl                            ;[18bd] 23
                    inc       hl                            ;[18be] 23
                    ld        a,(hl)                        ;[18bf] 7e
                    ret                                     ;[18c0] c9

                    exx                                     ;[18c1] d9
                    ld        hl,($5c8f)                    ;[18c2] 2a 8f 5c
                    push      hl                            ;[18c5] e5
                    res       7,h                           ;[18c6] cb bc
                    set       7,l                           ;[18c8] cb fd
                    ld        ($5c8f),hl                    ;[18ca] 22 8f 5c
                    ld        hl,$5c91                      ;[18cd] 21 91 5c
                    ld        d,(hl)                        ;[18d0] 56
                    push      de                            ;[18d1] d5
                    ld        (hl),$00                      ;[18d2] 36 00
                    call      $09f4                         ;[18d4] cd f4 09
                    pop       hl                            ;[18d7] e1
                    ld        (iy+$57),h                    ;[18d8] fd 74 57
                    pop       hl                            ;[18db] e1
                    ld        ($5c8f),hl                    ;[18dc] 22 8f 5c
                    exx                                     ;[18df] d9
                    ret                                     ;[18e0] c9

                    ld        hl,($5c5b)                    ;[18e1] 2a 5b 5c
                    and       a                             ;[18e4] a7
                    sbc       hl,de                         ;[18e5] ed 52
                    ret       nz                            ;[18e7] c0
                    ld        a,($5c41)                     ;[18e8] 3a 41 5c
                    rlc       a                             ;[18eb] cb 07
                    jr        z,$18f3                       ;[18ed] 28 04
                    add       $43                           ;[18ef] c6 43
                    jr        $1909                         ;[18f1] 18 16
                    ld        hl,$5c3b                      ;[18f3] 21 3b 5c
                    res       3,(hl)                        ;[18f6] cb 9e
                    ld        a,$4b                         ;[18f8] 3e 4b
                    bit       2,(hl)                        ;[18fa] cb 56
                    jr        z,$1909                       ;[18fc] 28 0b
                    set       3,(hl)                        ;[18fe] cb de
                    inc       a                             ;[1900] 3c
                    bit       3,(iy+$30)                    ;[1901] fd cb 30 5e
                    jr        z,$1909                       ;[1905] 28 02
                    ld        a,$43                         ;[1907] 3e 43
                    push      de                            ;[1909] d5
                    call      $18c1                         ;[190a] cd c1 18
                    pop       de                            ;[190d] d1
                    ret                                     ;[190e] c9

                    ld        e,(hl)                        ;[190f] 5e
                    inc       hl                            ;[1910] 23
                    ld        d,(hl)                        ;[1911] 56
                    push      hl                            ;[1912] e5
                    ex        de,hl                         ;[1913] eb
                    inc       hl                            ;[1914] 23
                    call      $196e                         ;[1915] cd 6e 19
                    call      $1695                         ;[1918] cd 95 16
                    pop       hl                            ;[191b] e1
                    ret                                     ;[191c] c9

                    nop                                     ;[191d] 00
                    nop                                     ;[191e] 00
                    nop                                     ;[191f] 00
                    nop                                     ;[1920] 00
                    nop                                     ;[1921] 00
                    nop                                     ;[1922] 00
                    nop                                     ;[1923] 00
                    nop                                     ;[1924] 00
                    ld        a,e                           ;[1925] 7b
                    and       a                             ;[1926] a7
                    ret       m                             ;[1927] f8
                    jr        $1937                         ;[1928] 18 0d
                    xor       a                             ;[192a] af
                    add       hl,bc                         ;[192b] 09
                    inc       a                             ;[192c] 3c
                    jr        c,$192b                       ;[192d] 38 fc
                    sbc       hl,bc                         ;[192f] ed 42
                    dec       a                             ;[1931] 3d
                    jr        z,$1925                       ;[1932] 28 f1
                    jp        $15ef                         ;[1934] c3 ef 15
                    call      $2d1b                         ;[1937] cd 1b 2d
                    jr        nc,$196c                      ;[193a] 30 30
                    cp        $21                           ;[193c] fe 21
                    jr        c,$196c                       ;[193e] 38 2c
                    res       2,(iy+$01)                    ;[1940] fd cb 01 96
                    cp        $cb                           ;[1944] fe cb
                    jr        z,$196c                       ;[1946] 28 24
                    cp        $3a                           ;[1948] fe 3a
                    jr        nz,$195a                      ;[194a] 20 0e
                    jr        $1968                         ;[194c] 18 1a
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
                    cp        $22                           ;[195a] fe 22
                    jr        nz,$1968                      ;[195c] 20 0a
                    push      af                            ;[195e] f5
                    ld        a,($5c6a)                     ;[195f] 3a 6a 5c
                    xor       $04                           ;[1962] ee 04
                    ld        ($5c6a),a                     ;[1964] 32 6a 5c
                    pop       af                            ;[1967] f1
                    set       2,(iy+$01)                    ;[1968] fd cb 01 d6
                    rst       $10                           ;[196c] d7
                    ret                                     ;[196d] c9

                    push      hl                            ;[196e] e5
                    ld        hl,($5c53)                    ;[196f] 2a 53 5c
                    ld        d,h                           ;[1972] 54
                    ld        e,l                           ;[1973] 5d
                    pop       bc                            ;[1974] c1
                    call      $1980                         ;[1975] cd 80 19
                    ret       nc                            ;[1978] d0
                    push      hl                            ;[1979] e5
                    call      $19c6                         ;[197a] cd c6 19
                    pop       de                            ;[197d] d1
                    jr        $1975                         ;[197e] 18 f5
                    ld        a,(hl)                        ;[1980] 7e
                    cp        b                             ;[1981] b8
                    ret       nz                            ;[1982] c0
                    inc       hl                            ;[1983] 23
                    ld        a,(hl)                        ;[1984] 7e
                    dec       hl                            ;[1985] 2b
                    cp        c                             ;[1986] b9
                    ret                                     ;[1987] c9

                    inc       hl                            ;[1988] 23
                    inc       hl                            ;[1989] 23
                    inc       hl                            ;[198a] 23
                    ld        ($5c5d),hl                    ;[198b] 22 5d 5c
                    ld        c,$00                         ;[198e] 0e 00
                    dec       d                             ;[1990] 15
                    ret       z                             ;[1991] c8
                    rst       $20                           ;[1992] e7
                    cp        e                             ;[1993] bb
                    jr        nz,$199a                      ;[1994] 20 04
                    and       a                             ;[1996] a7
                    ret                                     ;[1997] c9

                    inc       hl                            ;[1998] 23
                    ld        a,(hl)                        ;[1999] 7e
                    call      $18b6                         ;[199a] cd b6 18
                    ld        ($5c5d),hl                    ;[199d] 22 5d 5c
                    cp        $22                           ;[19a0] fe 22
                    jr        nz,$19a5                      ;[19a2] 20 01
                    dec       c                             ;[19a4] 0d
                    cp        $3a                           ;[19a5] fe 3a
                    jr        z,$19ad                       ;[19a7] 28 04
                    cp        $cb                           ;[19a9] fe cb
                    jr        nz,$19b1                      ;[19ab] 20 04
                    bit       0,c                           ;[19ad] cb 41
                    jr        z,$1990                       ;[19af] 28 df
                    cp        $0d                           ;[19b1] fe 0d
                    jr        nz,$1998                      ;[19b3] 20 e3
                    dec       d                             ;[19b5] 15
                    scf                                     ;[19b6] 37
                    ret                                     ;[19b7] c9

                    push      hl                            ;[19b8] e5
                    ld        a,$3f                         ;[19b9] 3e 3f
                    cp        (hl)                          ;[19bb] be
                    call      nc,$19c6                      ;[19bc] d4 c6 19
                    call      c,$1a48                       ;[19bf] dc 48 1a
                    pop       de                            ;[19c2] d1
                    jp        $19dd                         ;[19c3] c3 dd 19
                    inc       hl                            ;[19c6] 23
                    inc       hl                            ;[19c7] 23
                    ld        e,(hl)                        ;[19c8] 5e
                    inc       hl                            ;[19c9] 23
                    ld        d,(hl)                        ;[19ca] 56
                    inc       hl                            ;[19cb] 23
                    add       hl,de                         ;[19cc] 19
                    ret                                     ;[19cd] c9

                    push    $5b48                           ;[19ce] ed 8a 5b 48
                    push      hl                            ;[19d2] e5
                    jp        $5b4d                         ;[19d3] c3 4d 5b
                    rst       $38                           ;[19d6] ff
                    rst       $38                           ;[19d7] ff
                    rst       $38                           ;[19d8] ff
                    rst       $38                           ;[19d9] ff
                    rst       $38                           ;[19da] ff
                    rst       $38                           ;[19db] ff
                    rst       $38                           ;[19dc] ff
                    and       a                             ;[19dd] a7
                    sbc       hl,de                         ;[19de] ed 52
                    ld        b,h                           ;[19e0] 44
                    ld        c,l                           ;[19e1] 4d
                    add       hl,de                         ;[19e2] 19
                    ex        de,hl                         ;[19e3] eb
                    ret                                     ;[19e4] c9

                    call      $19dd                         ;[19e5] cd dd 19
                    push      bc                            ;[19e8] c5
                    ld        a,b                           ;[19e9] 78
                    cpl                                     ;[19ea] 2f
                    ld        b,a                           ;[19eb] 47
                    ld        a,c                           ;[19ec] 79
                    cpl                                     ;[19ed] 2f
                    ld        c,a                           ;[19ee] 4f
                    inc       bc                            ;[19ef] 03
                    call      $3b86                         ;[19f0] cd 86 3b
                    ex        de,hl                         ;[19f3] eb
                    pop       hl                            ;[19f4] e1
                    add       hl,de                         ;[19f5] 19
                    push      de                            ;[19f6] d5
                    ldir                                    ;[19f7] ed b0
                    pop       hl                            ;[19f9] e1
                    ret                                     ;[19fa] c9

                    ld        hl,($5c59)                    ;[19fb] 2a 59 5c
                    dec       hl                            ;[19fe] 2b
                    ld        ($5c5d),hl                    ;[19ff] 22 5d 5c
                    rst       $20                           ;[1a02] e7
                    ld        hl,$5c92                      ;[1a03] 21 92 5c
                    ld        ($5c65),hl                    ;[1a06] 22 65 5c
                    call      $2d3b                         ;[1a09] cd 3b 2d
                    call      $2da2                         ;[1a0c] cd a2 2d
                    jr        c,$1a15                       ;[1a0f] 38 04
                    ld        hl,$d8f0                      ;[1a11] 21 f0 d8
                    add       hl,bc                         ;[1a14] 09
                    jp        c,$1c8a                       ;[1a15] da 8a 1c
                    jp        $16c5                         ;[1a18] c3 c5 16
                    push      de                            ;[1a1b] d5
                    push      hl                            ;[1a1c] e5
                    xor       a                             ;[1a1d] af
                    bit       7,b                           ;[1a1e] cb 78
                    jr        nz,$1a42                      ;[1a20] 20 20
                    ld        h,b                           ;[1a22] 60
                    ld        l,c                           ;[1a23] 69
                    ld        e,$ff                         ;[1a24] 1e ff
                    jr        $1a30                         ;[1a26] 18 08
                    push      de                            ;[1a28] d5
                    ld        d,(hl)                        ;[1a29] 56
                    inc       hl                            ;[1a2a] 23
                    ld        e,(hl)                        ;[1a2b] 5e
                    push      hl                            ;[1a2c] e5
                    ex        de,hl                         ;[1a2d] eb
                    ld        e,$20                         ;[1a2e] 1e 20
                    ld        bc,$fc18                      ;[1a30] 01 18 fc
                    call      $192a                         ;[1a33] cd 2a 19
                    ld        bc,$ff9c                      ;[1a36] 01 9c ff
                    call      $192a                         ;[1a39] cd 2a 19
                    ld        c,$f6                         ;[1a3c] 0e f6
                    call      $192a                         ;[1a3e] cd 2a 19
                    ld        a,l                           ;[1a41] 7d
                    call      $15ef                         ;[1a42] cd ef 15
                    pop       hl                            ;[1a45] e1
                    pop       de                            ;[1a46] d1
                    ret                                     ;[1a47] c9

                    ld        a,(hl)                        ;[1a48] 7e
                    cp        $7f                           ;[1a49] fe 7f
                    jr        z,$1a68                       ;[1a4b] 28 1b
                    bit       5,a                           ;[1a4d] cb 6f
                    jr        z,$1a6e                       ;[1a4f] 28 1d
                    add       a                             ;[1a51] 87
                    jp        m,$1a5d                       ;[1a52] fa 5d 1a
                    inc       hl                            ;[1a55] 23
                    ld        a,(hl)                        ;[1a56] 7e
                    add       a                             ;[1a57] 87
                    jr        nc,$1a55                      ;[1a58] 30 fb
                    add       a                             ;[1a5a] 87
                    add       a                             ;[1a5b] 87
                    ccf                                     ;[1a5c] 3f
                    ld        de,$0006                      ;[1a5d] 11 06 00
                    jp        nc,$1a73                      ;[1a60] d2 73 1a
                    ld        e,$13                         ;[1a63] 1e 13
                    jp        $1a73                         ;[1a65] c3 73 1a
                    inc       hl                            ;[1a68] 23
                    inc       hl                            ;[1a69] 23
                    bit       7,(hl)                        ;[1a6a] cb 7e
                    jr        z,$1a69                       ;[1a6c] 28 fb
                    inc       hl                            ;[1a6e] 23
                    ld        e,(hl)                        ;[1a6f] 5e
                    inc       hl                            ;[1a70] 23
                    ld        d,(hl)                        ;[1a71] 56
                    inc       hl                            ;[1a72] 23
                    add       hl,de                         ;[1a73] 19
                    ret                                     ;[1a74] c9

                    exx                                     ;[1a75] d9
                    push      hl                            ;[1a76] e5
                    exx                                     ;[1a77] d9
                    pop       de                            ;[1a78] d1
                    inc       de                            ;[1a79] 13
                    inc       hl                            ;[1a7a] 23
                    ld        a,(de)                        ;[1a7b] 1a
                    inc       de                            ;[1a7c] 13
                    cp        $20                           ;[1a7d] fe 20
                    jr        z,$1a7b                       ;[1a7f] 28 fa
                    or        $20                           ;[1a81] f6 20
                    cp        (hl)                          ;[1a83] be
                    jr        z,$1a7a                       ;[1a84] 28 f4
                    or        $80                           ;[1a86] f6 80
                    cp        (hl)                          ;[1a88] be
                    jr        z,$1a90                       ;[1a89] 28 05
                    and       $df                           ;[1a8b] e6 df
                    cp        (hl)                          ;[1a8d] be
                    scf                                     ;[1a8e] 37
                    ret       nz                            ;[1a8f] c0
                    ld        a,(de)                        ;[1a90] 1a
                    inc       de                            ;[1a91] 13
                    cp        $20                           ;[1a92] fe 20
                    jr        z,$1a90                       ;[1a94] 28 fa
                    jp        $2c88                         ;[1a96] c3 88 2c
                    dec       sp                            ;[1a99] 3b
                    dec       sp                            ;[1a9a] 3b
                    dec       sp                            ;[1a9b] 3b
                    dec       sp                            ;[1a9c] 3b
                    dec       sp                            ;[1a9d] 3b
                    dec       sp                            ;[1a9e] 3b
                    dec       sp                            ;[1a9f] 3b
                    dec       sp                            ;[1aa0] 3b
                    dec       sp                            ;[1aa1] 3b
                    dec       sp                            ;[1aa2] 3b
                    dec       sp                            ;[1aa3] 3b
                    dec       sp                            ;[1aa4] 3b
                    dec       sp                            ;[1aa5] 3b
                    dec       sp                            ;[1aa6] 3b
                    dec       sp                            ;[1aa7] 3b
                    dec       sp                            ;[1aa8] 3b
                    dec       sp                            ;[1aa9] 3b
                    dec       sp                            ;[1aaa] 3b
                    dec       sp                            ;[1aab] 3b
                    dec       sp                            ;[1aac] 3b
                    dec       sp                            ;[1aad] 3b
                    dec       sp                            ;[1aae] 3b
                    dec       sp                            ;[1aaf] 3b
                    dec       sp                            ;[1ab0] 3b
                    dec       sp                            ;[1ab1] 3b
                    dec       sp                            ;[1ab2] 3b
                    dec       sp                            ;[1ab3] 3b
                    dec       sp                            ;[1ab4] 3b
                    dec       sp                            ;[1ab5] 3b
                    dec       sp                            ;[1ab6] 3b
                    dec       sp                            ;[1ab7] 3b
                    dec       sp                            ;[1ab8] 3b
                    dec       sp                            ;[1ab9] 3b
                    dec       sp                            ;[1aba] 3b
                    dec       sp                            ;[1abb] 3b
                    dec       sp                            ;[1abc] 3b
                    dec       sp                            ;[1abd] 3b
                    dec       sp                            ;[1abe] 3b
                    dec       sp                            ;[1abf] fd 3b
                    dec       sp                            ;[1ac1] 3b
                    dec       sp                            ;[1ac2] 3b
                    call      nz,$3bcf                      ;[1ac3] c4 cf 3b
                    jp        $c53b                         ;[1ac6] c3 3b c5
                    dec       sp                            ;[1ac9] 3b
                    dec       sp                            ;[1aca] 3b
                    dec       sp                            ;[1acb] 3b
                    dec       sp                            ;[1acc] 3b
                    dec       sp                            ;[1acd] 3b
                    dec       sp                            ;[1ace] 3b
                    dec       sp                            ;[1acf] 3b
                    dec       sp                            ;[1ad0] 3b
                    dec       sp                            ;[1ad1] 3b
                    dec       sp                            ;[1ad2] 3b
                    dec       sp                            ;[1ad3] 3b
                    dec       sp                            ;[1ad4] 3b
                    call      $ccce                         ;[1ad5] cd ce cc
                    nop                                     ;[1ad8] 00
                    dec       sp                            ;[1ad9] 3b
                    dec       sp                            ;[1ada] 3b
                    dec       sp                            ;[1adb] 3b
                    dec       sp                            ;[1adc] 3b
                    dec       sp                            ;[1add] 3b
                    dec       sp                            ;[1ade] 3b
                    dec       sp                            ;[1adf] 3b
                    dec       sp                            ;[1ae0] 3b
                    dec       sp                            ;[1ae1] 3b
                    dec       sp                            ;[1ae2] 3b
                    dec       sp                            ;[1ae3] 3b
                    dec       sp                            ;[1ae4] 3b
                    dec       sp                            ;[1ae5] 3b
                    dec       sp                            ;[1ae6] 3b
                    dec       sp                            ;[1ae7] 3b
                    dec       sp                            ;[1ae8] 3b
                    dec       sp                            ;[1ae9] 3b
                    dec       sp                            ;[1aea] 3b
                    dec       sp                            ;[1aeb] 3b
                    dec       sp                            ;[1aec] 3b
                    dec       sp                            ;[1aed] 3b
                    dec       sp                            ;[1aee] 3b
                    dec       sp                            ;[1aef] 3b
                    dec       sp                            ;[1af0] 3b
                    dec       sp                            ;[1af1] 3b
                    dec       sp                            ;[1af2] 3b
                    dec       sp                            ;[1af3] 3b
                    ld        bc,$3b3b                      ;[1af4] 01 3b 3b
                    add       $3b                           ;[1af7] c6 3b
                    dec       sp                            ;[1af9] 3b
                    dec       sp                            ;[1afa] 3b
                    dec       sp                            ;[1afb] 3b
                    dec       sp                            ;[1afc] 3b
                    dec       sp                            ;[1afd] 3b
                    dec       sp                            ;[1afe] 3b
                    dec       sp                            ;[1aff] 3b
                    dec       sp                            ;[1b00] 3b
                    dec       sp                            ;[1b01] 3b
                    dec       sp                            ;[1b02] 3b
                    dec       sp                            ;[1b03] 3b
                    dec       sp                            ;[1b04] 3b
                    dec       sp                            ;[1b05] 3b
                    dec       sp                            ;[1b06] 3b
                    dec       sp                            ;[1b07] 3b
                    dec       sp                            ;[1b08] 3b
                    dec       sp                            ;[1b09] 3b
                    dec       sp                            ;[1b0a] 3b
                    dec       sp                            ;[1b0b] 3b
                    dec       sp                            ;[1b0c] 3b
                    dec       sp                            ;[1b0d] 3b
                    dec       sp                            ;[1b0e] 3b
                    dec       sp                            ;[1b0f] 3b
                    dec       sp                            ;[1b10] 3b
                    dec       sp                            ;[1b11] 3b
                    dec       sp                            ;[1b12] 3b
                    dec       sp                            ;[1b13] 3b
                    dec       sp                            ;[1b14] 3b
                    cp        $3b                           ;[1b15] fe 3b
                    dec       sp                            ;[1b17] 3b
                    dec       sp                            ;[1b18] 3b
                    dec       sp                            ;[1b19] 3b
                    dec       sp                            ;[1b1a] 3b
                    dec       sp                            ;[1b1b] 3b
                    dec       sp                            ;[1b1c] 3b
                    dec       sp                            ;[1b1d] 3b
                    dec       sp                            ;[1b1e] 3b
                    dec       sp                            ;[1b1f] 3b
                    dec       sp                            ;[1b20] 3b
                    dec       sp                            ;[1b21] 3b
                    dec       sp                            ;[1b22] 3b
                    dec       sp                            ;[1b23] 3b
                    call      m,$c2c1                       ;[1b24] fc c1 c2
                    dec       sp                            ;[1b27] 3b
                    dec       sp                            ;[1b28] 3b
                    dec       sp                            ;[1b29] 3b
                    dec       sp                            ;[1b2a] 3b
                    dec       sp                            ;[1b2b] 3b
                    dec       sp                            ;[1b2c] 3b
                    dec       sp                            ;[1b2d] 3b
                    dec       sp                            ;[1b2e] 3b
                    dec       sp                            ;[1b2f] 3b
                    dec       sp                            ;[1b30] 3b
                    dec       sp                            ;[1b31] 3b
                    dec       sp                            ;[1b32] 3b
                    dec       sp                            ;[1b33] 3b
                    dec       sp                            ;[1b34] 3b
                    dec       sp                            ;[1b35] 3b
                    dec       sp                            ;[1b36] 3b
                    dec       sp                            ;[1b37] 3b
                    dec       sp                            ;[1b38] 3b
                    dec       sp                            ;[1b39] 3b
                    dec       sp                            ;[1b3a] 3b
                    dec       sp                            ;[1b3b] 3b
                    dec       sp                            ;[1b3c] 3b
                    dec       sp                            ;[1b3d] 3b
                    dec       sp                            ;[1b3e] 3b
                    dec       sp                            ;[1b3f] 3b
                    dec       sp                            ;[1b40] 3b
                    dec       sp                            ;[1b41] 3b
                    dec       sp                            ;[1b42] 3b
                    dec       sp                            ;[1b43] 3b
                    dec       sp                            ;[1b44] 3b
                    dec       sp                            ;[1b45] 3b
                    dec       sp                            ;[1b46] 3b
                    dec       sp                            ;[1b47] 3b
                    dec       sp                            ;[1b48] 3b
                    dec       sp                            ;[1b49] 3b
                    dec       sp                            ;[1b4a] 3b
                    dec       sp                            ;[1b4b] 3b
                    dec       sp                            ;[1b4c] 3b
                    dec       sp                            ;[1b4d] 3b
                    dec       sp                            ;[1b4e] 3b
                    dec       sp                            ;[1b4f] 3b
                    dec       sp                            ;[1b50] 3b
                    dec       sp                            ;[1b51] 3b
                    dec       sp                            ;[1b52] 3b
                    dec       sp                            ;[1b53] 3b
                    dec       sp                            ;[1b54] 3b
                    dec       sp                            ;[1b55] 3b
                    dec       sp                            ;[1b56] 3b
                    dec       sp                            ;[1b57] 3b
                    dec       sp                            ;[1b58] 3b
                    dec       sp                            ;[1b59] 3b
                    dec       sp                            ;[1b5a] 3b
                    dec       sp                            ;[1b5b] 3b
                    dec       sp                            ;[1b5c] 3b
                    dec       sp                            ;[1b5d] 3b
                    rst       $00                           ;[1b5e] c7
                    ret       z                             ;[1b5f] c8
                    ret                                     ;[1b60] c9

                    jp        z,$3bcb                       ;[1b61] ca cb 3b
                    dec       sp                            ;[1b64] 3b
                    dec       sp                            ;[1b65] 3b
                    dec       sp                            ;[1b66] 3b
                    dec       sp                            ;[1b67] 3b
                    dec       sp                            ;[1b68] 3b
                    dec       sp                            ;[1b69] 3b
                    dec       sp                            ;[1b6a] 3b
                    dec       sp                            ;[1b6b] 3b
                    dec       sp                            ;[1b6c] 3b
                    dec       sp                            ;[1b6d] 3b
                    dec       sp                            ;[1b6e] 3b
                    dec       sp                            ;[1b6f] 3b
                    dec       sp                            ;[1b70] 3b
                    dec       sp                            ;[1b71] 3b
                    dec       sp                            ;[1b72] 3b
                    dec       sp                            ;[1b73] 3b
                    dec       sp                            ;[1b74] 3b
                    dec       sp                            ;[1b75] 3b
                    dec       sp                            ;[1b76] 3b
                    dec       sp                            ;[1b77] 3b
                    dec       sp                            ;[1b78] 3b
                    dec       sp                            ;[1b79] 3b
                    dec       sp                            ;[1b7a] 3b
                    dec       sp                            ;[1b7b] 3b
                    dec       sp                            ;[1b7c] 3b
                    dec       sp                            ;[1b7d] 3b
                    dec       sp                            ;[1b7e] 3b
                    dec       sp                            ;[1b7f] 3b
                    dec       sp                            ;[1b80] 3b
                    dec       sp                            ;[1b81] 3b
                    dec       sp                            ;[1b82] 3b
                    dec       sp                            ;[1b83] 3b
                    dec       sp                            ;[1b84] 3b
                    dec       sp                            ;[1b85] 3b
                    dec       sp                            ;[1b86] 3b
                    dec       sp                            ;[1b87] 3b
                    dec       sp                            ;[1b88] 3b
                    dec       sp                            ;[1b89] 3b
                    dec       sp                            ;[1b8a] 3b
                    dec       sp                            ;[1b8b] 3b
                    dec       sp                            ;[1b8c] 3b
                    dec       sp                            ;[1b8d] 3b
                    dec       sp                            ;[1b8e] 3b
                    dec       sp                            ;[1b8f] 3b
                    dec       sp                            ;[1b90] 3b
                    dec       sp                            ;[1b91] 3b
                    dec       sp                            ;[1b92] 3b
                    dec       sp                            ;[1b93] 3b
                    dec       sp                            ;[1b94] 3b
                    dec       sp                            ;[1b95] 3b
                    dec       sp                            ;[1b96] 3b
                    dec       sp                            ;[1b97] 3b
                    dec       sp                            ;[1b98] 3b
                    jr        c,$1ba1                       ;[1b99] 38 06
                    inc       bc                            ;[1b9b] 03
                    ld        hl,($5c4d)                    ;[1b9c] 2a 4d 5c
                    lddr                                    ;[1b9f] ed b8
                    pop       de                            ;[1ba1] d1
                    call      $1bad                         ;[1ba2] cd ad 1b
                    ex        de,hl                         ;[1ba5] eb
                    pop       bc                            ;[1ba6] c1
                    inc       hl                            ;[1ba7] 23
                    ld        (hl),c                        ;[1ba8] 71
                    inc       hl                            ;[1ba9] 23
                    ld        (hl),b                        ;[1baa] 70
                    ret                                     ;[1bab] c9

                    nop                                     ;[1bac] 00
                    ld        hl,($5c5b)                    ;[1bad] 2a 5b 5c
                    ld        a,(hl)                        ;[1bb0] 7e
                    and       $1f                           ;[1bb1] e6 1f
                    or        ixh                           ;[1bb3] dd b4
                    ld        b,ixl                         ;[1bb5] dd 45
                    dec       b                             ;[1bb7] 05
                    jr        z,$1bce                       ;[1bb8] 28 14
                    ex        de,hl                         ;[1bba] eb
                    ld        (hl),$7f                      ;[1bbb] 36 7f
                    inc       hl                            ;[1bbd] 23
                    ex        de,hl                         ;[1bbe] eb
                    ld        (de),a                        ;[1bbf] 12
                    inc       hl                            ;[1bc0] 23
                    ld        a,(hl)                        ;[1bc1] 7e
                    cp        $20                           ;[1bc2] fe 20
                    jr        z,$1bc0                       ;[1bc4] 28 fa
                    or        $20                           ;[1bc6] f6 20
                    inc       de                            ;[1bc8] 13
                    ld        (de),a                        ;[1bc9] 12
                    djnz      $1bc0                         ;[1bca] 10 f4
                    or        $80                           ;[1bcc] f6 80
                    ld        (de),a                        ;[1bce] 12
                    ret                                     ;[1bcf] c9

                    nop                                     ;[1bd0] 00
                    ld        hl,($5c4d)                    ;[1bd1] 2a 4d 5c
                    ld        b,$00                         ;[1bd4] 06 00
                    jr        $1be2                         ;[1bd6] 18 0a
                    inc       hl                            ;[1bd8] 23
                    ld        a,(hl)                        ;[1bd9] 7e
                    cp        $20                           ;[1bda] fe 20
                    jr        z,$1bd8                       ;[1bdc] 28 fa
                    call      $2c88                         ;[1bde] cd 88 2c
                    ret       nc                            ;[1be1] d0
                    djnz      $1bd8                         ;[1be2] 10 f4
                    rst       $08                           ;[1be4] cf
                    dec       bc                            ;[1be5] 0b
                    ret                                     ;[1be6] c9

                    rst       $38                           ;[1be7] ff
                    rst       $38                           ;[1be8] ff
                    rst       $38                           ;[1be9] ff
                    rst       $38                           ;[1bea] ff
                    rst       $38                           ;[1beb] ff
                    rst       $38                           ;[1bec] ff
                    rst       $38                           ;[1bed] ff
                    call      $2530                         ;[1bee] cd 30 25
                    ret       nz                            ;[1bf1] c0
                    pop       bc                            ;[1bf2] c1
                    pop       hl                            ;[1bf3] e1
                    pop       bc                            ;[1bf4] c1
                    push    $0944                           ;[1bf5] ed 8a 09 44
                    jp        (hl)                          ;[1bf9] e9
                    ld        ($5b8c),de                    ;[1bfa] ed 53 8c 5b
                    jr        $1c22                         ;[1bfe] 18 22
                    rst       $38                           ;[1c00] ff
                    rrca                                    ;[1c01] 0f
                    dec       e                             ;[1c02] 1d
                    ld        c,e                           ;[1c03] 4b
                    add       hl,bc                         ;[1c04] 09
                    ld        h,a                           ;[1c05] 67
                    dec       bc                            ;[1c06] 0b
                    ld        a,e                           ;[1c07] 7b
                    adc       (hl)                          ;[1c08] 8e
                    ld        (hl),c                        ;[1c09] 71
                    or        h                             ;[1c0a] b4
                    add       c                             ;[1c0b] 81
                    rst       $08                           ;[1c0c] cf
                    call      $1cde                         ;[1c0d] cd de 1c
                    cp        a                             ;[1c10] bf
                    pop       bc                            ;[1c11] c1
                    call      z,$1bee                       ;[1c12] cc ee 1b
                    ex        de,hl                         ;[1c15] eb
                    ld        hl,($5c74)                    ;[1c16] 2a 74 5c
                    ld        c,(hl)                        ;[1c19] 4e
                    inc       hl                            ;[1c1a] 23
                    ld        b,(hl)                        ;[1c1b] 46
                    ex        de,hl                         ;[1c1c] eb
                    push      bc                            ;[1c1d] c5
                    ret                                     ;[1c1e] c9

                    call      $28b2                         ;[1c1f] cd b2 28
                    ld        (iy+$37),$00                  ;[1c22] fd 36 37 00
                    jr        nc,$1c30                      ;[1c26] 30 08
                    set       1,(iy+$37)                    ;[1c28] fd cb 37 ce
                    jr        nz,$1c46                      ;[1c2c] 20 18
                    rst       $08                           ;[1c2e] cf
                    ld        bc,$96cc                      ;[1c2f] 01 cc 96
                    add       hl,hl                         ;[1c32] 29
                    bit       6,(iy+$01)                    ;[1c33] fd cb 01 76
                    jr        nz,$1c4a                      ;[1c37] 20 11
                    xor       a                             ;[1c39] af
                    call      $2530                         ;[1c3a] cd 30 25
                    call      nz,$2bf1                      ;[1c3d] c4 f1 2b
                    ld        hl,$5c71                      ;[1c40] 21 71 5c
                    or        (hl)                          ;[1c43] b6
                    ld        (hl),a                        ;[1c44] 77
                    ex        de,hl                         ;[1c45] eb
                    ld        ($5c72),bc                    ;[1c46] ed 43 72 5c
                    ld        ($5c4d),hl                    ;[1c4a] 22 4d 5c
                    ret                                     ;[1c4d] c9

                    pop       bc                            ;[1c4e] c1
                    call      $1c56                         ;[1c4f] cd 56 1c
                    call      $1bee                         ;[1c52] cd ee 1b
                    ret                                     ;[1c55] c9

                    ld        a,($5c3b)                     ;[1c56] 3a 3b 5c
                    push      af                            ;[1c59] f5
                    call      $24fb                         ;[1c5a] cd fb 24
                    pop       af                            ;[1c5d] f1
                    ld        d,(iy+$01)                    ;[1c5e] fd 56 01
                    xor       d                             ;[1c61] aa
                    and       $40                           ;[1c62] e6 40
                    jr        nz,$1c8a                      ;[1c64] 20 24
                    bit       7,d                           ;[1c66] cb 7a
                    jp        nz,$2aff                      ;[1c68] c2 ff 2a
                    ret                                     ;[1c6b] c9

                    call      $28b3                         ;[1c6c] cd b3 28
                    push      af                            ;[1c6f] f5
                    bit       5,c                           ;[1c70] cb 69
                    jr        z,$1c8a                       ;[1c72] 28 16
                    pop       af                            ;[1c74] f1
                    jp        $1bfa                         ;[1c75] c3 fa 1b
                    nop                                     ;[1c78] 00
                    rst       $20                           ;[1c79] e7
                    call      $1c82                         ;[1c7a] cd 82 1c
                    cp        $2c                           ;[1c7d] fe 2c
                    jr        nz,$1c8a                      ;[1c7f] 20 09
                    rst       $20                           ;[1c81] e7
                    call      $24fb                         ;[1c82] cd fb 24
                    bit       6,(iy+$01)                    ;[1c85] fd cb 01 76
                    ret       nz                            ;[1c89] c0
                    rst       $08                           ;[1c8a] cf
                    dec       bc                            ;[1c8b] 0b
                    call      $24fb                         ;[1c8c] cd fb 24
                    bit       6,(iy+$01)                    ;[1c8f] fd cb 01 76
                    ret       z                             ;[1c93] c8
                    jr        $1c8a                         ;[1c94] 18 f4
                    bit       7,(iy+$01)                    ;[1c96] fd cb 01 7e
                    res       0,(iy+$02)                    ;[1c9a] fd cb 02 86
                    call      nz,$0d4d                      ;[1c9e] c4 4d 0d
                    pop       af                            ;[1ca1] f1
                    ld        a,($5c74)                     ;[1ca2] 3a 74 5c
                    sub       $13                           ;[1ca5] d6 13
                    call      $21fc                         ;[1ca7] cd fc 21
                    call      $1bee                         ;[1caa] cd ee 1b
                    ld        hl,($5c8f)                    ;[1cad] 2a 8f 5c
                    ld        ($5c8d),hl                    ;[1cb0] 22 8d 5c
                    ld        hl,$5c91                      ;[1cb3] 21 91 5c
                    ld        a,(hl)                        ;[1cb6] 7e
                    rlca                                    ;[1cb7] 07
                    xor       (hl)                          ;[1cb8] ae
                    and       $aa                           ;[1cb9] e6 aa
                    xor       (hl)                          ;[1cbb] ae
                    ld        (hl),a                        ;[1cbc] 77
                    ret                                     ;[1cbd] c9

                    call      $2530                         ;[1cbe] cd 30 25
                    jr        z,$1cd6                       ;[1cc1] 28 13
                    res       0,(iy+$02)                    ;[1cc3] fd cb 02 86
                    call      $0d4d                         ;[1cc7] cd 4d 0d
                    ld        hl,$5c90                      ;[1cca] 21 90 5c
                    ld        a,(hl)                        ;[1ccd] 7e
                    or        $f8                           ;[1cce] f6 f8
                    ld        (hl),a                        ;[1cd0] 77
                    res       6,(iy+$57)                    ;[1cd1] fd cb 57 b6
                    rst       $18                           ;[1cd5] df
                    call      $21e2                         ;[1cd6] cd e2 21
                    jr        $1c7a                         ;[1cd9] 18 9f
                    jp        $1cdb                         ;[1cdb] c3 db 1c
                    cp        $0d                           ;[1cde] fe 0d
                    jr        z,$1ce6                       ;[1ce0] 28 04
                    cp        $3a                           ;[1ce2] fe 3a
                    jr        nz,$1c82                      ;[1ce4] 20 9c
                    call      $2530                         ;[1ce6] cd 30 25
                    ret       z                             ;[1ce9] c8
                    rst       $28                           ;[1cea] ef
                    and       b                             ;[1ceb] a0
                    jr        c,$1cb7                       ;[1cec] 38 c9
                    rst       $28                           ;[1cee] ef
                    ret       nz                            ;[1cef] c0
                    ld        (bc),a                        ;[1cf0] 02
                    ld        bc,$01e0                      ;[1cf1] 01 e0 01
                    jr        c,$1cc3                       ;[1cf4] 38 cd
                    rst       $38                           ;[1cf6] ff
                    ld        hl,($2be5)                    ;[1cf7] 2a e5 2b
                    and       $40                           ;[1cfa] e6 40
                    jr        nz,$1d06                      ;[1cfc] 20 08
                    bit       5,(hl)                        ;[1cfe] cb 6e
                    jr        z,$1d3c                       ;[1d00] 28 3a
                    res       5,(hl)                        ;[1d02] cb ae
                    jr        $1d0c                         ;[1d04] 18 06
                    bit       7,(hl)                        ;[1d06] cb 7e
                    jr        nz,$1d3c                      ;[1d08] 20 32
                    set       7,(hl)                        ;[1d0a] cb fe
                    pop       hl                            ;[1d0c] e1
                    push      hl                            ;[1d0d] e5
                    add       hl,$0005                      ;[1d0e] ed 34 05 00
                    push      hl                            ;[1d12] e5
                    ld        bc,($5c65)                    ;[1d13] ed 4b 65 5c
                    and       a                             ;[1d17] a7
                    sbc       hl,bc                         ;[1d18] ed 42
                    pop       hl                            ;[1d1a] e1
                    ld        bc,$000d                      ;[1d1b] 01 0d 00
                    jr        c,$1d39                       ;[1d1e] 38 19
                    push      bc                            ;[1d20] c5
                    call      $3ad4                         ;[1d21] cd d4 3a
                    pop       bc                            ;[1d24] c1
                    ld        hl,($5b8c)                    ;[1d25] 2a 8c 5b
                    scf                                     ;[1d28] 37
                    sbc       hl,bc                         ;[1d29] ed 42
                    ld        d,(hl)                        ;[1d2b] 56
                    dec       hl                            ;[1d2c] 2b
                    ld        e,(hl)                        ;[1d2d] 5e
                    ex        de,hl                         ;[1d2e] eb
                    add       hl,bc                         ;[1d2f] 09
                    ex        de,hl                         ;[1d30] eb
                    ld        (hl),e                        ;[1d31] 73
                    inc       hl                            ;[1d32] 23
                    ld        (hl),d                        ;[1d33] 72
                    pop       hl                            ;[1d34] e1
                    sbc       hl,bc                         ;[1d35] ed 42
                    jr        $1d3d                         ;[1d37] 18 04
                    call      $1655                         ;[1d39] cd 55 16
                    pop       hl                            ;[1d3c] e1
                    push      hl                            ;[1d3d] e5
                    ld        ($5c68),hl                    ;[1d3e] 22 68 5c
                    rst       $28                           ;[1d41] ef
                    ld        (bc),a                        ;[1d42] 02
                    ld        (bc),a                        ;[1d43] 02
                    jr        c,$1d27                       ;[1d44] 38 e1
                    add       hl,$0005                      ;[1d46] ed 34 05 00
                    ex        de,hl                         ;[1d4a] eb
                    ld        c,$0a                         ;[1d4b] 0e 0a
                    ldir                                    ;[1d4d] ed b0
                    ld        hl,($5c45)                    ;[1d4f] 2a 45 5c
                    ex        de,hl                         ;[1d52] eb
                    ld        (hl),e                        ;[1d53] 73
                    inc       hl                            ;[1d54] 23
                    ld        (hl),d                        ;[1d55] 72
                    ld        d,(iy+$0d)                    ;[1d56] fd 56 0d
                    inc       hl                            ;[1d59] 23
                    ld        (hl),d                        ;[1d5a] 72
                    jr        $1dbb                         ;[1d5b] 18 5e
                    rst       $20                           ;[1d5d] e7
                    cp        $24                           ;[1d5e] fe 24
                    ld        de,$1f23                      ;[1d60] 11 23 1f
                    jp        z,$1063                       ;[1d63] ca 63 10
                    ld        de,($5c7a)                    ;[1d66] ed 5b 7a 5c
                    ld        hl,($5c78)                    ;[1d6a] 2a 78 5c
                    ld        a,h                           ;[1d6d] 7c
                    or        l                             ;[1d6e] b5
                    jr        nz,$1d75                      ;[1d6f] 20 04
                    ld        a,($5c7a)                     ;[1d71] 3a 7a 5c
                    ld        e,a                           ;[1d74] 5f
                    ld        d,$00                         ;[1d75] 16 00
                    call      $2530                         ;[1d77] cd 30 25
                    call      nz,$3932                      ;[1d7a] c4 32 39
                    jp        $2181                         ;[1d7d] c3 81 21
                    rst       $38                           ;[1d80] ff
                    rst       $38                           ;[1d81] ff
                    rst       $38                           ;[1d82] ff
                    rst       $38                           ;[1d83] ff
                    rst       $38                           ;[1d84] ff
                    rst       $38                           ;[1d85] ff
                    bit       1,(iy+$37)                    ;[1d86] fd cb 37 4e
                    ld        b,$01                         ;[1d8a] 06 01
                    ret       nz                            ;[1d8c] c0
                    dec       b                             ;[1d8d] 05
                    ld        hl,($5b8c)                    ;[1d8e] 2a 8c 5b
                    ld        a,(hl)                        ;[1d91] 7e
                    add       a                             ;[1d92] 87
                    ret       nc                            ;[1d93] d0
                    add       a                             ;[1d94] 87
                    ld        hl,($5c4d)                    ;[1d95] 2a 4d 5c
                    jr        c,$1d9d                       ;[1d98] 38 03
                    bit       5,(hl)                        ;[1d9a] cb 6e
                    ret       nz                            ;[1d9c] c0
                    inc       hl                            ;[1d9d] 23
                    ld        ($5c68),hl                    ;[1d9e] 22 68 5c
                    add       hl,$000f                      ;[1da1] ed 34 0f 00
                    add       de,$0007                      ;[1da5] ed 35 07 00
                    ld        c,$03                         ;[1da9] 0e 03
                    ld        a,(de)                        ;[1dab] 1a
                    inc       de                            ;[1dac] 13
                    cp        (hl)                          ;[1dad] be
                    inc       hl                            ;[1dae] 23
                    ret       nz                            ;[1daf] c0
                    dec       c                             ;[1db0] 0d
                    jr        nz,$1dab                      ;[1db1] 20 f8
                    ret                                     ;[1db3] c9

                    rst       $28                           ;[1db4] ef
                    ret       po                            ;[1db5] e0
                    jp        po,$c00f                      ;[1db6] e2 0f c0
                    ld        (bc),a                        ;[1db9] 02
                    jr        c,$1dab                       ;[1dba] 38 ef
                    pop       hl                            ;[1dbc] e1
                    ret       po                            ;[1dbd] e0
                    jp        po,$0036                      ;[1dbe] e2 36 00
                    ld        (bc),a                        ;[1dc1] 02
                    ld        bc,$3703                      ;[1dc2] 01 03 37
                    nop                                     ;[1dc5] 00
                    inc       b                             ;[1dc6] 04
                    jr        c,$1d70                       ;[1dc7] 38 a7
                    ret                                     ;[1dc9] c9

                    jr        c,$1e03                       ;[1dca] 38 37
                    ret                                     ;[1dcc] c9

                    ld        bc,$0000                      ;[1dcd] 01 00 00
                    push      bc                            ;[1dd0] c5
                    rst       $28                           ;[1dd1] ef
                    pop       hl                            ;[1dd2] e1
                    ld        ($3801),a                     ;[1dd3] 32 01 38
                    call      $2dd5                         ;[1dd6] cd d5 2d
                    call      $1e42                         ;[1dd9] cd 42 1e
                    pop       bc                            ;[1ddc] c1
                    push      af                            ;[1ddd] f5
                    inc       sp                            ;[1dde] 33
                    inc       c                             ;[1ddf] 0c
                    jp        z,$31ad                       ;[1de0] ca ad 31
                    call      $34e9                         ;[1de3] cd e9 34
                    jr        nc,$1dd0                      ;[1de6] 30 e8
                    bit       7,(iy+$68)                    ;[1de8] fd cb 68 7e
                    ld        a,$2d                         ;[1dec] 3e 2d
                    jr        z,$1df5                       ;[1dee] 28 05
                    push      af                            ;[1df0] f5
                    inc       sp                            ;[1df1] 33
                    inc       c                             ;[1df2] 0c
                    jr        z,$1de0                       ;[1df3] 28 eb
                    rst       $30                           ;[1df5] f7
                    ld        h,d                           ;[1df6] 62
                    ld        l,e                           ;[1df7] 6b
                    ld        b,c                           ;[1df8] 41
                    dec       sp                            ;[1df9] 3b
                    pop       af                            ;[1dfa] f1
                    ld        (hl),a                        ;[1dfb] 77
                    inc       hl                            ;[1dfc] 23
                    djnz      $1df9                         ;[1dfd] 10 fa
                    pop       af                            ;[1dff] f1
                    push      de                            ;[1e00] d5
                    and       a                             ;[1e01] a7
                    jr        z,$1e2b                       ;[1e02] 28 27
                    add       bc,a                          ;[1e04] ed 33
                    inc       bc                            ;[1e06] 03
                    push      bc                            ;[1e07] c5
                    push      af                            ;[1e08] f5
                    inc       a                             ;[1e09] 3c
                    ld        c,a                           ;[1e0a] 4f
                    ld        b,$00                         ;[1e0b] 06 00
                    rst       $30                           ;[1e0d] f7
                    ld        a,$2e                         ;[1e0e] 3e 2e
                    ld        (de),a                        ;[1e10] 12
                    inc       de                            ;[1e11] 13
                    pop       af                            ;[1e12] f1
                    push      af                            ;[1e13] f5
                    push      de                            ;[1e14] d5
                    rst       $28                           ;[1e15] ef
                    inc       bc                            ;[1e16] 03
                    pop       hl                            ;[1e17] e1
                    inc       b                             ;[1e18] 04
                    ld        sp,$313a                      ;[1e19] 31 3a 31
                    jr        c,$1deb                       ;[1e1c] 38 cd
                    push      de                            ;[1e1e] d5
                    dec       l                             ;[1e1f] 2d
                    call      $1e42                         ;[1e20] cd 42 1e
                    pop       de                            ;[1e23] d1
                    ld        (de),a                        ;[1e24] 12
                    inc       de                            ;[1e25] 13
                    pop       af                            ;[1e26] f1
                    dec       a                             ;[1e27] 3d
                    jr        nz,$1e13                      ;[1e28] 20 e9
                    pop       bc                            ;[1e2a] c1
                    push      bc                            ;[1e2b] c5
                    rst       $28                           ;[1e2c] ef
                    ld        (bc),a                        ;[1e2d] 02
                    ld        (bc),a                        ;[1e2e] 02
                    jr        c,$1df2                       ;[1e2f] 38 c1
                    pop       de                            ;[1e31] d1
                    jp        $25db                         ;[1e32] c3 db 25
                    rst       $38                           ;[1e35] ff
                    rst       $38                           ;[1e36] ff
                    rst       $38                           ;[1e37] ff
                    rst       $38                           ;[1e38] ff
                    ld        b,a                           ;[1e39] 47
                    cpdr                                    ;[1e3a] ed b9
                    ld        de,$0200                      ;[1e3c] 11 00 02
                    jp        $198b                         ;[1e3f] c3 8b 19
                    add       $30                           ;[1e42] c6 30
                    cp        $3a                           ;[1e44] fe 3a
                    ret       c                             ;[1e46] d8
                    add       $07                           ;[1e47] c6 07
                    ret                                     ;[1e49] c9

                    rst       $38                           ;[1e4a] ff
                    rst       $38                           ;[1e4b] ff
                    rst       $38                           ;[1e4c] ff
                    rst       $38                           ;[1e4d] ff
                    rst       $38                           ;[1e4e] ff
                    call      $1e99                         ;[1e4f] cd 99 1e
                    ld        a,b                           ;[1e52] 78
                    or        c                             ;[1e53] b1
                    jr        nz,$1e5a                      ;[1e54] 20 04
                    ld        bc,($5c78)                    ;[1e56] ed 4b 78 5c
                    ld        ($5c76),bc                    ;[1e5a] ed 43 76 5c
                    ret                                     ;[1e5e] c9

                    push    $5b48                           ;[1e5f] ed 8a 5b 48
                    push    $007b                           ;[1e63] ed 8a 00 7b
                    push      hl                            ;[1e67] e5
                    push    $007b                           ;[1e68] ed 8a 00 7b
                    jp        $5b4d                         ;[1e6c] c3 4d 5b
                    rr        d                             ;[1e6f] cb 1a
                    ld        c,$00                         ;[1e71] 0e 00
                    ret       c                             ;[1e73] d8
                    push      de                            ;[1e74] d5
                    call      $1e94                         ;[1e75] cd 94 1e
                    pop       de                            ;[1e78] d1
                    ret                                     ;[1e79] c9

                    call      $1e85                         ;[1e7a] cd 85 1e
                    out       (c),a                         ;[1e7d] ed 79
                    ret                                     ;[1e7f] c9

                    call      $1e85                         ;[1e80] cd 85 1e
                    ld        (bc),a                        ;[1e83] 02
                    ret                                     ;[1e84] c9

                    call      $2dd5                         ;[1e85] cd d5 2d
                    jr        c,$1e9f                       ;[1e88] 38 15
                    jr        z,$1e8e                       ;[1e8a] 28 02
                    neg                                     ;[1e8c] ed 44
                    push      af                            ;[1e8e] f5
                    call      $1e99                         ;[1e8f] cd 99 1e
                    pop       af                            ;[1e92] f1
                    ret                                     ;[1e93] c9

                    call      $2dd5                         ;[1e94] cd d5 2d
                    jr        $1e9c                         ;[1e97] 18 03
                    call      $2da2                         ;[1e99] cd a2 2d
                    jr        c,$1e9f                       ;[1e9c] 38 01
                    ret       z                             ;[1e9e] c8
                    rst       $08                           ;[1e9f] cf
                    ld        a,(bc)                        ;[1ea0] 0a
                    cp        $65                           ;[1ea1] fe 65
                    jr        z,$1ea8                       ;[1ea3] 28 03
                    cp        $45                           ;[1ea5] fe 45
                    ret       nz                            ;[1ea7] c0
                    ld        a,($5cad)                     ;[1ea8] 3a ad 5c
                    cp        $0a                           ;[1eab] fe 0a
                    ret       nz                            ;[1ead] c0
                    ld        b,$ff                         ;[1eae] 06 ff
                    rst       $20                           ;[1eb0] e7
                    cp        $2b                           ;[1eb1] fe 2b
                    jr        z,$1eba                       ;[1eb3] 28 05
                    cp        $2d                           ;[1eb5] fe 2d
                    jr        nz,$1ebb                      ;[1eb7] 20 02
                    inc       b                             ;[1eb9] 04
                    rst       $20                           ;[1eba] e7
                    push      bc                            ;[1ebb] c5
                    call      $1ed8                         ;[1ebc] cd d8 1e
                    jp        c,$2cf8                       ;[1ebf] da f8 2c
                    call      $2d3b                         ;[1ec2] cd 3b 2d
                    call      $2dd5                         ;[1ec5] cd d5 2d
                    pop       bc                            ;[1ec8] c1
                    jp        c,$31ad                       ;[1ec9] da ad 31
                    and       a                             ;[1ecc] a7
                    jp        m,$31ad                       ;[1ecd] fa ad 31
                    inc       b                             ;[1ed0] 04
                    jr        z,$1ed5                       ;[1ed1] 28 02
                    neg                                     ;[1ed3] ed 44
                    jp        $2d4f                         ;[1ed5] c3 4f 2d
                    ld        b,a                           ;[1ed8] 47
                    sub       $30                           ;[1ed9] d6 30
                    jr        c,$1ef0                       ;[1edb] 38 13
                    cp        $0a                           ;[1edd] fe 0a
                    jr        c,$1eeb                       ;[1edf] 38 0a
                    and       $df                           ;[1ee1] e6 df
                    sub       $07                           ;[1ee3] d6 07
                    jr        c,$1ef0                       ;[1ee5] 38 09
                    cp        $0a                           ;[1ee7] fe 0a
                    jr        c,$1ef0                       ;[1ee9] 38 05
                    cp        (iy+$73)                      ;[1eeb] fd be 73
                    ccf                                     ;[1eee] 3f
                    ret       nc                            ;[1eef] d0
                    ld        a,b                           ;[1ef0] 78
                    ret                                     ;[1ef1] c9

                    push    $5b48                           ;[1ef2] ed 8a 5b 48
                    push    $007b                           ;[1ef6] ed 8a 00 7b
                    push      bc                            ;[1efa] c5
                    push    $007b                           ;[1efb] ed 8a 00 7b
                    jp        $5b3e                         ;[1eff] c3 3e 5b
                    rst       $38                           ;[1f02] ff
                    rst       $38                           ;[1f03] ff
                    rst       $38                           ;[1f04] ff
                    ld        hl,($5c65)                    ;[1f05] 2a 65 5c
                    add       hl,bc                         ;[1f08] 09
                    jr        c,$1f15                       ;[1f09] 38 0a
                    ex        de,hl                         ;[1f0b] eb
                    ld        hl,$0050                      ;[1f0c] 21 50 00
                    add       hl,de                         ;[1f0f] 19
                    jr        c,$1f15                       ;[1f10] 38 03
                    sbc       hl,sp                         ;[1f12] ed 72
                    ret       c                             ;[1f14] d8
                    rst       $08                           ;[1f15] cf
                    inc       bc                            ;[1f16] 03
                    nop                                     ;[1f17] 00
                    nop                                     ;[1f18] 00
                    nop                                     ;[1f19] 00
                    ld        bc,$0000                      ;[1f1a] 01 00 00
                    call      $1f05                         ;[1f1d] cd 05 1f
                    ld        b,h                           ;[1f20] 44
                    ld        c,l                           ;[1f21] 4d
                    ret                                     ;[1f22] c9

                    rst       $08                           ;[1f23] cf
                    adc       (hl)                          ;[1f24] 8e
                    ret       c                             ;[1f25] d8
                    push      hl                            ;[1f26] e5
                    ld        hl,$3720                      ;[1f27] 21 20 37
                    call      $32cd                         ;[1f2a] cd cd 32
                    pop       hl                            ;[1f2d] e1
                    ld        l,h                           ;[1f2e] 6c
                    ld        bc,$3714                      ;[1f2f] 01 14 37
                    call      $32c5                         ;[1f32] cd c5 32
                    ret                                     ;[1f35] c9

                    rst       $38                           ;[1f36] ff
                    rst       $38                           ;[1f37] ff
                    rst       $38                           ;[1f38] ff
                    rst       $38                           ;[1f39] ff
                    call      $1e99                         ;[1f3a] cd 99 1e
                    halt                                    ;[1f3d] 76
                    dec       bc                            ;[1f3e] 0b
                    ld        a,b                           ;[1f3f] 78
                    or        c                             ;[1f40] b1
                    jr        z,$1f4f                       ;[1f41] 28 0c
                    ld        a,b                           ;[1f43] 78
                    and       c                             ;[1f44] a1
                    inc       a                             ;[1f45] 3c
                    jr        nz,$1f49                      ;[1f46] 20 01
                    inc       bc                            ;[1f48] 03
                    bit       5,(iy+$01)                    ;[1f49] fd cb 01 6e
                    jr        z,$1f3d                       ;[1f4d] 28 ee
                    res       5,(iy+$01)                    ;[1f4f] fd cb 01 ae
                    ret                                     ;[1f53] c9

                    ld        a,$7f                         ;[1f54] 3e 7f
                    in        a,($fe)                       ;[1f56] db fe
                    rra                                     ;[1f58] 1f
                    ret       c                             ;[1f59] d8
                    ld        a,$fe                         ;[1f5a] 3e fe
                    in        a,($fe)                       ;[1f5c] db fe
                    rra                                     ;[1f5e] 1f
                    ret       c                             ;[1f5f] d8
                    ld        bc,$3e75                      ;[1f60] 01 75 3e
                    call      $32c5                         ;[1f63] cd c5 32
                    scf                                     ;[1f66] 37
                    ret       nz                            ;[1f67] c0
                    and       a                             ;[1f68] a7
                    ret                                     ;[1f69] c9

                    ex        af,af'                        ;[1f6a] 08
                    ld        a,d                           ;[1f6b] 7a
                    inc       d                             ;[1f6c] 14
                    jr        z,$1f90                       ;[1f6d] 28 21
                    ex        de,hl                         ;[1f6f] eb
                    ld        hl,($5b6a)                    ;[1f70] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[1f73] ed 73 6a 5b
                    ld        sp,hl                         ;[1f77] f9
                    push      de                            ;[1f78] d5
                    ld        hl,$3328                      ;[1f79] 21 28 33
                    call      $32cd                         ;[1f7c] cd cd 32
                    pop       hl                            ;[1f7f] e1
                    jp        nz,$1f15                      ;[1f80] c2 15 1f
                    ld        a,c                           ;[1f83] 79
                    add       a                             ;[1f84] 87
                    nextreg $56,a                           ;[1f85] ed 92 56
                    inc       a                             ;[1f88] 3c
                    nextreg $57,a                           ;[1f89] ed 92 57
                    set       7,h                           ;[1f8c] cb fc
                    set       6,h                           ;[1f8e] cb f4
                    push      af                            ;[1f90] f5
                    call      $1fa8                         ;[1f91] cd a8 1f
                    pop       af                            ;[1f94] f1
                    jr        z,$1fa3                       ;[1f95] 28 0c
                    nextreg $8e,$0b                         ;[1f97] ed 91 8e 0b
                    ld        hl,($5b6a)                    ;[1f9b] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[1f9e] ed 73 6a 5b
                    ld        sp,hl                         ;[1fa2] f9
                    ld        iy,$5c3a                      ;[1fa3] fd 21 3a 5c
                    ret                                     ;[1fa7] c9

                    push      hl                            ;[1fa8] e5
                    ld        a,$10                         ;[1fa9] 3e 10
                    ld        ($5b5c),a                     ;[1fab] 32 5c 5b
                    ld        a,$04                         ;[1fae] 3e 04
                    ld        ($5b67),a                     ;[1fb0] 32 67 5b
                    exx                                     ;[1fb3] d9
                    ex        af,af'                        ;[1fb4] 08
                    ret                                     ;[1fb5] c9

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
                    call      $2530                         ;[1fc3] cd 30 25
                    pop       hl                            ;[1fc6] e1
                    ret       z                             ;[1fc7] c8
                    jp        (hl)                          ;[1fc8] e9
                    nop                                     ;[1fc9] 00
                    ex        af,af'                        ;[1fca] 08
                    inc       bc                            ;[1fcb] 03
                    ld        (bc),a                        ;[1fcc] 02
                    ld        (bc),a                        ;[1fcd] 02
                    nop                                     ;[1fce] 00
                    inc       c                             ;[1fcf] 0c
                    inc       c                             ;[1fd0] 0c
                    ld        b,$08                         ;[1fd1] 06 08
                    ex        af,af'                        ;[1fd3] 08
                    ld        a,(bc)                        ;[1fd4] 0a
                    ld        (bc),a                        ;[1fd5] 02
                    inc       bc                            ;[1fd6] 03
                    dec       b                             ;[1fd7] 05
                    dec       b                             ;[1fd8] 05
                    dec       b                             ;[1fd9] 05
                    dec       b                             ;[1fda] 05
                    dec       b                             ;[1fdb] 05
                    dec       b                             ;[1fdc] 05
                    ld        b,$ff                         ;[1fdd] 06 ff
                    rst       $18                           ;[1fdf] df
                    call      $2045                         ;[1fe0] cd 45 20
                    jr        z,$1ff2                       ;[1fe3] 28 0d
                    call      $204e                         ;[1fe5] cd 4e 20
                    jr        z,$1fe5                       ;[1fe8] 28 fb
                    call      $1ffc                         ;[1fea] cd fc 1f
                    call      $204e                         ;[1fed] cd 4e 20
                    jr        z,$1fe5                       ;[1ff0] 28 f3
                    cp        $29                           ;[1ff2] fe 29
                    ret       z                             ;[1ff4] c8
                    call      $1fc3                         ;[1ff5] cd c3 1f
                    ld        a,$0d                         ;[1ff8] 3e 0d
                    rst       $10                           ;[1ffa] d7
                    ret                                     ;[1ffb] c9

                    rst       $18                           ;[1ffc] df
                    cp        $ac                           ;[1ffd] fe ac
                    jr        nz,$200e                      ;[1fff] 20 0d
                    call      $1c79                         ;[2001] cd 79 1c
                    call      $1fc3                         ;[2004] cd c3 1f
                    call      $2307                         ;[2007] cd 07 23
                    ld        a,$16                         ;[200a] 3e 16
                    jr        $201e                         ;[200c] 18 10
                    cp        $ad                           ;[200e] fe ad
                    jr        nz,$2024                      ;[2010] 20 12
                    rst       $20                           ;[2012] e7
                    call      $1c82                         ;[2013] cd 82 1c
                    call      $1fc3                         ;[2016] cd c3 1f
                    call      $1e99                         ;[2019] cd 99 1e
                    ld        a,$17                         ;[201c] 3e 17
                    rst       $10                           ;[201e] d7
                    ld        a,c                           ;[201f] 79
                    rst       $10                           ;[2020] d7
                    ld        a,b                           ;[2021] 78
                    rst       $10                           ;[2022] d7
                    ret                                     ;[2023] c9

                    call      $21f2                         ;[2024] cd f2 21
                    ret       nc                            ;[2027] d0
                    call      $2070                         ;[2028] cd 70 20
                    ret       nc                            ;[202b] d0
                    call      $24fb                         ;[202c] cd fb 24
                    call      $1fc3                         ;[202f] cd c3 1f
                    bit       6,(iy+$01)                    ;[2032] fd cb 01 76
                    call      z,$2bf1                       ;[2036] cc f1 2b
                    jp        nz,$2de3                      ;[2039] c2 e3 2d
                    ld        a,b                           ;[203c] 78
                    or        c                             ;[203d] b1
                    dec       bc                            ;[203e] 0b
                    ret       z                             ;[203f] c8
                    ld        a,(de)                        ;[2040] 1a
                    inc       de                            ;[2041] 13
                    rst       $10                           ;[2042] d7
                    jr        $203c                         ;[2043] 18 f7
                    cp        $29                           ;[2045] fe 29
                    ret       z                             ;[2047] c8
                    cp        $0d                           ;[2048] fe 0d
                    ret       z                             ;[204a] c8
                    cp        $3a                           ;[204b] fe 3a
                    ret                                     ;[204d] c9

                    rst       $18                           ;[204e] df
                    cp        $3b                           ;[204f] fe 3b
                    jr        z,$2067                       ;[2051] 28 14
                    cp        $2c                           ;[2053] fe 2c
                    jr        nz,$2061                      ;[2055] 20 0a
                    call      $2530                         ;[2057] cd 30 25
                    jr        z,$2067                       ;[205a] 28 0b
                    ld        a,$06                         ;[205c] 3e 06
                    rst       $10                           ;[205e] d7
                    jr        $2067                         ;[205f] 18 06
                    cp        $27                           ;[2061] fe 27
                    ret       nz                            ;[2063] c0
                    call      $1ff5                         ;[2064] cd f5 1f
                    rst       $20                           ;[2067] e7
                    call      $2045                         ;[2068] cd 45 20
                    jr        nz,$206e                      ;[206b] 20 01
                    pop       bc                            ;[206d] c1
                    cp        a                             ;[206e] bf
                    ret                                     ;[206f] c9

                    cp        $23                           ;[2070] fe 23
                    scf                                     ;[2072] 37
                    ret       nz                            ;[2073] c0
                    rst       $20                           ;[2074] e7
                    call      $1c82                         ;[2075] cd 82 1c
                    and       a                             ;[2078] a7
                    call      $1fc3                         ;[2079] cd c3 1f
                    call      $1e94                         ;[207c] cd 94 1e
                    cp        $10                           ;[207f] fe 10
                    jp        nc,$160e                      ;[2081] d2 0e 16
                    call      $1601                         ;[2084] cd 01 16
                    and       a                             ;[2087] a7
                    ret                                     ;[2088] c9

                    ld        hl,$3140                      ;[2089] 21 40 31
                    call      $2705                         ;[208c] cd 05 27
                    ld        d,a                           ;[208f] 57
                    jr        z,$210d                       ;[2090] 28 7b
                    inc       a                             ;[2092] 3c
                    jr        nz,$2097                      ;[2093] 20 02
                    ld        e,$64                         ;[2095] 1e 64
                    ld        a,e                           ;[2097] 7b
                    ld        hl,$0d34                      ;[2098] 21 34 0d
                    call      $32cd                         ;[209b] cd cd 32
                    ret                                     ;[209e] c9

                    rst       $38                           ;[209f] ff
                    ld        bc,($5c88)                    ;[20a0] ed 4b 88 5c
                    ld        a,($5c6b)                     ;[20a4] 3a 6b 5c
                    cp        b                             ;[20a7] b8
                    jr        c,$20ad                       ;[20a8] 38 03
                    ld        c,$21                         ;[20aa] 0e 21
                    ld        b,a                           ;[20ac] 47
                    ld        ($5c88),bc                    ;[20ad] ed 43 88 5c
                    ld        a,$19                         ;[20b1] 3e 19
                    sub       b                             ;[20b3] 90
                    ld        ($5c8c),a                     ;[20b4] 32 8c 5c
                    res       0,(iy+$02)                    ;[20b7] fd cb 02 86
                    call      $0dd9                         ;[20bb] cd d9 0d
                    jp        $0d6e                         ;[20be] c3 6e 0d
                    ld        ix,$34a5                      ;[20c1] dd 21 a5 34
                    jp        z,$17a1                       ;[20c5] ca a1 17
                    bit       6,(hl)                        ;[20c8] cb 76
                    jp        nz,$27cb                      ;[20ca] c2 cb 27
                    call      $1c8c                         ;[20cd] cd 8c 1c
                    cp        $2c                           ;[20d0] fe 2c
                    jr        nz,$20e1                      ;[20d2] 20 0d
                    call      $1c81                         ;[20d4] cd 81 1c
                    cp        $2c                           ;[20d7] fe 2c
                    jr        nz,$20e9                      ;[20d9] 20 0e
                    rst       $20                           ;[20db] e7
                    call      $1c8c                         ;[20dc] cd 8c 1c
                    jr        $20f5                         ;[20df] 18 14
                    call      $2530                         ;[20e1] cd 30 25
                    ld        a,$01                         ;[20e4] 3e 01
                    call      nz,$2d28                      ;[20e6] c4 28 2d
                    call      $2530                         ;[20e9] cd 30 25
                    ld        de,$1539                      ;[20ec] 11 39 15
                    ld        bc,$0001                      ;[20ef] 01 01 00
                    call      nz,$2ab2                      ;[20f2] c4 b2 2a
                    rst       $18                           ;[20f5] df
                    cp        $29                           ;[20f6] fe 29
                    jp        nz,$27cb                      ;[20f8] c2 cb 27
                    rst       $20                           ;[20fb] e7
                    call      $2530                         ;[20fc] cd 30 25
                    jp        z,$2181                       ;[20ff] ca 81 21
                    call      $2bf1                         ;[2102] cd f1 2b
                    ld        a,b                           ;[2105] 78
                    or        c                             ;[2106] b1
                    jr        z,$210a                       ;[2107] 28 01
                    ld        a,(de)                        ;[2109] 1a
                    push      af                            ;[210a] f5
                    call      $2da2                         ;[210b] cd a2 2d
                    pop       de                            ;[210e] d1
                    jp        c,$24f9                       ;[210f] da f9 24
                    ld        a,d                           ;[2112] 7a
                    ex        af,af'                        ;[2113] 08
                    ld        a,b                           ;[2114] 78
                    or        c                             ;[2115] b1
                    jp        z,$39f8                       ;[2116] ca f8 39
                    exx                                     ;[2119] d9
                    call      $2bf1                         ;[211a] cd f1 2b
                    ex        de,hl                         ;[211d] eb
                    ld        a,b                           ;[211e] 78
                    or        c                             ;[211f] b1
                    jr        z,$2116                       ;[2120] 28 f4
                    exx                                     ;[2122] d9
                    push      bc                            ;[2123] c5
                    call      $2bf1                         ;[2124] cd f1 2b
                    exx                                     ;[2127] d9
                    pop       de                            ;[2128] d1
                    push      de                            ;[2129] d5
                    exx                                     ;[212a] d9
                    pop       hl                            ;[212b] e1
                    dec       hl                            ;[212c] 2b
                    ld        a,c                           ;[212d] 79
                    sub       l                             ;[212e] 95
                    ld        c,a                           ;[212f] 4f
                    ld        a,b                           ;[2130] 78
                    sbc       h                             ;[2131] 9c
                    ld        b,a                           ;[2132] 47
                    ld        a,$00                         ;[2133] 3e 00
                    jp        c,$39f8                       ;[2135] da f8 39
                    add       hl,de                         ;[2138] 19
                    ex        af,af'                        ;[2139] 08
                    ld        d,a                           ;[213a] 57
                    ex        af,af'                        ;[213b] 08
                    ld        a,b                           ;[213c] 78
                    or        c                             ;[213d] b1
                    jr        z,$2116                       ;[213e] 28 d6
                    push      bc                            ;[2140] c5
                    push      hl                            ;[2141] e5
                    exx                                     ;[2142] d9
                    push      bc                            ;[2143] c5
                    push      hl                            ;[2144] e5
                    ld        a,b                           ;[2145] 78
                    or        c                             ;[2146] b1
                    jr        z,$2178                       ;[2147] 28 2f
                    ld        a,(hl)                        ;[2149] 7e
                    inc       hl                            ;[214a] 23
                    dec       bc                            ;[214b] 0b
                    exx                                     ;[214c] d9
                    cp        d                             ;[214d] ba
                    jr        z,$2153                       ;[214e] 28 03
                    cp        (hl)                          ;[2150] be
                    jr        nz,$215c                      ;[2151] 20 09
                    inc       hl                            ;[2153] 23
                    ld        a,b                           ;[2154] 78
                    or        c                             ;[2155] b1
                    jr        z,$215c                       ;[2156] 28 04
                    dec       bc                            ;[2158] 0b
                    exx                                     ;[2159] d9
                    jr        $2145                         ;[215a] 18 e9
                    exx                                     ;[215c] d9
                    pop       hl                            ;[215d] e1
                    pop       bc                            ;[215e] c1
                    inc       de                            ;[215f] 13
                    ex        af,af'                        ;[2160] 08
                    jr        z,$2169                       ;[2161] 28 06
                    dec       de                            ;[2163] 1b
                    dec       de                            ;[2164] 1b
                    ld        a,d                           ;[2165] 7a
                    or        e                             ;[2166] b3
                    jr        z,$217a                       ;[2167] 28 11
                    exx                                     ;[2169] d9
                    pop       hl                            ;[216a] e1
                    pop       bc                            ;[216b] c1
                    jr        nz,$2173                      ;[216c] 20 05
                    ex        af,af'                        ;[216e] 08
                    inc       hl                            ;[216f] 23
                    dec       bc                            ;[2170] 0b
                    jr        $213c                         ;[2171] 18 c9
                    ex        af,af'                        ;[2173] 08
                    dec       hl                            ;[2174] 2b
                    inc       bc                            ;[2175] 03
                    jr        $213c                         ;[2176] 18 c4
                    pop       hl                            ;[2178] e1
                    pop       hl                            ;[2179] e1
                    pop       hl                            ;[217a] e1
                    pop       hl                            ;[217b] e1
                    ld        b,d                           ;[217c] 42
                    ld        c,e                           ;[217d] 4b
                    call      $2d2f                         ;[217e] cd 2f 2d
                    jp        $26c3                         ;[2181] c3 c3 26
                    push      de                            ;[2184] d5
                    and       a                             ;[2185] a7
                    jr        $218a                         ;[2186] 18 02
                    push      de                            ;[2188] d5
                    scf                                     ;[2189] 37
                    pop       de                            ;[218a] d1
                    dec       d                             ;[218b] 15
                    push      af                            ;[218c] f5
                    push      de                            ;[218d] d5
                    call      $1e99                         ;[218e] cd 99 1e
                    push      bc                            ;[2191] c5
                    exx                                     ;[2192] d9
                    pop       hl                            ;[2193] e1
                    pop       de                            ;[2194] d1
                    pop       af                            ;[2195] f1
                    inc       d                             ;[2196] 14
                    ld        a,$00                         ;[2197] 3e 00
                    push      af                            ;[2199] f5
                    ex        af,af'                        ;[219a] 08
                    bit       7,(iy+$01)                    ;[219b] fd cb 01 7e
                    call      nz,$1f6b                      ;[219f] c4 6b 1f
                    pop       af                            ;[21a2] f1
                    ld        hl,$5c3b                      ;[21a3] 21 3b 5c
                    set       6,(hl)                        ;[21a6] cb f6
                    jr        c,$21b4                       ;[21a8] 38 0a
                    res       6,(hl)                        ;[21aa] cb b6
                    bit       7,(hl)                        ;[21ac] cb 7e
                    call      nz,$2ab2                      ;[21ae] c4 b2 2a
                    jp        $35bf                         ;[21b1] c3 bf 35
                    bit       7,(hl)                        ;[21b4] cb 7e
                    call      nz,$2d2f                      ;[21b6] c4 2f 2d
                    ret                                     ;[21b9] c9

                    set       7,b                           ;[21ba] cb f8
                    jr        $21c5                         ;[21bc] 18 07
                    ld        b,c                           ;[21be] 41
                    ld        a,c                           ;[21bf] 79
                    and       $e0                           ;[21c0] e6 e0
                    set       7,a                           ;[21c2] cb ff
                    ld        c,a                           ;[21c4] 4f
                    rst       $18                           ;[21c5] df
                    exx                                     ;[21c6] d9
                    push      hl                            ;[21c7] e5
                    exx                                     ;[21c8] d9
                    pop       hl                            ;[21c9] e1
                    cp        $28                           ;[21ca] fe 28
                    jp        z,$298f                       ;[21cc] ca 8f 29
                    set       5,b                           ;[21cf] cb e8
                    jp        $298f                         ;[21d1] c3 8f 29
                    rst       $38                           ;[21d4] ff
                    rst       $38                           ;[21d5] ff
                    ld        hl,($5c51)                    ;[21d6] 2a 51 5c
                    inc       hl                            ;[21d9] 23
                    inc       hl                            ;[21da] 23
                    inc       hl                            ;[21db] 23
                    inc       hl                            ;[21dc] 23
                    ld        a,(hl)                        ;[21dd] 7e
                    cp        $4b                           ;[21de] fe 4b
                    ret                                     ;[21e0] c9

                    rst       $20                           ;[21e1] e7
                    call      $21f2                         ;[21e2] cd f2 21
                    ret       c                             ;[21e5] d8
                    rst       $18                           ;[21e6] df
                    cp        $2c                           ;[21e7] fe 2c
                    jr        z,$21e1                       ;[21e9] 28 f6
                    cp        $3b                           ;[21eb] fe 3b
                    jr        z,$21e1                       ;[21ed] 28 f2
                    jp        $1c8a                         ;[21ef] c3 8a 1c
                    cp        $d9                           ;[21f2] fe d9
                    ret       c                             ;[21f4] d8
                    cp        $df                           ;[21f5] fe df
                    ccf                                     ;[21f7] 3f
                    ret       c                             ;[21f8] d8
                    push      af                            ;[21f9] f5
                    rst       $20                           ;[21fa] e7
                    pop       af                            ;[21fb] f1
                    sub       $c9                           ;[21fc] d6 c9
                    push      af                            ;[21fe] f5
                    call      $1c82                         ;[21ff] cd 82 1c
                    pop       af                            ;[2202] f1
                    and       a                             ;[2203] a7
                    call      $1fc3                         ;[2204] cd c3 1f
                    push      af                            ;[2207] f5
                    call      $1e94                         ;[2208] cd 94 1e
                    ld        d,a                           ;[220b] 57
                    pop       af                            ;[220c] f1
                    rst       $10                           ;[220d] d7
                    ld        a,d                           ;[220e] 7a
                    rst       $10                           ;[220f] d7
                    ret                                     ;[2210] c9

                    sub       $11                           ;[2211] d6 11
                    adc       $00                           ;[2213] ce 00
                    jr        z,$2234                       ;[2215] 28 1d
                    sub       $02                           ;[2217] d6 02
                    adc       $00                           ;[2219] ce 00
                    jr        z,$2273                       ;[221b] 28 56
                    cp        $01                           ;[221d] fe 01
                    ld        a,d                           ;[221f] 7a
                    ld        b,$01                         ;[2220] 06 01
                    jr        nz,$2228                      ;[2222] 20 04
                    rlca                                    ;[2224] 07
                    rlca                                    ;[2225] 07
                    ld        b,$04                         ;[2226] 06 04
                    ld        c,a                           ;[2228] 4f
                    ld        a,d                           ;[2229] 7a
                    cp        $02                           ;[222a] fe 02
                    jr        nc,$2244                      ;[222c] 30 16
                    ld        a,c                           ;[222e] 79
                    ld        hl,$5c91                      ;[222f] 21 91 5c
                    jr        $226c                         ;[2232] 18 38
                    ld        a,d                           ;[2234] 7a
                    ld        b,$07                         ;[2235] 06 07
                    jr        c,$223e                       ;[2237] 38 05
                    rlca                                    ;[2239] 07
                    rlca                                    ;[223a] 07
                    rlca                                    ;[223b] 07
                    ld        b,$38                         ;[223c] 06 38
                    ld        c,a                           ;[223e] 4f
                    ld        a,d                           ;[223f] 7a
                    cp        $0a                           ;[2240] fe 0a
                    jr        c,$2246                       ;[2242] 38 02
                    rst       $08                           ;[2244] cf
                    inc       de                            ;[2245] 13
                    ld        hl,$5c8f                      ;[2246] 21 8f 5c
                    cp        $08                           ;[2249] fe 08
                    jr        c,$2258                       ;[224b] 38 0b
                    ld        a,(hl)                        ;[224d] 7e
                    jr        z,$2257                       ;[224e] 28 07
                    or        b                             ;[2250] b0
                    cpl                                     ;[2251] 2f
                    and       $24                           ;[2252] e6 24
                    jr        z,$2257                       ;[2254] 28 01
                    ld        a,b                           ;[2256] 78
                    ld        c,a                           ;[2257] 4f
                    ld        a,c                           ;[2258] 79
                    call      $226c                         ;[2259] cd 6c 22
                    ld        a,$07                         ;[225c] 3e 07
                    cp        d                             ;[225e] ba
                    sbc       a                             ;[225f] 9f
                    call      $226c                         ;[2260] cd 6c 22
                    rlca                                    ;[2263] 07
                    rlca                                    ;[2264] 07
                    and       $50                           ;[2265] e6 50
                    ld        b,a                           ;[2267] 47
                    ld        a,$08                         ;[2268] 3e 08
                    cp        d                             ;[226a] ba
                    sbc       a                             ;[226b] 9f
                    xor       (hl)                          ;[226c] ae
                    and       b                             ;[226d] a0
                    xor       (hl)                          ;[226e] ae
                    ld        (hl),a                        ;[226f] 77
                    inc       hl                            ;[2270] 23
                    ld        a,b                           ;[2271] 78
                    ret                                     ;[2272] c9

                    sbc       a                             ;[2273] 9f
                    ld        a,d                           ;[2274] 7a
                    rrca                                    ;[2275] 0f
                    ld        b,$80                         ;[2276] 06 80
                    jr        nz,$227d                      ;[2278] 20 03
                    rrca                                    ;[227a] 0f
                    ld        b,$40                         ;[227b] 06 40
                    ld        c,a                           ;[227d] 4f
                    ld        a,d                           ;[227e] 7a
                    cp        $08                           ;[227f] fe 08
                    jr        z,$2287                       ;[2281] 28 04
                    cp        $02                           ;[2283] fe 02
                    jr        nc,$2244                      ;[2285] 30 bd
                    ld        a,c                           ;[2287] 79
                    ld        hl,$5c8f                      ;[2288] 21 8f 5c
                    call      $226c                         ;[228b] cd 6c 22
                    ld        a,c                           ;[228e] 79
                    rrca                                    ;[228f] 0f
                    rrca                                    ;[2290] 0f
                    rrca                                    ;[2291] 0f
                    jr        $226c                         ;[2292] 18 d8
                    call      $1e94                         ;[2294] cd 94 1e
                    cp        $08                           ;[2297] fe 08
                    jr        nc,$2244                      ;[2299] 30 a9
                    out       ($fe),a                       ;[229b] d3 fe
                    rlca                                    ;[229d] 07
                    rlca                                    ;[229e] 07
                    rlca                                    ;[229f] 07
                    bit       5,a                           ;[22a0] cb 6f
                    jr        nz,$22a6                      ;[22a2] 20 02
                    xor       $07                           ;[22a4] ee 07
                    ld        ($5c48),a                     ;[22a6] 32 48 5c
                    ret                                     ;[22a9] c9

                    ld        a,$af                         ;[22aa] 3e af
                    sub       b                             ;[22ac] 90
                    jp        c,$24f9                       ;[22ad] da f9 24
                    ld        b,a                           ;[22b0] 47
                    and       a                             ;[22b1] a7
                    rra                                     ;[22b2] 1f
                    scf                                     ;[22b3] 37
                    rra                                     ;[22b4] 1f
                    and       a                             ;[22b5] a7
                    rra                                     ;[22b6] 1f
                    xor       b                             ;[22b7] a8
                    and       $f8                           ;[22b8] e6 f8
                    xor       b                             ;[22ba] a8
                    ld        h,a                           ;[22bb] 67
                    ld        a,c                           ;[22bc] 79
                    rlca                                    ;[22bd] 07
                    rlca                                    ;[22be] 07
                    rlca                                    ;[22bf] 07
                    xor       b                             ;[22c0] a8
                    and       $c7                           ;[22c1] e6 c7
                    xor       b                             ;[22c3] a8
                    rlca                                    ;[22c4] 07
                    rlca                                    ;[22c5] 07
                    ld        l,a                           ;[22c6] 6f
                    ld        a,c                           ;[22c7] 79
                    and       $07                           ;[22c8] e6 07
                    ret                                     ;[22ca] c9

                    call      $2307                         ;[22cb] cd 07 23
                    call      $22aa                         ;[22ce] cd aa 22
                    ld        b,a                           ;[22d1] 47
                    inc       b                             ;[22d2] 04
                    ld        a,(hl)                        ;[22d3] 7e
                    rlca                                    ;[22d4] 07
                    djnz      $22d4                         ;[22d5] 10 fd
                    and       $01                           ;[22d7] e6 01
                    jp        $2d28                         ;[22d9] c3 28 2d
                    call      $2307                         ;[22dc] cd 07 23
                    call      $22e5                         ;[22df] cd e5 22
                    jp        $0d4d                         ;[22e2] c3 4d 0d
                    ld        ($5c7d),bc                    ;[22e5] ed 43 7d 5c
                    call      $22aa                         ;[22e9] cd aa 22
                    ld        b,a                           ;[22ec] 47
                    inc       b                             ;[22ed] 04
                    ld        a,$fe                         ;[22ee] 3e fe
                    rrca                                    ;[22f0] 0f
                    djnz      $22f0                         ;[22f1] 10 fd
                    ld        b,a                           ;[22f3] 47
                    ld        a,(hl)                        ;[22f4] 7e
                    ld        c,(iy+$57)                    ;[22f5] fd 4e 57
                    bit       0,c                           ;[22f8] cb 41
                    jr        nz,$22fd                      ;[22fa] 20 01
                    and       b                             ;[22fc] a0
                    bit       2,c                           ;[22fd] cb 51
                    jr        nz,$2303                      ;[22ff] 20 02
                    xor       b                             ;[2301] a8
                    cpl                                     ;[2302] 2f
                    ld        (hl),a                        ;[2303] 77
                    jp        $0bdb                         ;[2304] c3 db 0b
                    call      $2314                         ;[2307] cd 14 23
                    ld        b,a                           ;[230a] 47
                    push      bc                            ;[230b] c5
                    call      $2314                         ;[230c] cd 14 23
                    ld        e,c                           ;[230f] 59
                    pop       bc                            ;[2310] c1
                    ld        d,c                           ;[2311] 51
                    ld        c,a                           ;[2312] 4f
                    ret                                     ;[2313] c9

                    call      $2dd5                         ;[2314] cd d5 2d
                    jp        c,$24f9                       ;[2317] da f9 24
                    ld        c,$01                         ;[231a] 0e 01
                    ret       z                             ;[231c] c8
                    ld        c,$ff                         ;[231d] 0e ff
                    ret                                     ;[231f] c9

                    rst       $18                           ;[2320] df
                    cp        $2c                           ;[2321] fe 2c
                    jp        nz,$1c8a                      ;[2323] c2 8a 1c
                    rst       $20                           ;[2326] e7
                    call      $1c82                         ;[2327] cd 82 1c
                    call      $1bee                         ;[232a] cd ee 1b
                    rst       $28                           ;[232d] ef
                    ld        hl,($383d)                    ;[232e] 2a 3d 38
                    ld        a,(hl)                        ;[2331] 7e
                    cp        $81                           ;[2332] fe 81
                    jr        nc,$233b                      ;[2334] 30 05
                    rst       $28                           ;[2336] ef
                    ld        (bc),a                        ;[2337] 02
                    jr        c,$2352                       ;[2338] 38 18
                    and       c                             ;[233a] a1
                    rst       $28                           ;[233b] ef
                    and       e                             ;[233c] a3
                    jr        c,$2375                       ;[233d] 38 36
                    add       e                             ;[233f] 83
                    rst       $28                           ;[2340] ef
                    push      bc                            ;[2341] c5
                    ld        (bc),a                        ;[2342] 02
                    jr        c,$2312                       ;[2343] 38 cd
                    ld        a,l                           ;[2345] 7d
                    inc       h                             ;[2346] 24
                    push      bc                            ;[2347] c5
                    rst       $28                           ;[2348] ef
                    ld        sp,$04e1                      ;[2349] 31 e1 04
                    jr        c,$23cc                       ;[234c] 38 7e
                    cp        $80                           ;[234e] fe 80
                    jr        nc,$235a                      ;[2350] 30 08
                    rst       $28                           ;[2352] ef
                    ld        (bc),a                        ;[2353] 02
                    ld        (bc),a                        ;[2354] 02
                    jr        c,$2318                       ;[2355] 38 c1
                    jp        $22dc                         ;[2357] c3 dc 22
                    rst       $28                           ;[235a] ef
                    jp        nz,$c001                      ;[235b] c2 01 c0
                    ld        (bc),a                        ;[235e] 02
                    inc       bc                            ;[235f] 03
                    ld        bc,$0fe0                      ;[2360] 01 e0 0f
                    ret       nz                            ;[2363] c0
                    ld        bc,$e031                      ;[2364] 01 31 e0
                    ld        bc,$e031                      ;[2367] 01 31 e0
                    and       b                             ;[236a] a0
                    pop       bc                            ;[236b] c1
                    ld        (bc),a                        ;[236c] 02
                    jr        c,$236c                       ;[236d] 38 fd
                    inc       (hl)                          ;[236f] 34
                    ld        h,d                           ;[2370] 62
                    call      $1e94                         ;[2371] cd 94 1e
                    ld        l,a                           ;[2374] 6f
                    push      hl                            ;[2375] e5
                    call      $1e94                         ;[2376] cd 94 1e
                    pop       hl                            ;[2379] e1
                    ld        h,a                           ;[237a] 67
                    ld        ($5c7d),hl                    ;[237b] 22 7d 5c
                    pop       bc                            ;[237e] c1
                    jp        $2420                         ;[237f] c3 20 24
                    rst       $18                           ;[2382] df
                    cp        $2c                           ;[2383] fe 2c
                    jr        z,$238d                       ;[2385] 28 06
                    call      $1bee                         ;[2387] cd ee 1b
                    jp        $2477                         ;[238a] c3 77 24
                    rst       $20                           ;[238d] e7
                    call      $1c82                         ;[238e] cd 82 1c
                    call      $1bee                         ;[2391] cd ee 1b
                    rst       $28                           ;[2394] ef
                    push      bc                            ;[2395] c5
                    and       d                             ;[2396] a2
                    inc       b                             ;[2397] 04
                    rra                                     ;[2398] 1f
                    ld        sp,$3030                      ;[2399] 31 30 30
                    nop                                     ;[239c] 00
                    ld        b,$02                         ;[239d] 06 02
                    jr        c,$2364                       ;[239f] 38 c3
                    ld        (hl),a                        ;[23a1] 77
                    inc       h                             ;[23a2] 24
                    ret       nz                            ;[23a3] c0
                    ld        (bc),a                        ;[23a4] 02
                    pop       bc                            ;[23a5] c1
                    ld        (bc),a                        ;[23a6] 02
                    ld        sp,$e12a                      ;[23a7] 31 2a e1
                    ld        bc,$2ae1                      ;[23aa] 01 e1 2a
                    rrca                                    ;[23ad] 0f
                    ret       po                            ;[23ae] e0
                    dec       b                             ;[23af] 05
                    ld        hl,($01e0)                    ;[23b0] 2a e0 01
                    dec       a                             ;[23b3] 3d
                    jr        c,$2434                       ;[23b4] 38 7e
                    cp        $81                           ;[23b6] fe 81
                    jr        nc,$23c1                      ;[23b8] 30 07
                    rst       $28                           ;[23ba] ef
                    ld        (bc),a                        ;[23bb] 02
                    ld        (bc),a                        ;[23bc] 02
                    jr        c,$2382                       ;[23bd] 38 c3
                    ld        (hl),a                        ;[23bf] 77
                    inc       h                             ;[23c0] 24
                    call      $247d                         ;[23c1] cd 7d 24
                    push      bc                            ;[23c4] c5
                    rst       $28                           ;[23c5] ef
                    ld        (bc),a                        ;[23c6] 02
                    pop       hl                            ;[23c7] e1
                    ld        bc,$c105                      ;[23c8] 01 05 c1
                    ld        (bc),a                        ;[23cb] 02
                    ld        bc,$e131                      ;[23cc] 01 31 e1
                    inc       b                             ;[23cf] 04
                    jp        nz,$0102                      ;[23d0] c2 02 01
                    ld        sp,$04e1                      ;[23d3] 31 e1 04
                    jp        po,$e0e5                      ;[23d6] e2 e5 e0
                    inc       bc                            ;[23d9] 03
                    and       d                             ;[23da] a2
                    inc       b                             ;[23db] 04
                    ld        sp,$c51f                      ;[23dc] 31 1f c5
                    ld        (bc),a                        ;[23df] 02
                    jr        nz,$23a2                      ;[23e0] 20 c0
                    ld        (bc),a                        ;[23e2] 02
                    jp        nz,$c102                      ;[23e3] c2 02 c1
                    push      hl                            ;[23e6] e5
                    inc       b                             ;[23e7] 04
                    ret       po                            ;[23e8] e0
                    jp        po,$0f04                      ;[23e9] e2 04 0f
                    pop       hl                            ;[23ec] e1
                    ld        bc,$02c1                      ;[23ed] 01 c1 02
                    ret       po                            ;[23f0] e0
                    inc       b                             ;[23f1] 04
                    jp        po,$04e5                      ;[23f2] e2 e5 04
                    inc       bc                            ;[23f5] 03
                    jp        nz,$e12a                      ;[23f6] c2 2a e1
                    ld        hl,($020f)                    ;[23f9] 2a 0f 02
                    jr        c,$2418                       ;[23fc] 38 1a
                    cp        $81                           ;[23fe] fe 81
                    pop       bc                            ;[2400] c1
                    jp        c,$2477                       ;[2401] da 77 24
                    push      bc                            ;[2404] c5
                    rst       $28                           ;[2405] ef
                    ld        bc,$3a38                      ;[2406] 01 38 3a
                    ld        a,l                           ;[2409] 7d
                    ld        e,h                           ;[240a] 5c
                    call      $2d28                         ;[240b] cd 28 2d
                    rst       $28                           ;[240e] ef
                    ret       nz                            ;[240f] c0
                    rrca                                    ;[2410] 0f
                    ld        bc,$3a38                      ;[2411] 01 38 3a
                    ld        a,(hl)                        ;[2414] 7e
                    ld        e,h                           ;[2415] 5c
                    call      $2d28                         ;[2416] cd 28 2d
                    rst       $28                           ;[2419] ef
                    push      bc                            ;[241a] c5
                    rrca                                    ;[241b] 0f
                    ret       po                            ;[241c] e0
                    push      hl                            ;[241d] e5
                    jr        c,$23e1                       ;[241e] 38 c1
                    dec       b                             ;[2420] 05
                    jr        z,$245f                       ;[2421] 28 3c
                    jr        $2439                         ;[2423] 18 14
                    rst       $28                           ;[2425] ef
                    pop       hl                            ;[2426] e1
                    ld        sp,$04e3                      ;[2427] 31 e3 04
                    jp        po,$04e4                      ;[242a] e2 e4 04
                    inc       bc                            ;[242d] 03
                    pop       bc                            ;[242e] c1
                    ld        (bc),a                        ;[242f] 02
                    call      po,$e204                      ;[2430] e4 04 e2
                    ex        (sp),hl                       ;[2433] e3
                    inc       b                             ;[2434] 04
                    rrca                                    ;[2435] 0f
                    jp        nz,$3802                      ;[2436] c2 02 38
                    push      bc                            ;[2439] c5
                    rst       $28                           ;[243a] ef
                    ret       nz                            ;[243b] c0
                    ld        (bc),a                        ;[243c] 02
                    pop       hl                            ;[243d] e1
                    rrca                                    ;[243e] 0f
                    ld        sp,$3a38                      ;[243f] 31 38 3a
                    ld        a,l                           ;[2442] 7d
                    ld        e,h                           ;[2443] 5c
                    call      $2d28                         ;[2444] cd 28 2d
                    rst       $28                           ;[2447] ef
                    inc       bc                            ;[2448] 03
                    ret       po                            ;[2449] e0
                    jp        po,$c00f                      ;[244a] e2 0f c0
                    ld        bc,$38e0                      ;[244d] 01 e0 38
                    ld        a,($5c7e)                     ;[2450] 3a 7e 5c
                    call      $2d28                         ;[2453] cd 28 2d
                    rst       $28                           ;[2456] ef
                    inc       bc                            ;[2457] 03
                    jr        c,$2427                       ;[2458] 38 cd
                    or        a                             ;[245a] b7
                    inc       h                             ;[245b] 24
                    pop       bc                            ;[245c] c1
                    djnz      $2425                         ;[245d] 10 c6
                    rst       $28                           ;[245f] ef
                    ld        (bc),a                        ;[2460] 02
                    ld        (bc),a                        ;[2461] 02
                    ld        bc,$3a38                      ;[2462] 01 38 3a
                    ld        a,l                           ;[2465] 7d
                    ld        e,h                           ;[2466] 5c
                    call      $2d28                         ;[2467] cd 28 2d
                    rst       $28                           ;[246a] ef
                    inc       bc                            ;[246b] 03
                    ld        bc,$3a38                      ;[246c] 01 38 3a
                    ld        a,(hl)                        ;[246f] 7e
                    ld        e,h                           ;[2470] 5c
                    call      $2d28                         ;[2471] cd 28 2d
                    rst       $28                           ;[2474] ef
                    inc       bc                            ;[2475] 03
                    jr        c,$2445                       ;[2476] 38 cd
                    or        a                             ;[2478] b7
                    inc       h                             ;[2479] 24
                    jp        $0d4d                         ;[247a] c3 4d 0d
                    rst       $28                           ;[247d] ef
                    ld        sp,$3428                      ;[247e] 31 28 34
                    ld        ($0100),a                     ;[2481] 32 00 01
                    dec       b                             ;[2484] 05
                    push      hl                            ;[2485] e5
                    ld        bc,$2a05                      ;[2486] 01 05 2a
                    jr        c,$2458                       ;[2489] 38 cd
                    push      de                            ;[248b] d5
                    dec       l                             ;[248c] 2d
                    jr        c,$2495                       ;[248d] 38 06
                    and       $fc                           ;[248f] e6 fc
                    add       $04                           ;[2491] c6 04
                    jr        nc,$2497                      ;[2493] 30 02
                    ld        a,$fc                         ;[2495] 3e fc
                    push      af                            ;[2497] f5
                    call      $2d28                         ;[2498] cd 28 2d
                    rst       $28                           ;[249b] ef
                    push      hl                            ;[249c] e5
                    ld        bc,$3105                      ;[249d] 01 05 31
                    rra                                     ;[24a0] 1f
                    call      nz,$3102                      ;[24a1] c4 02 31
                    and       d                             ;[24a4] a2
                    inc       b                             ;[24a5] 04
                    rra                                     ;[24a6] 1f
                    pop       bc                            ;[24a7] c1
                    ld        bc,$02c0                      ;[24a8] 01 c0 02
                    ld        sp,$3104                      ;[24ab] 31 04 31
                    rrca                                    ;[24ae] 0f
                    and       c                             ;[24af] a1
                    inc       bc                            ;[24b0] 03
                    dec       de                            ;[24b1] 1b
                    jp        $3802                         ;[24b2] c3 02 38
                    pop       bc                            ;[24b5] c1
                    ret                                     ;[24b6] c9

                    call      $2307                         ;[24b7] cd 07 23
                    ld        a,c                           ;[24ba] 79
                    cp        b                             ;[24bb] b8
                    jr        nc,$24c4                      ;[24bc] 30 06
                    ld        l,c                           ;[24be] 69
                    push      de                            ;[24bf] d5
                    xor       a                             ;[24c0] af
                    ld        e,a                           ;[24c1] 5f
                    jr        $24cb                         ;[24c2] 18 07
                    or        c                             ;[24c4] b1
                    ret       z                             ;[24c5] c8
                    ld        l,b                           ;[24c6] 68
                    ld        b,c                           ;[24c7] 41
                    push      de                            ;[24c8] d5
                    ld        d,$00                         ;[24c9] 16 00
                    ld        h,b                           ;[24cb] 60
                    ld        a,b                           ;[24cc] 78
                    rra                                     ;[24cd] 1f
                    add       l                             ;[24ce] 85
                    jr        c,$24d4                       ;[24cf] 38 03
                    cp        h                             ;[24d1] bc
                    jr        c,$24db                       ;[24d2] 38 07
                    sub       h                             ;[24d4] 94
                    ld        c,a                           ;[24d5] 4f
                    exx                                     ;[24d6] d9
                    pop       bc                            ;[24d7] c1
                    push      bc                            ;[24d8] c5
                    jr        $24df                         ;[24d9] 18 04
                    ld        c,a                           ;[24db] 4f
                    push      de                            ;[24dc] d5
                    exx                                     ;[24dd] d9
                    pop       bc                            ;[24de] c1
                    ld        hl,($5c7d)                    ;[24df] 2a 7d 5c
                    ld        a,b                           ;[24e2] 78
                    add       h                             ;[24e3] 84
                    ld        b,a                           ;[24e4] 47
                    ld        a,c                           ;[24e5] 79
                    inc       a                             ;[24e6] 3c
                    add       l                             ;[24e7] 85
                    jr        c,$24f7                       ;[24e8] 38 0d
                    jr        z,$24f9                       ;[24ea] 28 0d
                    dec       a                             ;[24ec] 3d
                    ld        c,a                           ;[24ed] 4f
                    call      $22e5                         ;[24ee] cd e5 22
                    exx                                     ;[24f1] d9
                    ld        a,c                           ;[24f2] 79
                    djnz      $24ce                         ;[24f3] 10 d9
                    pop       de                            ;[24f5] d1
                    ret                                     ;[24f6] c9

                    jr        z,$24ec                       ;[24f7] 28 f3
                    rst       $08                           ;[24f9] cf
                    ld        a,(bc)                        ;[24fa] 0a
                    rst       $18                           ;[24fb] df
                    cp        $25                           ;[24fc] fe 25
                    jp        z,$0605                       ;[24fe] ca 05 06
                    ld        b,$00                         ;[2501] 06 00
                    push      bc                            ;[2503] c5
                    ex        de,hl                         ;[2504] eb
                    ld        h,$06                         ;[2505] 26 06
                    ld        l,a                           ;[2507] 6f
                    add       hl,a                          ;[2508] ed 31
                    ld        c,(hl)                        ;[250a] 4e
                    inc       hl                            ;[250b] 23
                    ld        h,(hl)                        ;[250c] 66
                    ld        l,c                           ;[250d] 69
                    jp        (hl)                          ;[250e] e9
                    call      $0074                         ;[250f] cd 74 00
                    inc       bc                            ;[2512] 03
                    cp        $0d                           ;[2513] fe 0d
                    jp        z,$1c8a                       ;[2515] ca 8a 1c
                    cp        $22                           ;[2518] fe 22
                    jr        nz,$250f                      ;[251a] 20 f3
                    call      $0074                         ;[251c] cd 74 00
                    cp        $22                           ;[251f] fe 22
                    ret                                     ;[2521] c9

                    rst       $20                           ;[2522] e7
                    cp        $28                           ;[2523] fe 28
                    jr        nz,$252d                      ;[2525] 20 06
                    call      $1c79                         ;[2527] cd 79 1c
                    rst       $18                           ;[252a] df
                    cp        $29                           ;[252b] fe 29
                    jp        nz,$1c8a                      ;[252d] c2 8a 1c
                    bit       7,(iy+$01)                    ;[2530] fd cb 01 7e
                    ret                                     ;[2534] c9

                    call      $2307                         ;[2535] cd 07 23
                    ld        hl,($5c36)                    ;[2538] 2a 36 5c
                    add       hl,$0100                      ;[253b] ed 34 00 01
                    ld        a,c                           ;[253f] 79
                    rrca                                    ;[2540] 0f
                    rrca                                    ;[2541] 0f
                    rrca                                    ;[2542] 0f
                    and       $e0                           ;[2543] e6 e0
                    xor       b                             ;[2545] a8
                    ld        e,a                           ;[2546] 5f
                    ld        a,c                           ;[2547] 79
                    and       $18                           ;[2548] e6 18
                    xor       $40                           ;[254a] ee 40
                    ld        d,a                           ;[254c] 57
                    ld        b,$60                         ;[254d] 06 60
                    push      bc                            ;[254f] c5
                    push      de                            ;[2550] d5
                    push      hl                            ;[2551] e5
                    ld        a,(de)                        ;[2552] 1a
                    xor       (hl)                          ;[2553] ae
                    jr        z,$255a                       ;[2554] 28 04
                    inc       a                             ;[2556] 3c
                    jr        nz,$2572                      ;[2557] 20 19
                    dec       a                             ;[2559] 3d
                    ld        c,a                           ;[255a] 4f
                    ld        b,$07                         ;[255b] 06 07
                    inc       d                             ;[255d] 14
                    inc       hl                            ;[255e] 23
                    ld        a,(de)                        ;[255f] 1a
                    xor       (hl)                          ;[2560] ae
                    xor       c                             ;[2561] a9
                    jr        nz,$2572                      ;[2562] 20 0e
                    djnz      $255d                         ;[2564] 10 f7
                    pop       bc                            ;[2566] c1
                    pop       bc                            ;[2567] c1
                    pop       bc                            ;[2568] c1
                    ld        a,$80                         ;[2569] 3e 80
                    sub       b                             ;[256b] 90
                    ld        bc,$0001                      ;[256c] 01 01 00
                    rst       $30                           ;[256f] f7
                    ld        (de),a                        ;[2570] 12
                    ret                                     ;[2571] c9

                    pop       hl                            ;[2572] e1
                    add       hl,$0008                      ;[2573] ed 34 08 00
                    pop       de                            ;[2577] d1
                    pop       bc                            ;[2578] c1
                    djnz      $254f                         ;[2579] 10 d4
                    ld        c,b                           ;[257b] 48
                    ret                                     ;[257c] c9

                    rst       $38                           ;[257d] ff
                    rst       $38                           ;[257e] ff
                    rst       $38                           ;[257f] ff
                    call      $2307                         ;[2580] cd 07 23
                    ld        a,c                           ;[2583] 79
                    rrca                                    ;[2584] 0f
                    rrca                                    ;[2585] 0f
                    rrca                                    ;[2586] 0f
                    ld        c,a                           ;[2587] 4f
                    and       $e0                           ;[2588] e6 e0
                    xor       b                             ;[258a] a8
                    ld        l,a                           ;[258b] 6f
                    ld        a,c                           ;[258c] 79
                    and       $03                           ;[258d] e6 03
                    xor       $58                           ;[258f] ee 58
                    ld        h,a                           ;[2591] 67
                    ld        a,(hl)                        ;[2592] 7e
                    jp        $2d28                         ;[2593] c3 28 2d
                    rst       $38                           ;[2596] ff
                    rst       $38                           ;[2597] ff
                    rst       $38                           ;[2598] ff
                    rst       $38                           ;[2599] ff
                    rst       $38                           ;[259a] ff
                    rst       $38                           ;[259b] ff
                    rst       $38                           ;[259c] ff
                    rst       $38                           ;[259d] ff
                    rst       $38                           ;[259e] ff
                    rst       $38                           ;[259f] ff
                    rst       $38                           ;[25a0] ff
                    rst       $38                           ;[25a1] ff
                    rst       $38                           ;[25a2] ff
                    rst       $38                           ;[25a3] ff
                    rst       $38                           ;[25a4] ff
                    pop       bc                            ;[25a5] c1
                    ld        a,c                           ;[25a6] 79
                    cp        $c6                           ;[25a7] fe c6
                    jp        nz,$1c8a                      ;[25a9] c2 8a 1c
                    ld        c,$ff                         ;[25ac] 0e ff
                    push      bc                            ;[25ae] c5
                    rst       $20                           ;[25af] e7
                    jp        $2504                         ;[25b0] c3 04 25
                    rst       $18                           ;[25b3] df
                    inc       hl                            ;[25b4] 23
                    push      hl                            ;[25b5] e5
                    ld        bc,$0000                      ;[25b6] 01 00 00
                    call      $250f                         ;[25b9] cd 0f 25
                    jr        nz,$25d9                      ;[25bc] 20 1b
                    call      $250f                         ;[25be] cd 0f 25
                    jr        z,$25be                       ;[25c1] 28 fb
                    call      $2530                         ;[25c3] cd 30 25
                    jr        z,$25d9                       ;[25c6] 28 11
                    rst       $30                           ;[25c8] f7
                    pop       hl                            ;[25c9] e1
                    push      de                            ;[25ca] d5
                    ld        a,(hl)                        ;[25cb] 7e
                    inc       hl                            ;[25cc] 23
                    ld        (de),a                        ;[25cd] 12
                    inc       de                            ;[25ce] 13
                    cp        $22                           ;[25cf] fe 22
                    jr        nz,$25cb                      ;[25d1] 20 f8
                    ld        a,(hl)                        ;[25d3] 7e
                    inc       hl                            ;[25d4] 23
                    cp        $22                           ;[25d5] fe 22
                    jr        z,$25cb                       ;[25d7] 28 f2
                    dec       bc                            ;[25d9] 0b
                    pop       de                            ;[25da] d1
                    ld        hl,$5c3b                      ;[25db] 21 3b 5c
                    res       6,(hl)                        ;[25de] cb b6
                    bit       7,(hl)                        ;[25e0] cb 7e
                    call      nz,$2ab2                      ;[25e2] c4 b2 2a
                    jp        $2712                         ;[25e5] c3 12 27
                    rst       $20                           ;[25e8] e7
                    call      $24fb                         ;[25e9] cd fb 24
                    cp        $29                           ;[25ec] fe 29
                    jp        nz,$1c8a                      ;[25ee] c2 8a 1c
                    rst       $20                           ;[25f1] e7
                    jp        $2712                         ;[25f2] c3 12 27
                    jp        $0898                         ;[25f5] c3 98 08
                    rst       $20                           ;[25f8] e7
                    cp        $28                           ;[25f9] fe 28
                    jr        nz,$261a                      ;[25fb] 20 1d
                    call      $1c81                         ;[25fd] cd 81 1c
                    cp        $29                           ;[2600] fe 29
                    jr        nz,$25ee                      ;[2602] 20 ea
                    call      $2530                         ;[2604] cd 30 25
                    jr        z,$264d                       ;[2607] 28 44
                    call      $1e99                         ;[2609] cd 99 1e
                    ld        d,b                           ;[260c] 50
                    ld        e,c                           ;[260d] 59
                    call      $2705                         ;[260e] cd 05 27
                    inc       hl                            ;[2611] 23
                    ld        hl,($4b42)                    ;[2612] 2a 42 4b
                    call      $2d2f                         ;[2615] cd 2f 2d
                    jr        $264d                         ;[2618] 18 33
                    call      $2530                         ;[261a] cd 30 25
                    jr        z,$2642                       ;[261d] 28 23
                    ld        de,$ffff                      ;[261f] 11 ff ff
                    call      $2705                         ;[2622] cd 05 27
                    dec       l                             ;[2625] 2d
                    ld        hl,($762a)                    ;[2626] 2a 2a 76
                    ld        e,h                           ;[2629] 5c
                    ld        a,$80                         ;[262a] 3e 80
                    bit       7,d                           ;[262c] cb 7a
                    jr        nz,$2638                      ;[262e] 20 08
                    add       hl,hl                         ;[2630] 29
                    ex        de,hl                         ;[2631] eb
                    adc       hl,hl                         ;[2632] ed 6a
                    ex        de,hl                         ;[2634] eb
                    dec       a                             ;[2635] 3d
                    jr        $262c                         ;[2636] 18 f4
                    res       7,d                           ;[2638] cb ba
                    ld        b,e                           ;[263a] 43
                    ld        e,d                           ;[263b] 5a
                    ld        d,b                           ;[263c] 50
                    ld        c,h                           ;[263d] 4c
                    ld        b,l                           ;[263e] 45
                    call      $2ab6                         ;[263f] cd b6 2a
                    jr        $26c3                         ;[2642] 18 7f
                    call      $2530                         ;[2644] cd 30 25
                    jr        z,$264d                       ;[2647] 28 04
                    rst       $28                           ;[2649] ef
                    and       e                             ;[264a] a3
                    jr        c,$2681                       ;[264b] 38 34
                    rst       $20                           ;[264d] e7
                    jp        $26c3                         ;[264e] c3 c3 26
                    ld        bc,$105a                      ;[2651] 01 5a 10
                    rst       $20                           ;[2654] e7
                    cp        $23                           ;[2655] fe 23
                    jp        z,$082f                       ;[2657] ca 2f 08
                    ld        hl,$5c3b                      ;[265a] 21 3b 5c
                    res       6,(hl)                        ;[265d] cb b6
                    bit       7,(hl)                        ;[265f] cb 7e
                    jr        z,$2682                       ;[2661] 28 1f
                    call      $028e                         ;[2663] cd 8e 02
                    ld        c,$00                         ;[2666] 0e 00
                    jr        nz,$267d                      ;[2668] 20 13
                    call      $031e                         ;[266a] cd 1e 03
                    jr        nc,$267d                      ;[266d] 30 0e
                    dec       d                             ;[266f] 15
                    ld        e,a                           ;[2670] 5f
                    call      $0333                         ;[2671] cd 33 03
                    push      af                            ;[2674] f5
                    ld        bc,$0001                      ;[2675] 01 01 00
                    rst       $30                           ;[2678] f7
                    pop       af                            ;[2679] f1
                    ld        (de),a                        ;[267a] 12
                    ld        c,$01                         ;[267b] 0e 01
                    ld        b,$00                         ;[267d] 06 00
                    call      $2ab2                         ;[267f] cd b2 2a
                    jp        $2712                         ;[2682] c3 12 27
                    rst       $38                           ;[2685] ff
                    rst       $38                           ;[2686] ff
                    rst       $38                           ;[2687] ff
                    rst       $38                           ;[2688] ff
                    rst       $38                           ;[2689] ff
                    rst       $38                           ;[268a] ff
                    rst       $38                           ;[268b] ff
                    rst       $38                           ;[268c] ff
                    call      $2530                         ;[268d] cd 30 25
                    jr        nz,$26b5                      ;[2690] 20 23
                    call      $2ccc                         ;[2692] cd cc 2c
                    rst       $18                           ;[2695] df
                    ld        bc,$0006                      ;[2696] 01 06 00
                    call      $1655                         ;[2699] cd 55 16
                    inc       hl                            ;[269c] 23
                    ld        (hl),$0e                      ;[269d] 36 0e
                    inc       hl                            ;[269f] 23
                    ex        de,hl                         ;[26a0] eb
                    ld        hl,($5c65)                    ;[26a1] 2a 65 5c
                    ld        c,$05                         ;[26a4] 0e 05
                    and       a                             ;[26a6] a7
                    sbc       hl,bc                         ;[26a7] ed 42
                    ld        ($5c65),hl                    ;[26a9] 22 65 5c
                    ldir                                    ;[26ac] ed b0
                    ex        de,hl                         ;[26ae] eb
                    dec       hl                            ;[26af] 2b
                    call      $155e                         ;[26b0] cd 5e 15
                    jr        $26c3                         ;[26b3] 18 0e
                    rst       $18                           ;[26b5] df
                    inc       hl                            ;[26b6] 23
                    ld        a,(hl)                        ;[26b7] 7e
                    cp        $0e                           ;[26b8] fe 0e
                    jr        nz,$26b6                      ;[26ba] 20 fa
                    inc       hl                            ;[26bc] 23
                    call      $33b4                         ;[26bd] cd b4 33
                    ld        ($5c5d),hl                    ;[26c0] 22 5d 5c
                    set       6,(iy+$01)                    ;[26c3] fd cb 01 f6
                    jr        $26de                         ;[26c7] 18 15
                    ex        de,hl                         ;[26c9] eb
                    call      $28c1                         ;[26ca] cd c1 28
                    jp        c,$1c2e                       ;[26cd] da 2e 1c
                    call      z,$2996                       ;[26d0] cc 96 29
                    ld        a,($5c3b)                     ;[26d3] 3a 3b 5c
                    cp        $c0                           ;[26d6] fe c0
                    jr        c,$26de                       ;[26d8] 38 04
                    inc       hl                            ;[26da] 23
                    call      $33b4                         ;[26db] cd b4 33
                    jr        $2712                         ;[26de] 18 32
                    nop                                     ;[26e0] 00
                    nop                                     ;[26e1] 00
                    ld        ($5b54),bc                    ;[26e2] ed 43 54 5b
                    ex        (sp),hl                       ;[26e6] e3
                    ld        c,(hl)                        ;[26e7] 4e
                    inc       hl                            ;[26e8] 23
                    ld        b,(hl)                        ;[26e9] 46
                    inc       hl                            ;[26ea] 23
                    ex        (sp),hl                       ;[26eb] e3
                    push    $26f3                           ;[26ec] ed 8a 26 f3
                    push      bc                            ;[26f0] c5
                    push      af                            ;[26f1] f5
                    ld        a,($5b56)                     ;[26f2] 3a 56 5b
                    jr        $26fb                         ;[26f5] 18 04
                    ld        ($5b54),bc                    ;[26f7] ed 43 54 5b
                    pop       bc                            ;[26fb] c1
                    ld        c,$e3                         ;[26fc] 0e e3
                    out       (c),b                         ;[26fe] ed 41
                    ld        bc,($5b54)                    ;[2700] ed 4b 54 5b
                    ret                                     ;[2704] c9

                    ld        ($5b56),a                     ;[2705] 32 56 5b
                    ld        a,$87                         ;[2708] 3e 87
                    jp        $26e2                         ;[270a] c3 e2 26
                    rst       $38                           ;[270d] ff
                    rst       $38                           ;[270e] ff
                    rst       $38                           ;[270f] ff
                    rst       $38                           ;[2710] ff
                    rst       $38                           ;[2711] ff
                    rst       $18                           ;[2712] df
                    cp        $28                           ;[2713] fe 28
                    jr        nz,$2723                      ;[2715] 20 0c
                    bit       6,(iy+$01)                    ;[2717] fd cb 01 76
                    jr        nz,$2737                      ;[271b] 20 1a
                    call      $2a52                         ;[271d] cd 52 2a
                    rst       $20                           ;[2720] e7
                    jr        $2713                         ;[2721] 18 f0
                    ld        hl,$1a99                      ;[2723] 21 99 1a
                    add       hl,a                          ;[2726] ed 31
                    ld        c,(hl)                        ;[2728] 4e
                    ld        hl,$1fc9                      ;[2729] 21 c9 1f
                    ld        a,c                           ;[272c] 79
                    sub       $3b                           ;[272d] d6 3b
                    jp        c,$11b7                       ;[272f] da b7 11
                    and       $3f                           ;[2732] e6 3f
                    add       hl,a                          ;[2734] ed 31
                    ld        b,(hl)                        ;[2736] 46
                    pop       de                            ;[2737] d1
                    ld        a,d                           ;[2738] 7a
                    cp        b                             ;[2739] b8
                    jr        c,$2773                       ;[273a] 38 37
                    and       a                             ;[273c] a7
                    jp        z,$1555                       ;[273d] ca 55 15
                    push      bc                            ;[2740] c5
                    ld        hl,$5c3b                      ;[2741] 21 3b 5c
                    ld        a,e                           ;[2744] 7b
                    cp        $ed                           ;[2745] fe ed
                    jr        nz,$274f                      ;[2747] 20 06
                    bit       6,(hl)                        ;[2749] cb 76
                    jr        nz,$274f                      ;[274b] 20 02
                    ld        e,$99                         ;[274d] 1e 99
                    push      de                            ;[274f] d5
                    bit       7,(hl)                        ;[2750] cb 7e
                    jr        z,$27b9                       ;[2752] 28 65
                    ld        a,e                           ;[2754] 7b
                    and       $3f                           ;[2755] e6 3f
                    jp        z,$3886                       ;[2757] ca 86 38
                    call      $279d                         ;[275a] cd 9d 27
                    ld        ($5c65),de                    ;[275d] ed 53 65 5c
                    pop       de                            ;[2761] d1
                    ld        hl,$5c3b                      ;[2762] 21 3b 5c
                    set       6,(hl)                        ;[2765] cb f6
                    bit       7,e                           ;[2767] cb 7b
                    jr        nz,$276d                      ;[2769] 20 02
                    res       6,(hl)                        ;[276b] cb b6
                    pop       bc                            ;[276d] c1
                    jr        $2737                         ;[276e] 18 c7
                    rst       $38                           ;[2770] ff
                    rst       $38                           ;[2771] ff
                    rst       $38                           ;[2772] ff
                    push      de                            ;[2773] d5
                    ld        a,c                           ;[2774] 79
                    bit       6,(iy+$01)                    ;[2775] fd cb 01 76
                    jr        nz,$2790                      ;[2779] 20 15
                    and       $3f                           ;[277b] e6 3f
                    add       $08                           ;[277d] c6 08
                    ld        c,a                           ;[277f] 4f
                    cp        $10                           ;[2780] fe 10
                    jr        nz,$2788                      ;[2782] 20 04
                    set       6,c                           ;[2784] cb f1
                    jr        $2790                         ;[2786] 18 08
                    jr        c,$2795                       ;[2788] 38 0b
                    cp        $17                           ;[278a] fe 17
                    jr        z,$2790                       ;[278c] 28 02
                    set       7,c                           ;[278e] cb f9
                    push      bc                            ;[2790] c5
                    rst       $20                           ;[2791] e7
                    jp        $2504                         ;[2792] c3 04 25
                    cp        $0c                           ;[2795] fe 0c
                    ld        c,$7b                         ;[2797] 0e 7b
                    jr        z,$2790                       ;[2799] 28 f5
                    jr        $27cb                         ;[279b] 18 2e
                    ld        hl,$32d7                      ;[279d] 21 d7 32
                    add       hl,a                          ;[27a0] ed 31
                    add       hl,a                          ;[27a2] ed 31
                    ld        c,(hl)                        ;[27a4] 4e
                    inc       hl                            ;[27a5] 23
                    ld        b,(hl)                        ;[27a6] 46
                    push      bc                            ;[27a7] c5
                    ld        b,a                           ;[27a8] 47
                    ld        de,$fffb                      ;[27a9] 11 fb ff
                    ld        hl,($5c65)                    ;[27ac] 2a 65 5c
                    sub       $18                           ;[27af] d6 18
                    cp        $23                           ;[27b1] fe 23
                    jr        c,$27b6                       ;[27b3] 38 01
                    add       hl,de                         ;[27b5] 19
                    ex        de,hl                         ;[27b6] eb
                    add       hl,de                         ;[27b7] 19
                    ret                                     ;[27b8] c9

                    pop       bc                            ;[27b9] c1
                    ld        a,c                           ;[27ba] 79
                    and       $3f                           ;[27bb] e6 3f
                    jr        nz,$27c2                      ;[27bd] 20 03
                    pop       hl                            ;[27bf] e1
                    pop       af                            ;[27c0] f1
                    push      hl                            ;[27c1] e5
                    push      bc                            ;[27c2] c5
                    ld        a,e                           ;[27c3] 7b
                    xor       (iy+$01)                      ;[27c4] fd ae 01
                    and       $40                           ;[27c7] e6 40
                    jr        z,$2761                       ;[27c9] 28 96
                    jp        $1c8a                         ;[27cb] c3 8a 1c
                    ld        b,$00                         ;[27ce] 06 00
                    push      bc                            ;[27d0] c5
                    jp        $2740                         ;[27d1] c3 40 27
                    rst       $18                           ;[27d4] df
                    push      hl                            ;[27d5] e5
                    rst       $20                           ;[27d6] e7
                    cp        $28                           ;[27d7] fe 28
                    jr        nz,$27f3                      ;[27d9] 20 18
                    pop       hl                            ;[27db] e1
                    rst       $20                           ;[27dc] e7
                    call      $24fb                         ;[27dd] cd fb 24
                    cp        $29                           ;[27e0] fe 29
                    jr        z,$27ea                       ;[27e2] 28 06
                    cp        $2c                           ;[27e4] fe 2c
                    jp        nz,$1c8a                      ;[27e6] c2 8a 1c
                    inc       a                             ;[27e9] 3c
                    push      af                            ;[27ea] f5
                    rst       $20                           ;[27eb] e7
                    pop       af                            ;[27ec] f1
                    ld        a,(hl)                        ;[27ed] 7e
                    ld        hl,$5c3b                      ;[27ee] 21 3b 5c
                    scf                                     ;[27f1] 37
                    ret                                     ;[27f2] c9

                    pop       hl                            ;[27f3] e1
                    jp        $155f                         ;[27f4] c3 5f 15
                    rst       $28                           ;[27f7] ef
                    ld        ($3802),a                     ;[27f8] 32 02 38
                    ret                                     ;[27fb] c9

                    push      hl                            ;[27fc] e5
                    call      $2bf1                         ;[27fd] cd f1 2b
                    pop       hl                            ;[2800] e1
                    push      bc                            ;[2801] c5
                    push      de                            ;[2802] d5
                    dec       hl                            ;[2803] 2b
                    dec       hl                            ;[2804] 2b
                    push      hl                            ;[2805] e5
                    xor       a                             ;[2806] af
                    dec       hl                            ;[2807] 2b
                    dec       a                             ;[2808] 3d
                    bit       5,(hl)                        ;[2809] cb 6e
                    jr        nz,$2807                      ;[280b] 20 fa
                    cp        $ff                           ;[280d] fe ff
                    jr        z,$2813                       ;[280f] 28 02
                    dec       hl                            ;[2811] 2b
                    dec       a                             ;[2812] 3d
                    dec       hl                            ;[2813] 2b
                    ld        d,(hl)                        ;[2814] 56
                    dec       hl                            ;[2815] 2b
                    ld        e,(hl)                        ;[2816] 5e
                    push      hl                            ;[2817] e5
                    exx                                     ;[2818] d9
                    pop       hl                            ;[2819] e1
                    exx                                     ;[281a] d9
                    dec       de                            ;[281b] 1b
                    dec       de                            ;[281c] 1b
                    ex        de,hl                         ;[281d] eb
                    push      bc                            ;[281e] c5
                    ld        c,a                           ;[281f] 4f
                    ld        b,$ff                         ;[2820] 06 ff
                    add       hl,bc                         ;[2822] 09
                    pop       bc                            ;[2823] c1
                    and       a                             ;[2824] a7
                    sbc       hl,bc                         ;[2825] ed 42
                    jr        nc,$285a                      ;[2827] 30 31
                    ld        a,h                           ;[2829] 7c
                    cpl                                     ;[282a] 2f
                    ld        b,a                           ;[282b] 47
                    ld        a,l                           ;[282c] 7d
                    cpl                                     ;[282d] 2f
                    ld        c,a                           ;[282e] 4f
                    inc       bc                            ;[282f] 03
                    pop       hl                            ;[2830] e1
                    pop       de                            ;[2831] d1
                    push      hl                            ;[2832] e5
                    and       a                             ;[2833] a7
                    sbc       hl,de                         ;[2834] ed 52
                    jr        c,$2845                       ;[2836] 38 0d
                    ld        hl,($5c65)                    ;[2838] 2a 65 5c
                    and       a                             ;[283b] a7
                    sbc       hl,de                         ;[283c] ed 52
                    jr        nc,$2845                      ;[283e] 30 05
                    and       a                             ;[2840] a7
                    ex        de,hl                         ;[2841] eb
                    sbc       hl,bc                         ;[2842] ed 42
                    ex        de,hl                         ;[2844] eb
                    pop       hl                            ;[2845] e1
                    push      de                            ;[2846] d5
                    push      bc                            ;[2847] c5
                    call      $3ad4                         ;[2848] cd d4 3a
                    exx                                     ;[284b] d9
                    pop       bc                            ;[284c] c1
                    sbc       hl,bc                         ;[284d] ed 42
                    ld        e,(hl)                        ;[284f] 5e
                    inc       hl                            ;[2850] 23
                    ld        d,(hl)                        ;[2851] 56
                    ex        de,hl                         ;[2852] eb
                    add       hl,bc                         ;[2853] 09
                    ex        de,hl                         ;[2854] eb
                    ld        (hl),d                        ;[2855] 72
                    dec       hl                            ;[2856] 2b
                    ld        (hl),e                        ;[2857] 73
                    exx                                     ;[2858] d9
                    push      de                            ;[2859] d5
                    pop       hl                            ;[285a] e1
                    pop       de                            ;[285b] d1
                    pop       bc                            ;[285c] c1
                    ld        (hl),c                        ;[285d] 71
                    inc       hl                            ;[285e] 23
                    ld        (hl),b                        ;[285f] 70
                    ld        a,ixh                         ;[2860] dd 7c
                    add       a                             ;[2862] 87
                    ret       c                             ;[2863] d8
                    inc       hl                            ;[2864] 23
                    ex        de,hl                         ;[2865] eb
                    ld        a,b                           ;[2866] 78
                    or        c                             ;[2867] b1
                    ret       z                             ;[2868] c8
                    ldir                                    ;[2869] ed b0
                    ret                                     ;[286b] c9

                    set       2,(iy+$30)                    ;[286c] fd cb 30 d6
                    set       6,(iy+$01)                    ;[2870] fd cb 01 f6
                    ld        bc,$279f                      ;[2874] 01 9f 27
                    ld        a,$87                         ;[2877] 3e 87
                    call      $26ec                         ;[2879] cd ec 26
                    ld        c,$60                         ;[287c] 0e 60
                    ld        a,h                           ;[287e] 7c
                    cp        $32                           ;[287f] fe 32
                    jr        z,$2885                       ;[2881] 28 02
                    ld        c,$00                         ;[2883] 0e 00
                    xor       a                             ;[2885] af
                    inc       a                             ;[2886] 3c
                    ret                                     ;[2887] c9

                    and       $60                           ;[2888] e6 60
                    cp        $20                           ;[288a] fe 20
                    jp        z,$2923                       ;[288c] ca 23 29
                    call      $2a3d                         ;[288f] cd 3d 2a
                    jp        c,$28ff                       ;[2892] da ff 28
                    djnz      $28af                         ;[2895] 10 18
                    push      hl                            ;[2897] e5
                    ld        a,c                           ;[2898] 79
                    exx                                     ;[2899] d9
                    pop       hl                            ;[289a] e1
                    ex        af,af'                        ;[289b] 08
                    inc       hl                            ;[289c] 23
                    ld        a,(hl)                        ;[289d] 7e
                    inc       hl                            ;[289e] 23
                    and       a                             ;[289f] a7
                    exx                                     ;[28a0] d9
                    ld        b,$80                         ;[28a1] 06 80
                    ld        c,a                           ;[28a3] 4f
                    jr        nz,$28ff                      ;[28a4] 20 59
                    exx                                     ;[28a6] d9
                    ex        af,af'                        ;[28a7] 08
                    ld        c,a                           ;[28a8] 4f
                    inc       hl                            ;[28a9] 23
                    inc       hl                            ;[28aa] 23
                    set       0,(iy+$01)                    ;[28ab] fd cb 01 c6
                    jp        $2983                         ;[28af] c3 83 29
                    rst       $18                           ;[28b2] df
                    cp        $25                           ;[28b3] fe 25
                    jr        z,$286c                       ;[28b5] 28 b5
                    res       2,(iy+$30)                    ;[28b7] fd cb 30 96
                    call      $2c8d                         ;[28bb] cd 8d 2c
                    jp        nc,$1c8a                      ;[28be] d2 8a 1c
                    set       6,(iy+$01)                    ;[28c1] fd cb 01 f6
                    push      hl                            ;[28c5] e5
                    exx                                     ;[28c6] d9
                    pop       hl                            ;[28c7] e1
                    and       $1f                           ;[28c8] e6 1f
                    ld        c,a                           ;[28ca] 4f
                    ld        b,$00                         ;[28cb] 06 00
                    rst       $20                           ;[28cd] e7
                    call      $2c88                         ;[28ce] cd 88 2c
                    inc       b                             ;[28d1] 04
                    jr        c,$28cd                       ;[28d2] 38 f9
                    cp        $28                           ;[28d4] fe 28
                    jr        z,$28ec                       ;[28d6] 28 14
                    set       6,c                           ;[28d8] cb f1
                    cp        $24                           ;[28da] fe 24
                    jr        z,$28e7                       ;[28dc] 28 09
                    set       5,c                           ;[28de] cb e9
                    dec       b                             ;[28e0] 05
                    jr        z,$28ec                       ;[28e1] 28 09
                    res       6,c                           ;[28e3] cb b1
                    jr        $28ec                         ;[28e5] 18 05
                    rst       $20                           ;[28e7] e7
                    res       6,(iy+$01)                    ;[28e8] fd cb 01 b6
                    bit       7,(iy+$01)                    ;[28ec] fd cb 01 7e
                    jr        z,$295f                       ;[28f0] 28 6d
                    ld        a,($5c0c)                     ;[28f2] 3a 0c 5c
                    and       a                             ;[28f5] a7
                    jr        nz,$2962                      ;[28f6] 20 6a
                    ld        hl,($5b58)                    ;[28f8] 2a 58 5b
                    ld        d,a                           ;[28fb] 57
                    ld        e,a                           ;[28fc] 5f
                    res       7,b                           ;[28fd] cb b8
                    inc       hl                            ;[28ff] 23
                    add       hl,de                         ;[2900] 19
                    ld        a,(hl)                        ;[2901] 7e
                    cp        $02                           ;[2902] fe 02
                    jr        nc,$2934                      ;[2904] 30 2e
                    inc       hl                            ;[2906] 23
                    ld        e,(hl)                        ;[2907] 5e
                    inc       hl                            ;[2908] 23
                    ld        d,(hl)                        ;[2909] 56
                    inc       hl                            ;[290a] 23
                    bit       7,b                           ;[290b] cb 78
                    jr        nz,$28ff                      ;[290d] 20 f0
                    ld        b,a                           ;[290f] 47
                    ld        a,(hl)                        ;[2910] 7e
                    and       $7f                           ;[2911] e6 7f
                    cp        c                             ;[2913] b9
                    jp        z,$2888                       ;[2914] ca 88 28
                    inc       a                             ;[2917] 3c
                    jp        p,$28ff                       ;[2918] f2 ff 28
                    inc       hl                            ;[291b] 23
                    dec       de                            ;[291c] 1b
                    ld        a,(hl)                        ;[291d] 7e
                    and       $7f                           ;[291e] e6 7f
                    cp        c                             ;[2920] b9
                    jr        nz,$28ff                      ;[2921] 20 dc
                    push      hl                            ;[2923] e5
                    push      de                            ;[2924] d5
                    call      $1a75                         ;[2925] cd 75 1a
                    pop       de                            ;[2928] d1
                    jr        nc,$292e                      ;[2929] 30 03
                    pop       hl                            ;[292b] e1
                    jr        $28ff                         ;[292c] 18 d1
                    djnz      $2987                         ;[292e] 10 57
                    ex        (sp),hl                       ;[2930] e3
                    jp        $2898                         ;[2931] c3 98 28
                    ld        de,$000a                      ;[2934] 11 0a 00
                    cp        $22                           ;[2937] fe 22
                    jr        c,$28ff                       ;[2939] 38 c4
                    cp        $3e                           ;[293b] fe 3e
                    jr        c,$28fd                       ;[293d] 38 be
                    ld        e,a                           ;[293f] 5f
                    res       6,e                           ;[2940] cb b3
                    jr        nz,$2900                      ;[2942] 20 bc
                    ld        hl,($5c4b)                    ;[2944] 2a 4b 5c
                    ld        a,(hl)                        ;[2947] 7e
                    and       $7f                           ;[2948] e6 7f
                    jr        z,$297f                       ;[294a] 28 33
                    cp        c                             ;[294c] b9
                    jr        z,$2966                       ;[294d] 28 17
                    inc       a                             ;[294f] 3c
                    jp        p,$297a                       ;[2950] f2 7a 29
                    inc       hl                            ;[2953] 23
                    ld        a,(hl)                        ;[2954] 7e
                    dec       hl                            ;[2955] 2b
                    and       $7f                           ;[2956] e6 7f
                    cp        c                             ;[2958] b9
                    jr        nz,$297a                      ;[2959] 20 1f
                    push      hl                            ;[295b] e5
                    inc       hl                            ;[295c] 23
                    jr        $2974                         ;[295d] 18 15
                    jp        $21be                         ;[295f] c3 be 21
                    nextreg $8c,$80                         ;[2962] ed 91 8c 80
                    and       $60                           ;[2966] e6 60
                    cp        $20                           ;[2968] fe 20
                    jr        z,$2973                       ;[296a] 28 07
                    call      $2a3d                         ;[296c] cd 3d 2a
                    jr        c,$297a                       ;[296f] 38 09
                    jr        $2983                         ;[2971] 18 10
                    push      hl                            ;[2973] e5
                    call      $1a75                         ;[2974] cd 75 1a
                    jr        nc,$2987                      ;[2977] 30 0e
                    pop       hl                            ;[2979] e1
                    call      $1a48                         ;[297a] cd 48 1a
                    jr        $2947                         ;[297d] 18 c8
                    ld        b,c                           ;[297f] 41
                    jp        $21ba                         ;[2980] c3 ba 21
                    ld        d,h                           ;[2983] 54
                    ld        e,l                           ;[2984] 5d
                    jr        $298e                         ;[2985] 18 07
                    pop       de                            ;[2987] d1
                    ld        a,(de)                        ;[2988] 1a
                    cp        $7f                           ;[2989] fe 7f
                    jr        nz,$298e                      ;[298b] 20 01
                    inc       de                            ;[298d] 13
                    ld        b,c                           ;[298e] 41
                    rl        b                             ;[298f] cb 10
                    bit       6,b                           ;[2991] cb 70
                    ld        a,(de)                        ;[2993] 1a
                    ld        b,a                           ;[2994] 47
                    ret                                     ;[2995] c9

                    xor       a                             ;[2996] af
                    nop                                     ;[2997] 00
                    bit       7,c                           ;[2998] cb 79
                    jr        nz,$29e9                      ;[299a] 20 4d
                    bit       7,b                           ;[299c] cb 78
                    jr        nz,$29ae                      ;[299e] 20 0e
                    inc       a                             ;[29a0] 3c
                    inc       hl                            ;[29a1] 23
                    ld        c,(hl)                        ;[29a2] 4e
                    inc       hl                            ;[29a3] 23
                    ld        b,(hl)                        ;[29a4] 46
                    inc       hl                            ;[29a5] 23
                    ex        de,hl                         ;[29a6] eb
                    call      $2ab2                         ;[29a7] cd b2 2a
                    rst       $18                           ;[29aa] df
                    jp        $1685                         ;[29ab] c3 85 16
                    inc       hl                            ;[29ae] 23
                    inc       hl                            ;[29af] 23
                    inc       hl                            ;[29b0] 23
                    ld        b,(hl)                        ;[29b1] 46
                    bit       6,c                           ;[29b2] cb 71
                    jr        z,$29c0                       ;[29b4] 28 0a
                    dec       b                             ;[29b6] 05
                    jr        z,$29a1                       ;[29b7] 28 e8
                    ex        de,hl                         ;[29b9] eb
                    rst       $18                           ;[29ba] df
                    cp        $28                           ;[29bb] fe 28
                    jr        nz,$2a25                      ;[29bd] 20 66
                    ex        de,hl                         ;[29bf] eb
                    ex        de,hl                         ;[29c0] eb
                    jr        $29ea                         ;[29c1] 18 27
                    push      hl                            ;[29c3] e5
                    rst       $18                           ;[29c4] df
                    pop       hl                            ;[29c5] e1
                    cp        $2c                           ;[29c6] fe 2c
                    jr        z,$29ed                       ;[29c8] 28 23
                    bit       7,c                           ;[29ca] cb 79
                    jr        z,$2a25                       ;[29cc] 28 57
                    bit       6,c                           ;[29ce] cb 71
                    jr        nz,$29d8                      ;[29d0] 20 06
                    cp        $29                           ;[29d2] fe 29
                    jr        nz,$2a16                      ;[29d4] 20 40
                    rst       $20                           ;[29d6] e7
                    ret                                     ;[29d7] c9

                    cp        $29                           ;[29d8] fe 29
                    jp        z,$1684                       ;[29da] ca 84 16
                    cp        $cc                           ;[29dd] fe cc
                    jr        nz,$2a16                      ;[29df] 20 35
                    rst       $18                           ;[29e1] df
                    dec       hl                            ;[29e2] 2b
                    ld        ($5c5d),hl                    ;[29e3] 22 5d 5c
                    jp        $1681                         ;[29e6] c3 81 16
                    ld        b,a                           ;[29e9] 47
                    ld        hl,$0000                      ;[29ea] 21 00 00
                    push      hl                            ;[29ed] e5
                    rst       $20                           ;[29ee] e7
                    pop       hl                            ;[29ef] e1
                    ld        a,c                           ;[29f0] 79
                    cp        $c0                           ;[29f1] fe c0
                    jr        nz,$29ff                      ;[29f3] 20 0a
                    rst       $18                           ;[29f5] df
                    cp        $29                           ;[29f6] fe 29
                    jp        z,$1684                       ;[29f8] ca 84 16
                    cp        $cc                           ;[29fb] fe cc
                    jr        z,$29e1                       ;[29fd] 28 e2
                    push      bc                            ;[29ff] c5
                    push      hl                            ;[2a00] e5
                    call      $2aee                         ;[2a01] cd ee 2a
                    ex        (sp),hl                       ;[2a04] e3
                    ex        de,hl                         ;[2a05] eb
                    call      $2acc                         ;[2a06] cd cc 2a
                    jr        c,$2a25                       ;[2a09] 38 1a
                    dec       bc                            ;[2a0b] 0b
                    call      $159f                         ;[2a0c] cd 9f 15
                    add       hl,bc                         ;[2a0f] 09
                    pop       de                            ;[2a10] d1
                    pop       bc                            ;[2a11] c1
                    djnz      $29c3                         ;[2a12] 10 af
                    bit       7,c                           ;[2a14] cb 79
                    jr        nz,$2a7a                      ;[2a16] 20 62
                    push      hl                            ;[2a18] e5
                    bit       6,c                           ;[2a19] cb 71
                    jp        nz,$1667                      ;[2a1b] c2 67 16
                    ld        b,d                           ;[2a1e] 42
                    ld        c,e                           ;[2a1f] 4b
                    rst       $18                           ;[2a20] df
                    cp        $29                           ;[2a21] fe 29
                    jr        z,$2a27                       ;[2a23] 28 02
                    rst       $08                           ;[2a25] cf
                    ld        (bc),a                        ;[2a26] 02
                    rst       $20                           ;[2a27] e7
                    pop       hl                            ;[2a28] e1
                    ld        de,$0005                      ;[2a29] 11 05 00
                    call      $159f                         ;[2a2c] cd 9f 15
                    add       hl,bc                         ;[2a2f] 09
                    ret                                     ;[2a30] c9

                    ld        bc,$09db                      ;[2a31] 01 db 09
                    jp        $082f                         ;[2a34] c3 2f 08
                    ld        bc,$1018                      ;[2a37] 01 18 10
                    jp        $082f                         ;[2a3a] c3 2f 08
                    exx                                     ;[2a3d] d9
                    push      hl                            ;[2a3e] e5
                    inc       hl                            ;[2a3f] 23
                    ld        a,(hl)                        ;[2a40] 7e
                    cp        $20                           ;[2a41] fe 20
                    jr        z,$2a3f                       ;[2a43] 28 fa
                    pop       hl                            ;[2a45] e1
                    exx                                     ;[2a46] d9
                    jp        $2c88                         ;[2a47] c3 88 2c
                    rst       $38                           ;[2a4a] ff
                    rst       $38                           ;[2a4b] ff
                    rst       $38                           ;[2a4c] ff
                    rst       $38                           ;[2a4d] ff
                    rst       $38                           ;[2a4e] ff
                    rst       $38                           ;[2a4f] ff
                    rst       $38                           ;[2a50] ff
                    rst       $38                           ;[2a51] ff
                    call      $2530                         ;[2a52] cd 30 25
                    call      nz,$2bf1                      ;[2a55] c4 f1 2b
                    rst       $20                           ;[2a58] e7
                    cp        $29                           ;[2a59] fe 29
                    jr        z,$2aad                       ;[2a5b] 28 50
                    push      de                            ;[2a5d] d5
                    xor       a                             ;[2a5e] af
                    push      af                            ;[2a5f] f5
                    push      bc                            ;[2a60] c5
                    ld        de,$0001                      ;[2a61] 11 01 00
                    rst       $18                           ;[2a64] df
                    pop       hl                            ;[2a65] e1
                    cp        $cc                           ;[2a66] fe cc
                    jr        z,$2a81                       ;[2a68] 28 17
                    pop       af                            ;[2a6a] f1
                    call      $2acd                         ;[2a6b] cd cd 2a
                    push      af                            ;[2a6e] f5
                    ld        d,b                           ;[2a6f] 50
                    ld        e,c                           ;[2a70] 59
                    push      hl                            ;[2a71] e5
                    rst       $18                           ;[2a72] df
                    pop       hl                            ;[2a73] e1
                    cp        $cc                           ;[2a74] fe cc
                    jr        z,$2a81                       ;[2a76] 28 09
                    cp        $29                           ;[2a78] fe 29
                    jp        nz,$1c8a                      ;[2a7a] c2 8a 1c
                    ld        h,d                           ;[2a7d] 62
                    ld        l,e                           ;[2a7e] 6b
                    jr        $2a94                         ;[2a7f] 18 13
                    push      hl                            ;[2a81] e5
                    rst       $20                           ;[2a82] e7
                    pop       hl                            ;[2a83] e1
                    cp        $29                           ;[2a84] fe 29
                    jr        z,$2a94                       ;[2a86] 28 0c
                    pop       af                            ;[2a88] f1
                    call      $2acd                         ;[2a89] cd cd 2a
                    push      af                            ;[2a8c] f5
                    rst       $18                           ;[2a8d] df
                    ld        h,b                           ;[2a8e] 60
                    ld        l,c                           ;[2a8f] 69
                    cp        $29                           ;[2a90] fe 29
                    jr        nz,$2a7a                      ;[2a92] 20 e6
                    pop       af                            ;[2a94] f1
                    ex        (sp),hl                       ;[2a95] e3
                    add       hl,de                         ;[2a96] 19
                    dec       hl                            ;[2a97] 2b
                    ex        (sp),hl                       ;[2a98] e3
                    and       a                             ;[2a99] a7
                    sbc       hl,de                         ;[2a9a] ed 52
                    ld        bc,$0000                      ;[2a9c] 01 00 00
                    jr        c,$2aa8                       ;[2a9f] 38 07
                    inc       hl                            ;[2aa1] 23
                    and       a                             ;[2aa2] a7
                    jp        m,$2a25                       ;[2aa3] fa 25 2a
                    ld        b,h                           ;[2aa6] 44
                    ld        c,l                           ;[2aa7] 4d
                    pop       de                            ;[2aa8] d1
                    res       6,(iy+$01)                    ;[2aa9] fd cb 01 b6
                    call      $2530                         ;[2aad] cd 30 25
                    ret       z                             ;[2ab0] c8
                    xor       a                             ;[2ab1] af
                    res       6,(iy+$01)                    ;[2ab2] fd cb 01 b6
                    push      bc                            ;[2ab6] c5
                    call      $3c1b                         ;[2ab7] cd 1b 3c
                    pop       bc                            ;[2aba] c1
                    ld        hl,($5c65)                    ;[2abb] 2a 65 5c
                    ld        (hl),a                        ;[2abe] 77
                    inc       hl                            ;[2abf] 23
                    ld        (hl),e                        ;[2ac0] 73
                    inc       hl                            ;[2ac1] 23
                    ld        (hl),d                        ;[2ac2] 72
                    inc       hl                            ;[2ac3] 23
                    ld        (hl),c                        ;[2ac4] 71
                    inc       hl                            ;[2ac5] 23
                    ld        (hl),b                        ;[2ac6] 70
                    inc       hl                            ;[2ac7] 23
                    ld        ($5c65),hl                    ;[2ac8] 22 65 5c
                    ret                                     ;[2acb] c9

                    xor       a                             ;[2acc] af
                    push      de                            ;[2acd] d5
                    push      hl                            ;[2ace] e5
                    push      af                            ;[2acf] f5
                    call      $1c82                         ;[2ad0] cd 82 1c
                    pop       af                            ;[2ad3] f1
                    call      $2530                         ;[2ad4] cd 30 25
                    jr        z,$2aeb                       ;[2ad7] 28 12
                    push      af                            ;[2ad9] f5
                    call      $1e99                         ;[2ada] cd 99 1e
                    pop       de                            ;[2add] d1
                    ld        a,b                           ;[2ade] 78
                    or        c                             ;[2adf] b1
                    scf                                     ;[2ae0] 37
                    jr        z,$2ae8                       ;[2ae1] 28 05
                    pop       hl                            ;[2ae3] e1
                    push      hl                            ;[2ae4] e5
                    and       a                             ;[2ae5] a7
                    sbc       hl,bc                         ;[2ae6] ed 42
                    ld        a,d                           ;[2ae8] 7a
                    sbc       $00                           ;[2ae9] de 00
                    pop       hl                            ;[2aeb] e1
                    pop       de                            ;[2aec] d1
                    ret                                     ;[2aed] c9

                    ex        de,hl                         ;[2aee] eb
                    inc       hl                            ;[2aef] 23
                    ld        e,(hl)                        ;[2af0] 5e
                    inc       hl                            ;[2af1] 23
                    ld        d,(hl)                        ;[2af2] 56
                    ret                                     ;[2af3] c9

                    call      $2530                         ;[2af4] cd 30 25
                    ret       z                             ;[2af7] c8
                    call      $30a9                         ;[2af8] cd a9 30
                    jp        c,$1f15                       ;[2afb] da 15 1f
                    ret                                     ;[2afe] c9

                    ld        hl,($5c4d)                    ;[2aff] 2a 4d 5c
                    bit       2,(iy+$30)                    ;[2b02] fd cb 30 56
                    jp        nz,$060d                      ;[2b06] c2 0d 06
                    ld        ixh,$40                       ;[2b09] dd 26 40
                    bit       1,(iy+$37)                    ;[2b0c] fd cb 37 4e
                    jr        z,$2b4e                       ;[2b10] 28 3c
                    call      $1bd4                         ;[2b12] cd d4 1b
                    cp        $24                           ;[2b15] fe 24
                    jp        z,$2bb5                       ;[2b17] ca b5 2b
                    ld        a,$05                         ;[2b1a] 3e 05
                    sub       b                             ;[2b1c] 90
                    ld        c,a                           ;[2b1d] 4f
                    ld        b,$00                         ;[2b1e] 06 00
                    ld        hl,($5c59)                    ;[2b20] 2a 59 5c
                    dec       hl                            ;[2b23] 2b
                    call      $1655                         ;[2b24] cd 55 16
                    inc       hl                            ;[2b27] 23
                    ex        de,hl                         ;[2b28] eb
                    ld        hl,($5c4d)                    ;[2b29] 2a 4d 5c
                    sub       $06                           ;[2b2c] d6 06
                    ld        b,a                           ;[2b2e] 47
                    ld        a,(hl)                        ;[2b2f] 7e
                    set       5,a                           ;[2b30] cb ef
                    ld        (de),a                        ;[2b32] 12
                    jr        z,$2b3c                       ;[2b33] 28 07
                    xor       $c0                           ;[2b35] ee c0
                    ld        c,a                           ;[2b37] 4f
                    call      $1bbf                         ;[2b38] cd bf 1b
                    ld        a,c                           ;[2b3b] 79
                    inc       de                            ;[2b3c] 13
                    ld        bc,$0005                      ;[2b3d] 01 05 00
                    ld        hl,($5c65)                    ;[2b40] 2a 65 5c
                    and       a                             ;[2b43] a7
                    sbc       hl,bc                         ;[2b44] ed 42
                    ld        ($5c65),hl                    ;[2b46] 22 65 5c
                    push      de                            ;[2b49] d5
                    ldir                                    ;[2b4a] ed b0
                    pop       hl                            ;[2b4c] e1
                    ret                                     ;[2b4d] c9

                    bit       6,(iy+$01)                    ;[2b4e] fd cb 01 76
                    ex        de,hl                         ;[2b52] eb
                    ld        a,(de)                        ;[2b53] 1a
                    jr        nz,$2b3c                      ;[2b54] 20 e6
                    ex        de,hl                         ;[2b56] eb
                    ld        bc,($5c72)                    ;[2b57] ed 4b 72 5c
                    bit       0,(iy+$37)                    ;[2b5b] fd cb 37 46
                    jr        nz,$2b82                      ;[2b5f] 20 21
                    ld        a,b                           ;[2b61] 78
                    or        c                             ;[2b62] b1
                    ret       z                             ;[2b63] c8
                    push      hl                            ;[2b64] e5
                    push      bc                            ;[2b65] c5
                    call      $2bf1                         ;[2b66] cd f1 2b
                    pop       hl                            ;[2b69] e1
                    sbc       hl,bc                         ;[2b6a] ed 42
                    jp        c,$0067                       ;[2b6c] da 67 00
                    ex        (sp),hl                       ;[2b6f] e3
                    ex        de,hl                         ;[2b70] eb
                    ld        a,b                           ;[2b71] 78
                    or        c                             ;[2b72] b1
                    jr        z,$2b77                       ;[2b73] 28 02
                    ldir                                    ;[2b75] ed b0
                    pop       bc                            ;[2b77] c1
                    ex        de,hl                         ;[2b78] eb
                    ld        a,b                           ;[2b79] 78
                    or        c                             ;[2b7a] b1
                    ret       z                             ;[2b7b] c8
                    ld        (hl),$20                      ;[2b7c] 36 20
                    inc       hl                            ;[2b7e] 23
                    dec       bc                            ;[2b7f] 0b
                    jr        $2b79                         ;[2b80] 18 f7
                    ex        de,hl                         ;[2b82] eb
                    ld        hl,($5c65)                    ;[2b83] 2a 65 5c
                    sbc       hl,de                         ;[2b86] ed 52
                    ex        de,hl                         ;[2b88] eb
                    jp        c,$27fc                       ;[2b89] da fc 27
                    dec       hl                            ;[2b8c] 2b
                    inc       bc                            ;[2b8d] 03
                    dec       hl                            ;[2b8e] 2b
                    inc       bc                            ;[2b8f] 03
                    xor       a                             ;[2b90] af
                    dec       a                             ;[2b91] 3d
                    dec       hl                            ;[2b92] 2b
                    inc       bc                            ;[2b93] 03
                    res       7,(hl)                        ;[2b94] cb be
                    bit       5,(hl)                        ;[2b96] cb 6e
                    jr        nz,$2b91                      ;[2b98] 20 f7
                    set       6,(hl)                        ;[2b9a] cb f6
                    push      hl                            ;[2b9c] e5
                    push      bc                            ;[2b9d] c5
                    push      af                            ;[2b9e] f5
                    ld        b,a                           ;[2b9f] 47
                    call      $2bb8                         ;[2ba0] cd b8 2b
                    pop       af                            ;[2ba3] f1
                    pop       bc                            ;[2ba4] c1
                    ex        (sp),hl                       ;[2ba5] e3
                    inc       a                             ;[2ba6] 3c
                    jr        z,$2bab                       ;[2ba7] 28 02
                    dec       hl                            ;[2ba9] 2b
                    inc       bc                            ;[2baa] 03
                    push      bc                            ;[2bab] c5
                    call      $19e8                         ;[2bac] cd e8 19
                    pop       bc                            ;[2baf] c1
                    pop       hl                            ;[2bb0] e1
                    and       a                             ;[2bb1] a7
                    sbc       hl,bc                         ;[2bb2] ed 42
                    ret                                     ;[2bb4] c9

                    ld        hl,($5c4d)                    ;[2bb5] 2a 4d 5c
                    ld        ($5c5b),hl                    ;[2bb8] 22 5b 5c
                    push      bc                            ;[2bbb] c5
                    call      $2bf1                         ;[2bbc] cd f1 2b
                    pop       af                            ;[2bbf] f1
                    neg                                     ;[2bc0] ed 44
                    ld        ixl,a                         ;[2bc2] dd 6f
                    dec       a                             ;[2bc4] 3d
                    jr        z,$2bc8                       ;[2bc5] 28 01
                    inc       a                             ;[2bc7] 3c
                    push      bc                            ;[2bc8] c5
                    ex        de,hl                         ;[2bc9] eb
                    add       hl,bc                         ;[2bca] 09
                    dec       hl                            ;[2bcb] 2b
                    ld        ($5c4d),hl                    ;[2bcc] 22 4d 5c
                    add       bc,a                          ;[2bcf] ed 33
                    inc       bc                            ;[2bd1] 03
                    inc       bc                            ;[2bd2] 03
                    inc       bc                            ;[2bd3] 03
                    ld        hl,($5c59)                    ;[2bd4] 2a 59 5c
                    dec       hl                            ;[2bd7] 2b
                    call      $1655                         ;[2bd8] cd 55 16
                    pop       bc                            ;[2bdb] c1
                    push      bc                            ;[2bdc] c5
                    inc       hl                            ;[2bdd] 23
                    push      hl                            ;[2bde] e5
                    ld        a,ixh                         ;[2bdf] dd 7c
                    add       a                             ;[2be1] 87
                    jp        $1b99                         ;[2be2] c3 99 1b
                    rst       $38                           ;[2be5] ff
                    rst       $38                           ;[2be6] ff
                    rst       $38                           ;[2be7] ff
                    rst       $38                           ;[2be8] ff
                    rst       $38                           ;[2be9] ff
                    rst       $38                           ;[2bea] ff
                    rst       $38                           ;[2beb] ff
                    rst       $38                           ;[2bec] ff
                    rst       $38                           ;[2bed] ff
                    rst       $38                           ;[2bee] ff
                    rst       $38                           ;[2bef] ff
                    rst       $38                           ;[2bf0] ff
                    ld        hl,($5c65)                    ;[2bf1] 2a 65 5c
                    dec       hl                            ;[2bf4] 2b
                    ld        b,(hl)                        ;[2bf5] 46
                    dec       hl                            ;[2bf6] 2b
                    ld        c,(hl)                        ;[2bf7] 4e
                    dec       hl                            ;[2bf8] 2b
                    ld        d,(hl)                        ;[2bf9] 56
                    dec       hl                            ;[2bfa] 2b
                    ld        e,(hl)                        ;[2bfb] 5e
                    dec       hl                            ;[2bfc] 2b
                    ld        a,(hl)                        ;[2bfd] 7e
                    ld        ($5c65),hl                    ;[2bfe] 22 65 5c
                    ret                                     ;[2c01] c9

                    call      $28b7                         ;[2c02] cd b7 28
                    jp        nz,$1c8a                      ;[2c05] c2 8a 1c
                    ld        ($5c4d),hl                    ;[2c08] 22 4d 5c
                    ld        ($5c5b),de                    ;[2c0b] ed 53 5b 5c
                    bit       7,c                           ;[2c0f] cb 79
                    jr        z,$2c1b                       ;[2c11] 28 08
                    res       6,c                           ;[2c13] cb b1
                    call      $2996                         ;[2c15] cd 96 29
                    call      $1bee                         ;[2c18] cd ee 1b
                    push      af                            ;[2c1b] f5
                    pop       hl                            ;[2c1c] e1
                    ld        ($5b52),hl                    ;[2c1d] 22 52 5b
                    set       7,c                           ;[2c20] cb f9
                    ld        ixh,c                         ;[2c22] dd 61
                    ld        b,$00                         ;[2c24] 06 00
                    push      bc                            ;[2c26] c5
                    ld        de,$0001                      ;[2c27] 11 01 00
                    bit       6,c                           ;[2c2a] cb 71
                    jr        nz,$2c30                      ;[2c2c] 20 02
                    ld        e,$05                         ;[2c2e] 1e 05
                    rst       $20                           ;[2c30] e7
                    ld        h,$ff                         ;[2c31] 26 ff
                    call      $2acc                         ;[2c33] cd cc 2a
                    jp        c,$2a25                       ;[2c36] da 25 2a
                    pop       hl                            ;[2c39] e1
                    push      bc                            ;[2c3a] c5
                    inc       h                             ;[2c3b] 24
                    push      hl                            ;[2c3c] e5
                    ld        h,b                           ;[2c3d] 60
                    ld        l,c                           ;[2c3e] 69
                    call      $159f                         ;[2c3f] cd 9f 15
                    ex        de,hl                         ;[2c42] eb
                    rst       $18                           ;[2c43] df
                    cp        $2c                           ;[2c44] fe 2c
                    jr        z,$2c30                       ;[2c46] 28 e8
                    cp        $29                           ;[2c48] fe 29
                    jr        nz,$2c05                      ;[2c4a] 20 b9
                    rst       $20                           ;[2c4c] e7
                    pop       bc                            ;[2c4d] c1
                    ld        l,b                           ;[2c4e] 68
                    ld        h,$00                         ;[2c4f] 26 00
                    add       hl,hl                         ;[2c51] 29
                    inc       hl                            ;[2c52] 23
                    add       hl,de                         ;[2c53] 19
                    push      de                            ;[2c54] d5
                    push      hl                            ;[2c55] e5
                    push      bc                            ;[2c56] c5
                    jp        c,$1f15                       ;[2c57] da 15 1f
                    ld        a,$c0                         ;[2c5a] 3e c0
                    cp        h                             ;[2c5c] bc
                    jr        c,$2c57                       ;[2c5d] 38 f8
                    ld        b,h                           ;[2c5f] 44
                    ld        c,l                           ;[2c60] 4d
                    call      $2ab6                         ;[2c61] cd b6 2a
                    ld        hl,($5b52)                    ;[2c64] 2a 52 5b
                    push      hl                            ;[2c67] e5
                    pop       af                            ;[2c68] f1
                    jr        c,$2ca4                       ;[2c69] 38 39
                    ld        hl,($5c5b)                    ;[2c6b] 2a 5b 5c
                    set       7,(hl)                        ;[2c6e] cb fe
                    ld        hl,($5c4d)                    ;[2c70] 2a 4d 5c
                    inc       hl                            ;[2c73] 23
                    xor       a                             ;[2c74] af
                    ld        c,(hl)                        ;[2c75] 4e
                    ld        (hl),a                        ;[2c76] 77
                    inc       hl                            ;[2c77] 23
                    ld        b,(hl)                        ;[2c78] 46
                    ld        (hl),a                        ;[2c79] 77
                    inc       hl                            ;[2c7a] 23
                    push      hl                            ;[2c7b] e5
                    ex        de,hl                         ;[2c7c] eb
                    ld        hl,($5c65)                    ;[2c7d] 2a 65 5c
                    sbc       hl,de                         ;[2c80] ed 52
                    ex        de,hl                         ;[2c82] eb
                    call      nc,$19e8                      ;[2c83] d4 e8 19
                    jr        $2c9b                         ;[2c86] 18 13
                    call      $2d1b                         ;[2c88] cd 1b 2d
                    ccf                                     ;[2c8b] 3f
                    ret       c                             ;[2c8c] d8
                    cp        $41                           ;[2c8d] fe 41
                    ccf                                     ;[2c8f] 3f
                    ret       nc                            ;[2c90] d0
                    cp        $5b                           ;[2c91] fe 5b
                    ret       c                             ;[2c93] d8
                    cp        $61                           ;[2c94] fe 61
                    ccf                                     ;[2c96] 3f
                    ret       nc                            ;[2c97] d0
                    cp        $7b                           ;[2c98] fe 7b
                    ret                                     ;[2c9a] c9

                    ld        bc,$0000                      ;[2c9b] 01 00 00
                    pop       hl                            ;[2c9e] e1
                    call      $2b82                         ;[2c9f] cd 82 2b
                    jr        $2caa                         ;[2ca2] 18 06
                    call      $1bd1                         ;[2ca4] cd d1 1b
                    call      $2bb5                         ;[2ca7] cd b5 2b
                    inc       hl                            ;[2caa] 23
                    pop       af                            ;[2cab] f1
                    ld        (hl),a                        ;[2cac] 77
                    ld        a,ixh                         ;[2cad] dd 7c
                    add       a                             ;[2caf] 87
                    ld        a,(hl)                        ;[2cb0] 7e
                    pop       bc                            ;[2cb1] c1
                    add       hl,bc                         ;[2cb2] 09
                    dec       hl                            ;[2cb3] 2b
                    ld        d,h                           ;[2cb4] 54
                    ld        e,l                           ;[2cb5] 5d
                    dec       de                            ;[2cb6] 1b
                    pop       bc                            ;[2cb7] c1
                    ld        (hl),$00                      ;[2cb8] 36 00
                    jp        p,$2cbf                       ;[2cba] f2 bf 2c
                    ld        (hl),$20                      ;[2cbd] 36 20
                    lddr                                    ;[2cbf] ed b8
                    ex        de,hl                         ;[2cc1] eb
                    inc       hl                            ;[2cc2] 23
                    pop       de                            ;[2cc3] d1
                    ld        (hl),d                        ;[2cc4] 72
                    dec       hl                            ;[2cc5] 2b
                    ld        (hl),e                        ;[2cc6] 73
                    dec       hl                            ;[2cc7] 2b
                    dec       a                             ;[2cc8] 3d
                    jr        nz,$2cc3                      ;[2cc9] 20 f8
                    ret                                     ;[2ccb] c9

                    ld        c,$10                         ;[2ccc] 0e 10
                    cp        $24                           ;[2cce] fe 24
                    jr        z,$2ce0                       ;[2cd0] 28 0e
                    ld        c,$02                         ;[2cd2] 0e 02
                    cp        $c4                           ;[2cd4] fe c4
                    jr        z,$2ce0                       ;[2cd6] 28 08
                    cp        $40                           ;[2cd8] fe 40
                    jr        z,$2ce0                       ;[2cda] 28 04
                    ld        c,$0a                         ;[2cdc] 0e 0a
                    jr        $2ce1                         ;[2cde] 18 01
                    rst       $20                           ;[2ce0] e7
                    ld        a,c                           ;[2ce1] 79
                    call      $2d0f                         ;[2ce2] cd 0f 2d
                    cp        $2e                           ;[2ce5] fe 2e
                    jr        z,$2cf4                       ;[2ce7] 28 0b
                    call      $2d3e                         ;[2ce9] cd 3e 2d
                    cp        $2e                           ;[2cec] fe 2e
                    jp        nz,$1ea1                      ;[2cee] c2 a1 1e
                    rst       $20                           ;[2cf1] e7
                    jr        $2cfc                         ;[2cf2] 18 08
                    rst       $20                           ;[2cf4] e7
                    call      $1ed8                         ;[2cf5] cd d8 1e
                    jp        c,$1c8a                       ;[2cf8] da 8a 1c
                    rst       $18                           ;[2cfb] df
                    call      $2d22                         ;[2cfc] cd 22 2d
                    jp        c,$1ea1                       ;[2cff] da a1 1e
                    rst       $28                           ;[2d02] ef
                    call      po,$04e5                      ;[2d03] e4 e5 04
                    call      nz,$0f05                      ;[2d06] c4 05 0f
                    jr        c,$2cf2                       ;[2d09] 38 e7
                    jr        $2cfc                         ;[2d0b] 18 ef
                    ld        a,$0a                         ;[2d0d] 3e 0a
                    call      $2d28                         ;[2d0f] cd 28 2d
                    rst       $28                           ;[2d12] ef
                    push      bc                            ;[2d13] c5
                    ld        (bc),a                        ;[2d14] 02
                    and       c                             ;[2d15] a1
                    call      nz,$a002                      ;[2d16] c4 02 a0
                    jr        c,$2ce4                       ;[2d19] 38 c9
                    cp        $30                           ;[2d1b] fe 30
                    ret       c                             ;[2d1d] d8
                    cp        $3a                           ;[2d1e] fe 3a
                    ccf                                     ;[2d20] 3f
                    ret                                     ;[2d21] c9

                    call      $1ed8                         ;[2d22] cd d8 1e
                    ret       c                             ;[2d25] d8
                    nop                                     ;[2d26] 00
                    nop                                     ;[2d27] 00
                    ld        c,a                           ;[2d28] 4f
                    ld        b,$00                         ;[2d29] 06 00
                    ld        iy,$5c3a                      ;[2d2b] fd 21 3a 5c
                    xor       a                             ;[2d2f] af
                    ld        e,a                           ;[2d30] 5f
                    ld        d,c                           ;[2d31] 51
                    ld        c,b                           ;[2d32] 48
                    ld        b,a                           ;[2d33] 47
                    call      $2ab6                         ;[2d34] cd b6 2a
                    rst       $28                           ;[2d37] ef
                    jr        c,$2ce1                       ;[2d38] 38 a7
                    ret                                     ;[2d3a] c9

                    call      $2d0d                         ;[2d3b] cd 0d 2d
                    rst       $18                           ;[2d3e] df
                    call      $2d22                         ;[2d3f] cd 22 2d
                    ret       c                             ;[2d42] d8
                    rst       $28                           ;[2d43] ef
                    ld        bc,$04e5                      ;[2d44] 01 e5 04
                    rrca                                    ;[2d47] 0f
                    jr        c,$2d17                       ;[2d48] 38 cd
                    ld        (hl),h                        ;[2d4a] 74
                    nop                                     ;[2d4b] 00
                    jr        $2d3f                         ;[2d4c] 18 f1
                    ld        a,d                           ;[2d4e] 7a
                    rlca                                    ;[2d4f] 07
                    rrca                                    ;[2d50] 0f
                    jr        nc,$2d55                      ;[2d51] 30 02
                    cpl                                     ;[2d53] 2f
                    inc       a                             ;[2d54] 3c
                    push      af                            ;[2d55] f5
                    ld        hl,$5c92                      ;[2d56] 21 92 5c
                    call      $350b                         ;[2d59] cd 0b 35
                    rst       $28                           ;[2d5c] ef
                    and       h                             ;[2d5d] a4
                    jr        c,$2d51                       ;[2d5e] 38 f1
                    srl       a                             ;[2d60] cb 3f
                    jr        nc,$2d71                      ;[2d62] 30 0d
                    push      af                            ;[2d64] f5
                    rst       $28                           ;[2d65] ef
                    pop       bc                            ;[2d66] c1
                    ret       po                            ;[2d67] e0
                    nop                                     ;[2d68] 00
                    inc       b                             ;[2d69] 04
                    inc       b                             ;[2d6a] 04
                    inc       sp                            ;[2d6b] 33
                    ld        (bc),a                        ;[2d6c] 02
                    dec       b                             ;[2d6d] 05
                    pop       hl                            ;[2d6e] e1
                    jr        c,$2d62                       ;[2d6f] 38 f1
                    jr        z,$2d7b                       ;[2d71] 28 08
                    push      af                            ;[2d73] f5
                    rst       $28                           ;[2d74] ef
                    ld        sp,$3804                      ;[2d75] 31 04 38
                    pop       af                            ;[2d78] f1
                    jr        $2d60                         ;[2d79] 18 e5
                    rst       $28                           ;[2d7b] ef
                    ld        (bc),a                        ;[2d7c] 02
                    jr        c,$2d48                       ;[2d7d] 38 c9
                    inc       hl                            ;[2d7f] 23
                    ld        c,(hl)                        ;[2d80] 4e
                    inc       hl                            ;[2d81] 23
                    ld        a,(hl)                        ;[2d82] 7e
                    xor       c                             ;[2d83] a9
                    sub       c                             ;[2d84] 91
                    ld        e,a                           ;[2d85] 5f
                    inc       hl                            ;[2d86] 23
                    ld        a,(hl)                        ;[2d87] 7e
                    adc       c                             ;[2d88] 89
                    xor       c                             ;[2d89] a9
                    ld        d,a                           ;[2d8a] 57
                    ret                                     ;[2d8b] c9

                    ld        c,$00                         ;[2d8c] 0e 00
                    push      hl                            ;[2d8e] e5
                    ld        (hl),$00                      ;[2d8f] 36 00
                    inc       hl                            ;[2d91] 23
                    ld        (hl),c                        ;[2d92] 71
                    inc       hl                            ;[2d93] 23
                    ld        a,e                           ;[2d94] 7b
                    xor       c                             ;[2d95] a9
                    sub       c                             ;[2d96] 91
                    ld        (hl),a                        ;[2d97] 77
                    inc       hl                            ;[2d98] 23
                    ld        a,d                           ;[2d99] 7a
                    adc       c                             ;[2d9a] 89
                    xor       c                             ;[2d9b] a9
                    ld        (hl),a                        ;[2d9c] 77
                    inc       hl                            ;[2d9d] 23
                    ld        (hl),$00                      ;[2d9e] 36 00
                    pop       hl                            ;[2da0] e1
                    ret                                     ;[2da1] c9

                    rst       $28                           ;[2da2] ef
                    jr        c,$2e23                       ;[2da3] 38 7e
                    and       a                             ;[2da5] a7
                    jr        z,$2dad                       ;[2da6] 28 05
                    rst       $28                           ;[2da8] ef
                    and       d                             ;[2da9] a2
                    rrca                                    ;[2daa] 0f
                    daa                                     ;[2dab] 27
                    jr        c,$2d9d                       ;[2dac] 38 ef
                    ld        (bc),a                        ;[2dae] 02
                    jr        c,$2d96                       ;[2daf] 38 e5
                    push      de                            ;[2db1] d5
                    ex        de,hl                         ;[2db2] eb
                    ld        b,(hl)                        ;[2db3] 46
                    call      $2d7f                         ;[2db4] cd 7f 2d
                    xor       a                             ;[2db7] af
                    sub       b                             ;[2db8] 90
                    bit       7,c                           ;[2db9] cb 79
                    ld        b,d                           ;[2dbb] 42
                    ld        c,e                           ;[2dbc] 4b
                    ld        a,e                           ;[2dbd] 7b
                    pop       de                            ;[2dbe] d1
                    pop       hl                            ;[2dbf] e1
                    ret                                     ;[2dc0] c9

                    ld        d,a                           ;[2dc1] 57
                    rla                                     ;[2dc2] 17
                    sbc       a                             ;[2dc3] 9f
                    ld        e,a                           ;[2dc4] 5f
                    ld        c,a                           ;[2dc5] 4f
                    xor       a                             ;[2dc6] af
                    ld        b,a                           ;[2dc7] 47
                    call      $2ab6                         ;[2dc8] cd b6 2a
                    rst       $28                           ;[2dcb] ef
                    inc       (hl)                          ;[2dcc] 34
                    rst       $28                           ;[2dcd] ef
                    ld        a,(de)                        ;[2dce] 1a
                    jr        nz,$2d6b                      ;[2dcf] 20 9a
                    add       l                             ;[2dd1] 85
                    inc       b                             ;[2dd2] 04
                    daa                                     ;[2dd3] 27
                    jr        c,$2da3                       ;[2dd4] 38 cd
                    and       d                             ;[2dd6] a2
                    dec       l                             ;[2dd7] 2d
                    ret       c                             ;[2dd8] d8
                    push      af                            ;[2dd9] f5
                    dec       b                             ;[2dda] 05
                    inc       b                             ;[2ddb] 04
                    jr        z,$2de1                       ;[2ddc] 28 03
                    pop       af                            ;[2dde] f1
                    scf                                     ;[2ddf] 37
                    ret                                     ;[2de0] c9

                    pop       af                            ;[2de1] f1
                    ret                                     ;[2de2] c9

                    rst       $28                           ;[2de3] ef
                    ld        sp,$0036                      ;[2de4] 31 36 00
                    dec       bc                            ;[2de7] 0b
                    ld        sp,$0037                      ;[2de8] 31 37 00
                    dec       c                             ;[2deb] 0d
                    ld        (bc),a                        ;[2dec] 02
                    jr        c,$2e2d                       ;[2ded] 38 3e
                    jr        nc,$2dc8                      ;[2def] 30 d7
                    ret                                     ;[2df1] c9

                    ld        hl,($3e38)                    ;[2df2] 2a 38 3e
                    dec       l                             ;[2df5] 2d
                    rst       $10                           ;[2df6] d7
                    rst       $28                           ;[2df7] ef
                    and       b                             ;[2df8] a0
                    jp        $c5c4                         ;[2df9] c3 c4 c5
                    ld        (bc),a                        ;[2dfc] 02
                    jr        c,$2dd8                       ;[2dfd] 38 d9
                    push      hl                            ;[2dff] e5
                    exx                                     ;[2e00] d9
                    rst       $28                           ;[2e01] ef
                    ld        sp,$c227                      ;[2e02] 31 27 c2
                    inc       bc                            ;[2e05] 03
                    jp        po,$c201                      ;[2e06] e2 01 c2
                    ld        (bc),a                        ;[2e09] 02
                    jr        c,$2e8a                       ;[2e0a] 38 7e
                    and       a                             ;[2e0c] a7
                    jr        nz,$2e56                      ;[2e0d] 20 47
                    call      $2d7f                         ;[2e0f] cd 7f 2d
                    ld        b,$10                         ;[2e12] 06 10
                    ld        a,d                           ;[2e14] 7a
                    and       a                             ;[2e15] a7
                    jr        nz,$2e1e                      ;[2e16] 20 06
                    or        e                             ;[2e18] b3
                    jr        z,$2e24                       ;[2e19] 28 09
                    ld        d,e                           ;[2e1b] 53
                    ld        b,$08                         ;[2e1c] 06 08
                    push      de                            ;[2e1e] d5
                    exx                                     ;[2e1f] d9
                    pop       de                            ;[2e20] d1
                    exx                                     ;[2e21] d9
                    jr        $2e7b                         ;[2e22] 18 57
                    rst       $28                           ;[2e24] ef
                    ld        (bc),a                        ;[2e25] 02
                    jp        po,$7e38                      ;[2e26] e2 38 7e
                    sub       $7e                           ;[2e29] d6 7e
                    call      $2dc1                         ;[2e2b] cd c1 2d
                    ld        d,a                           ;[2e2e] 57
                    ld        a,($5cac)                     ;[2e2f] 3a ac 5c
                    sub       d                             ;[2e32] 92
                    ld        ($5cac),a                     ;[2e33] 32 ac 5c
                    call      $2d4e                         ;[2e36] cd 4e 2d
                    rst       $28                           ;[2e39] ef
                    ld        sp,$c127                      ;[2e3a] 31 27 c1
                    inc       bc                            ;[2e3d] 03
                    pop       hl                            ;[2e3e] e1
                    jr        c,$2e0e                       ;[2e3f] 38 cd
                    push      de                            ;[2e41] d5
                    dec       l                             ;[2e42] 2d
                    push      hl                            ;[2e43] e5
                    ld        ($5ca1),a                     ;[2e44] 32 a1 5c
                    dec       a                             ;[2e47] 3d
                    rla                                     ;[2e48] 17
                    sbc       a                             ;[2e49] 9f
                    inc       a                             ;[2e4a] 3c
                    ld        hl,$5cab                      ;[2e4b] 21 ab 5c
                    ld        (hl),a                        ;[2e4e] 77
                    inc       hl                            ;[2e4f] 23
                    add       (hl)                          ;[2e50] 86
                    ld        (hl),a                        ;[2e51] 77
                    pop       hl                            ;[2e52] e1
                    jp        $2ecf                         ;[2e53] c3 cf 2e
                    sub       $80                           ;[2e56] d6 80
                    cp        $1c                           ;[2e58] fe 1c
                    jr        c,$2e6f                       ;[2e5a] 38 13
                    call      $2dc1                         ;[2e5c] cd c1 2d
                    sub       $07                           ;[2e5f] d6 07
                    ld        b,a                           ;[2e61] 47
                    ld        hl,$5cac                      ;[2e62] 21 ac 5c
                    add       (hl)                          ;[2e65] 86
                    ld        (hl),a                        ;[2e66] 77
                    ld        a,b                           ;[2e67] 78
                    neg                                     ;[2e68] ed 44
                    call      $2d4f                         ;[2e6a] cd 4f 2d
                    jr        $2e01                         ;[2e6d] 18 92
                    ex        de,hl                         ;[2e6f] eb
                    call      $2fba                         ;[2e70] cd ba 2f
                    exx                                     ;[2e73] d9
                    set       7,d                           ;[2e74] cb fa
                    ld        a,l                           ;[2e76] 7d
                    exx                                     ;[2e77] d9
                    sub       $80                           ;[2e78] d6 80
                    ld        b,a                           ;[2e7a] 47
                    sla       e                             ;[2e7b] cb 23
                    rl        d                             ;[2e7d] cb 12
                    exx                                     ;[2e7f] d9
                    rl        e                             ;[2e80] cb 13
                    rl        d                             ;[2e82] cb 12
                    exx                                     ;[2e84] d9
                    ld        hl,$5caa                      ;[2e85] 21 aa 5c
                    ld        c,$05                         ;[2e88] 0e 05
                    ld        a,(hl)                        ;[2e8a] 7e
                    adc       a                             ;[2e8b] 8f
                    daa                                     ;[2e8c] 27
                    ld        (hl),a                        ;[2e8d] 77
                    dec       hl                            ;[2e8e] 2b
                    dec       c                             ;[2e8f] 0d
                    jr        nz,$2e8a                      ;[2e90] 20 f8
                    djnz      $2e7b                         ;[2e92] 10 e7
                    xor       a                             ;[2e94] af
                    ld        hl,$5ca6                      ;[2e95] 21 a6 5c
                    ld        de,$5ca1                      ;[2e98] 11 a1 5c
                    ld        b,$09                         ;[2e9b] 06 09
                    rld                                     ;[2e9d] ed 6f
                    ld        c,$ff                         ;[2e9f] 0e ff
                    rld                                     ;[2ea1] ed 6f
                    jr        nz,$2ea9                      ;[2ea3] 20 04
                    dec       c                             ;[2ea5] 0d
                    inc       c                             ;[2ea6] 0c
                    jr        nz,$2eb3                      ;[2ea7] 20 0a
                    ld        (de),a                        ;[2ea9] 12
                    inc       de                            ;[2eaa] 13
                    inc       (iy+$71)                      ;[2eab] fd 34 71
                    inc       (iy+$72)                      ;[2eae] fd 34 72
                    ld        c,$00                         ;[2eb1] 0e 00
                    bit       0,b                           ;[2eb3] cb 40
                    jr        z,$2eb8                       ;[2eb5] 28 01
                    inc       hl                            ;[2eb7] 23
                    djnz      $2ea1                         ;[2eb8] 10 e7
                    ld        a,($5cab)                     ;[2eba] 3a ab 5c
                    sub       $09                           ;[2ebd] d6 09
                    jr        c,$2ecb                       ;[2ebf] 38 0a
                    dec       (iy+$71)                      ;[2ec1] fd 35 71
                    ld        a,$04                         ;[2ec4] 3e 04
                    cp        (iy+$6f)                      ;[2ec6] fd be 6f
                    jr        $2f0c                         ;[2ec9] 18 41
                    rst       $28                           ;[2ecb] ef
                    ld        (bc),a                        ;[2ecc] 02
                    jp        po,$eb38                      ;[2ecd] e2 38 eb
                    call      $2fba                         ;[2ed0] cd ba 2f
                    exx                                     ;[2ed3] d9
                    ld        a,$80                         ;[2ed4] 3e 80
                    sub       l                             ;[2ed6] 95
                    ld        l,$00                         ;[2ed7] 2e 00
                    set       7,d                           ;[2ed9] cb fa
                    exx                                     ;[2edb] d9
                    call      $2fdd                         ;[2edc] cd dd 2f
                    ld        a,(iy+$71)                    ;[2edf] fd 7e 71
                    cp        $08                           ;[2ee2] fe 08
                    jr        c,$2eec                       ;[2ee4] 38 06
                    exx                                     ;[2ee6] d9
                    rl        d                             ;[2ee7] cb 12
                    exx                                     ;[2ee9] d9
                    jr        $2f0c                         ;[2eea] 18 20
                    ld        bc,$0200                      ;[2eec] 01 00 02
                    ld        a,e                           ;[2eef] 7b
                    call      $2f8b                         ;[2ef0] cd 8b 2f
                    ld        e,a                           ;[2ef3] 5f
                    ld        a,d                           ;[2ef4] 7a
                    call      $2f8b                         ;[2ef5] cd 8b 2f
                    ld        d,a                           ;[2ef8] 57
                    push      bc                            ;[2ef9] c5
                    exx                                     ;[2efa] d9
                    pop       bc                            ;[2efb] c1
                    djnz      $2eef                         ;[2efc] 10 f1
                    ld        hl,$5ca1                      ;[2efe] 21 a1 5c
                    ld        a,(iy+$71)                    ;[2f01] fd 7e 71
                    add       hl,a                          ;[2f04] ed 31
                    ld        (hl),c                        ;[2f06] 71
                    inc       (iy+$71)                      ;[2f07] fd 34 71
                    jr        $2edf                         ;[2f0a] 18 d3
                    push      af                            ;[2f0c] f5
                    ld        hl,$5ca1                      ;[2f0d] 21 a1 5c
                    ld        b,(iy+$71)                    ;[2f10] fd 46 71
                    ld        a,b                           ;[2f13] 78
                    add       hl,a                          ;[2f14] ed 31
                    pop       af                            ;[2f16] f1
                    nop                                     ;[2f17] 00
                    dec       hl                            ;[2f18] 2b
                    ld        a,(hl)                        ;[2f19] 7e
                    adc       $00                           ;[2f1a] ce 00
                    ld        (hl),a                        ;[2f1c] 77
                    and       a                             ;[2f1d] a7
                    jr        z,$2f25                       ;[2f1e] 28 05
                    cp        $0a                           ;[2f20] fe 0a
                    ccf                                     ;[2f22] 3f
                    jr        nc,$2f2d                      ;[2f23] 30 08
                    djnz      $2f18                         ;[2f25] 10 f1
                    ld        (hl),$01                      ;[2f27] 36 01
                    inc       b                             ;[2f29] 04
                    inc       (iy+$72)                      ;[2f2a] fd 34 72
                    ld        (iy+$71),b                    ;[2f2d] fd 70 71
                    rst       $28                           ;[2f30] ef
                    ld        (bc),a                        ;[2f31] 02
                    jr        c,$2f0d                       ;[2f32] 38 d9
                    pop       hl                            ;[2f34] e1
                    exx                                     ;[2f35] d9
                    ld        bc,($5cab)                    ;[2f36] ed 4b ab 5c
                    ld        hl,$5ca1                      ;[2f3a] 21 a1 5c
                    ld        a,b                           ;[2f3d] 78
                    cp        $09                           ;[2f3e] fe 09
                    jr        c,$2f46                       ;[2f40] 38 04
                    cp        $fc                           ;[2f42] fe fc
                    jr        c,$2f6c                       ;[2f44] 38 26
                    and       a                             ;[2f46] a7
                    call      z,$15ef                       ;[2f47] cc ef 15
                    xor       a                             ;[2f4a] af
                    sub       b                             ;[2f4b] 90
                    jp        m,$2f52                       ;[2f4c] fa 52 2f
                    ld        b,a                           ;[2f4f] 47
                    jr        $2f5e                         ;[2f50] 18 0c
                    ld        a,c                           ;[2f52] 79
                    and       a                             ;[2f53] a7
                    jr        z,$2f59                       ;[2f54] 28 03
                    ld        a,(hl)                        ;[2f56] 7e
                    inc       hl                            ;[2f57] 23
                    dec       c                             ;[2f58] 0d
                    call      $15ef                         ;[2f59] cd ef 15
                    djnz      $2f52                         ;[2f5c] 10 f4
                    ld        a,c                           ;[2f5e] 79
                    and       a                             ;[2f5f] a7
                    ret       z                             ;[2f60] c8
                    inc       b                             ;[2f61] 04
                    ld        a,$2e                         ;[2f62] 3e 2e
                    rst       $10                           ;[2f64] d7
                    ld        a,$30                         ;[2f65] 3e 30
                    djnz      $2f64                         ;[2f67] 10 fb
                    ld        b,c                           ;[2f69] 41
                    jr        $2f52                         ;[2f6a] 18 e6
                    ld        d,b                           ;[2f6c] 50
                    dec       d                             ;[2f6d] 15
                    ld        b,$01                         ;[2f6e] 06 01
                    call      $2f4a                         ;[2f70] cd 4a 2f
                    ld        a,$45                         ;[2f73] 3e 45
                    rst       $10                           ;[2f75] d7
                    ld        c,d                           ;[2f76] 4a
                    ld        a,c                           ;[2f77] 79
                    and       a                             ;[2f78] a7
                    jp        p,$2f83                       ;[2f79] f2 83 2f
                    neg                                     ;[2f7c] ed 44
                    ld        c,a                           ;[2f7e] 4f
                    ld        a,$2d                         ;[2f7f] 3e 2d
                    jr        $2f85                         ;[2f81] 18 02
                    ld        a,$2b                         ;[2f83] 3e 2b
                    rst       $10                           ;[2f85] d7
                    ld        b,$00                         ;[2f86] 06 00
                    jp        $1a1b                         ;[2f88] c3 1b 1a
                    push      de                            ;[2f8b] d5
                    ld        l,a                           ;[2f8c] 6f
                    ld        h,$00                         ;[2f8d] 26 00
                    ld        e,l                           ;[2f8f] 5d
                    ld        d,h                           ;[2f90] 54
                    add       hl,hl                         ;[2f91] 29
                    add       hl,hl                         ;[2f92] 29
                    add       hl,de                         ;[2f93] 19
                    add       hl,hl                         ;[2f94] 29
                    ld        e,c                           ;[2f95] 59
                    add       hl,de                         ;[2f96] 19
                    ld        c,h                           ;[2f97] 4c
                    ld        a,l                           ;[2f98] 7d
                    pop       de                            ;[2f99] d1
                    ret                                     ;[2f9a] c9

                    ld        a,(hl)                        ;[2f9b] 7e
                    ld        (hl),$00                      ;[2f9c] 36 00
                    and       a                             ;[2f9e] a7
                    ret       z                             ;[2f9f] c8
                    inc       hl                            ;[2fa0] 23
                    bit       7,(hl)                        ;[2fa1] cb 7e
                    set       7,(hl)                        ;[2fa3] cb fe
                    dec       hl                            ;[2fa5] 2b
                    ret       z                             ;[2fa6] c8
                    push      bc                            ;[2fa7] c5
                    ld        bc,$0005                      ;[2fa8] 01 05 00
                    add       hl,bc                         ;[2fab] 09
                    ld        b,c                           ;[2fac] 41
                    ld        c,a                           ;[2fad] 4f
                    scf                                     ;[2fae] 37
                    dec       hl                            ;[2faf] 2b
                    ld        a,(hl)                        ;[2fb0] 7e
                    cpl                                     ;[2fb1] 2f
                    adc       $00                           ;[2fb2] ce 00
                    ld        (hl),a                        ;[2fb4] 77
                    djnz      $2faf                         ;[2fb5] 10 f8
                    ld        a,c                           ;[2fb7] 79
                    pop       bc                            ;[2fb8] c1
                    ret                                     ;[2fb9] c9

                    push      hl                            ;[2fba] e5
                    push      af                            ;[2fbb] f5
                    ld        c,(hl)                        ;[2fbc] 4e
                    inc       hl                            ;[2fbd] 23
                    ld        b,(hl)                        ;[2fbe] 46
                    ld        (hl),a                        ;[2fbf] 77
                    inc       hl                            ;[2fc0] 23
                    ld        a,c                           ;[2fc1] 79
                    ld        c,(hl)                        ;[2fc2] 4e
                    push      bc                            ;[2fc3] c5
                    inc       hl                            ;[2fc4] 23
                    ld        c,(hl)                        ;[2fc5] 4e
                    inc       hl                            ;[2fc6] 23
                    ld        b,(hl)                        ;[2fc7] 46
                    ex        de,hl                         ;[2fc8] eb
                    ld        d,a                           ;[2fc9] 57
                    ld        e,(hl)                        ;[2fca] 5e
                    push      de                            ;[2fcb] d5
                    inc       hl                            ;[2fcc] 23
                    ld        d,(hl)                        ;[2fcd] 56
                    inc       hl                            ;[2fce] 23
                    ld        e,(hl)                        ;[2fcf] 5e
                    push      de                            ;[2fd0] d5
                    exx                                     ;[2fd1] d9
                    pop       de                            ;[2fd2] d1
                    pop       hl                            ;[2fd3] e1
                    pop       bc                            ;[2fd4] c1
                    exx                                     ;[2fd5] d9
                    inc       hl                            ;[2fd6] 23
                    ld        d,(hl)                        ;[2fd7] 56
                    inc       hl                            ;[2fd8] 23
                    ld        e,(hl)                        ;[2fd9] 5e
                    pop       af                            ;[2fda] f1
                    pop       hl                            ;[2fdb] e1
                    ret                                     ;[2fdc] c9

                    and       a                             ;[2fdd] a7
                    ret       z                             ;[2fde] c8
                    cp        $21                           ;[2fdf] fe 21
                    jr        nc,$2ff9                      ;[2fe1] 30 16
                    push      bc                            ;[2fe3] c5
                    ld        b,a                           ;[2fe4] 47
                    exx                                     ;[2fe5] d9
                    sra       l                             ;[2fe6] cb 2d
                    rr        d                             ;[2fe8] cb 1a
                    rr        e                             ;[2fea] cb 1b
                    exx                                     ;[2fec] d9
                    rr        d                             ;[2fed] cb 1a
                    rr        e                             ;[2fef] cb 1b
                    djnz      $2fe5                         ;[2ff1] 10 f2
                    pop       bc                            ;[2ff3] c1
                    ret       nc                            ;[2ff4] d0
                    call      $3004                         ;[2ff5] cd 04 30
                    ret       nz                            ;[2ff8] c0
                    exx                                     ;[2ff9] d9
                    xor       a                             ;[2ffa] af
                    ld        l,$00                         ;[2ffb] 2e 00
                    ld        d,a                           ;[2ffd] 57
                    ld        e,l                           ;[2ffe] 5d
                    exx                                     ;[2fff] d9
                    ld        de,$0000                      ;[3000] 11 00 00
                    ret                                     ;[3003] c9

                    inc       e                             ;[3004] 1c
                    ret       nz                            ;[3005] c0
                    inc       d                             ;[3006] 14
                    ret       nz                            ;[3007] c0
                    exx                                     ;[3008] d9
                    inc       e                             ;[3009] 1c
                    jr        nz,$300d                      ;[300a] 20 01
                    inc       d                             ;[300c] 14
                    exx                                     ;[300d] d9
                    ret                                     ;[300e] c9

                    ex        de,hl                         ;[300f] eb
                    call      $346e                         ;[3010] cd 6e 34
                    ex        de,hl                         ;[3013] eb
                    ld        a,(de)                        ;[3014] 1a
                    or        (hl)                          ;[3015] b6
                    jr        nz,$303e                      ;[3016] 20 26
                    push      de                            ;[3018] d5
                    inc       hl                            ;[3019] 23
                    push      hl                            ;[301a] e5
                    inc       hl                            ;[301b] 23
                    ld        e,(hl)                        ;[301c] 5e
                    inc       hl                            ;[301d] 23
                    ld        d,(hl)                        ;[301e] 56
                    inc       hl                            ;[301f] 23
                    inc       hl                            ;[3020] 23
                    inc       hl                            ;[3021] 23
                    ld        a,(hl)                        ;[3022] 7e
                    inc       hl                            ;[3023] 23
                    ld        c,(hl)                        ;[3024] 4e
                    inc       hl                            ;[3025] 23
                    ld        b,(hl)                        ;[3026] 46
                    pop       hl                            ;[3027] e1
                    ex        de,hl                         ;[3028] eb
                    adc       hl,bc                         ;[3029] ed 4a
                    ex        de,hl                         ;[302b] eb
                    jp        z,$3226                       ;[302c] ca 26 32
                    adc       (hl)                          ;[302f] 8e
                    rrca                                    ;[3030] 0f
                    adc       $00                           ;[3031] ce 00
                    jr        nz,$303c                      ;[3033] 20 07
                    sbc       a                             ;[3035] 9f
                    ld        (hl),a                        ;[3036] 77
                    inc       hl                            ;[3037] 23
                    ld        (hl),e                        ;[3038] 73
                    jp        $3237                         ;[3039] c3 37 32
                    dec       hl                            ;[303c] 2b
                    pop       de                            ;[303d] d1
                    call      $3293                         ;[303e] cd 93 32
                    exx                                     ;[3041] d9
                    push      hl                            ;[3042] e5
                    exx                                     ;[3043] d9
                    push      de                            ;[3044] d5
                    push      hl                            ;[3045] e5
                    call      $2f9b                         ;[3046] cd 9b 2f
                    ld        b,a                           ;[3049] 47
                    ex        de,hl                         ;[304a] eb
                    call      $2f9b                         ;[304b] cd 9b 2f
                    ld        c,a                           ;[304e] 4f
                    cp        b                             ;[304f] b8
                    jr        nc,$3055                      ;[3050] 30 03
                    ld        a,b                           ;[3052] 78
                    ld        b,c                           ;[3053] 41
                    ex        de,hl                         ;[3054] eb
                    push      af                            ;[3055] f5
                    sub       b                             ;[3056] 90
                    call      $2fba                         ;[3057] cd ba 2f
                    call      $2fdd                         ;[305a] cd dd 2f
                    pop       af                            ;[305d] f1
                    pop       hl                            ;[305e] e1
                    ld        (hl),a                        ;[305f] 77
                    push      hl                            ;[3060] e5
                    ld        l,b                           ;[3061] 68
                    ld        h,c                           ;[3062] 61
                    add       hl,de                         ;[3063] 19
                    exx                                     ;[3064] d9
                    ex        de,hl                         ;[3065] eb
                    adc       hl,bc                         ;[3066] ed 4a
                    ex        de,hl                         ;[3068] eb
                    ld        a,h                           ;[3069] 7c
                    adc       l                             ;[306a] 8d
                    ld        l,a                           ;[306b] 6f
                    rra                                     ;[306c] 1f
                    xor       l                             ;[306d] ad
                    exx                                     ;[306e] d9
                    ex        de,hl                         ;[306f] eb
                    pop       hl                            ;[3070] e1
                    rra                                     ;[3071] 1f
                    jr        nc,$307c                      ;[3072] 30 08
                    ld        a,$01                         ;[3074] 3e 01
                    call      $2fdd                         ;[3076] cd dd 2f
                    inc       (hl)                          ;[3079] 34
                    jr        z,$309f                       ;[307a] 28 23
                    exx                                     ;[307c] d9
                    ld        a,l                           ;[307d] 7d
                    and       $80                           ;[307e] e6 80
                    exx                                     ;[3080] d9
                    inc       hl                            ;[3081] 23
                    ld        (hl),a                        ;[3082] 77
                    dec       hl                            ;[3083] 2b
                    jr        z,$30a5                       ;[3084] 28 1f
                    ld        a,e                           ;[3086] 7b
                    neg                                     ;[3087] ed 44
                    ccf                                     ;[3089] 3f
                    ld        e,a                           ;[308a] 5f
                    ld        a,d                           ;[308b] 7a
                    cpl                                     ;[308c] 2f
                    adc       $00                           ;[308d] ce 00
                    ld        d,a                           ;[308f] 57
                    exx                                     ;[3090] d9
                    ld        a,e                           ;[3091] 7b
                    cpl                                     ;[3092] 2f
                    adc       $00                           ;[3093] ce 00
                    ld        e,a                           ;[3095] 5f
                    ld        a,d                           ;[3096] 7a
                    cpl                                     ;[3097] 2f
                    adc       $00                           ;[3098] ce 00
                    jr        nc,$30a3                      ;[309a] 30 07
                    rra                                     ;[309c] 1f
                    exx                                     ;[309d] d9
                    inc       (hl)                          ;[309e] 34
                    jp        z,$31ad                       ;[309f] ca ad 31
                    exx                                     ;[30a2] d9
                    ld        d,a                           ;[30a3] 57
                    exx                                     ;[30a4] d9
                    xor       a                             ;[30a5] af
                    jp        $3155                         ;[30a6] c3 55 31
                    push      bc                            ;[30a9] c5
                    ld        b,$10                         ;[30aa] 06 10
                    ld        a,h                           ;[30ac] 7c
                    ld        c,l                           ;[30ad] 4d
                    ld        hl,$0000                      ;[30ae] 21 00 00
                    add       hl,hl                         ;[30b1] 29
                    jr        c,$30be                       ;[30b2] 38 0a
                    rl        c                             ;[30b4] cb 11
                    rla                                     ;[30b6] 17
                    jr        nc,$30bc                      ;[30b7] 30 03
                    add       hl,de                         ;[30b9] 19
                    jr        c,$30be                       ;[30ba] 38 02
                    djnz      $30b1                         ;[30bc] 10 f3
                    pop       bc                            ;[30be] c1
                    ret                                     ;[30bf] c9

                    call      $34e9                         ;[30c0] cd e9 34
                    ret       c                             ;[30c3] d8
                    inc       hl                            ;[30c4] 23
                    xor       (hl)                          ;[30c5] ae
                    set       7,(hl)                        ;[30c6] cb fe
                    dec       hl                            ;[30c8] 2b
                    ret                                     ;[30c9] c9

                    ld        a,(de)                        ;[30ca] 1a
                    or        (hl)                          ;[30cb] b6
                    jr        nz,$30f0                      ;[30cc] 20 22
                    push      de                            ;[30ce] d5
                    push      hl                            ;[30cf] e5
                    push      de                            ;[30d0] d5
                    call      $2d7f                         ;[30d1] cd 7f 2d
                    ex        de,hl                         ;[30d4] eb
                    ex        (sp),hl                       ;[30d5] e3
                    ld        b,c                           ;[30d6] 41
                    call      $2d7f                         ;[30d7] cd 7f 2d
                    ld        a,b                           ;[30da] 78
                    xor       c                             ;[30db] a9
                    ld        c,a                           ;[30dc] 4f
                    pop       hl                            ;[30dd] e1
                    push      bc                            ;[30de] c5
                    call      $1579                         ;[30df] cd 79 15
                    pop       bc                            ;[30e2] c1
                    ex        de,hl                         ;[30e3] eb
                    pop       hl                            ;[30e4] e1
                    jr        c,$30ef                       ;[30e5] 38 08
                    jr        nz,$30ea                      ;[30e7] 20 01
                    ld        c,a                           ;[30e9] 4f
                    call      $2d8e                         ;[30ea] cd 8e 2d
                    pop       de                            ;[30ed] d1
                    ret                                     ;[30ee] c9

                    pop       de                            ;[30ef] d1
                    call      $3293                         ;[30f0] cd 93 32
                    xor       a                             ;[30f3] af
                    call      $30c0                         ;[30f4] cd c0 30
                    ret       c                             ;[30f7] d8
                    exx                                     ;[30f8] d9
                    push      hl                            ;[30f9] e5
                    exx                                     ;[30fa] d9
                    push      de                            ;[30fb] d5
                    ex        de,hl                         ;[30fc] eb
                    call      $30c0                         ;[30fd] cd c0 30
                    ex        de,hl                         ;[3100] eb
                    jr        c,$315d                       ;[3101] 38 5a
                    push      hl                            ;[3103] e5
                    call      $2fba                         ;[3104] cd ba 2f
                    ld        a,b                           ;[3107] 78
                    and       a                             ;[3108] a7
                    sbc       hl,hl                         ;[3109] ed 62
                    exx                                     ;[310b] d9
                    push      hl                            ;[310c] e5
                    sbc       hl,hl                         ;[310d] ed 62
                    exx                                     ;[310f] d9
                    ld        b,$21                         ;[3110] 06 21
                    jr        $3125                         ;[3112] 18 11
                    jr        nc,$311b                      ;[3114] 30 05
                    add       hl,de                         ;[3116] 19
                    exx                                     ;[3117] d9
                    adc       hl,de                         ;[3118] ed 5a
                    exx                                     ;[311a] d9
                    exx                                     ;[311b] d9
                    rr        h                             ;[311c] cb 1c
                    rr        l                             ;[311e] cb 1d
                    exx                                     ;[3120] d9
                    rr        h                             ;[3121] cb 1c
                    rr        l                             ;[3123] cb 1d
                    exx                                     ;[3125] d9
                    rr        b                             ;[3126] cb 18
                    rr        c                             ;[3128] cb 19
                    exx                                     ;[312a] d9
                    rr        c                             ;[312b] cb 19
                    rra                                     ;[312d] 1f
                    djnz      $3114                         ;[312e] 10 e4
                    ex        de,hl                         ;[3130] eb
                    exx                                     ;[3131] d9
                    ex        de,hl                         ;[3132] eb
                    exx                                     ;[3133] d9
                    pop       bc                            ;[3134] c1
                    pop       hl                            ;[3135] e1
                    ld        a,b                           ;[3136] 78
                    add       c                             ;[3137] 81
                    jr        nz,$313b                      ;[3138] 20 01
                    and       a                             ;[313a] a7
                    dec       a                             ;[313b] 3d
                    ccf                                     ;[313c] 3f
                    rla                                     ;[313d] 17
                    ccf                                     ;[313e] 3f
                    rra                                     ;[313f] 1f
                    jp        p,$3146                       ;[3140] f2 46 31
                    jr        nc,$31ad                      ;[3143] 30 68
                    and       a                             ;[3145] a7
                    inc       a                             ;[3146] 3c
                    jr        nz,$3151                      ;[3147] 20 08
                    jr        c,$3151                       ;[3149] 38 06
                    exx                                     ;[314b] d9
                    bit       7,d                           ;[314c] cb 7a
                    exx                                     ;[314e] d9
                    jr        nz,$31ad                      ;[314f] 20 5c
                    ld        (hl),a                        ;[3151] 77
                    exx                                     ;[3152] d9
                    ld        a,b                           ;[3153] 78
                    exx                                     ;[3154] d9
                    jr        nc,$316c                      ;[3155] 30 15
                    ld        a,(hl)                        ;[3157] 7e
                    and       a                             ;[3158] a7
                    ld        a,$80                         ;[3159] 3e 80
                    jr        z,$315e                       ;[315b] 28 01
                    xor       a                             ;[315d] af
                    exx                                     ;[315e] d9
                    and       d                             ;[315f] a2
                    call      $2ffb                         ;[3160] cd fb 2f
                    rlca                                    ;[3163] 07
                    ld        (hl),a                        ;[3164] 77
                    jr        c,$3195                       ;[3165] 38 2e
                    inc       hl                            ;[3167] 23
                    ld        (hl),a                        ;[3168] 77
                    dec       hl                            ;[3169] 2b
                    jr        $3195                         ;[316a] 18 29
                    ld        b,$20                         ;[316c] 06 20
                    exx                                     ;[316e] d9
                    bit       7,d                           ;[316f] cb 7a
                    exx                                     ;[3171] d9
                    jr        nz,$3186                      ;[3172] 20 12
                    rlca                                    ;[3174] 07
                    rl        e                             ;[3175] cb 13
                    rl        d                             ;[3177] cb 12
                    exx                                     ;[3179] d9
                    rl        e                             ;[317a] cb 13
                    rl        d                             ;[317c] cb 12
                    exx                                     ;[317e] d9
                    dec       (hl)                          ;[317f] 35
                    jr        z,$3159                       ;[3180] 28 d7
                    djnz      $316e                         ;[3182] 10 ea
                    jr        $315d                         ;[3184] 18 d7
                    rla                                     ;[3186] 17
                    jr        nc,$3195                      ;[3187] 30 0c
                    call      $3004                         ;[3189] cd 04 30
                    jr        nz,$3195                      ;[318c] 20 07
                    exx                                     ;[318e] d9
                    ld        d,$80                         ;[318f] 16 80
                    exx                                     ;[3191] d9
                    inc       (hl)                          ;[3192] 34
                    jr        z,$31ad                       ;[3193] 28 18
                    push      hl                            ;[3195] e5
                    inc       hl                            ;[3196] 23
                    exx                                     ;[3197] d9
                    push      de                            ;[3198] d5
                    exx                                     ;[3199] d9
                    pop       bc                            ;[319a] c1
                    ld        a,b                           ;[319b] 78
                    rla                                     ;[319c] 17
                    rl        (hl)                          ;[319d] cb 16
                    rra                                     ;[319f] 1f
                    ld        (hl),a                        ;[31a0] 77
                    inc       hl                            ;[31a1] 23
                    ld        (hl),c                        ;[31a2] 71
                    inc       hl                            ;[31a3] 23
                    ld        (hl),d                        ;[31a4] 72
                    inc       hl                            ;[31a5] 23
                    ld        (hl),e                        ;[31a6] 73
                    pop       hl                            ;[31a7] e1
                    pop       de                            ;[31a8] d1
                    exx                                     ;[31a9] d9
                    pop       hl                            ;[31aa] e1
                    exx                                     ;[31ab] d9
                    ret                                     ;[31ac] c9

                    rst       $08                           ;[31ad] cf
                    dec       b                             ;[31ae] 05
                    call      $3293                         ;[31af] cd 93 32
                    ex        de,hl                         ;[31b2] eb
                    xor       a                             ;[31b3] af
                    call      $30c0                         ;[31b4] cd c0 30
                    jr        c,$31ad                       ;[31b7] 38 f4
                    ex        de,hl                         ;[31b9] eb
                    call      $30c0                         ;[31ba] cd c0 30
                    ret       c                             ;[31bd] d8
                    exx                                     ;[31be] d9
                    push      hl                            ;[31bf] e5
                    exx                                     ;[31c0] d9
                    push      de                            ;[31c1] d5
                    push      hl                            ;[31c2] e5
                    call      $2fba                         ;[31c3] cd ba 2f
                    exx                                     ;[31c6] d9
                    push      hl                            ;[31c7] e5
                    ld        h,b                           ;[31c8] 60
                    ld        l,c                           ;[31c9] 69
                    exx                                     ;[31ca] d9
                    ld        h,c                           ;[31cb] 61
                    ld        l,b                           ;[31cc] 68
                    xor       a                             ;[31cd] af
                    ld        b,$df                         ;[31ce] 06 df
                    jr        $31e2                         ;[31d0] 18 10
                    rla                                     ;[31d2] 17
                    rl        c                             ;[31d3] cb 11
                    exx                                     ;[31d5] d9
                    rl        c                             ;[31d6] cb 11
                    rl        b                             ;[31d8] cb 10
                    exx                                     ;[31da] d9
                    add       hl,hl                         ;[31db] 29
                    exx                                     ;[31dc] d9
                    adc       hl,hl                         ;[31dd] ed 6a
                    exx                                     ;[31df] d9
                    jr        c,$31f2                       ;[31e0] 38 10
                    sbc       hl,de                         ;[31e2] ed 52
                    exx                                     ;[31e4] d9
                    sbc       hl,de                         ;[31e5] ed 52
                    exx                                     ;[31e7] d9
                    jr        nc,$31f9                      ;[31e8] 30 0f
                    add       hl,de                         ;[31ea] 19
                    exx                                     ;[31eb] d9
                    adc       hl,de                         ;[31ec] ed 5a
                    exx                                     ;[31ee] d9
                    and       a                             ;[31ef] a7
                    jr        $31fa                         ;[31f0] 18 08
                    and       a                             ;[31f2] a7
                    sbc       hl,de                         ;[31f3] ed 52
                    exx                                     ;[31f5] d9
                    sbc       hl,de                         ;[31f6] ed 52
                    exx                                     ;[31f8] d9
                    scf                                     ;[31f9] 37
                    inc       b                             ;[31fa] 04
                    jp        m,$31d2                       ;[31fb] fa d2 31
                    push      af                            ;[31fe] f5
                    jr        z,$31db                       ;[31ff] 28 da
                    ld        e,a                           ;[3201] 5f
                    ld        d,c                           ;[3202] 51
                    exx                                     ;[3203] d9
                    ld        e,c                           ;[3204] 59
                    ld        d,b                           ;[3205] 50
                    pop       af                            ;[3206] f1
                    rr        b                             ;[3207] cb 18
                    pop       af                            ;[3209] f1
                    rr        b                             ;[320a] cb 18
                    exx                                     ;[320c] d9
                    pop       bc                            ;[320d] c1
                    pop       hl                            ;[320e] e1
                    ld        a,b                           ;[320f] 78
                    sub       c                             ;[3210] 91
                    jp        $313d                         ;[3211] c3 3d 31
                    ld        a,(hl)                        ;[3214] 7e
                    and       a                             ;[3215] a7
                    ret       z                             ;[3216] c8
                    cp        $81                           ;[3217] fe 81
                    jr        nc,$3221                      ;[3219] 30 06
                    ld        (hl),$00                      ;[321b] 36 00
                    ld        a,$20                         ;[321d] 3e 20
                    jr        $3272                         ;[321f] 18 51
                    cp        $91                           ;[3221] fe 91
                    jp        $323f                         ;[3223] c3 3f 32
                    jr        nc,$323b                      ;[3226] 30 13
                    xor       (hl)                          ;[3228] ae
                    ld        a,e                           ;[3229] 7b
                    jp        nz,$3036                      ;[322a] c2 36 30
                    ld        a,(hl)                        ;[322d] 7e
                    and       $80                           ;[322e] e6 80
                    dec       hl                            ;[3230] 2b
                    ld        (hl),$91                      ;[3231] 36 91
                    inc       hl                            ;[3233] 23
                    jp        $3036                         ;[3234] c3 36 30
                    inc       hl                            ;[3237] 23
                    ld        (hl),d                        ;[3238] 72
                    dec       hl                            ;[3239] 2b
                    dec       hl                            ;[323a] 2b
                    dec       hl                            ;[323b] 2b
                    pop       de                            ;[323c] d1
                    ret                                     ;[323d] c9

                    rst       $38                           ;[323e] ff
                    jr        nc,$326d                      ;[323f] 30 2c
                    push      de                            ;[3241] d5
                    cpl                                     ;[3242] 2f
                    add       $91                           ;[3243] c6 91
                    inc       hl                            ;[3245] 23
                    ld        d,(hl)                        ;[3246] 56
                    inc       hl                            ;[3247] 23
                    ld        e,(hl)                        ;[3248] 5e
                    dec       hl                            ;[3249] 2b
                    dec       hl                            ;[324a] 2b
                    ld        c,$00                         ;[324b] 0e 00
                    bit       7,d                           ;[324d] cb 7a
                    jr        z,$3252                       ;[324f] 28 01
                    dec       c                             ;[3251] 0d
                    set       7,d                           ;[3252] cb fa
                    ld        b,$08                         ;[3254] 06 08
                    sub       b                             ;[3256] 90
                    add       b                             ;[3257] 80
                    jr        c,$325e                       ;[3258] 38 04
                    ld        e,d                           ;[325a] 5a
                    ld        d,$00                         ;[325b] 16 00
                    sub       b                             ;[325d] 90
                    jr        z,$3267                       ;[325e] 28 07
                    ld        b,a                           ;[3260] 47
                    srl       d                             ;[3261] cb 3a
                    rr        e                             ;[3263] cb 1b
                    djnz      $3261                         ;[3265] 10 fa
                    call      $2d8e                         ;[3267] cd 8e 2d
                    pop       de                            ;[326a] d1
                    ret                                     ;[326b] c9

                    ld        a,(hl)                        ;[326c] 7e
                    sub       $a0                           ;[326d] d6 a0
                    ret       p                             ;[326f] f0
                    neg                                     ;[3270] ed 44
                    push      de                            ;[3272] d5
                    ex        de,hl                         ;[3273] eb
                    dec       hl                            ;[3274] 2b
                    ld        b,a                           ;[3275] 47
                    srl       b                             ;[3276] cb 38
                    srl       b                             ;[3278] cb 38
                    srl       b                             ;[327a] cb 38
                    jr        z,$3283                       ;[327c] 28 05
                    ld        (hl),$00                      ;[327e] 36 00
                    dec       hl                            ;[3280] 2b
                    djnz      $327e                         ;[3281] 10 fb
                    and       $07                           ;[3283] e6 07
                    jr        z,$3290                       ;[3285] 28 09
                    ld        b,a                           ;[3287] 47
                    ld        a,$ff                         ;[3288] 3e ff
                    sla       a                             ;[328a] cb 27
                    djnz      $328a                         ;[328c] 10 fc
                    and       (hl)                          ;[328e] a6
                    ld        (hl),a                        ;[328f] 77
                    ex        de,hl                         ;[3290] eb
                    pop       de                            ;[3291] d1
                    ret                                     ;[3292] c9

                    call      $3296                         ;[3293] cd 96 32
                    ex        de,hl                         ;[3296] eb
                    ld        a,(hl)                        ;[3297] 7e
                    and       a                             ;[3298] a7
                    ret       nz                            ;[3299] c0
                    push      de                            ;[329a] d5
                    call      $2d7f                         ;[329b] cd 7f 2d
                    xor       a                             ;[329e] af
                    inc       hl                            ;[329f] 23
                    ld        (hl),a                        ;[32a0] 77
                    dec       hl                            ;[32a1] 2b
                    ld        (hl),a                        ;[32a2] 77
                    ld        b,$91                         ;[32a3] 06 91
                    ld        a,d                           ;[32a5] 7a
                    and       a                             ;[32a6] a7
                    jr        nz,$32b1                      ;[32a7] 20 08
                    or        e                             ;[32a9] b3
                    ld        b,d                           ;[32aa] 42
                    jr        z,$32bd                       ;[32ab] 28 10
                    ld        d,e                           ;[32ad] 53
                    ld        e,b                           ;[32ae] 58
                    ld        b,$89                         ;[32af] 06 89
                    ex        de,hl                         ;[32b1] eb
                    dec       b                             ;[32b2] 05
                    add       hl,hl                         ;[32b3] 29
                    jr        nc,$32b2                      ;[32b4] 30 fc
                    rrc       c                             ;[32b6] cb 09
                    rr        h                             ;[32b8] cb 1c
                    rr        l                             ;[32ba] cb 1d
                    ex        de,hl                         ;[32bc] eb
                    dec       hl                            ;[32bd] 2b
                    ld        (hl),e                        ;[32be] 73
                    dec       hl                            ;[32bf] 2b
                    ld        (hl),d                        ;[32c0] 72
                    dec       hl                            ;[32c1] 2b
                    ld        (hl),b                        ;[32c2] 70
                    pop       de                            ;[32c3] d1
                    ret                                     ;[32c4] c9

                    push    $5b48                           ;[32c5] ed 8a 5b 48
                    push      bc                            ;[32c9] c5
                    jp        $5b3e                         ;[32ca] c3 3e 5b
                    push    $5b48                           ;[32cd] ed 8a 5b 48
                    push      hl                            ;[32d1] e5
                    jp        $5b3e                         ;[32d2] c3 3e 5b
                    rst       $38                           ;[32d5] ff
                    rst       $38                           ;[32d6] ff
                    nop                                     ;[32d7] 00
                    nop                                     ;[32d8] 00
                    ld        c,e                           ;[32d9] 4b
                    inc       a                             ;[32da] 3c
                    ld        h,c                           ;[32db] 61
                    inc       a                             ;[32dc] 3c
                    rrca                                    ;[32dd] 0f
                    jr        nc,$32aa                      ;[32de] 30 ca
                    jr        nc,$3291                      ;[32e0] 30 af
                    ld        sp,$3851                      ;[32e2] 31 51 38
                    dec       de                            ;[32e5] 1b
                    dec       (hl)                          ;[32e6] 35
                    inc       h                             ;[32e7] 24
                    dec       (hl)                          ;[32e8] 35
                    dec       sp                            ;[32e9] 3b
                    dec       (hl)                          ;[32ea] 35
                    dec       sp                            ;[32eb] 3b
                    dec       (hl)                          ;[32ec] 35
                    dec       sp                            ;[32ed] 3b
                    dec       (hl)                          ;[32ee] 35
                    dec       sp                            ;[32ef] 3b
                    dec       (hl)                          ;[32f0] 35
                    dec       sp                            ;[32f1] 3b
                    dec       (hl)                          ;[32f2] 35
                    dec       sp                            ;[32f3] 3b
                    dec       (hl)                          ;[32f4] 35
                    inc       d                             ;[32f5] 14
                    jr        nc,$3325                      ;[32f6] 30 2d
                    dec       (hl)                          ;[32f8] 35
                    dec       sp                            ;[32f9] 3b
                    dec       (hl)                          ;[32fa] 35
                    dec       sp                            ;[32fb] 3b
                    dec       (hl)                          ;[32fc] 35
                    dec       sp                            ;[32fd] 3b
                    dec       (hl)                          ;[32fe] 35
                    dec       sp                            ;[32ff] 3b
                    dec       (hl)                          ;[3300] 35
                    dec       sp                            ;[3301] 3b
                    dec       (hl)                          ;[3302] 35
                    dec       sp                            ;[3303] 3b
                    dec       (hl)                          ;[3304] 35
                    sbc       h                             ;[3305] 9c
                    dec       (hl)                          ;[3306] 35
                    sbc       $35                           ;[3307] de 35
                    cp        h                             ;[3309] bc
                    inc       (hl)                          ;[330a] 34
                    ld        b,l                           ;[330b] 45
                    ld        (hl),$6e                      ;[330c] 36 6e
                    inc       (hl)                          ;[330e] 34
                    ld        l,c                           ;[330f] 69
                    ld        (hl),$de                      ;[3310] 36 de
                    dec       (hl)                          ;[3312] 35
                    ld        (hl),h                        ;[3313] 74
                    ld        (hl),$b5                      ;[3314] 36 b5
                    scf                                     ;[3316] 37
                    xor       d                             ;[3317] aa
                    scf                                     ;[3318] 37
                    jp        c,$3337                       ;[3319] da 37 33
                    jr        c,$3361                       ;[331c] 38 43
                    jr        c,$3302                       ;[331e] 38 e2
                    scf                                     ;[3320] 37
                    inc       de                            ;[3321] 13
                    scf                                     ;[3322] 37
                    call      nz,$af36                      ;[3323] c4 36 af
                    ld        (hl),$4a                      ;[3326] 36 4a
                    jr        c,$32bc                       ;[3328] 38 92
                    inc       (hl)                          ;[332a] 34
                    ld        l,d                           ;[332b] 6a
                    inc       (hl)                          ;[332c] 34
                    xor       h                             ;[332d] ac
                    inc       (hl)                          ;[332e] 34
                    and       l                             ;[332f] a5
                    inc       (hl)                          ;[3330] 34
                    or        e                             ;[3331] b3
                    inc       (hl)                          ;[3332] 34
                    rra                                     ;[3333] 1f
                    ld        (hl),$c9                      ;[3334] 36 c9
                    dec       (hl)                          ;[3336] 35
                    ld        bc,$7135                      ;[3337] 01 35 71
                    djnz      $33b3                         ;[333a] 10 77
                    inc       a                             ;[333c] 3c
                    ld        b,(hl)                        ;[333d] 46
                    add       hl,sp                         ;[333e] 39
                    ld        (bc),a                        ;[333f] 02
                    add       hl,sp                         ;[3340] 39
                    ld        (bc),a                        ;[3341] 02
                    add       hl,sp                         ;[3342] 39
                    ld        (bc),a                        ;[3343] 02
                    add       hl,sp                         ;[3344] 39
                    cp        c                             ;[3345] b9
                    inc       (hl)                          ;[3346] 34
                    cp        d                             ;[3347] ba
                    jr        c,$334a                       ;[3348] 38 00
                    nop                                     ;[334a] 00
                    nop                                     ;[334b] 00
                    nop                                     ;[334c] 00
                    ld        h,$09                         ;[334d] 26 09
                    rst       $30                           ;[334f] f7
                    daa                                     ;[3350] 27
                    sub       c                             ;[3351] 91
                    inc       a                             ;[3352] 3c
                    adc       e                             ;[3353] 8b
                    inc       a                             ;[3354] 3c
                    add       l                             ;[3355] 85
                    inc       a                             ;[3356] 3c
                    rst       $38                           ;[3357] ff
                    rst       $38                           ;[3358] ff
                    rst       $38                           ;[3359] ff
                    rst       $38                           ;[335a] ff
                    call      $35bf                         ;[335b] cd bf 35
                    ld        a,b                           ;[335e] 78
                    ld        ($5c67),a                     ;[335f] 32 67 5c
                    exx                                     ;[3362] d9
                    ex        (sp),hl                       ;[3363] e3
                    exx                                     ;[3364] d9
                    ld        ($5c65),de                    ;[3365] ed 53 65 5c
                    exx                                     ;[3369] d9
                    ld        a,(hl)                        ;[336a] 7e
                    inc       hl                            ;[336b] 23
                    push      hl                            ;[336c] e5
                    and       a                             ;[336d] a7
                    jp        p,$3380                       ;[336e] f2 80 33
                    ld        d,a                           ;[3371] 57
                    and       $60                           ;[3372] e6 60
                    swapnib                                 ;[3374] ed 23
                    add       $7c                           ;[3376] c6 7c
                    ld        l,a                           ;[3378] 6f
                    ld        a,d                           ;[3379] 7a
                    and       $1f                           ;[337a] e6 1f
                    jp        $338e                         ;[337c] c3 8e 33
                    nop                                     ;[337f] 00
                    cp        $18                           ;[3380] fe 18
                    jr        nc,$338c                      ;[3382] 30 08
                    exx                                     ;[3384] d9
                    ld        d,h                           ;[3385] 54
                    ld        e,l                           ;[3386] 5d
                    add       hl,$fffb                      ;[3387] ed 34 fb ff
                    exx                                     ;[338b] d9
                    rlca                                    ;[338c] 07
                    ld        l,a                           ;[338d] 6f
                    ld        h,$00                         ;[338e] 26 00
                    add       hl,$3b02                      ;[3390] ed 34 02 3b
                    ld        e,(hl)                        ;[3394] 5e
                    inc       hl                            ;[3395] 23
                    ld        d,(hl)                        ;[3396] 56
                    ld        hl,$3365                      ;[3397] 21 65 33
                    ex        (sp),hl                       ;[339a] e3
                    push      de                            ;[339b] d5
                    exx                                     ;[339c] d9
                    ld        bc,($5c66)                    ;[339d] ed 4b 66 5c
                    ret                                     ;[33a1] c9

                    pop       af                            ;[33a2] f1
                    ld        a,($5c67)                     ;[33a3] 3a 67 5c
                    exx                                     ;[33a6] d9
                    jr        $336c                         ;[33a7] 18 c3
                    push      de                            ;[33a9] d5
                    push      hl                            ;[33aa] e5
                    ld        bc,$0005                      ;[33ab] 01 05 00
                    call      $1f05                         ;[33ae] cd 05 1f
                    pop       hl                            ;[33b1] e1
                    pop       de                            ;[33b2] d1
                    ret                                     ;[33b3] c9

                    ld        de,($5c65)                    ;[33b4] ed 5b 65 5c
                    call      $3c2e                         ;[33b8] cd 2e 3c
                    ld        ($5c65),de                    ;[33bb] ed 53 65 5c
                    ret                                     ;[33bf] c9

                    call      $33a9                         ;[33c0] cd a9 33
                    ldir                                    ;[33c3] ed b0
                    ret                                     ;[33c5] c9

                    ld        h,d                           ;[33c6] 62
                    ld        l,e                           ;[33c7] 6b
                    call      $3c1b                         ;[33c8] cd 1b 3c
                    exx                                     ;[33cb] d9
                    push      hl                            ;[33cc] e5
                    exx                                     ;[33cd] d9
                    ex        (sp),hl                       ;[33ce] e3
                    ld        a,(hl)                        ;[33cf] 7e
                    and       $c0                           ;[33d0] e6 c0
                    rlca                                    ;[33d2] 07
                    rlca                                    ;[33d3] 07
                    ld        c,a                           ;[33d4] 4f
                    inc       c                             ;[33d5] 0c
                    ld        a,(hl)                        ;[33d6] 7e
                    and       $3f                           ;[33d7] e6 3f
                    jr        nz,$33dd                      ;[33d9] 20 02
                    inc       hl                            ;[33db] 23
                    ld        a,(hl)                        ;[33dc] 7e
                    add       $50                           ;[33dd] c6 50
                    ld        (de),a                        ;[33df] 12
                    ld        a,$05                         ;[33e0] 3e 05
                    sub       c                             ;[33e2] 91
                    inc       hl                            ;[33e3] 23
                    inc       de                            ;[33e4] 13
                    ld        b,$00                         ;[33e5] 06 00
                    ldir                                    ;[33e7] ed b0
                    ex        (sp),hl                       ;[33e9] e3
                    exx                                     ;[33ea] d9
                    pop       hl                            ;[33eb] e1
                    exx                                     ;[33ec] d9
                    ld        b,a                           ;[33ed] 47
                    xor       a                             ;[33ee] af
                    dec       b                             ;[33ef] 05
                    ret       z                             ;[33f0] c8
                    ld        (de),a                        ;[33f1] 12
                    inc       de                            ;[33f2] 13
                    jr        $33ef                         ;[33f3] 18 fa
                    rst       $38                           ;[33f5] ff
                    rst       $38                           ;[33f6] ff
                    rst       $38                           ;[33f7] ff
                    rst       $38                           ;[33f8] ff
                    rst       $38                           ;[33f9] ff
                    rst       $38                           ;[33fa] ff
                    rst       $38                           ;[33fb] ff
                    rst       $38                           ;[33fc] ff
                    rst       $38                           ;[33fd] ff
                    rst       $38                           ;[33fe] ff
                    rst       $38                           ;[33ff] ff
                    rst       $38                           ;[3400] ff
                    rst       $38                           ;[3401] ff
                    rst       $38                           ;[3402] ff
                    rst       $38                           ;[3403] ff
                    rst       $38                           ;[3404] ff
                    rst       $38                           ;[3405] ff
                    ld        c,a                           ;[3406] 4f
                    rlca                                    ;[3407] 07
                    rlca                                    ;[3408] 07
                    add       c                             ;[3409] 81
                    add       hl,a                          ;[340a] ed 31
                    ret                                     ;[340c] c9

                    rst       $38                           ;[340d] ff
                    rst       $38                           ;[340e] ff
                    push      de                            ;[340f] d5
                    ld        hl,($5c68)                    ;[3410] 2a 68 5c
                    call      $3406                         ;[3413] cd 06 34
                    call      $3c2e                         ;[3416] cd 2e 3c
                    pop       hl                            ;[3419] e1
                    ret                                     ;[341a] c9

                    ld        hl,$3c02                      ;[341b] 21 02 3c
                    ld        c,a                           ;[341e] 4f
                    add       a                             ;[341f] 87
                    add       a                             ;[3420] 87
                    add       c                             ;[3421] 81
                    add       hl,a                          ;[3422] ed 31
                    push      de                            ;[3424] d5
                    call      $3c2e                         ;[3425] cd 2e 3c
                    pop       hl                            ;[3428] e1
                    ret                                     ;[3429] c9

                    rst       $38                           ;[342a] ff
                    rst       $38                           ;[342b] ff
                    rst       $38                           ;[342c] ff
                    push      hl                            ;[342d] e5
                    ex        de,hl                         ;[342e] eb
                    ld        hl,($5c68)                    ;[342f] 2a 68 5c
                    call      $3406                         ;[3432] cd 06 34
                    ex        de,hl                         ;[3435] eb
                    call      $3c40                         ;[3436] cd 40 3c
                    ex        de,hl                         ;[3439] eb
                    pop       hl                            ;[343a] e1
                    ret                                     ;[343b] c9

                    ld        b,$05                         ;[343c] 06 05
                    ld        a,(de)                        ;[343e] 1a
                    ld        c,(hl)                        ;[343f] 4e
                    ex        de,hl                         ;[3440] eb
                    ld        (de),a                        ;[3441] 12
                    ld        (hl),c                        ;[3442] 71
                    inc       hl                            ;[3443] 23
                    inc       de                            ;[3444] 13
                    djnz      $343e                         ;[3445] 10 f7
                    ex        de,hl                         ;[3447] eb
                    ret                                     ;[3448] c9

                    ld        b,a                           ;[3449] 47
                    call      $335e                         ;[344a] cd 5e 33
                    ld        sp,$c00f                      ;[344d] 31 0f c0
                    ld        (bc),a                        ;[3450] 02
                    and       b                             ;[3451] a0
                    jp        nz,$e031                      ;[3452] c2 31 e0
                    inc       b                             ;[3455] 04
                    jp        po,$03c1                      ;[3456] e2 c1 03
                    jr        c,$3428                       ;[3459] 38 cd
                    add       $33                           ;[345b] c6 33
                    call      $3362                         ;[345d] cd 62 33
                    rrca                                    ;[3460] 0f
                    ld        bc,$02c2                      ;[3461] 01 c2 02
                    dec       (hl)                          ;[3464] 35
                    xor       $e1                           ;[3465] ee e1
                    inc       bc                            ;[3467] 03
                    jr        c,$3433                       ;[3468] 38 c9
                    ld        b,$ff                         ;[346a] 06 ff
                    jr        $3474                         ;[346c] 18 06
                    call      $34e9                         ;[346e] cd e9 34
                    ret       c                             ;[3471] d8
                    ld        b,$00                         ;[3472] 06 00
                    ld        a,(hl)                        ;[3474] 7e
                    and       a                             ;[3475] a7
                    jr        z,$3483                       ;[3476] 28 0b
                    inc       hl                            ;[3478] 23
                    ld        a,b                           ;[3479] 78
                    and       $80                           ;[347a] e6 80
                    or        (hl)                          ;[347c] b6
                    rla                                     ;[347d] 17
                    ccf                                     ;[347e] 3f
                    rra                                     ;[347f] 1f
                    ld        (hl),a                        ;[3480] 77
                    dec       hl                            ;[3481] 2b
                    ret                                     ;[3482] c9

                    push      de                            ;[3483] d5
                    push      hl                            ;[3484] e5
                    call      $2d7f                         ;[3485] cd 7f 2d
                    pop       hl                            ;[3488] e1
                    ld        a,b                           ;[3489] 78
                    or        c                             ;[348a] b1
                    cpl                                     ;[348b] 2f
                    ld        c,a                           ;[348c] 4f
                    call      $2d8e                         ;[348d] cd 8e 2d
                    pop       de                            ;[3490] d1
                    ret                                     ;[3491] c9

                    call      $34e9                         ;[3492] cd e9 34
                    ret       c                             ;[3495] d8
                    push      de                            ;[3496] d5
                    ld        de,$0001                      ;[3497] 11 01 00
                    inc       hl                            ;[349a] 23
                    rl        (hl)                          ;[349b] cb 16
                    dec       hl                            ;[349d] 2b
                    sbc       a                             ;[349e] 9f
                    ld        c,a                           ;[349f] 4f
                    call      $2d8e                         ;[34a0] cd 8e 2d
                    pop       de                            ;[34a3] d1
                    ret                                     ;[34a4] c9

                    call      $1e99                         ;[34a5] cd 99 1e
                    in        a,(c)                         ;[34a8] ed 78
                    jr        $34b0                         ;[34aa] 18 04
                    call      $1e99                         ;[34ac] cd 99 1e
                    ld        a,(bc)                        ;[34af] 0a
                    jp        $2d28                         ;[34b0] c3 28 2d
                    scf                                     ;[34b3] 37
                    ld        d,$fe                         ;[34b4] 16 fe
                    jp        $218c                         ;[34b6] c3 8c 21
                    and       a                             ;[34b9] a7
                    jr        $34b4                         ;[34ba] 18 f8
                    call      $2bf1                         ;[34bc] cd f1 2b
                    dec       bc                            ;[34bf] 0b
                    ld        a,b                           ;[34c0] 78
                    or        c                             ;[34c1] b1
                    jr        nz,$34e7                      ;[34c2] 20 23
                    ld        a,(de)                        ;[34c4] 1a
                    call      $3bdf                         ;[34c5] cd df 3b
                    sub       $90                           ;[34c8] d6 90
                    jr        c,$34e7                       ;[34ca] 38 1b
                    ld        h,$00                         ;[34cc] 26 00
                    ld        l,a                           ;[34ce] 6f
                    add       hl,hl                         ;[34cf] 29
                    add       hl,hl                         ;[34d0] 29
                    add       hl,hl                         ;[34d1] 29
                    ld        bc,($5c7b)                    ;[34d2] ed 4b 7b 5c
                    add       hl,bc                         ;[34d6] 09
                    ld        b,h                           ;[34d7] 44
                    ld        c,l                           ;[34d8] 4d
                    jr        nc,$34df                      ;[34d9] 30 04
                    cp        $15                           ;[34db] fe 15
                    jr        nc,$34e7                      ;[34dd] 30 08
                    jp        $2d2f                         ;[34df] c3 2f 2d
                    push      hl                            ;[34e2] e5
                    jp        $1601                         ;[34e3] c3 01 16
                    nop                                     ;[34e6] 00
                    rst       $08                           ;[34e7] cf
                    add       hl,bc                         ;[34e8] 09
                    push      hl                            ;[34e9] e5
                    push      bc                            ;[34ea] c5
                    ld        b,a                           ;[34eb] 47
                    ld        a,(hl)                        ;[34ec] 7e
                    inc       hl                            ;[34ed] 23
                    or        (hl)                          ;[34ee] b6
                    inc       hl                            ;[34ef] 23
                    or        (hl)                          ;[34f0] b6
                    inc       hl                            ;[34f1] 23
                    or        (hl)                          ;[34f2] b6
                    ld        a,b                           ;[34f3] 78
                    pop       bc                            ;[34f4] c1
                    pop       hl                            ;[34f5] e1
                    ret       nz                            ;[34f6] c0
                    scf                                     ;[34f7] 37
                    ret                                     ;[34f8] c9

                    call      $34e9                         ;[34f9] cd e9 34
                    ret       c                             ;[34fc] d8
                    ld        a,$ff                         ;[34fd] 3e ff
                    jr        $3507                         ;[34ff] 18 06
                    call      $34e9                         ;[3501] cd e9 34
                    jr        $350b                         ;[3504] 18 05
                    xor       a                             ;[3506] af
                    inc       hl                            ;[3507] 23
                    xor       (hl)                          ;[3508] ae
                    dec       hl                            ;[3509] 2b
                    rlca                                    ;[350a] 07
                    push      hl                            ;[350b] e5
                    ld        a,$00                         ;[350c] 3e 00
                    ld        (hl),a                        ;[350e] 77
                    inc       hl                            ;[350f] 23
                    ld        (hl),a                        ;[3510] 77
                    inc       hl                            ;[3511] 23
                    rla                                     ;[3512] 17
                    ld        (hl),a                        ;[3513] 77
                    rra                                     ;[3514] 1f
                    inc       hl                            ;[3515] 23
                    ld        (hl),a                        ;[3516] 77
                    inc       hl                            ;[3517] 23
                    ld        (hl),a                        ;[3518] 77
                    pop       hl                            ;[3519] e1
                    ret                                     ;[351a] c9

                    ex        de,hl                         ;[351b] eb
                    call      $34e9                         ;[351c] cd e9 34
                    ex        de,hl                         ;[351f] eb
                    ret       c                             ;[3520] d8
                    scf                                     ;[3521] 37
                    jr        $350b                         ;[3522] 18 e7
                    ex        de,hl                         ;[3524] eb
                    call      $34e9                         ;[3525] cd e9 34
                    ex        de,hl                         ;[3528] eb
                    ret       nc                            ;[3529] d0
                    and       a                             ;[352a] a7
                    jr        $350b                         ;[352b] 18 de
                    ex        de,hl                         ;[352d] eb
                    call      $34e9                         ;[352e] cd e9 34
                    ex        de,hl                         ;[3531] eb
                    ret       nc                            ;[3532] d0
                    push      de                            ;[3533] d5
                    dec       de                            ;[3534] 1b
                    xor       a                             ;[3535] af
                    ld        (de),a                        ;[3536] 12
                    dec       de                            ;[3537] 1b
                    ld        (de),a                        ;[3538] 12
                    pop       de                            ;[3539] d1
                    ret                                     ;[353a] c9

                    ld        a,b                           ;[353b] 78
                    sub       $08                           ;[353c] d6 08
                    bit       2,a                           ;[353e] cb 57
                    jr        nz,$3543                      ;[3540] 20 01
                    dec       a                             ;[3542] 3d
                    rrca                                    ;[3543] 0f
                    jr        nc,$354e                      ;[3544] 30 08
                    push      af                            ;[3546] f5
                    push      hl                            ;[3547] e5
                    call      $343c                         ;[3548] cd 3c 34
                    pop       de                            ;[354b] d1
                    ex        de,hl                         ;[354c] eb
                    pop       af                            ;[354d] f1
                    bit       2,a                           ;[354e] cb 57
                    jr        nz,$3559                      ;[3550] 20 07
                    rrca                                    ;[3552] 0f
                    push      af                            ;[3553] f5
                    call      $300f                         ;[3554] cd 0f 30
                    jr        $358c                         ;[3557] 18 33
                    rrca                                    ;[3559] 0f
                    push      af                            ;[355a] f5
                    call      $2bf1                         ;[355b] cd f1 2b
                    push      de                            ;[355e] d5
                    push      bc                            ;[355f] c5
                    call      $2bf1                         ;[3560] cd f1 2b
                    pop       hl                            ;[3563] e1
                    ld        a,h                           ;[3564] 7c
                    or        l                             ;[3565] b5
                    ex        (sp),hl                       ;[3566] e3
                    ld        a,b                           ;[3567] 78
                    jr        nz,$3575                      ;[3568] 20 0b
                    or        c                             ;[356a] b1
                    pop       bc                            ;[356b] c1
                    jr        z,$3572                       ;[356c] 28 04
                    pop       af                            ;[356e] f1
                    ccf                                     ;[356f] 3f
                    jr        $3588                         ;[3570] 18 16
                    pop       af                            ;[3572] f1
                    jr        $3588                         ;[3573] 18 13
                    or        c                             ;[3575] b1
                    jr        z,$3585                       ;[3576] 28 0d
                    ld        a,(de)                        ;[3578] 1a
                    sub       (hl)                          ;[3579] 96
                    jr        c,$3585                       ;[357a] 38 09
                    jr        nz,$356b                      ;[357c] 20 ed
                    dec       bc                            ;[357e] 0b
                    inc       de                            ;[357f] 13
                    inc       hl                            ;[3580] 23
                    ex        (sp),hl                       ;[3581] e3
                    dec       hl                            ;[3582] 2b
                    jr        $3564                         ;[3583] 18 df
                    pop       bc                            ;[3585] c1
                    pop       af                            ;[3586] f1
                    and       a                             ;[3587] a7
                    push      af                            ;[3588] f5
                    rst       $28                           ;[3589] ef
                    and       b                             ;[358a] a0
                    jr        c,$357e                       ;[358b] 38 f1
                    push      af                            ;[358d] f5
                    call      c,$3501                       ;[358e] dc 01 35
                    pop       af                            ;[3591] f1
                    push      af                            ;[3592] f5
                    call      nc,$34f9                      ;[3593] d4 f9 34
                    pop       af                            ;[3596] f1
                    rrca                                    ;[3597] 0f
                    call      nc,$3501                      ;[3598] d4 01 35
                    ret                                     ;[359b] c9

                    call      $2bf1                         ;[359c] cd f1 2b
                    push      de                            ;[359f] d5
                    push      bc                            ;[35a0] c5
                    call      $2bf1                         ;[35a1] cd f1 2b
                    pop       hl                            ;[35a4] e1
                    push      hl                            ;[35a5] e5
                    push      de                            ;[35a6] d5
                    push      bc                            ;[35a7] c5
                    add       hl,bc                         ;[35a8] 09
                    ld        b,h                           ;[35a9] 44
                    ld        c,l                           ;[35aa] 4d
                    rst       $30                           ;[35ab] f7
                    call      $2ab2                         ;[35ac] cd b2 2a
                    pop       bc                            ;[35af] c1
                    pop       hl                            ;[35b0] e1
                    ld        a,b                           ;[35b1] 78
                    or        c                             ;[35b2] b1
                    jr        z,$35b7                       ;[35b3] 28 02
                    ldir                                    ;[35b5] ed b0
                    pop       bc                            ;[35b7] c1
                    pop       hl                            ;[35b8] e1
                    ld        a,b                           ;[35b9] 78
                    or        c                             ;[35ba] b1
                    jr        z,$35bf                       ;[35bb] 28 02
                    ldir                                    ;[35bd] ed b0
                    ld        hl,($5c65)                    ;[35bf] 2a 65 5c
                    ld        d,h                           ;[35c2] 54
                    ld        e,l                           ;[35c3] 5d
                    add       hl,$fffb                      ;[35c4] ed 34 fb ff
                    ret                                     ;[35c8] c9

                    call      $2dd5                         ;[35c9] cd d5 2d
                    jr        c,$35dc                       ;[35cc] 38 0e
                    jr        nz,$35dc                      ;[35ce] 20 0c
                    push      af                            ;[35d0] f5
                    ld        bc,$0001                      ;[35d1] 01 01 00
                    rst       $30                           ;[35d4] f7
                    pop       af                            ;[35d5] f1
                    ld        (de),a                        ;[35d6] 12
                    call      $2ab2                         ;[35d7] cd b2 2a
                    ex        de,hl                         ;[35da] eb
                    ret                                     ;[35db] c9

                    rst       $08                           ;[35dc] cf
                    ld        a,(bc)                        ;[35dd] 0a
                    ld        hl,($5c5d)                    ;[35de] 2a 5d 5c
                    push      hl                            ;[35e1] e5
                    ld        a,b                           ;[35e2] 78
                    add       $e3                           ;[35e3] c6 e3
                    sbc       a                             ;[35e5] 9f
                    push      af                            ;[35e6] f5
                    call      $2bf1                         ;[35e7] cd f1 2b
                    push      de                            ;[35ea] d5
                    inc       bc                            ;[35eb] 03
                    rst       $30                           ;[35ec] f7
                    pop       hl                            ;[35ed] e1
                    ld        ($5c5d),de                    ;[35ee] ed 53 5d 5c
                    push      de                            ;[35f2] d5
                    ldir                                    ;[35f3] ed b0
                    ex        de,hl                         ;[35f5] eb
                    dec       hl                            ;[35f6] 2b
                    ld        (hl),$0d                      ;[35f7] 36 0d
                    res       7,(iy+$01)                    ;[35f9] fd cb 01 be
                    call      $24fb                         ;[35fd] cd fb 24
                    rst       $18                           ;[3600] df
                    cp        $0d                           ;[3601] fe 0d
                    jr        nz,$360c                      ;[3603] 20 07
                    pop       hl                            ;[3605] e1
                    pop       af                            ;[3606] f1
                    xor       (iy+$01)                      ;[3607] fd ae 01
                    and       $40                           ;[360a] e6 40
                    jp        nz,$1c8a                      ;[360c] c2 8a 1c
                    ld        ($5c5d),hl                    ;[360f] 22 5d 5c
                    set       7,(iy+$01)                    ;[3612] fd cb 01 fe
                    call      $24fb                         ;[3616] cd fb 24
                    pop       hl                            ;[3619] e1
                    ld        ($5c5d),hl                    ;[361a] 22 5d 5c
                    jr        $35bf                         ;[361d] 18 a0
                    ld        de,$2de3                      ;[361f] 11 e3 2d
                    push      de                            ;[3622] d5
                    ld        bc,$0001                      ;[3623] 01 01 00
                    rst       $30                           ;[3626] f7
                    ld        ($5c5b),hl                    ;[3627] 22 5b 5c
                    ex        (sp),hl                       ;[362a] e3
                    push      hl                            ;[362b] e5
                    ld        hl,($5c51)                    ;[362c] 2a 51 5c
                    ex        (sp),hl                       ;[362f] e3
                    ld        a,$ff                         ;[3630] 3e ff
                    call      $34e2                         ;[3632] cd e2 34
                    pop       hl                            ;[3635] e1
                    call      $1615                         ;[3636] cd 15 16
                    pop       de                            ;[3639] d1
                    ld        hl,($5c5b)                    ;[363a] 2a 5b 5c
                    and       a                             ;[363d] a7
                    sbc       hl,de                         ;[363e] ed 52
                    ld        b,h                           ;[3640] 44
                    ld        c,l                           ;[3641] 4d
                    jp        $35d7                         ;[3642] c3 d7 35
                    call      $1e94                         ;[3645] cd 94 1e
                    cp        $10                           ;[3648] fe 10
                    jp        nc,$1e9f                      ;[364a] d2 9f 1e
                    ld        hl,($5c51)                    ;[364d] 2a 51 5c
                    push      hl                            ;[3650] e5
                    call      $1601                         ;[3651] cd 01 16
                    call      $15e6                         ;[3654] cd e6 15
                    ld        bc,$0000                      ;[3657] 01 00 00
                    jr        nc,$365f                      ;[365a] 30 03
                    inc       c                             ;[365c] 0c
                    rst       $30                           ;[365d] f7
                    ld        (de),a                        ;[365e] 12
                    call      $2ab2                         ;[365f] cd b2 2a
                    pop       hl                            ;[3662] e1
                    call      $1615                         ;[3663] cd 15 16
                    jp        $35bf                         ;[3666] c3 bf 35
                    call      $2bf1                         ;[3669] cd f1 2b
                    ld        a,b                           ;[366c] 78
                    or        c                             ;[366d] b1
                    jr        z,$3671                       ;[366e] 28 01
                    ld        a,(de)                        ;[3670] 1a
                    jp        $2d28                         ;[3671] c3 28 2d
                    call      $2bf1                         ;[3674] cd f1 2b
                    jp        $2d2f                         ;[3677] c3 2f 2d
                    exx                                     ;[367a] d9
                    push      hl                            ;[367b] e5
                    ld        hl,$5c67                      ;[367c] 21 67 5c
                    dec       (hl)                          ;[367f] 35
                    pop       hl                            ;[3680] e1
                    jr        nz,$3687                      ;[3681] 20 04
                    inc       hl                            ;[3683] 23
                    exx                                     ;[3684] d9
                    ret                                     ;[3685] c9

                    exx                                     ;[3686] d9
                    ld        e,(hl)                        ;[3687] 5e
                    ld        a,e                           ;[3688] 7b
                    rla                                     ;[3689] 17
                    sbc       a                             ;[368a] 9f
                    ld        d,a                           ;[368b] 57
                    add       hl,de                         ;[368c] 19
                    exx                                     ;[368d] d9
                    ret                                     ;[368e] c9

                    inc       de                            ;[368f] 13
                    inc       de                            ;[3690] 13
                    ld        a,(de)                        ;[3691] 1a
                    dec       de                            ;[3692] 1b
                    dec       de                            ;[3693] 1b
                    and       a                             ;[3694] a7
                    jr        nz,$3686                      ;[3695] 20 ef
                    exx                                     ;[3697] d9
                    inc       hl                            ;[3698] 23
                    exx                                     ;[3699] d9
                    ret                                     ;[369a] c9

                    pop       af                            ;[369b] f1
                    exx                                     ;[369c] d9
                    ex        (sp),hl                       ;[369d] e3
                    exx                                     ;[369e] d9
                    ret                                     ;[369f] c9

                    rst       $28                           ;[36a0] ef
                    ret       nz                            ;[36a1] c0
                    ld        (bc),a                        ;[36a2] 02
                    ld        sp,$05e0                      ;[36a3] 31 e0 05
                    daa                                     ;[36a6] 27
                    ret       po                            ;[36a7] e0
                    ld        bc,$04c0                      ;[36a8] 01 c0 04
                    inc       bc                            ;[36ab] 03
                    ret       po                            ;[36ac] e0
                    jr        c,$3678                       ;[36ad] 38 c9
                    rst       $28                           ;[36af] ef
                    ld        sp,$0036                      ;[36b0] 31 36 00
                    inc       b                             ;[36b3] 04
                    ld        a,($c938)                     ;[36b4] 3a 38 c9
                    ld        sp,$c03a                      ;[36b7] 31 3a c0
                    inc       bc                            ;[36ba] 03
                    ret       po                            ;[36bb] e0
                    ld        bc,$0030                      ;[36bc] 01 30 00
                    inc       bc                            ;[36bf] 03
                    and       c                             ;[36c0] a1
                    inc       bc                            ;[36c1] 03
                    jr        c,$368d                       ;[36c2] 38 c9
                    rst       $28                           ;[36c4] ef
                    dec       a                             ;[36c5] 3d
                    inc       (hl)                          ;[36c6] 34
                    pop       af                            ;[36c7] f1
                    jr        c,$3674                       ;[36c8] 38 aa
                    dec       sp                            ;[36ca] 3b
                    add       hl,hl                         ;[36cb] 29
                    inc       b                             ;[36cc] 04
                    ld        sp,$c327                      ;[36cd] 31 27 c3
                    inc       bc                            ;[36d0] 03
                    ld        sp,$a10f                      ;[36d1] 31 0f a1
                    inc       bc                            ;[36d4] 03
                    adc       b                             ;[36d5] 88
                    inc       de                            ;[36d6] 13
                    ld        (hl),$58                      ;[36d7] 36 58
                    ld        h,l                           ;[36d9] 65
                    ld        h,(hl)                        ;[36da] 66
                    sbc       l                             ;[36db] 9d
                    ld        a,b                           ;[36dc] 78
                    ld        h,l                           ;[36dd] 65
                    ld        b,b                           ;[36de] 40
                    and       d                             ;[36df] a2
                    ld        h,b                           ;[36e0] 60
                    ld        ($e7c9),a                     ;[36e1] 32 c9 e7
                    ld        hl,$aff7                      ;[36e4] 21 f7 af
                    inc       h                             ;[36e7] 24
                    ex        de,hl                         ;[36e8] eb
                    cpl                                     ;[36e9] 2f
                    or        b                             ;[36ea] b0
                    or        b                             ;[36eb] b0
                    inc       d                             ;[36ec] 14
                    xor       $7e                           ;[36ed] ee 7e
                    cp        e                             ;[36ef] bb
                    sub       h                             ;[36f0] 94
                    ld        e,b                           ;[36f1] 58
                    pop       af                            ;[36f2] f1
                    ld        a,($f87e)                     ;[36f3] 3a 7e f8
                    rst       $08                           ;[36f6] cf
                    ex        (sp),hl                       ;[36f7] e3
                    jr        c,$36c7                       ;[36f8] 38 cd
                    push      de                            ;[36fa] d5
                    dec       l                             ;[36fb] 2d
                    jr        nz,$3705                      ;[36fc] 20 07
                    jr        c,$3703                       ;[36fe] 38 03
                    add       (hl)                          ;[3700] 86
                    jr        nc,$370c                      ;[3701] 30 09
                    rst       $08                           ;[3703] cf
                    dec       b                             ;[3704] 05
                    jr        c,$370e                       ;[3705] 38 07
                    sub       (hl)                          ;[3707] 96
                    jr        nc,$370e                      ;[3708] 30 04
                    neg                                     ;[370a] ed 44
                    ld        (hl),a                        ;[370c] 77
                    ret                                     ;[370d] c9

                    rst       $28                           ;[370e] ef
                    ld        (bc),a                        ;[370f] 02
                    and       b                             ;[3710] a0
                    jr        c,$36dc                       ;[3711] 38 c9
                    rst       $28                           ;[3713] ef
                    dec       a                             ;[3714] 3d
                    ld        sp,$0037                      ;[3715] 31 37 00
                    inc       b                             ;[3718] 04
                    jr        c,$36ea                       ;[3719] 38 cf
                    add       hl,bc                         ;[371b] 09
                    and       b                             ;[371c] a0
                    ld        (bc),a                        ;[371d] 02
                    jr        c,$379e                       ;[371e] 38 7e
                    ld        (hl),$80                      ;[3720] 36 80
                    call      $2d28                         ;[3722] cd 28 2d
                    rst       $28                           ;[3725] ef
                    inc       (hl)                          ;[3726] 34
                    jr        c,$3729                       ;[3727] 38 00
                    inc       bc                            ;[3729] 03
                    ld        bc,$3431                      ;[372a] 01 31 34
                    ret       p                             ;[372d] f0
                    ld        c,h                           ;[372e] 4c
                    call      z,$cdcc                       ;[372f] cc cc cd
                    inc       bc                            ;[3732] 03
                    scf                                     ;[3733] 37
                    nop                                     ;[3734] 00
                    ex        af,af'                        ;[3735] 08
                    ld        bc,$03a1                      ;[3736] 01 a1 03
                    ld        bc,$3438                      ;[3739] 01 38 34
                    rst       $28                           ;[373c] ef
                    ld        bc,$f034                      ;[373d] 01 34 f0
                    ld        sp,$1772                      ;[3740] 31 72 17
                    ret       m                             ;[3743] f8
                    inc       b                             ;[3744] 04
                    ld        bc,$03a2                      ;[3745] 01 a2 03
                    and       d                             ;[3748] a2
                    inc       bc                            ;[3749] 03
                    ld        sp,$3234                      ;[374a] 31 34 32
                    jr        nz,$3753                      ;[374d] 20 04
                    and       d                             ;[374f] a2
                    inc       bc                            ;[3750] 03
                    adc       h                             ;[3751] 8c
                    ld        de,$14ac                      ;[3752] 11 ac 14
                    add       hl,bc                         ;[3755] 09
                    ld        d,(hl)                        ;[3756] 56
                    jp        c,$59a5                       ;[3757] da a5 59
                    jr        nc,$3721                      ;[375a] 30 c5
                    ld        e,h                           ;[375c] 5c
                    sub       b                             ;[375d] 90
                    xor       d                             ;[375e] aa
                    sbc       (hl)                          ;[375f] 9e
                    ld        (hl),b                        ;[3760] 70
                    ld        l,a                           ;[3761] 6f
                    ld        h,c                           ;[3762] 61
                    and       c                             ;[3763] a1
                    set       3,d                           ;[3764] cb da
                    sub       (hl)                          ;[3766] 96
                    and       h                             ;[3767] a4
                    ld        sp,$b49f                      ;[3768] 31 9f b4
                    rst       $20                           ;[376b] e7
                    and       b                             ;[376c] a0
                    cp        $5c                           ;[376d] fe 5c
                    call      m,$1bea                       ;[376f] fc ea 1b
                    ld        b,e                           ;[3772] 43
                    jp        z,$ed36                       ;[3773] ca 36 ed
                    and       a                             ;[3776] a7
                    sbc       h                             ;[3777] 9c
                    ld        a,(hl)                        ;[3778] 7e
                    ld        e,(hl)                        ;[3779] 5e
                    ret       p                             ;[377a] f0
                    ld        l,(hl)                        ;[377b] 6e
                    inc       hl                            ;[377c] 23
                    add       b                             ;[377d] 80
                    sub       e                             ;[377e] 93
                    inc       b                             ;[377f] 04
                    rrca                                    ;[3780] 0f
                    jr        c,$374c                       ;[3781] 38 c9
                    rst       $28                           ;[3783] ef
                    dec       a                             ;[3784] 3d
                    inc       (hl)                          ;[3785] 34
                    xor       $22                           ;[3786] ee 22
                    ld        sp,hl                         ;[3788] f9
                    add       e                             ;[3789] 83
                    ld        l,(hl)                        ;[378a] 6e
                    inc       b                             ;[378b] 04
                    ld        sp,$0fa2                      ;[378c] 31 a2 0f
                    daa                                     ;[378f] 27
                    inc       bc                            ;[3790] 03
                    ld        sp,$310f                      ;[3791] 31 0f 31
                    rrca                                    ;[3794] 0f
                    ld        sp,$a12a                      ;[3795] 31 2a a1
                    inc       bc                            ;[3798] 03
                    ld        sp,$c037                      ;[3799] 31 37 c0
                    nop                                     ;[379c] 00
                    inc       b                             ;[379d] 04
                    ld        (bc),a                        ;[379e] 02
                    jr        c,$376a                       ;[379f] 38 c9
                    and       c                             ;[37a1] a1
                    inc       bc                            ;[37a2] 03
                    ld        bc,$0036                      ;[37a3] 01 36 00
                    ld        (bc),a                        ;[37a6] 02
                    dec       de                            ;[37a7] 1b
                    jr        c,$3773                       ;[37a8] 38 c9
                    rst       $28                           ;[37aa] ef
                    add       hl,sp                         ;[37ab] 39
                    ld        hl,($03a1)                    ;[37ac] 2a a1 03
                    ret       po                            ;[37af] e0
                    nop                                     ;[37b0] 00
                    ld        b,$1b                         ;[37b1] 06 1b
                    inc       sp                            ;[37b3] 33
                    inc       bc                            ;[37b4] 03
                    rst       $28                           ;[37b5] ef
                    add       hl,sp                         ;[37b6] 39
                    ld        sp,$0431                      ;[37b7] 31 31 04
                    ld        sp,$a10f                      ;[37ba] 31 0f a1
                    inc       bc                            ;[37bd] 03
                    add       (hl)                          ;[37be] 86
                    inc       d                             ;[37bf] 14
                    and       $5c                           ;[37c0] e6 5c
                    rra                                     ;[37c2] 1f
                    dec       bc                            ;[37c3] 0b
                    and       e                             ;[37c4] a3
                    adc       a                             ;[37c5] 8f
                    jr        c,$37b6                       ;[37c6] 38 ee
                    jp        (hl)                          ;[37c8] e9
                    dec       d                             ;[37c9] 15
                    ld        h,e                           ;[37ca] 63
                    cp        e                             ;[37cb] bb
                    inc       hl                            ;[37cc] 23
                    xor       $92                           ;[37cd] ee 92
                    dec       c                             ;[37cf] 0d
                    call      $f1ed                         ;[37d0] cd ed f1
                    inc       hl                            ;[37d3] 23
                    ld        e,l                           ;[37d4] 5d
                    dec       de                            ;[37d5] 1b
                    jp        pe,$3804                      ;[37d6] ea 04 38
                    ret                                     ;[37d9] c9

                    rst       $28                           ;[37da] ef
                    ld        sp,$011f                      ;[37db] 31 1f 01
                    jr        nz,$37e5                      ;[37de] 20 05
                    jr        c,$37ab                       ;[37e0] 38 c9
                    call      $3297                         ;[37e2] cd 97 32
                    ld        a,(hl)                        ;[37e5] 7e
                    cp        $81                           ;[37e6] fe 81
                    jr        c,$37f8                       ;[37e8] 38 0e
                    rst       $28                           ;[37ea] ef
                    and       c                             ;[37eb] a1
                    dec       de                            ;[37ec] 1b
                    ld        bc,$3105                      ;[37ed] 01 05 31
                    ld        (hl),$a3                      ;[37f0] 36 a3
                    ld        bc,$0600                      ;[37f2] 01 00 06
                    dec       de                            ;[37f5] 1b
                    inc       sp                            ;[37f6] 33
                    inc       bc                            ;[37f7] 03
                    rst       $28                           ;[37f8] ef
                    and       b                             ;[37f9] a0
                    ld        bc,$3131                      ;[37fa] 01 31 31
                    inc       b                             ;[37fd] 04
                    ld        sp,$a10f                      ;[37fe] 31 0f a1
                    inc       bc                            ;[3801] 03
                    adc       h                             ;[3802] 8c
                    djnz      $37b7                         ;[3803] 10 b2
                    inc       de                            ;[3805] 13
                    ld        c,$55                         ;[3806] 0e 55
                    call      po,$588d                      ;[3808] e4 8d 58
                    add       hl,sp                         ;[380b] 39
                    cp        h                             ;[380c] bc
                    ld        e,e                           ;[380d] 5b
                    sbc       b                             ;[380e] 98
                    sbc       (iy+$00)                      ;[380f] fd 9e 00
                    ld        (hl),$75                      ;[3812] 36 75
                    and       b                             ;[3814] a0
                    in        a,($e8)                       ;[3815] db e8
                    or        h                             ;[3817] b4
                    ld        h,e                           ;[3818] 63
                    ld        b,d                           ;[3819] 42
                    call      nz,$b5e6                      ;[381a] c4 e6 b5
                    add       hl,bc                         ;[381d] 09
                    ld        (hl),$be                      ;[381e] 36 be
                    jp        (hl)                          ;[3820] e9
                    ld        (hl),$73                      ;[3821] 36 73
                    dec       de                            ;[3823] 1b
                    ld        e,l                           ;[3824] 5d
                    call      pe,$ded8                      ;[3825] ec d8 de
                    ld        h,e                           ;[3828] 63
                    cp        (hl)                          ;[3829] be
                    ret       p                             ;[382a] f0
                    ld        h,c                           ;[382b] 61
                    and       c                             ;[382c] a1
                    or        e                             ;[382d] b3
                    inc       c                             ;[382e] 0c
                    inc       b                             ;[382f] 04
                    rrca                                    ;[3830] 0f
                    jr        c,$37fc                       ;[3831] 38 c9
                    rst       $28                           ;[3833] ef
                    ld        sp,$0431                      ;[3834] 31 31 04
                    and       c                             ;[3837] a1
                    inc       bc                            ;[3838] 03
                    dec       de                            ;[3839] 1b
                    jr        z,$37dd                       ;[383a] 28 a1
                    rrca                                    ;[383c] 0f
                    dec       b                             ;[383d] 05
                    inc       h                             ;[383e] 24
                    ld        sp,$380f                      ;[383f] 31 0f 38
                    ret                                     ;[3842] c9

                    rst       $28                           ;[3843] ef
                    ld        ($03a3),hl                    ;[3844] 22 a3 03
                    dec       de                            ;[3847] 1b
                    jr        c,$3813                       ;[3848] 38 c9
                    rst       $28                           ;[384a] ef
                    ld        sp,$0030                      ;[384b] 31 30 00
                    ld        e,$a2                         ;[384e] 1e a2
                    jr        c,$3841                       ;[3850] 38 ef
                    ld        bc,$3031                      ;[3852] 01 31 30
                    nop                                     ;[3855] 00
                    rlca                                    ;[3856] 07
                    dec       h                             ;[3857] 25
                    inc       b                             ;[3858] 04
                    jr        c,$381e                       ;[3859] 38 c3
                    call      nz,$0236                      ;[385b] c4 36 02
                    ld        sp,$0030                      ;[385e] 31 30 00
                    add       hl,bc                         ;[3861] 09
                    and       b                             ;[3862] a0
                    ld        bc,$0037                      ;[3863] 01 37 00
                    ld        b,$a1                         ;[3866] 06 a1
                    ld        bc,$0205                      ;[3868] 01 05 02
                    and       c                             ;[386b] a1
                    jr        c,$3837                       ;[386c] 38 c9
                    jp        $02bf                         ;[386e] c3 bf 02
                    rst       $38                           ;[3871] ff
                    rst       $38                           ;[3872] ff
                    rst       $38                           ;[3873] ff
                    rst       $38                           ;[3874] ff
                    rst       $38                           ;[3875] ff
                    rst       $38                           ;[3876] ff
                    rst       $38                           ;[3877] ff
                    rst       $38                           ;[3878] ff
                    rst       $38                           ;[3879] ff
                    rst       $38                           ;[387a] ff
                    rst       $38                           ;[387b] ff
                    rst       $38                           ;[387c] ff
                    rst       $38                           ;[387d] ff
                    sub       $a5                           ;[387e] d6 a5
                    jp        nc,$0b5f                      ;[3880] d2 5f 0b
                    jp        $0b56                         ;[3883] c3 56 0b
                    pop       hl                            ;[3886] e1
                    pop       bc                            ;[3887] c1
                    pop       de                            ;[3888] d1
                    push      bc                            ;[3889] c5
                    push      hl                            ;[388a] e5
                    call      $2530                         ;[388b] cd 30 25
                    jp        z,$27b9                       ;[388e] ca b9 27
                    push    $2761                           ;[3891] ed 8a 27 61
                    ld        a,e                           ;[3895] 7b
                    cp        $24                           ;[3896] fe 24
                    jp        z,$2184                       ;[3898] ca 84 21
                    push      de                            ;[389b] d5
                    call      $1e99                         ;[389c] cd 99 1e
                    pop       de                            ;[389f] d1
                    ld        a,e                           ;[38a0] 7b
                    cp        $9e                           ;[38a1] fe 9e
                    jr        z,$38b1                       ;[38a3] 28 0c
                    cp        $e8                           ;[38a5] fe e8
                    jr        z,$38b1                       ;[38a7] 28 08
                    ld        h,b                           ;[38a9] 60
                    ld        l,c                           ;[38aa] 69
                    call      $38d3                         ;[38ab] cd d3 38
                    jp        $2d2f                         ;[38ae] c3 2f 2d
                    ld        e,c                           ;[38b1] 59
                    ld        l,a                           ;[38b2] 6f
                    inc       b                             ;[38b3] 04
                    dec       b                             ;[38b4] 05
                    jp        z,$39a4                       ;[38b5] ca a4 39
                    rst       $08                           ;[38b8] cf
                    ld        a,(bc)                        ;[38b9] 0a
                    call      $2da2                         ;[38ba] cd a2 2d
                    jp        c,$24f9                       ;[38bd] da f9 24
                    jr        z,$38c9                       ;[38c0] 28 07
                    ld        a,b                           ;[38c2] 78
                    cpl                                     ;[38c3] 2f
                    ld        b,a                           ;[38c4] 47
                    ld        a,c                           ;[38c5] 79
                    cpl                                     ;[38c6] 2f
                    ld        c,a                           ;[38c7] 4f
                    inc       bc                            ;[38c8] 03
                    ld        d,b                           ;[38c9] 50
                    ld        e,c                           ;[38ca] 59
                    call      $2705                         ;[38cb] cd 05 27
                    sub       c                             ;[38ce] 91
                    ld        l,$eb                         ;[38cf] 2e eb
                    jr        $394e                         ;[38d1] 18 7b
                    push      de                            ;[38d3] d5
                    ld        a,d                           ;[38d4] 7a
                    ld        bc,$3304                      ;[38d5] 01 04 33
                    call      $32c5                         ;[38d8] cd c5 32
                    ex        af,af'                        ;[38db] 08
                    pop       de                            ;[38dc] d1
                    ld        a,e                           ;[38dd] 7b
                    cp        $c0                           ;[38de] fe c0
                    jr        z,$38f1                       ;[38e0] 28 0f
                    push      de                            ;[38e2] d5
                    call      $08fc                         ;[38e3] cd fc 08
                    inc       hl                            ;[38e6] 23
                    ld        c,a                           ;[38e7] 4f
                    pop       de                            ;[38e8] d1
                    ld        a,e                           ;[38e9] 7b
                    sub       $be                           ;[38ea] d6 be
                    call      nz,$08fc                      ;[38ec] c4 fc 08
                    ld        b,a                           ;[38ef] 47
                    ret                                     ;[38f0] c9

                    ld        a,d                           ;[38f1] 7a
                    push      hl                            ;[38f2] e5
                    ld        b,h                           ;[38f3] 44
                    ld        c,l                           ;[38f4] 4d
                    exx                                     ;[38f5] d9
                    ld        d,a                           ;[38f6] 57
                    pop       hl                            ;[38f7] e1
                    xor       a                             ;[38f8] af
                    cp        $01                           ;[38f9] fe 01
                    jp        $1f6a                         ;[38fb] c3 6a 1f
                    rst       $38                           ;[38fe] ff
                    rst       $38                           ;[38ff] ff
                    rst       $38                           ;[3900] ff
                    rst       $38                           ;[3901] ff
                    push      bc                            ;[3902] c5
                    ld        hl,($5c51)                    ;[3903] 2a 51 5c
                    push      hl                            ;[3906] e5
                    push      bc                            ;[3907] c5
                    call      $1e94                         ;[3908] cd 94 1e
                    call      $1601                         ;[390b] cd 01 16
                    pop       af                            ;[390e] f1
                    sub       $34                           ;[390f] d6 34
                    ld        b,a                           ;[3911] 47
                    exx                                     ;[3912] d9
                    add       a                             ;[3913] 87
                    jr        nz,$3918                      ;[3914] 20 02
                    ld        a,$04                         ;[3916] 3e 04
                    ld        e,a                           ;[3918] 5f
                    ld        d,$00                         ;[3919] 16 00
                    ld        hl,$03e5                      ;[391b] 21 e5 03
                    call      $19ce                         ;[391e] cd ce 19
                    ex        (sp),hl                       ;[3921] e3
                    push      de                            ;[3922] d5
                    push      af                            ;[3923] f5
                    call      $1615                         ;[3924] cd 15 16
                    pop       af                            ;[3927] f1
                    pop       de                            ;[3928] d1
                    pop       hl                            ;[3929] e1
                    ld        c,a                           ;[392a] 4f
                    ld        b,$00                         ;[392b] 06 00
                    pop       af                            ;[392d] f1
                    cp        $35                           ;[392e] fe 35
                    jr        z,$3950                       ;[3930] 28 1e
                    push      de                            ;[3932] d5
                    ld        b,h                           ;[3933] 44
                    ld        c,l                           ;[3934] 4d
                    call      $2d2f                         ;[3935] cd 2f 2d
                    pop       bc                            ;[3938] c1
                    call      $2d2f                         ;[3939] cd 2f 2d
                    rst       $28                           ;[393c] ef
                    inc       (hl)                          ;[393d] 34
                    ld        b,b                           ;[393e] 40
                    ld        b,c                           ;[393f] 41
                    nop                                     ;[3940] 00
                    nop                                     ;[3941] 00
                    inc       b                             ;[3942] 04
                    rrca                                    ;[3943] 0f
                    jr        c,$390f                       ;[3944] 38 c9
                    call      $1e99                         ;[3946] cd 99 1e
                    ld        d,b                           ;[3949] 50
                    ld        e,c                           ;[394a] 59
                    call      $0918                         ;[394b] cd 18 09
                    ld        b,h                           ;[394e] 44
                    ld        c,l                           ;[394f] 4d
                    jp        $2d2f                         ;[3950] c3 2f 2d
                    call      $1c81                         ;[3953] cd 81 1c
                    call      $2530                         ;[3956] cd 30 25
                    call      nz,$1e94                      ;[3959] c4 94 1e
                    ld        d,a                           ;[395c] 57
                    rst       $18                           ;[395d] df
                    cp        $a8                           ;[395e] fe a8
                    jp        z,$089c                       ;[3960] ca 9c 08
                    cp        $87                           ;[3963] fe 87
                    jp        z,$3a04                       ;[3965] ca 04 3a
                    cp        $c0                           ;[3968] fe c0
                    jp        z,$17af                       ;[396a] ca af 17
                    cp        $be                           ;[396d] fe be
                    jr        z,$3976                       ;[396f] 28 05
                    cp        $8a                           ;[3971] fe 8a
                    jp        nz,$3a0c                      ;[3973] c2 0c 3a
                    ld        e,a                           ;[3976] 5f
                    push      de                            ;[3977] d5
                    ld        bc,$10c0                      ;[3978] 01 c0 10
                    jp        $082f                         ;[397b] c3 2f 08
                    rst       $20                           ;[397e] e7
                    cp        $e8                           ;[397f] fe e8
                    jr        z,$3976                       ;[3981] 28 f3
                    cp        $ac                           ;[3983] fe ac
                    jr        z,$3993                       ;[3985] 28 0c
                    cp        $de                           ;[3987] fe de
                    jr        z,$39af                       ;[3989] 28 24
                    dec       hl                            ;[398b] 2b
                    ld        ($5c5d),hl                    ;[398c] 22 5d 5c
                    ld        a,$9e                         ;[398f] 3e 9e
                    jr        $3976                         ;[3991] 18 e3
                    call      $2522                         ;[3993] cd 22 25
                    call      nz,$399d                      ;[3996] c4 9d 39
                    rst       $20                           ;[3999] e7
                    jp        $26c3                         ;[399a] c3 c3 26
                    call      $2307                         ;[399d] cd 07 23
                    ld        e,c                           ;[39a0] 59
                    ld        d,b                           ;[39a1] 50
                    ld        l,$ac                         ;[39a2] 2e ac
                    ld        bc,$099b                      ;[39a4] 01 9b 09
                    call      $1ef2                         ;[39a7] cd f2 1e
                    ld        b,d                           ;[39aa] 42
                    ld        c,e                           ;[39ab] 4b
                    jp        $2d2f                         ;[39ac] c3 2f 2d
                    rst       $20                           ;[39af] e7
                    cp        $28                           ;[39b0] fe 28
                    jr        nz,$3a0c                      ;[39b2] 20 58
                    call      $1c79                         ;[39b4] cd 79 1c
                    cp        $cc                           ;[39b7] fe cc
                    call      $0ff5                         ;[39b9] cd f5 0f
                    cp        $2c                           ;[39bc] fe 2c
                    call      $0ff5                         ;[39be] cd f5 0f
                    cp        $2c                           ;[39c1] fe 2c
                    call      $0ff5                         ;[39c3] cd f5 0f
                    cp        $29                           ;[39c6] fe 29
                    jr        nz,$3a0c                      ;[39c8] 20 42
                    rst       $20                           ;[39ca] e7
                    call      $2530                         ;[39cb] cd 30 25
                    jr        z,$39fb                       ;[39ce] 28 2b
                    call      $1e6f                         ;[39d0] cd 6f 1e
                    ld        l,c                           ;[39d3] 69
                    push      hl                            ;[39d4] e5
                    call      $1e6f                         ;[39d5] cd 6f 1e
                    pop       hl                            ;[39d8] e1
                    ld        h,c                           ;[39d9] 61
                    push      hl                            ;[39da] e5
                    call      $1e6f                         ;[39db] cd 6f 1e
                    push      bc                            ;[39de] c5
                    call      $1e94                         ;[39df] cd 94 1e
                    pop       de                            ;[39e2] d1
                    ld        b,e                           ;[39e3] 43
                    inc       b                             ;[39e4] 04
                    dec       b                             ;[39e5] 05
                    jr        nz,$39e9                      ;[39e6] 20 01
                    ld        b,c                           ;[39e8] 41
                    push      bc                            ;[39e9] c5
                    call      $1e94                         ;[39ea] cd 94 1e
                    ld        l,c                           ;[39ed] 69
                    pop       de                            ;[39ee] d1
                    ld        a,e                           ;[39ef] 7b
                    ld        h,d                           ;[39f0] 62
                    pop       de                            ;[39f1] d1
                    ld        bc,$0934                      ;[39f2] 01 34 09
                    call      $1ef2                         ;[39f5] cd f2 1e
                    call      $2d28                         ;[39f8] cd 28 2d
                    jp        $26c3                         ;[39fb] c3 c3 26
                    rst       $38                           ;[39fe] ff
                    rst       $38                           ;[39ff] ff
                    rst       $38                           ;[3a00] ff
                    rst       $38                           ;[3a01] ff
                    ld        d,$ff                         ;[3a02] 16 ff
                    push    $25db                           ;[3a04] ed 8a 25 db
                    push      de                            ;[3a08] d5
                    rst       $20                           ;[3a09] e7
                    cp        $28                           ;[3a0a] fe 28
                    jp        nz,$1c8a                      ;[3a0c] c2 8a 1c
                    call      $1c81                         ;[3a0f] cd 81 1c
                    cp        $2c                           ;[3a12] fe 2c
                    jr        nz,$3a0c                      ;[3a14] 20 f6
                    rst       $20                           ;[3a16] e7
                    ld        b,a                           ;[3a17] 47
                    ld        c,a                           ;[3a18] 4f
                    cp        $7e                           ;[3a19] fe 7e
                    jr        nz,$3a23                      ;[3a1b] 20 06
                    rst       $20                           ;[3a1d] e7
                    ld        c,a                           ;[3a1e] 4f
                    cp        $29                           ;[3a1f] fe 29
                    jr        z,$3a2c                       ;[3a21] 28 09
                    push      bc                            ;[3a23] c5
                    call      $1c82                         ;[3a24] cd 82 1c
                    pop       bc                            ;[3a27] c1
                    cp        $29                           ;[3a28] fe 29
                    jr        nz,$3a0c                      ;[3a2a] 20 e0
                    pop       de                            ;[3a2c] d1
                    rst       $20                           ;[3a2d] e7
                    call      $2530                         ;[3a2e] cd 30 25
                    ret       z                             ;[3a31] c8
                    push      de                            ;[3a32] d5
                    push      bc                            ;[3a33] c5
                    ld        a,c                           ;[3a34] 79
                    cp        $29                           ;[3a35] fe 29
                    call      nz,$1e99                      ;[3a37] c4 99 1e
                    push      bc                            ;[3a3a] c5
                    call      $1e99                         ;[3a3b] cd 99 1e
                    ld        d,b                           ;[3a3e] 50
                    ld        e,c                           ;[3a3f] 59
                    pop       bc                            ;[3a40] c1
                    pop       hl                            ;[3a41] e1
                    pop       af                            ;[3a42] f1
                    ex        af,af'                        ;[3a43] 08
                    ld        a,h                           ;[3a44] 7c
                    cp        $7e                           ;[3a45] fe 7e
                    jp        nz,$3a6f                      ;[3a47] c2 6f 3a
                    ld        a,l                           ;[3a4a] 7d
                    cp        $29                           ;[3a4b] fe 29
                    ld        hl,$8080                      ;[3a4d] 21 80 80
                    jr        z,$3a5a                       ;[3a50] 28 08
                    ld        h,$ff                         ;[3a52] 26 ff
                    ld        l,c                           ;[3a54] 69
                    ld        a,b                           ;[3a55] 78
                    and       a                             ;[3a56] a7
                    jp        nz,$24f9                      ;[3a57] c2 f9 24
                    ld        bc,$ffff                      ;[3a5a] 01 ff ff
                    ex        af,af'                        ;[3a5d] 08
                    inc       a                             ;[3a5e] 3c
                    jr        nz,$3a75                      ;[3a5f] 20 14
                    push      de                            ;[3a61] d5
                    ld        a,(de)                        ;[3a62] 1a
                    inc       de                            ;[3a63] 13
                    inc       bc                            ;[3a64] 03
                    and       h                             ;[3a65] a4
                    cp        l                             ;[3a66] bd
                    jr        nz,$3a62                      ;[3a67] 20 f9
                    inc       h                             ;[3a69] 24
                    jr        z,$3a6d                       ;[3a6a] 28 01
                    inc       bc                            ;[3a6c] 03
                    pop       de                            ;[3a6d] d1
                    ret                                     ;[3a6e] c9

                    ex        af,af'                        ;[3a6f] 08
                    inc       a                             ;[3a70] 3c
                    ret       z                             ;[3a71] c8
                    ld        hl,$00ff                      ;[3a72] 21 ff 00
                    ex        de,hl                         ;[3a75] eb
                    dec       a                             ;[3a76] 3d
                    push      bc                            ;[3a77] c5
                    push      de                            ;[3a78] d5
                    ld        bc,$3304                      ;[3a79] 01 04 33
                    call      $32c5                         ;[3a7c] cd c5 32
                    ex        af,af'                        ;[3a7f] 08
                    pop       de                            ;[3a80] d1
                    pop       bc                            ;[3a81] c1
                    exx                                     ;[3a82] d9
                    ld        de,($5c63)                    ;[3a83] ed 5b 63 5c
                    ld        hl,$6000                      ;[3a87] 21 00 60
                    and       a                             ;[3a8a] a7
                    sbc       hl,de                         ;[3a8b] ed 52
                    ld        b,h                           ;[3a8d] 44
                    ld        c,l                           ;[3a8e] 4d
                    call      nc,$0030                      ;[3a8f] d4 30 00
                    ld        hl,($5c63)                    ;[3a92] 2a 63 5c
                    push      hl                            ;[3a95] e5
                    ld        bc,$0000                      ;[3a96] 01 00 00
                    exx                                     ;[3a99] d9
                    ld        a,b                           ;[3a9a] 78
                    or        c                             ;[3a9b] b1
                    jr        z,$3ab1                       ;[3a9c] 28 13
                    dec       bc                            ;[3a9e] 0b
                    call      $08fc                         ;[3a9f] cd fc 08
                    inc       hl                            ;[3aa2] 23
                    exx                                     ;[3aa3] d9
                    push      bc                            ;[3aa4] c5
                    ld        bc,$0001                      ;[3aa5] 01 01 00
                    rst       $30                           ;[3aa8] f7
                    pop       bc                            ;[3aa9] c1
                    ld        (de),a                        ;[3aaa] 12
                    inc       bc                            ;[3aab] 03
                    exx                                     ;[3aac] d9
                    and       d                             ;[3aad] a2
                    cp        e                             ;[3aae] bb
                    jr        nz,$3a9a                      ;[3aaf] 20 e9
                    inc       d                             ;[3ab1] 14
                    exx                                     ;[3ab2] d9
                    jr        nz,$3ab6                      ;[3ab3] 20 01
                    dec       bc                            ;[3ab5] 0b
                    pop       de                            ;[3ab6] d1
                    ret                                     ;[3ab7] c9

                    push    $25db                           ;[3ab8] ed 8a 25 db
                    rst       $20                           ;[3abc] e7
                    call      $1c8c                         ;[3abd] cd 8c 1c
                    cp        $7d                           ;[3ac0] fe 7d
                    jp        nz,$3a0c                      ;[3ac2] c2 0c 3a
                    rst       $20                           ;[3ac5] e7
                    call      $2530                         ;[3ac6] cd 30 25
                    ret       z                             ;[3ac9] c8
                    call      $2bf1                         ;[3aca] cd f1 2b
                    ld        hl,$1859                      ;[3acd] 21 59 18
                    call      $1e5f                         ;[3ad0] cd 5f 1e
                    ret                                     ;[3ad3] c9

                    push      hl                            ;[3ad4] e5
                    call      $1f05                         ;[3ad5] cd 05 1f
                    pop       de                            ;[3ad8] d1
                    ld        hl,($5c3d)                    ;[3ad9] 2a 3d 5c
                    and       a                             ;[3adc] a7
                    sbc       hl,bc                         ;[3add] ed 42
                    ld        ($5c3d),hl                    ;[3adf] 22 3d 5c
                    ld        hl,($5b58)                    ;[3ae2] 2a 58 5b
                    sbc       hl,bc                         ;[3ae5] ed 42
                    ld        ($5b58),hl                    ;[3ae7] 22 58 5b
                    ld        hl,$0000                      ;[3aea] 21 00 00
                    add       hl,sp                         ;[3aed] 39
                    ex        de,hl                         ;[3aee] eb
                    sbc       hl,de                         ;[3aef] ed 52
                    push      hl                            ;[3af1] e5
                    ld        h,d                           ;[3af2] 62
                    ld        l,e                           ;[3af3] 6b
                    sbc       hl,bc                         ;[3af4] ed 42
                    pop       bc                            ;[3af6] c1
                    ld        sp,hl                         ;[3af7] f9
                    ex        de,hl                         ;[3af8] eb
                    ldir                                    ;[3af9] ed b0
                    ret                                     ;[3afb] c9

                    rst       $38                           ;[3afc] ff
                    rst       $38                           ;[3afd] ff
                    rst       $38                           ;[3afe] ff
                    rst       $38                           ;[3aff] ff
                    rst       $38                           ;[3b00] ff
                    rst       $38                           ;[3b01] ff
                    adc       a                             ;[3b02] 8f
                    ld        (hl),$3c                      ;[3b03] 36 3c
                    inc       (hl)                          ;[3b05] 34
                    and       c                             ;[3b06] a1
                    inc       sp                            ;[3b07] 33
                    rrca                                    ;[3b08] 0f
                    jr        nc,$3ad5                      ;[3b09] 30 ca
                    jr        nc,$3abc                      ;[3b0b] 30 af
                    ld        sp,$3851                      ;[3b0d] 31 51 38
                    dec       de                            ;[3b10] 1b
                    dec       (hl)                          ;[3b11] 35
                    inc       h                             ;[3b12] 24
                    dec       (hl)                          ;[3b13] 35
                    dec       sp                            ;[3b14] 3b
                    dec       (hl)                          ;[3b15] 35
                    dec       sp                            ;[3b16] 3b
                    dec       (hl)                          ;[3b17] 35
                    dec       sp                            ;[3b18] 3b
                    dec       (hl)                          ;[3b19] 35
                    dec       sp                            ;[3b1a] 3b
                    dec       (hl)                          ;[3b1b] 35
                    dec       sp                            ;[3b1c] 3b
                    dec       (hl)                          ;[3b1d] 35
                    dec       sp                            ;[3b1e] 3b
                    dec       (hl)                          ;[3b1f] 35
                    inc       d                             ;[3b20] 14
                    jr        nc,$3b50                      ;[3b21] 30 2d
                    dec       (hl)                          ;[3b23] 35
                    dec       sp                            ;[3b24] 3b
                    dec       (hl)                          ;[3b25] 35
                    dec       sp                            ;[3b26] 3b
                    dec       (hl)                          ;[3b27] 35
                    dec       sp                            ;[3b28] 3b
                    dec       (hl)                          ;[3b29] 35
                    dec       sp                            ;[3b2a] 3b
                    dec       (hl)                          ;[3b2b] 35
                    dec       sp                            ;[3b2c] 3b
                    dec       (hl)                          ;[3b2d] 35
                    dec       sp                            ;[3b2e] 3b
                    dec       (hl)                          ;[3b2f] 35
                    sbc       h                             ;[3b30] 9c
                    dec       (hl)                          ;[3b31] 35
                    sbc       $35                           ;[3b32] de 35
                    cp        h                             ;[3b34] bc
                    inc       (hl)                          ;[3b35] 34
                    ld        b,l                           ;[3b36] 45
                    ld        (hl),$6e                      ;[3b37] 36 6e
                    inc       (hl)                          ;[3b39] 34
                    ld        l,c                           ;[3b3a] 69
                    ld        (hl),$de                      ;[3b3b] 36 de
                    dec       (hl)                          ;[3b3d] 35
                    ld        (hl),h                        ;[3b3e] 74
                    ld        (hl),$b5                      ;[3b3f] 36 b5
                    scf                                     ;[3b41] 37
                    xor       d                             ;[3b42] aa
                    scf                                     ;[3b43] 37
                    jp        c,$3337                       ;[3b44] da 37 33
                    jr        c,$3b8c                       ;[3b47] 38 43
                    jr        c,$3b2d                       ;[3b49] 38 e2
                    scf                                     ;[3b4b] 37
                    inc       de                            ;[3b4c] 13
                    scf                                     ;[3b4d] 37
                    call      nz,$af36                      ;[3b4e] c4 36 af
                    ld        (hl),$4a                      ;[3b51] 36 4a
                    jr        c,$3ae7                       ;[3b53] 38 92
                    inc       (hl)                          ;[3b55] 34
                    ld        l,d                           ;[3b56] 6a
                    inc       (hl)                          ;[3b57] 34
                    xor       h                             ;[3b58] ac
                    inc       (hl)                          ;[3b59] 34
                    and       l                             ;[3b5a] a5
                    inc       (hl)                          ;[3b5b] 34
                    or        e                             ;[3b5c] b3
                    inc       (hl)                          ;[3b5d] 34
                    rra                                     ;[3b5e] 1f
                    ld        (hl),$c9                      ;[3b5f] 36 c9
                    dec       (hl)                          ;[3b61] 35
                    ld        bc,$2e35                      ;[3b62] 01 35 2e
                    inc       a                             ;[3b65] 3c
                    and       b                             ;[3b66] a0
                    ld        (hl),$86                      ;[3b67] 36 86
                    ld        (hl),$c6                      ;[3b69] 36 c6
                    inc       sp                            ;[3b6b] 33
                    ld        a,d                           ;[3b6c] 7a
                    ld        (hl),$06                      ;[3b6d] 36 06
                    dec       (hl)                          ;[3b6f] 35
                    ld        sp,hl                         ;[3b70] f9
                    inc       (hl)                          ;[3b71] 34
                    sbc       e                             ;[3b72] 9b
                    ld        (hl),$83                      ;[3b73] 36 83
                    scf                                     ;[3b75] 37
                    inc       d                             ;[3b76] 14
                    ld        ($0000),a                     ;[3b77] 32 00 00
                    ld        c,a                           ;[3b7a] 4f
                    dec       l                             ;[3b7b] 2d
                    sub       a                             ;[3b7c] 97
                    ld        ($3449),a                     ;[3b7d] 32 49 34
                    dec       de                            ;[3b80] 1b
                    inc       (hl)                          ;[3b81] 34
                    dec       l                             ;[3b82] 2d
                    inc       (hl)                          ;[3b83] 34
                    rrca                                    ;[3b84] 0f
                    inc       (hl)                          ;[3b85] 34
                    push      af                            ;[3b86] f5
                    push      ix                            ;[3b87] dd e5
                    ld        a,h                           ;[3b89] 7c
                    ld        ixh,a                         ;[3b8a] dd 67
                    ld        a,l                           ;[3b8c] 7d
                    ld        ixl,a                         ;[3b8d] dd 6f
                    ld        hl,($5c4b)                    ;[3b8f] 2a 4b 5c
                    sub       l                             ;[3b92] 95
                    ld        a,ixh                         ;[3b93] dd 7c
                    sbc       h                             ;[3b95] 9c
                    jr        nc,$3b9c                      ;[3b96] 30 04
                    add       hl,bc                         ;[3b98] 09
                    ld        ($5c4b),hl                    ;[3b99] 22 4b 5c
                    ld        hl,($5c4d)                    ;[3b9c] 2a 4d 5c
                    sbc       hl,sp                         ;[3b9f] ed 72
                    jr        nc,$3bb0                      ;[3ba1] 30 0d
                    add       hl,sp                         ;[3ba3] 39
                    ld        a,ixl                         ;[3ba4] dd 7d
                    sub       l                             ;[3ba6] 95
                    ld        a,ixh                         ;[3ba7] dd 7c
                    sbc       h                             ;[3ba9] 9c
                    jr        nc,$3bb0                      ;[3baa] 30 04
                    add       hl,bc                         ;[3bac] 09
                    ld        ($5c4d),hl                    ;[3bad] 22 4d 5c
                    ld        hl,$5c4f                      ;[3bb0] 21 4f 5c
                    ld        e,(hl)                        ;[3bb3] 5e
                    inc       hl                            ;[3bb4] 23
                    ld        d,(hl)                        ;[3bb5] 56
                    ld        a,ixl                         ;[3bb6] dd 7d
                    sub       e                             ;[3bb8] 93
                    ld        a,ixh                         ;[3bb9] dd 7c
                    sbc       d                             ;[3bbb] 9a
                    jr        nc,$3bc5                      ;[3bbc] 30 07
                    ex        de,hl                         ;[3bbe] eb
                    add       hl,bc                         ;[3bbf] 09
                    ex        de,hl                         ;[3bc0] eb
                    ld        (hl),d                        ;[3bc1] 72
                    dec       hl                            ;[3bc2] 2b
                    ld        (hl),e                        ;[3bc3] 73
                    inc       hl                            ;[3bc4] 23
                    inc       hl                            ;[3bc5] 23
                    ld        a,$65                         ;[3bc6] 3e 65
                    cp        l                             ;[3bc8] bd
                    jp        nc,$3bb3                      ;[3bc9] d2 b3 3b
                    ex        de,hl                         ;[3bcc] eb
                    ld        e,ixl                         ;[3bcd] dd 5d
                    ld        d,ixh                         ;[3bcf] dd 54
                    pop       ix                            ;[3bd1] dd e1
                    pop       af                            ;[3bd3] f1
                    and       a                             ;[3bd4] a7
                    sbc       hl,bc                         ;[3bd5] ed 42
                    sbc       hl,de                         ;[3bd7] ed 52
                    ld        b,h                           ;[3bd9] 44
                    ld        c,l                           ;[3bda] 4d
                    inc       bc                            ;[3bdb] 03
                    add       hl,de                         ;[3bdc] 19
                    ex        de,hl                         ;[3bdd] eb
                    ret                                     ;[3bde] c9

                    call      $2c8d                         ;[3bdf] cd 8d 2c
                    ret       nc                            ;[3be2] d0
                    and       $1f                           ;[3be3] e6 1f
                    add       $8f                           ;[3be5] c6 8f
                    ret                                     ;[3be7] c9

                    ld        a,$02                         ;[3be8] 3e 02
                    out       (c),a                         ;[3bea] ed 79
                    inc       b                             ;[3bec] 04
                    in        a,(c)                         ;[3bed] ed 78
                    and       $80                           ;[3bef] e6 80
                    or        $01                           ;[3bf1] f6 01
                    out       (c),a                         ;[3bf3] ed 79
                    rst       $38                           ;[3bf5] ff
                    rst       $38                           ;[3bf6] ff
                    rst       $38                           ;[3bf7] ff
                    rst       $38                           ;[3bf8] ff
                    rst       $38                           ;[3bf9] ff
                    rst       $38                           ;[3bfa] ff
                    rst       $38                           ;[3bfb] ff
                    rst       $38                           ;[3bfc] ff
                    rst       $38                           ;[3bfd] ff
                    rst       $38                           ;[3bfe] ff
                    rst       $38                           ;[3bff] ff
                    rst       $38                           ;[3c00] ff
                    rst       $38                           ;[3c01] ff
                    nop                                     ;[3c02] 00
                    nop                                     ;[3c03] 00
                    nop                                     ;[3c04] 00
                    nop                                     ;[3c05] 00
                    nop                                     ;[3c06] 00
                    nop                                     ;[3c07] 00
                    nop                                     ;[3c08] 00
                    ld        bc,$0000                      ;[3c09] 01 00 00
                    add       b                             ;[3c0c] 80
                    nop                                     ;[3c0d] 00
                    nop                                     ;[3c0e] 00
                    nop                                     ;[3c0f] 00
                    nop                                     ;[3c10] 00
                    add       c                             ;[3c11] 81
                    ld        c,c                           ;[3c12] 49
                    rrca                                    ;[3c13] 0f
                    jp        c,$00a2                       ;[3c14] da a2 00
                    nop                                     ;[3c17] 00
                    ld        a,(bc)                        ;[3c18] 0a
                    nop                                     ;[3c19] 00
                    nop                                     ;[3c1a] 00
                    push      hl                            ;[3c1b] e5
                    ld        hl,($5c65)                    ;[3c1c] 2a 65 5c
                    ld        bc,$0055                      ;[3c1f] 01 55 00
                    add       hl,bc                         ;[3c22] 09
                    jr        c,$3c2b                       ;[3c23] 38 06
                    sbc       hl,sp                         ;[3c25] ed 72
                    jr        nc,$3c2b                      ;[3c27] 30 02
                    pop       hl                            ;[3c29] e1
                    ret                                     ;[3c2a] c9

                    jp        $1f15                         ;[3c2b] c3 15 1f
                    push      hl                            ;[3c2e] e5
                    ld        hl,($5c65)                    ;[3c2f] 2a 65 5c
                    ld        bc,$0055                      ;[3c32] 01 55 00
                    add       hl,bc                         ;[3c35] 09
                    jr        c,$3c2b                       ;[3c36] 38 f3
                    sbc       hl,sp                         ;[3c38] ed 72
                    jr        nc,$3c2b                      ;[3c3a] 30 ef
                    pop       hl                            ;[3c3c] e1
                    ld        bc,$0005                      ;[3c3d] 01 05 00
                    ldi                                     ;[3c40] ed a0
                    ldi                                     ;[3c42] ed a0
                    ldi                                     ;[3c44] ed a0
                    ldi                                     ;[3c46] ed a0
                    ldi                                     ;[3c48] ed a0
                    ret                                     ;[3c4a] c9

                    call      $2314                         ;[3c4b] cd 14 23
                    jr        nz,$3c66                      ;[3c4e] 20 16
                    and       a                             ;[3c50] a7
                    ret       z                             ;[3c51] c8
                    push      af                            ;[3c52] f5
                    call      $3297                         ;[3c53] cd 97 32
                    pop       af                            ;[3c56] f1
                    call      $34e9                         ;[3c57] cd e9 34
                    ret       c                             ;[3c5a] d8
                    add       (hl)                          ;[3c5b] 86
                    jp        c,$31ad                       ;[3c5c] da ad 31
                    ld        (hl),a                        ;[3c5f] 77
                    ret                                     ;[3c60] c9

                    call      $2314                         ;[3c61] cd 14 23
                    jr        nz,$3c50                      ;[3c64] 20 ea
                    and       a                             ;[3c66] a7
                    ret       z                             ;[3c67] c8
                    push      af                            ;[3c68] f5
                    call      $3297                         ;[3c69] cd 97 32
                    pop       bc                            ;[3c6c] c1
                    ld        a,(hl)                        ;[3c6d] 7e
                    sub       b                             ;[3c6e] 90
                    jr        c,$3c73                       ;[3c6f] 38 02
                    jr        nz,$3c5f                      ;[3c71] 20 ec
                    and       a                             ;[3c73] a7
                    jp        $350b                         ;[3c74] c3 0b 35
                    ld        bc,$ffff                      ;[3c77] 01 ff ff
                    call      $2d2f                         ;[3c7a] cd 2f 2d
                    ld        d,h                           ;[3c7d] 54
                    ld        e,l                           ;[3c7e] 5d
                    inc       hl                            ;[3c7f] 23
                    dec       (hl)                          ;[3c80] 35
                    add       hl,$fffa                      ;[3c81] ed 34 fa ff
                    ld        ix,$3cfd                      ;[3c85] dd 21 fd 3c
                    jr        $3c95                         ;[3c89] 18 0a
                    ld        ix,$3cfb                      ;[3c8b] dd 21 fb 3c
                    jr        $3c95                         ;[3c8f] 18 04
                    ld        ix,$3cf9                      ;[3c91] dd 21 f9 3c
                    ld        a,(de)                        ;[3c95] 1a
                    or        (hl)                          ;[3c96] b6
                    jr        nz,$3ca7                      ;[3c97] 20 0e
                    push      de                            ;[3c99] d5
                    ld        b,$03                         ;[3c9a] 06 03
                    inc       hl                            ;[3c9c] 23
                    inc       de                            ;[3c9d] 13
                    ld        a,(de)                        ;[3c9e] 1a
                    call      $0013                         ;[3c9f] cd 13 00
                    ld        (hl),a                        ;[3ca2] 77
                    djnz      $3c9c                         ;[3ca3] 10 f7
                    pop       de                            ;[3ca5] d1
                    ret                                     ;[3ca6] c9

                    call      $3293                         ;[3ca7] cd 93 32
                    exx                                     ;[3caa] d9
                    push      hl                            ;[3cab] e5
                    exx                                     ;[3cac] d9
                    push      de                            ;[3cad] d5
                    push      hl                            ;[3cae] e5
                    call      $2f9b                         ;[3caf] cd 9b 2f
                    ld        b,a                           ;[3cb2] 47
                    ex        de,hl                         ;[3cb3] eb
                    call      $2f9b                         ;[3cb4] cd 9b 2f
                    ld        c,a                           ;[3cb7] 4f
                    cp        b                             ;[3cb8] b8
                    jr        nc,$3cbe                      ;[3cb9] 30 03
                    ld        a,b                           ;[3cbb] 78
                    ld        b,c                           ;[3cbc] 41
                    ex        de,hl                         ;[3cbd] eb
                    push      af                            ;[3cbe] f5
                    sub       b                             ;[3cbf] 90
                    call      $2fba                         ;[3cc0] cd ba 2f
                    call      $2fdd                         ;[3cc3] cd dd 2f
                    pop       af                            ;[3cc6] f1
                    pop       hl                            ;[3cc7] e1
                    ld        (hl),a                        ;[3cc8] 77
                    push      hl                            ;[3cc9] e5
                    exx                                     ;[3cca] d9
                    ld        a,l                           ;[3ccb] 7d
                    ex        af,af'                        ;[3ccc] 08
                    ld        a,h                           ;[3ccd] 7c
                    ld        hl,$5b5e                      ;[3cce] 21 5e 5b
                    ld        (hl),a                        ;[3cd1] 77
                    ex        af,af'                        ;[3cd2] 08
                    call      $0013                         ;[3cd3] cd 13 00
                    ex        af,af'                        ;[3cd6] 08
                    ld        (hl),b                        ;[3cd7] 70
                    ld        a,d                           ;[3cd8] 7a
                    call      $0013                         ;[3cd9] cd 13 00
                    ld        d,a                           ;[3cdc] 57
                    ld        (hl),c                        ;[3cdd] 71
                    ld        a,e                           ;[3cde] 7b
                    call      $0013                         ;[3cdf] cd 13 00
                    ld        e,a                           ;[3ce2] 5f
                    ex        af,af'                        ;[3ce3] 08
                    ld        l,a                           ;[3ce4] 6f
                    exx                                     ;[3ce5] d9
                    ld        hl,$5b5e                      ;[3ce6] 21 5e 5b
                    ld        (hl),c                        ;[3ce9] 71
                    ld        a,d                           ;[3cea] 7a
                    call      $0013                         ;[3ceb] cd 13 00
                    ld        d,a                           ;[3cee] 57
                    ld        (hl),b                        ;[3cef] 70
                    ld        a,e                           ;[3cf0] 7b
                    call      $0013                         ;[3cf1] cd 13 00
                    ld        e,a                           ;[3cf4] 5f
                    pop       hl                            ;[3cf5] e1
                    jp        $307c                         ;[3cf6] c3 7c 30
                    and       (hl)                          ;[3cf9] a6
                    ret                                     ;[3cfa] c9

                    or        (hl)                          ;[3cfb] b6
                    ret                                     ;[3cfc] c9

                    xor       (hl)                          ;[3cfd] ae
                    ret                                     ;[3cfe] c9

                    rst       $38                           ;[3cff] ff
                    nop                                     ;[3d00] 00
                    nop                                     ;[3d01] 00
                    nop                                     ;[3d02] 00
                    nop                                     ;[3d03] 00
                    nop                                     ;[3d04] 00
                    nop                                     ;[3d05] 00
                    nop                                     ;[3d06] 00
                    nop                                     ;[3d07] 00
                    nop                                     ;[3d08] 00
                    djnz      $3d1b                         ;[3d09] 10 10
                    djnz      $3d1d                         ;[3d0b] 10 10
                    nop                                     ;[3d0d] 00
                    djnz      $3d10                         ;[3d0e] 10 00
                    nop                                     ;[3d10] 00
                    inc       h                             ;[3d11] 24
                    inc       h                             ;[3d12] 24
                    nop                                     ;[3d13] 00
                    nop                                     ;[3d14] 00
                    nop                                     ;[3d15] 00
                    nop                                     ;[3d16] 00
                    nop                                     ;[3d17] 00
                    nop                                     ;[3d18] 00
                    inc       h                             ;[3d19] 24
                    ld        a,(hl)                        ;[3d1a] 7e
                    inc       h                             ;[3d1b] 24
                    inc       h                             ;[3d1c] 24
                    ld        a,(hl)                        ;[3d1d] 7e
                    inc       h                             ;[3d1e] 24
                    nop                                     ;[3d1f] 00
                    nop                                     ;[3d20] 00
                    ex        af,af'                        ;[3d21] 08
                    ld        a,$28                         ;[3d22] 3e 28
                    ld        a,$0a                         ;[3d24] 3e 0a
                    ld        a,$08                         ;[3d26] 3e 08
                    nop                                     ;[3d28] 00
                    ld        h,d                           ;[3d29] 62
                    ld        h,h                           ;[3d2a] 64
                    ex        af,af'                        ;[3d2b] 08
                    djnz      $3d54                         ;[3d2c] 10 26
                    ld        b,(hl)                        ;[3d2e] 46
                    nop                                     ;[3d2f] 00
                    nop                                     ;[3d30] 00
                    djnz      $3d5b                         ;[3d31] 10 28
                    djnz      $3d5f                         ;[3d33] 10 2a
                    ld        b,h                           ;[3d35] 44
                    ld        a,($0000)                     ;[3d36] 3a 00 00
                    ex        af,af'                        ;[3d39] 08
                    djnz      $3d3c                         ;[3d3a] 10 00
                    nop                                     ;[3d3c] 00
                    nop                                     ;[3d3d] 00
                    nop                                     ;[3d3e] 00
                    nop                                     ;[3d3f] 00
                    nop                                     ;[3d40] 00
                    inc       b                             ;[3d41] 04
                    ex        af,af'                        ;[3d42] 08
                    ex        af,af'                        ;[3d43] 08
                    ex        af,af'                        ;[3d44] 08
                    ex        af,af'                        ;[3d45] 08
                    inc       b                             ;[3d46] 04
                    nop                                     ;[3d47] 00
                    nop                                     ;[3d48] 00
                    jr        nz,$3d5b                      ;[3d49] 20 10
                    djnz      $3d5d                         ;[3d4b] 10 10
                    djnz      $3d6f                         ;[3d4d] 10 20
                    nop                                     ;[3d4f] 00
                    nop                                     ;[3d50] 00
                    nop                                     ;[3d51] 00
                    inc       d                             ;[3d52] 14
                    ex        af,af'                        ;[3d53] 08
                    ld        a,$08                         ;[3d54] 3e 08
                    inc       d                             ;[3d56] 14
                    nop                                     ;[3d57] 00
                    nop                                     ;[3d58] 00
                    nop                                     ;[3d59] 00
                    ex        af,af'                        ;[3d5a] 08
                    ex        af,af'                        ;[3d5b] 08
                    ld        a,$08                         ;[3d5c] 3e 08
                    ex        af,af'                        ;[3d5e] 08
                    nop                                     ;[3d5f] 00
                    nop                                     ;[3d60] 00
                    nop                                     ;[3d61] 00
                    nop                                     ;[3d62] 00
                    nop                                     ;[3d63] 00
                    nop                                     ;[3d64] 00
                    ex        af,af'                        ;[3d65] 08
                    ex        af,af'                        ;[3d66] 08
                    djnz      $3d69                         ;[3d67] 10 00
                    nop                                     ;[3d69] 00
                    nop                                     ;[3d6a] 00
                    nop                                     ;[3d6b] 00
                    ld        a,$00                         ;[3d6c] 3e 00
                    nop                                     ;[3d6e] 00
                    nop                                     ;[3d6f] 00
                    nop                                     ;[3d70] 00
                    nop                                     ;[3d71] 00
                    nop                                     ;[3d72] 00
                    nop                                     ;[3d73] 00
                    nop                                     ;[3d74] 00
                    jr        $3d8f                         ;[3d75] 18 18
                    nop                                     ;[3d77] 00
                    nop                                     ;[3d78] 00
                    nop                                     ;[3d79] 00
                    ld        (bc),a                        ;[3d7a] 02
                    inc       b                             ;[3d7b] 04
                    ex        af,af'                        ;[3d7c] 08
                    djnz      $3d9f                         ;[3d7d] 10 20
                    nop                                     ;[3d7f] 00
                    nop                                     ;[3d80] 00
                    inc       a                             ;[3d81] 3c
                    ld        b,(hl)                        ;[3d82] 46
                    ld        c,d                           ;[3d83] 4a
                    ld        d,d                           ;[3d84] 52
                    ld        h,d                           ;[3d85] 62
                    inc       a                             ;[3d86] 3c
                    nop                                     ;[3d87] 00
                    nop                                     ;[3d88] 00
                    jr        $3db3                         ;[3d89] 18 28
                    ex        af,af'                        ;[3d8b] 08
                    ex        af,af'                        ;[3d8c] 08
                    ex        af,af'                        ;[3d8d] 08
                    ld        a,$00                         ;[3d8e] 3e 00
                    nop                                     ;[3d90] 00
                    inc       a                             ;[3d91] 3c
                    ld        b,d                           ;[3d92] 42
                    ld        (bc),a                        ;[3d93] 02
                    inc       a                             ;[3d94] 3c
                    ld        b,b                           ;[3d95] 40
                    ld        a,(hl)                        ;[3d96] 7e
                    nop                                     ;[3d97] 00
                    nop                                     ;[3d98] 00
                    inc       a                             ;[3d99] 3c
                    ld        b,d                           ;[3d9a] 42
                    inc       c                             ;[3d9b] 0c
                    ld        (bc),a                        ;[3d9c] 02
                    ld        b,d                           ;[3d9d] 42
                    inc       a                             ;[3d9e] 3c
                    nop                                     ;[3d9f] 00
                    nop                                     ;[3da0] 00
                    ex        af,af'                        ;[3da1] 08
                    jr        $3dcc                         ;[3da2] 18 28
                    ld        c,b                           ;[3da4] 48
                    ld        a,(hl)                        ;[3da5] 7e
                    ex        af,af'                        ;[3da6] 08
                    nop                                     ;[3da7] 00
                    nop                                     ;[3da8] 00
                    ld        a,(hl)                        ;[3da9] 7e
                    ld        b,b                           ;[3daa] 40
                    ld        a,h                           ;[3dab] 7c
                    ld        (bc),a                        ;[3dac] 02
                    ld        b,d                           ;[3dad] 42
                    inc       a                             ;[3dae] 3c
                    nop                                     ;[3daf] 00
                    nop                                     ;[3db0] 00
                    inc       a                             ;[3db1] 3c
                    ld        b,b                           ;[3db2] 40
                    ld        a,h                           ;[3db3] 7c
                    ld        b,d                           ;[3db4] 42
                    ld        b,d                           ;[3db5] 42
                    inc       a                             ;[3db6] 3c
                    nop                                     ;[3db7] 00
                    nop                                     ;[3db8] 00
                    ld        a,(hl)                        ;[3db9] 7e
                    ld        (bc),a                        ;[3dba] 02
                    inc       b                             ;[3dbb] 04
                    ex        af,af'                        ;[3dbc] 08
                    djnz      $3dcf                         ;[3dbd] 10 10
                    nop                                     ;[3dbf] 00
                    nop                                     ;[3dc0] 00
                    inc       a                             ;[3dc1] 3c
                    ld        b,d                           ;[3dc2] 42
                    inc       a                             ;[3dc3] 3c
                    ld        b,d                           ;[3dc4] 42
                    ld        b,d                           ;[3dc5] 42
                    inc       a                             ;[3dc6] 3c
                    nop                                     ;[3dc7] 00
                    nop                                     ;[3dc8] 00
                    inc       a                             ;[3dc9] 3c
                    ld        b,d                           ;[3dca] 42
                    ld        b,d                           ;[3dcb] 42
                    ld        a,$02                         ;[3dcc] 3e 02
                    inc       a                             ;[3dce] 3c
                    nop                                     ;[3dcf] 00
                    nop                                     ;[3dd0] 00
                    nop                                     ;[3dd1] 00
                    nop                                     ;[3dd2] 00
                    djnz      $3dd5                         ;[3dd3] 10 00
                    nop                                     ;[3dd5] 00
                    djnz      $3dd8                         ;[3dd6] 10 00
                    nop                                     ;[3dd8] 00
                    nop                                     ;[3dd9] 00
                    djnz      $3ddc                         ;[3dda] 10 00
                    nop                                     ;[3ddc] 00
                    djnz      $3def                         ;[3ddd] 10 10
                    jr        nz,$3de1                      ;[3ddf] 20 00
                    nop                                     ;[3de1] 00
                    inc       b                             ;[3de2] 04
                    ex        af,af'                        ;[3de3] 08
                    djnz      $3dee                         ;[3de4] 10 08
                    inc       b                             ;[3de6] 04
                    nop                                     ;[3de7] 00
                    nop                                     ;[3de8] 00
                    nop                                     ;[3de9] 00
                    nop                                     ;[3dea] 00
                    ld        a,$00                         ;[3deb] 3e 00
                    ld        a,$00                         ;[3ded] 3e 00
                    nop                                     ;[3def] 00
                    nop                                     ;[3df0] 00
                    nop                                     ;[3df1] 00
                    djnz      $3dfc                         ;[3df2] 10 08
                    inc       b                             ;[3df4] 04
                    ex        af,af'                        ;[3df5] 08
                    djnz      $3df8                         ;[3df6] 10 00
                    nop                                     ;[3df8] 00
                    inc       a                             ;[3df9] 3c
                    ld        b,d                           ;[3dfa] 42
                    inc       b                             ;[3dfb] 04
                    ex        af,af'                        ;[3dfc] 08
                    nop                                     ;[3dfd] 00
                    ex        af,af'                        ;[3dfe] 08
                    nop                                     ;[3dff] 00
                    nop                                     ;[3e00] 00
                    inc       a                             ;[3e01] 3c
                    ld        c,d                           ;[3e02] 4a
                    ld        d,(hl)                        ;[3e03] 56
                    ld        e,(hl)                        ;[3e04] 5e
                    ld        b,b                           ;[3e05] 40
                    inc       a                             ;[3e06] 3c
                    nop                                     ;[3e07] 00
                    nop                                     ;[3e08] 00
                    inc       a                             ;[3e09] 3c
                    ld        b,d                           ;[3e0a] 42
                    ld        b,d                           ;[3e0b] 42
                    ld        a,(hl)                        ;[3e0c] 7e
                    ld        b,d                           ;[3e0d] 42
                    ld        b,d                           ;[3e0e] 42
                    nop                                     ;[3e0f] 00
                    nop                                     ;[3e10] 00
                    ld        a,h                           ;[3e11] 7c
                    ld        b,d                           ;[3e12] 42
                    ld        a,h                           ;[3e13] 7c
                    ld        b,d                           ;[3e14] 42
                    ld        b,d                           ;[3e15] 42
                    ld        a,h                           ;[3e16] 7c
                    nop                                     ;[3e17] 00
                    nop                                     ;[3e18] 00
                    inc       a                             ;[3e19] 3c
                    ld        b,d                           ;[3e1a] 42
                    ld        b,b                           ;[3e1b] 40
                    ld        b,b                           ;[3e1c] 40
                    ld        b,d                           ;[3e1d] 42
                    inc       a                             ;[3e1e] 3c
                    nop                                     ;[3e1f] 00
                    nop                                     ;[3e20] 00
                    ld        a,b                           ;[3e21] 78
                    ld        b,h                           ;[3e22] 44
                    ld        b,d                           ;[3e23] 42
                    ld        b,d                           ;[3e24] 42
                    ld        b,h                           ;[3e25] 44
                    ld        a,b                           ;[3e26] 78
                    nop                                     ;[3e27] 00
                    nop                                     ;[3e28] 00
                    ld        a,(hl)                        ;[3e29] 7e
                    ld        b,b                           ;[3e2a] 40
                    ld        a,h                           ;[3e2b] 7c
                    ld        b,b                           ;[3e2c] 40
                    ld        b,b                           ;[3e2d] 40
                    ld        a,(hl)                        ;[3e2e] 7e
                    nop                                     ;[3e2f] 00
                    nop                                     ;[3e30] 00
                    ld        a,(hl)                        ;[3e31] 7e
                    ld        b,b                           ;[3e32] 40
                    ld        a,h                           ;[3e33] 7c
                    ld        b,b                           ;[3e34] 40
                    ld        b,b                           ;[3e35] 40
                    ld        b,b                           ;[3e36] 40
                    nop                                     ;[3e37] 00
                    nop                                     ;[3e38] 00
                    inc       a                             ;[3e39] 3c
                    ld        b,d                           ;[3e3a] 42
                    ld        b,b                           ;[3e3b] 40
                    ld        c,(hl)                        ;[3e3c] 4e
                    ld        b,d                           ;[3e3d] 42
                    inc       a                             ;[3e3e] 3c
                    nop                                     ;[3e3f] 00
                    nop                                     ;[3e40] 00
                    ld        b,d                           ;[3e41] 42
                    ld        b,d                           ;[3e42] 42
                    ld        a,(hl)                        ;[3e43] 7e
                    ld        b,d                           ;[3e44] 42
                    ld        b,d                           ;[3e45] 42
                    ld        b,d                           ;[3e46] 42
                    nop                                     ;[3e47] 00
                    nop                                     ;[3e48] 00
                    ld        a,$08                         ;[3e49] 3e 08
                    ex        af,af'                        ;[3e4b] 08
                    ex        af,af'                        ;[3e4c] 08
                    ex        af,af'                        ;[3e4d] 08
                    ld        a,$00                         ;[3e4e] 3e 00
                    nop                                     ;[3e50] 00
                    ld        (bc),a                        ;[3e51] 02
                    ld        (bc),a                        ;[3e52] 02
                    ld        (bc),a                        ;[3e53] 02
                    ld        b,d                           ;[3e54] 42
                    ld        b,d                           ;[3e55] 42
                    inc       a                             ;[3e56] 3c
                    nop                                     ;[3e57] 00
                    nop                                     ;[3e58] 00
                    ld        b,h                           ;[3e59] 44
                    ld        c,b                           ;[3e5a] 48
                    ld        (hl),b                        ;[3e5b] 70
                    ld        c,b                           ;[3e5c] 48
                    ld        b,h                           ;[3e5d] 44
                    ld        b,d                           ;[3e5e] 42
                    nop                                     ;[3e5f] 00
                    nop                                     ;[3e60] 00
                    ld        b,b                           ;[3e61] 40
                    ld        b,b                           ;[3e62] 40
                    ld        b,b                           ;[3e63] 40
                    ld        b,b                           ;[3e64] 40
                    ld        b,b                           ;[3e65] 40
                    ld        a,(hl)                        ;[3e66] 7e
                    nop                                     ;[3e67] 00
                    nop                                     ;[3e68] 00
                    ld        b,d                           ;[3e69] 42
                    ld        h,(hl)                        ;[3e6a] 66
                    ld        e,d                           ;[3e6b] 5a
                    ld        b,d                           ;[3e6c] 42
                    ld        b,d                           ;[3e6d] 42
                    ld        b,d                           ;[3e6e] 42
                    nop                                     ;[3e6f] 00
                    nop                                     ;[3e70] 00
                    ld        b,d                           ;[3e71] 42
                    ld        h,d                           ;[3e72] 62
                    ld        d,d                           ;[3e73] 52
                    ld        c,d                           ;[3e74] 4a
                    ld        b,(hl)                        ;[3e75] 46
                    ld        b,d                           ;[3e76] 42
                    nop                                     ;[3e77] 00
                    nop                                     ;[3e78] 00
                    inc       a                             ;[3e79] 3c
                    ld        b,d                           ;[3e7a] 42
                    ld        b,d                           ;[3e7b] 42
                    ld        b,d                           ;[3e7c] 42
                    ld        b,d                           ;[3e7d] 42
                    inc       a                             ;[3e7e] 3c
                    nop                                     ;[3e7f] 00
                    nop                                     ;[3e80] 00
                    ld        a,h                           ;[3e81] 7c
                    ld        b,d                           ;[3e82] 42
                    ld        b,d                           ;[3e83] 42
                    ld        a,h                           ;[3e84] 7c
                    ld        b,b                           ;[3e85] 40
                    ld        b,b                           ;[3e86] 40
                    nop                                     ;[3e87] 00
                    nop                                     ;[3e88] 00
                    inc       a                             ;[3e89] 3c
                    ld        b,d                           ;[3e8a] 42
                    ld        b,d                           ;[3e8b] 42
                    ld        d,d                           ;[3e8c] 52
                    ld        c,d                           ;[3e8d] 4a
                    inc       a                             ;[3e8e] 3c
                    nop                                     ;[3e8f] 00
                    nop                                     ;[3e90] 00
                    ld        a,h                           ;[3e91] 7c
                    ld        b,d                           ;[3e92] 42
                    ld        b,d                           ;[3e93] 42
                    ld        a,h                           ;[3e94] 7c
                    ld        b,h                           ;[3e95] 44
                    ld        b,d                           ;[3e96] 42
                    nop                                     ;[3e97] 00
                    nop                                     ;[3e98] 00
                    inc       a                             ;[3e99] 3c
                    ld        b,b                           ;[3e9a] 40
                    inc       a                             ;[3e9b] 3c
                    ld        (bc),a                        ;[3e9c] 02
                    ld        b,d                           ;[3e9d] 42
                    inc       a                             ;[3e9e] 3c
                    nop                                     ;[3e9f] 00
                    nop                                     ;[3ea0] 00
                    cp        $10                           ;[3ea1] fe 10
                    djnz      $3eb5                         ;[3ea3] 10 10
                    djnz      $3eb7                         ;[3ea5] 10 10
                    nop                                     ;[3ea7] 00
                    nop                                     ;[3ea8] 00
                    ld        b,d                           ;[3ea9] 42
                    ld        b,d                           ;[3eaa] 42
                    ld        b,d                           ;[3eab] 42
                    ld        b,d                           ;[3eac] 42
                    ld        b,d                           ;[3ead] 42
                    inc       a                             ;[3eae] 3c
                    nop                                     ;[3eaf] 00
                    nop                                     ;[3eb0] 00
                    ld        b,d                           ;[3eb1] 42
                    ld        b,d                           ;[3eb2] 42
                    ld        b,d                           ;[3eb3] 42
                    ld        b,d                           ;[3eb4] 42
                    inc       h                             ;[3eb5] 24
                    jr        $3eb8                         ;[3eb6] 18 00
                    nop                                     ;[3eb8] 00
                    ld        b,d                           ;[3eb9] 42
                    ld        b,d                           ;[3eba] 42
                    ld        b,d                           ;[3ebb] 42
                    ld        b,d                           ;[3ebc] 42
                    ld        e,d                           ;[3ebd] 5a
                    inc       h                             ;[3ebe] 24
                    nop                                     ;[3ebf] 00
                    nop                                     ;[3ec0] 00
                    ld        b,d                           ;[3ec1] 42
                    inc       h                             ;[3ec2] 24
                    jr        $3edd                         ;[3ec3] 18 18
                    inc       h                             ;[3ec5] 24
                    ld        b,d                           ;[3ec6] 42
                    nop                                     ;[3ec7] 00
                    nop                                     ;[3ec8] 00
                    add       d                             ;[3ec9] 82
                    ld        b,h                           ;[3eca] 44
                    jr        z,$3edd                       ;[3ecb] 28 10
                    djnz      $3edf                         ;[3ecd] 10 10
                    nop                                     ;[3ecf] 00
                    nop                                     ;[3ed0] 00
                    ld        a,(hl)                        ;[3ed1] 7e
                    inc       b                             ;[3ed2] 04
                    ex        af,af'                        ;[3ed3] 08
                    djnz      $3ef6                         ;[3ed4] 10 20
                    ld        a,(hl)                        ;[3ed6] 7e
                    nop                                     ;[3ed7] 00
                    nop                                     ;[3ed8] 00
                    ld        c,$08                         ;[3ed9] 0e 08
                    ex        af,af'                        ;[3edb] 08
                    ex        af,af'                        ;[3edc] 08
                    ex        af,af'                        ;[3edd] 08
                    ld        c,$00                         ;[3ede] 0e 00
                    nop                                     ;[3ee0] 00
                    nop                                     ;[3ee1] 00
                    ld        b,b                           ;[3ee2] 40
                    jr        nz,$3ef5                      ;[3ee3] 20 10
                    ex        af,af'                        ;[3ee5] 08
                    inc       b                             ;[3ee6] 04
                    nop                                     ;[3ee7] 00
                    nop                                     ;[3ee8] 00
                    ld        (hl),b                        ;[3ee9] 70
                    djnz      $3efc                         ;[3eea] 10 10
                    djnz      $3efe                         ;[3eec] 10 10
                    ld        (hl),b                        ;[3eee] 70
                    nop                                     ;[3eef] 00
                    nop                                     ;[3ef0] 00
                    djnz      $3f2b                         ;[3ef1] 10 38
                    ld        d,h                           ;[3ef3] 54
                    djnz      $3f06                         ;[3ef4] 10 10
                    djnz      $3ef8                         ;[3ef6] 10 00
                    nop                                     ;[3ef8] 00
                    nop                                     ;[3ef9] 00
                    nop                                     ;[3efa] 00
                    nop                                     ;[3efb] 00
                    nop                                     ;[3efc] 00
                    nop                                     ;[3efd] 00
                    nop                                     ;[3efe] 00
                    rst       $38                           ;[3eff] ff
                    nop                                     ;[3f00] 00
                    inc       e                             ;[3f01] 1c
                    ld        ($2078),hl                    ;[3f02] 22 78 20
                    jr        nz,$3f85                      ;[3f05] 20 7e
                    nop                                     ;[3f07] 00
                    nop                                     ;[3f08] 00
                    nop                                     ;[3f09] 00
                    jr        c,$3f10                       ;[3f0a] 38 04
                    inc       a                             ;[3f0c] 3c
                    ld        b,h                           ;[3f0d] 44
                    inc       a                             ;[3f0e] 3c
                    nop                                     ;[3f0f] 00
                    nop                                     ;[3f10] 00
                    jr        nz,$3f33                      ;[3f11] 20 20
                    inc       a                             ;[3f13] 3c
                    ld        ($3c22),hl                    ;[3f14] 22 22 3c
                    nop                                     ;[3f17] 00
                    nop                                     ;[3f18] 00
                    nop                                     ;[3f19] 00
                    inc       e                             ;[3f1a] 1c
                    jr        nz,$3f3d                      ;[3f1b] 20 20
                    jr        nz,$3f3b                      ;[3f1d] 20 1c
                    nop                                     ;[3f1f] 00
                    nop                                     ;[3f20] 00
                    inc       b                             ;[3f21] 04
                    inc       b                             ;[3f22] 04
                    inc       a                             ;[3f23] 3c
                    ld        b,h                           ;[3f24] 44
                    ld        b,h                           ;[3f25] 44
                    inc       a                             ;[3f26] 3c
                    nop                                     ;[3f27] 00
                    nop                                     ;[3f28] 00
                    nop                                     ;[3f29] 00
                    jr        c,$3f70                       ;[3f2a] 38 44
                    ld        a,b                           ;[3f2c] 78
                    ld        b,b                           ;[3f2d] 40
                    inc       a                             ;[3f2e] 3c
                    nop                                     ;[3f2f] 00
                    nop                                     ;[3f30] 00
                    inc       c                             ;[3f31] 0c
                    djnz      $3f4c                         ;[3f32] 10 18
                    djnz      $3f46                         ;[3f34] 10 10
                    djnz      $3f38                         ;[3f36] 10 00
                    nop                                     ;[3f38] 00
                    nop                                     ;[3f39] 00
                    inc       a                             ;[3f3a] 3c
                    ld        b,h                           ;[3f3b] 44
                    ld        b,h                           ;[3f3c] 44
                    inc       a                             ;[3f3d] 3c
                    inc       b                             ;[3f3e] 04
                    jr        c,$3f41                       ;[3f3f] 38 00
                    ld        b,b                           ;[3f41] 40
                    ld        b,b                           ;[3f42] 40
                    ld        a,b                           ;[3f43] 78
                    ld        b,h                           ;[3f44] 44
                    ld        b,h                           ;[3f45] 44
                    ld        b,h                           ;[3f46] 44
                    nop                                     ;[3f47] 00
                    nop                                     ;[3f48] 00
                    djnz      $3f4b                         ;[3f49] 10 00
                    jr        nc,$3f5d                      ;[3f4b] 30 10
                    djnz      $3f87                         ;[3f4d] 10 38
                    nop                                     ;[3f4f] 00
                    nop                                     ;[3f50] 00
                    inc       b                             ;[3f51] 04
                    nop                                     ;[3f52] 00
                    inc       b                             ;[3f53] 04
                    inc       b                             ;[3f54] 04
                    inc       b                             ;[3f55] 04
                    inc       h                             ;[3f56] 24
                    jr        $3f59                         ;[3f57] 18 00
                    jr        nz,$3f83                      ;[3f59] 20 28
                    jr        nc,$3f8d                      ;[3f5b] 30 30
                    jr        z,$3f83                       ;[3f5d] 28 24
                    nop                                     ;[3f5f] 00
                    nop                                     ;[3f60] 00
                    djnz      $3f73                         ;[3f61] 10 10
                    djnz      $3f75                         ;[3f63] 10 10
                    djnz      $3f73                         ;[3f65] 10 0c
                    nop                                     ;[3f67] 00
                    nop                                     ;[3f68] 00
                    nop                                     ;[3f69] 00
                    ld        l,b                           ;[3f6a] 68
                    ld        d,h                           ;[3f6b] 54
                    ld        d,h                           ;[3f6c] 54
                    ld        d,h                           ;[3f6d] 54
                    ld        d,h                           ;[3f6e] 54
                    nop                                     ;[3f6f] 00
                    nop                                     ;[3f70] 00
                    nop                                     ;[3f71] 00
                    ld        a,b                           ;[3f72] 78
                    ld        b,h                           ;[3f73] 44
                    ld        b,h                           ;[3f74] 44
                    ld        b,h                           ;[3f75] 44
                    ld        b,h                           ;[3f76] 44
                    nop                                     ;[3f77] 00
                    nop                                     ;[3f78] 00
                    nop                                     ;[3f79] 00
                    jr        c,$3fc0                       ;[3f7a] 38 44
                    ld        b,h                           ;[3f7c] 44
                    ld        b,h                           ;[3f7d] 44
                    jr        c,$3f80                       ;[3f7e] 38 00
                    nop                                     ;[3f80] 00
                    nop                                     ;[3f81] 00
                    ld        a,b                           ;[3f82] 78
                    ld        b,h                           ;[3f83] 44
                    ld        b,h                           ;[3f84] 44
                    ld        a,b                           ;[3f85] 78
                    ld        b,b                           ;[3f86] 40
                    ld        b,b                           ;[3f87] 40
                    nop                                     ;[3f88] 00
                    nop                                     ;[3f89] 00
                    inc       a                             ;[3f8a] 3c
                    ld        b,h                           ;[3f8b] 44
                    ld        b,h                           ;[3f8c] 44
                    inc       a                             ;[3f8d] 3c
                    inc       b                             ;[3f8e] 04
                    ld        b,$00                         ;[3f8f] 06 00
                    nop                                     ;[3f91] 00
                    inc       e                             ;[3f92] 1c
                    jr        nz,$3fb5                      ;[3f93] 20 20
                    jr        nz,$3fb7                      ;[3f95] 20 20
                    nop                                     ;[3f97] 00
                    nop                                     ;[3f98] 00
                    nop                                     ;[3f99] 00
                    jr        c,$3fdc                       ;[3f9a] 38 40
                    jr        c,$3fa2                       ;[3f9c] 38 04
                    ld        a,b                           ;[3f9e] 78
                    nop                                     ;[3f9f] 00
                    nop                                     ;[3fa0] 00
                    djnz      $3fdb                         ;[3fa1] 10 38
                    djnz      $3fb5                         ;[3fa3] 10 10
                    djnz      $3fb3                         ;[3fa5] 10 0c
                    nop                                     ;[3fa7] 00
                    nop                                     ;[3fa8] 00
                    nop                                     ;[3fa9] 00
                    ld        b,h                           ;[3faa] 44
                    ld        b,h                           ;[3fab] 44
                    ld        b,h                           ;[3fac] 44
                    ld        b,h                           ;[3fad] 44
                    jr        c,$3fb0                       ;[3fae] 38 00
                    nop                                     ;[3fb0] 00
                    nop                                     ;[3fb1] 00
                    ld        b,h                           ;[3fb2] 44
                    ld        b,h                           ;[3fb3] 44
                    jr        z,$3fde                       ;[3fb4] 28 28
                    djnz      $3fb8                         ;[3fb6] 10 00
                    nop                                     ;[3fb8] 00
                    nop                                     ;[3fb9] 00
                    ld        b,h                           ;[3fba] 44
                    ld        d,h                           ;[3fbb] 54
                    ld        d,h                           ;[3fbc] 54
                    ld        d,h                           ;[3fbd] 54
                    jr        z,$3fc0                       ;[3fbe] 28 00
                    nop                                     ;[3fc0] 00
                    nop                                     ;[3fc1] 00
                    ld        b,h                           ;[3fc2] 44
                    jr        z,$3fd5                       ;[3fc3] 28 10
                    jr        z,$400b                       ;[3fc5] 28 44
                    nop                                     ;[3fc7] 00
                    nop                                     ;[3fc8] 00
                    nop                                     ;[3fc9] 00
                    ld        b,h                           ;[3fca] 44
                    ld        b,h                           ;[3fcb] 44
                    ld        b,h                           ;[3fcc] 44
                    inc       a                             ;[3fcd] 3c
                    inc       b                             ;[3fce] 04
                    jr        c,$3fd1                       ;[3fcf] 38 00
                    nop                                     ;[3fd1] 00
                    ld        a,h                           ;[3fd2] 7c
                    ex        af,af'                        ;[3fd3] 08
                    djnz      $3ff6                         ;[3fd4] 10 20
                    ld        a,h                           ;[3fd6] 7c
                    nop                                     ;[3fd7] 00
                    nop                                     ;[3fd8] 00
                    ld        c,$08                         ;[3fd9] 0e 08
                    jr        nc,$3fe5                      ;[3fdb] 30 08
                    ex        af,af'                        ;[3fdd] 08
                    ld        c,$00                         ;[3fde] 0e 00
                    nop                                     ;[3fe0] 00
                    ex        af,af'                        ;[3fe1] 08
                    ex        af,af'                        ;[3fe2] 08
                    ex        af,af'                        ;[3fe3] 08
                    ex        af,af'                        ;[3fe4] 08
                    ex        af,af'                        ;[3fe5] 08
                    ex        af,af'                        ;[3fe6] 08
                    nop                                     ;[3fe7] 00
                    nop                                     ;[3fe8] 00
                    ld        (hl),b                        ;[3fe9] 70
                    djnz      $3ff8                         ;[3fea] 10 0c
                    djnz      $3ffe                         ;[3fec] 10 10
                    ld        (hl),b                        ;[3fee] 70
                    nop                                     ;[3fef] 00
                    nop                                     ;[3ff0] 00
                    inc       d                             ;[3ff1] 14
                    jr        z,$3ff4                       ;[3ff2] 28 00
                    nop                                     ;[3ff4] 00
                    nop                                     ;[3ff5] 00
                    nop                                     ;[3ff6] 00
                    nop                                     ;[3ff7] 00
                    inc       a                             ;[3ff8] 3c
                    ld        b,d                           ;[3ff9] 42
                    sbc       c                             ;[3ffa] 99
                    and       c                             ;[3ffb] a1
                    and       c                             ;[3ffc] a1
                    sbc       c                             ;[3ffd] 99
                    ld        b,d                           ;[3ffe] 42
                    inc       a                             ;[3fff] 3c
