                    di                                      ;[0000] f3
                    xor       a                             ;[0001] af
                    ld        de,$ffff                      ;[0002] 11 ff ff
                    jp        $11cb                         ;[0005] c3 cb 11
                    ld        hl,($5c5d)                    ;[0008] 2a 5d 5c
                    ld        ($5c5f),hl                    ;[000b] 22 5f 5c
                    jr        $0053                         ;[000e] 18 43
                    jp        $15f2                         ;[0010] c3 f2 15
                    rst       $38                           ;[0013] ff
                    rst       $38                           ;[0014] ff
                    rst       $38                           ;[0015] ff
                    rst       $38                           ;[0016] ff
                    rst       $38                           ;[0017] ff
                    ld        hl,($5c5d)                    ;[0018] 2a 5d 5c
                    ld        a,(hl)                        ;[001b] 7e
                    call      $007d                         ;[001c] cd 7d 00
                    ret       nc                            ;[001f] d0
                    call      $0074                         ;[0020] cd 74 00
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
                    call      $02bf                         ;[004a] cd bf 02
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
                    push      hl                            ;[0067] e5
                    ld        hl,($5cb0)                    ;[0068] 2a b0 5c
                    ld        a,h                           ;[006b] 7c
                    or        l                             ;[006c] b5
                    jr        nz,$0070                      ;[006d] 20 01
                    jp        (hl)                          ;[006f] e9
                    pop       hl                            ;[0070] e1
                    pop       af                            ;[0071] f1
                    retn                                    ;[0072] ed 45

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
                    call      $24fb                         ;[04aa] cd fb 24
                    ld        a,($5c3b)                     ;[04ad] 3a 3b 5c
                    add       a                             ;[04b0] 87
                    jp        m,$1c8a                       ;[04b1] fa 8a 1c
                    pop       hl                            ;[04b4] e1
                    ret       nc                            ;[04b5] d0
                    push      hl                            ;[04b6] e5
                    call      $2bf1                         ;[04b7] cd f1 2b
                    ld        h,d                           ;[04ba] 62
                    ld        l,e                           ;[04bb] 6b
                    dec       c                             ;[04bc] 0d
                    ret       m                             ;[04bd] f8
                    add       hl,bc                         ;[04be] 09
                    set       7,(hl)                        ;[04bf] cb fe
                    ret                                     ;[04c1] c9

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
                    jr        c,$0554                       ;[0550] 38 02
                    rst       $08                           ;[0552] cf
                    inc       c                             ;[0553] 0c
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

                    pop       af                            ;[0605] f1
                    ld        a,($5c74)                     ;[0606] 3a 74 5c
                    sub       $e0                           ;[0609] d6 e0
                    ld        ($5c74),a                     ;[060b] 32 74 5c
                    call      $1c8c                         ;[060e] cd 8c 1c
                    call      $2530                         ;[0611] cd 30 25
                    jr        z,$0652                       ;[0614] 28 3c
                    ld        bc,$0011                      ;[0616] 01 11 00
                    ld        a,($5c74)                     ;[0619] 3a 74 5c
                    and       a                             ;[061c] a7
                    jr        z,$0621                       ;[061d] 28 02
                    ld        c,$22                         ;[061f] 0e 22
                    rst       $30                           ;[0621] f7
                    push      de                            ;[0622] d5
                    pop       ix                            ;[0623] dd e1
                    ld        b,$0b                         ;[0625] 06 0b
                    ld        a,$20                         ;[0627] 3e 20
                    ld        (de),a                        ;[0629] 12
                    inc       de                            ;[062a] 13
                    djnz      $0629                         ;[062b] 10 fc
                    ld        (ix+$01),$ff                  ;[062d] dd 36 01 ff
                    call      $2bf1                         ;[0631] cd f1 2b
                    ld        hl,$fff6                      ;[0634] 21 f6 ff
                    dec       bc                            ;[0637] 0b
                    add       hl,bc                         ;[0638] 09
                    inc       bc                            ;[0639] 03
                    jr        nc,$064b                      ;[063a] 30 0f
                    ld        a,($5c74)                     ;[063c] 3a 74 5c
                    and       a                             ;[063f] a7
                    jr        nz,$0644                      ;[0640] 20 02
                    rst       $08                           ;[0642] cf
                    ld        c,$78                         ;[0643] 0e 78
                    or        c                             ;[0645] b1
                    jr        z,$0652                       ;[0646] 28 0a
                    ld        bc,$000a                      ;[0648] 01 0a 00
                    push      ix                            ;[064b] dd e5
                    pop       hl                            ;[064d] e1
                    inc       hl                            ;[064e] 23
                    ex        de,hl                         ;[064f] eb
                    ldir                                    ;[0650] ed b0
                    rst       $18                           ;[0652] df
                    cp        $e4                           ;[0653] fe e4
                    jr        nz,$06a0                      ;[0655] 20 49
                    ld        a,($5c74)                     ;[0657] 3a 74 5c
                    cp        $03                           ;[065a] fe 03
                    jp        z,$1c8a                       ;[065c] ca 8a 1c
                    rst       $20                           ;[065f] e7
                    call      $28b2                         ;[0660] cd b2 28
                    set       7,c                           ;[0663] cb f9
                    jr        nc,$0672                      ;[0665] 30 0b
                    ld        hl,$0000                      ;[0667] 21 00 00
                    ld        a,($5c74)                     ;[066a] 3a 74 5c
                    dec       a                             ;[066d] 3d
                    jr        z,$0685                       ;[066e] 28 15
                    rst       $08                           ;[0670] cf
                    ld        bc,$8ac2                      ;[0671] 01 c2 8a
                    inc       e                             ;[0674] 1c
                    call      $2530                         ;[0675] cd 30 25
                    jr        z,$0692                       ;[0678] 28 18
                    inc       hl                            ;[067a] 23
                    ld        a,(hl)                        ;[067b] 7e
                    ld        (ix+$0b),a                    ;[067c] dd 77 0b
                    inc       hl                            ;[067f] 23
                    ld        a,(hl)                        ;[0680] 7e
                    ld        (ix+$0c),a                    ;[0681] dd 77 0c
                    inc       hl                            ;[0684] 23
                    ld        (ix+$0e),c                    ;[0685] dd 71 0e
                    ld        a,$01                         ;[0688] 3e 01
                    bit       6,c                           ;[068a] cb 71
                    jr        z,$068f                       ;[068c] 28 01
                    inc       a                             ;[068e] 3c
                    ld        (ix+$00),a                    ;[068f] dd 77 00
                    ex        de,hl                         ;[0692] eb
                    rst       $20                           ;[0693] e7
                    cp        $29                           ;[0694] fe 29
                    jr        nz,$0672                      ;[0696] 20 da
                    rst       $20                           ;[0698] e7
                    call      $1bee                         ;[0699] cd ee 1b
                    ex        de,hl                         ;[069c] eb
                    jp        $075a                         ;[069d] c3 5a 07
                    cp        $aa                           ;[06a0] fe aa
                    jr        nz,$06c3                      ;[06a2] 20 1f
                    ld        a,($5c74)                     ;[06a4] 3a 74 5c
                    cp        $03                           ;[06a7] fe 03
                    jp        z,$1c8a                       ;[06a9] ca 8a 1c
                    rst       $20                           ;[06ac] e7
                    call      $1bee                         ;[06ad] cd ee 1b
                    ld        (ix+$0b),$00                  ;[06b0] dd 36 0b 00
                    ld        (ix+$0c),$1b                  ;[06b4] dd 36 0c 1b
                    ld        hl,$4000                      ;[06b8] 21 00 40
                    ld        (ix+$0d),l                    ;[06bb] dd 75 0d
                    ld        (ix+$0e),h                    ;[06be] dd 74 0e
                    jr        $0710                         ;[06c1] 18 4d
                    cp        $af                           ;[06c3] fe af
                    jr        nz,$0716                      ;[06c5] 20 4f
                    ld        a,($5c74)                     ;[06c7] 3a 74 5c
                    cp        $03                           ;[06ca] fe 03
                    jp        z,$1c8a                       ;[06cc] ca 8a 1c
                    rst       $20                           ;[06cf] e7
                    call      $2048                         ;[06d0] cd 48 20
                    jr        nz,$06e1                      ;[06d3] 20 0c
                    ld        a,($5c74)                     ;[06d5] 3a 74 5c
                    and       a                             ;[06d8] a7
                    jp        z,$1c8a                       ;[06d9] ca 8a 1c
                    call      $1ce6                         ;[06dc] cd e6 1c
                    jr        $06f0                         ;[06df] 18 0f
                    call      $1c82                         ;[06e1] cd 82 1c
                    rst       $18                           ;[06e4] df
                    cp        $2c                           ;[06e5] fe 2c
                    jr        z,$06f5                       ;[06e7] 28 0c
                    ld        a,($5c74)                     ;[06e9] 3a 74 5c
                    and       a                             ;[06ec] a7
                    jp        z,$1c8a                       ;[06ed] ca 8a 1c
                    call      $1ce6                         ;[06f0] cd e6 1c
                    jr        $06f9                         ;[06f3] 18 04
                    rst       $20                           ;[06f5] e7
                    call      $1c82                         ;[06f6] cd 82 1c
                    call      $1bee                         ;[06f9] cd ee 1b
                    call      $1e99                         ;[06fc] cd 99 1e
                    ld        (ix+$0b),c                    ;[06ff] dd 71 0b
                    ld        (ix+$0c),b                    ;[0702] dd 70 0c
                    call      $1e99                         ;[0705] cd 99 1e
                    ld        (ix+$0d),c                    ;[0708] dd 71 0d
                    ld        (ix+$0e),b                    ;[070b] dd 70 0e
                    ld        h,b                           ;[070e] 60
                    ld        l,c                           ;[070f] 69
                    ld        (ix+$00),$03                  ;[0710] dd 36 00 03
                    jr        $075a                         ;[0714] 18 44
                    cp        $ca                           ;[0716] fe ca
                    jr        z,$0723                       ;[0718] 28 09
                    call      $1bee                         ;[071a] cd ee 1b
                    ld        (ix+$0e),$80                  ;[071d] dd 36 0e 80
                    jr        $073a                         ;[0721] 18 17
                    ld        a,($5c74)                     ;[0723] 3a 74 5c
                    and       a                             ;[0726] a7
                    jp        nz,$1c8a                      ;[0727] c2 8a 1c
                    rst       $20                           ;[072a] e7
                    call      $1c82                         ;[072b] cd 82 1c
                    call      $1bee                         ;[072e] cd ee 1b
                    call      $1e99                         ;[0731] cd 99 1e
                    ld        (ix+$0d),c                    ;[0734] dd 71 0d
                    ld        (ix+$0e),b                    ;[0737] dd 70 0e
                    ld        (ix+$00),$00                  ;[073a] dd 36 00 00
                    ld        hl,($5c59)                    ;[073e] 2a 59 5c
                    ld        de,($5c53)                    ;[0741] ed 5b 53 5c
                    scf                                     ;[0745] 37
                    sbc       hl,de                         ;[0746] ed 52
                    ld        (ix+$0b),l                    ;[0748] dd 75 0b
                    ld        (ix+$0c),h                    ;[074b] dd 74 0c
                    ld        hl,($5c4b)                    ;[074e] 2a 4b 5c
                    sbc       hl,de                         ;[0751] ed 52
                    ld        (ix+$0f),l                    ;[0753] dd 75 0f
                    ld        (ix+$10),h                    ;[0756] dd 74 10
                    ex        de,hl                         ;[0759] eb
                    ld        a,($5c74)                     ;[075a] 3a 74 5c
                    and       a                             ;[075d] a7
                    jp        z,$0970                       ;[075e] ca 70 09
                    push      hl                            ;[0761] e5
                    ld        bc,$0011                      ;[0762] 01 11 00
                    add       ix,bc                         ;[0765] dd 09
                    push      ix                            ;[0767] dd e5
                    ld        de,$0011                      ;[0769] 11 11 00
                    xor       a                             ;[076c] af
                    scf                                     ;[076d] 37
                    call      $0556                         ;[076e] cd 56 05
                    pop       ix                            ;[0771] dd e1
                    jr        nc,$0767                      ;[0773] 30 f2
                    ld        a,$fe                         ;[0775] 3e fe
                    call      $1601                         ;[0777] cd 01 16
                    ld        (iy+$52),$03                  ;[077a] fd 36 52 03
                    ld        c,$80                         ;[077e] 0e 80
                    ld        a,(ix+$00)                    ;[0780] dd 7e 00
                    cp        (ix-$11)                      ;[0783] dd be ef
                    jr        nz,$078a                      ;[0786] 20 02
                    ld        c,$f6                         ;[0788] 0e f6
                    cp        $04                           ;[078a] fe 04
                    jr        nc,$0767                      ;[078c] 30 d9
                    ld        de,$09c0                      ;[078e] 11 c0 09
                    push      bc                            ;[0791] c5
                    call      $0c0a                         ;[0792] cd 0a 0c
                    pop       bc                            ;[0795] c1
                    push      ix                            ;[0796] dd e5
                    pop       de                            ;[0798] d1
                    ld        hl,$fff0                      ;[0799] 21 f0 ff
                    add       hl,de                         ;[079c] 19
                    ld        b,$0a                         ;[079d] 06 0a
                    ld        a,(hl)                        ;[079f] 7e
                    inc       a                             ;[07a0] 3c
                    jr        nz,$07a6                      ;[07a1] 20 03
                    ld        a,c                           ;[07a3] 79
                    add       b                             ;[07a4] 80
                    ld        c,a                           ;[07a5] 4f
                    inc       de                            ;[07a6] 13
                    ld        a,(de)                        ;[07a7] 1a
                    cp        (hl)                          ;[07a8] be
                    inc       hl                            ;[07a9] 23
                    jr        nz,$07ad                      ;[07aa] 20 01
                    inc       c                             ;[07ac] 0c
                    rst       $10                           ;[07ad] d7
                    djnz      $07a6                         ;[07ae] 10 f6
                    bit       7,c                           ;[07b0] cb 79
                    jr        nz,$0767                      ;[07b2] 20 b3
                    ld        a,$0d                         ;[07b4] 3e 0d
                    rst       $10                           ;[07b6] d7
                    pop       hl                            ;[07b7] e1
                    ld        a,(ix+$00)                    ;[07b8] dd 7e 00
                    cp        $03                           ;[07bb] fe 03
                    jr        z,$07cb                       ;[07bd] 28 0c
                    ld        a,($5c74)                     ;[07bf] 3a 74 5c
                    dec       a                             ;[07c2] 3d
                    jp        z,$0808                       ;[07c3] ca 08 08
                    cp        $02                           ;[07c6] fe 02
                    jp        z,$08b6                       ;[07c8] ca b6 08
                    push      hl                            ;[07cb] e5
                    ld        l,(ix-$06)                    ;[07cc] dd 6e fa
                    ld        h,(ix-$05)                    ;[07cf] dd 66 fb
                    ld        e,(ix+$0b)                    ;[07d2] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[07d5] dd 56 0c
                    ld        a,h                           ;[07d8] 7c
                    or        l                             ;[07d9] b5
                    jr        z,$07e9                       ;[07da] 28 0d
                    sbc       hl,de                         ;[07dc] ed 52
                    jr        c,$0806                       ;[07de] 38 26
                    jr        z,$07e9                       ;[07e0] 28 07
                    ld        a,(ix+$00)                    ;[07e2] dd 7e 00
                    cp        $03                           ;[07e5] fe 03
                    jr        nz,$0806                      ;[07e7] 20 1d
                    pop       hl                            ;[07e9] e1
                    ld        a,h                           ;[07ea] 7c
                    or        l                             ;[07eb] b5
                    jr        nz,$07f4                      ;[07ec] 20 06
                    ld        l,(ix+$0d)                    ;[07ee] dd 6e 0d
                    ld        h,(ix+$0e)                    ;[07f1] dd 66 0e
                    push      hl                            ;[07f4] e5
                    pop       ix                            ;[07f5] dd e1
                    ld        a,($5c74)                     ;[07f7] 3a 74 5c
                    cp        $02                           ;[07fa] fe 02
                    scf                                     ;[07fc] 37
                    jr        nz,$0800                      ;[07fd] 20 01
                    and       a                             ;[07ff] a7
                    ld        a,$ff                         ;[0800] 3e ff
                    call      $0556                         ;[0802] cd 56 05
                    ret       c                             ;[0805] d8
                    rst       $08                           ;[0806] cf
                    ld        a,(de)                        ;[0807] 1a
                    ld        e,(ix+$0b)                    ;[0808] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[080b] dd 56 0c
                    push      hl                            ;[080e] e5
                    ld        a,h                           ;[080f] 7c
                    or        l                             ;[0810] b5
                    jr        nz,$0819                      ;[0811] 20 06
                    inc       de                            ;[0813] 13
                    inc       de                            ;[0814] 13
                    inc       de                            ;[0815] 13
                    ex        de,hl                         ;[0816] eb
                    jr        $0825                         ;[0817] 18 0c
                    ld        l,(ix-$06)                    ;[0819] dd 6e fa
                    ld        h,(ix-$05)                    ;[081c] dd 66 fb
                    ex        de,hl                         ;[081f] eb
                    scf                                     ;[0820] 37
                    sbc       hl,de                         ;[0821] ed 52
                    jr        c,$082e                       ;[0823] 38 09
                    ld        de,$0005                      ;[0825] 11 05 00
                    add       hl,de                         ;[0828] 19
                    ld        b,h                           ;[0829] 44
                    ld        c,l                           ;[082a] 4d
                    call      $1f05                         ;[082b] cd 05 1f
                    pop       hl                            ;[082e] e1
                    ld        a,(ix+$00)                    ;[082f] dd 7e 00
                    and       a                             ;[0832] a7
                    jr        z,$0873                       ;[0833] 28 3e
                    ld        a,h                           ;[0835] 7c
                    or        l                             ;[0836] b5
                    jr        z,$084c                       ;[0837] 28 13
                    dec       hl                            ;[0839] 2b
                    ld        b,(hl)                        ;[083a] 46
                    dec       hl                            ;[083b] 2b
                    ld        c,(hl)                        ;[083c] 4e
                    dec       hl                            ;[083d] 2b
                    inc       bc                            ;[083e] 03
                    inc       bc                            ;[083f] 03
                    inc       bc                            ;[0840] 03
                    ld        ($5c5f),ix                    ;[0841] dd 22 5f 5c
                    call      $19e8                         ;[0845] cd e8 19
                    ld        ix,($5c5f)                    ;[0848] dd 2a 5f 5c
                    ld        hl,($5c59)                    ;[084c] 2a 59 5c
                    dec       hl                            ;[084f] 2b
                    ld        c,(ix+$0b)                    ;[0850] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[0853] dd 46 0c
                    push      bc                            ;[0856] c5
                    inc       bc                            ;[0857] 03
                    inc       bc                            ;[0858] 03
                    inc       bc                            ;[0859] 03
                    ld        a,(ix-$03)                    ;[085a] dd 7e fd
                    push      af                            ;[085d] f5
                    call      $1655                         ;[085e] cd 55 16
                    inc       hl                            ;[0861] 23
                    pop       af                            ;[0862] f1
                    ld        (hl),a                        ;[0863] 77
                    pop       de                            ;[0864] d1
                    inc       hl                            ;[0865] 23
                    ld        (hl),e                        ;[0866] 73
                    inc       hl                            ;[0867] 23
                    ld        (hl),d                        ;[0868] 72
                    inc       hl                            ;[0869] 23
                    push      hl                            ;[086a] e5
                    pop       ix                            ;[086b] dd e1
                    scf                                     ;[086d] 37
                    ld        a,$ff                         ;[086e] 3e ff
                    jp        $0802                         ;[0870] c3 02 08
                    ex        de,hl                         ;[0873] eb
                    ld        hl,($5c59)                    ;[0874] 2a 59 5c
                    dec       hl                            ;[0877] 2b
                    ld        ($5c5f),ix                    ;[0878] dd 22 5f 5c
                    ld        c,(ix+$0b)                    ;[087c] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[087f] dd 46 0c
                    push      bc                            ;[0882] c5
                    call      $19e5                         ;[0883] cd e5 19
                    pop       bc                            ;[0886] c1
                    push      hl                            ;[0887] e5
                    push      bc                            ;[0888] c5
                    call      $1655                         ;[0889] cd 55 16
                    ld        ix,($5c5f)                    ;[088c] dd 2a 5f 5c
                    inc       hl                            ;[0890] 23
                    ld        c,(ix+$0f)                    ;[0891] dd 4e 0f
                    ld        b,(ix+$10)                    ;[0894] dd 46 10
                    add       hl,bc                         ;[0897] 09
                    ld        ($5c4b),hl                    ;[0898] 22 4b 5c
                    ld        h,(ix+$0e)                    ;[089b] dd 66 0e
                    ld        a,h                           ;[089e] 7c
                    and       $c0                           ;[089f] e6 c0
                    jr        nz,$08ad                      ;[08a1] 20 0a
                    ld        l,(ix+$0d)                    ;[08a3] dd 6e 0d
                    ld        ($5c42),hl                    ;[08a6] 22 42 5c
                    ld        (iy+$0a),$00                  ;[08a9] fd 36 0a 00
                    pop       de                            ;[08ad] d1
                    pop       ix                            ;[08ae] dd e1
                    scf                                     ;[08b0] 37
                    ld        a,$ff                         ;[08b1] 3e ff
                    jp        $0802                         ;[08b3] c3 02 08
                    ld        c,(ix+$0b)                    ;[08b6] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[08b9] dd 46 0c
                    push      bc                            ;[08bc] c5
                    inc       bc                            ;[08bd] 03
                    rst       $30                           ;[08be] f7
                    ld        (hl),$80                      ;[08bf] 36 80
                    ex        de,hl                         ;[08c1] eb
                    pop       de                            ;[08c2] d1
                    push      hl                            ;[08c3] e5
                    push      hl                            ;[08c4] e5
                    pop       ix                            ;[08c5] dd e1
                    scf                                     ;[08c7] 37
                    ld        a,$ff                         ;[08c8] 3e ff
                    call      $0802                         ;[08ca] cd 02 08
                    pop       hl                            ;[08cd] e1
                    ld        de,($5c53)                    ;[08ce] ed 5b 53 5c
                    ld        a,(hl)                        ;[08d2] 7e
                    and       $c0                           ;[08d3] e6 c0
                    jr        nz,$08f0                      ;[08d5] 20 19
                    ld        a,(de)                        ;[08d7] 1a
                    inc       de                            ;[08d8] 13
                    cp        (hl)                          ;[08d9] be
                    inc       hl                            ;[08da] 23
                    jr        nz,$08df                      ;[08db] 20 02
                    ld        a,(de)                        ;[08dd] 1a
                    cp        (hl)                          ;[08de] be
                    dec       de                            ;[08df] 1b
                    dec       hl                            ;[08e0] 2b
                    jr        nc,$08eb                      ;[08e1] 30 08
                    push      hl                            ;[08e3] e5
                    ex        de,hl                         ;[08e4] eb
                    call      $19b8                         ;[08e5] cd b8 19
                    pop       hl                            ;[08e8] e1
                    jr        $08d7                         ;[08e9] 18 ec
                    call      $092c                         ;[08eb] cd 2c 09
                    jr        $08d2                         ;[08ee] 18 e2
                    ld        a,(hl)                        ;[08f0] 7e
                    ld        c,a                           ;[08f1] 4f
                    cp        $80                           ;[08f2] fe 80
                    ret       z                             ;[08f4] c8
                    push      hl                            ;[08f5] e5
                    ld        hl,($5c4b)                    ;[08f6] 2a 4b 5c
                    ld        a,(hl)                        ;[08f9] 7e
                    cp        $80                           ;[08fa] fe 80
                    jr        z,$0923                       ;[08fc] 28 25
                    cp        c                             ;[08fe] b9
                    jr        z,$0909                       ;[08ff] 28 08
                    push      bc                            ;[0901] c5
                    call      $19b8                         ;[0902] cd b8 19
                    pop       bc                            ;[0905] c1
                    ex        de,hl                         ;[0906] eb
                    jr        $08f9                         ;[0907] 18 f0
                    and       $e0                           ;[0909] e6 e0
                    cp        $a0                           ;[090b] fe a0
                    jr        nz,$0921                      ;[090d] 20 12
                    pop       de                            ;[090f] d1
                    push      de                            ;[0910] d5
                    push      hl                            ;[0911] e5
                    inc       hl                            ;[0912] 23
                    inc       de                            ;[0913] 13
                    ld        a,(de)                        ;[0914] 1a
                    cp        (hl)                          ;[0915] be
                    jr        nz,$091e                      ;[0916] 20 06
                    rla                                     ;[0918] 17
                    jr        nc,$0912                      ;[0919] 30 f7
                    pop       hl                            ;[091b] e1
                    jr        $0921                         ;[091c] 18 03
                    pop       hl                            ;[091e] e1
                    jr        $0901                         ;[091f] 18 e0
                    ld        a,$ff                         ;[0921] 3e ff
                    pop       de                            ;[0923] d1
                    ex        de,hl                         ;[0924] eb
                    inc       a                             ;[0925] 3c
                    scf                                     ;[0926] 37
                    call      $092c                         ;[0927] cd 2c 09
                    jr        $08f0                         ;[092a] 18 c4
                    jr        nz,$093e                      ;[092c] 20 10
                    ex        af,af'                        ;[092e] 08
                    ld        ($5c5f),hl                    ;[092f] 22 5f 5c
                    ex        de,hl                         ;[0932] eb
                    call      $19b8                         ;[0933] cd b8 19
                    call      $19e8                         ;[0936] cd e8 19
                    ex        de,hl                         ;[0939] eb
                    ld        hl,($5c5f)                    ;[093a] 2a 5f 5c
                    ex        af,af'                        ;[093d] 08
                    ex        af,af'                        ;[093e] 08
                    push      de                            ;[093f] d5
                    call      $19b8                         ;[0940] cd b8 19
                    ld        ($5c5f),hl                    ;[0943] 22 5f 5c
                    ld        hl,($5c53)                    ;[0946] 2a 53 5c
                    ex        (sp),hl                       ;[0949] e3
                    push      bc                            ;[094a] c5
                    ex        af,af'                        ;[094b] 08
                    jr        c,$0955                       ;[094c] 38 07
                    dec       hl                            ;[094e] 2b
                    call      $1655                         ;[094f] cd 55 16
                    inc       hl                            ;[0952] 23
                    jr        $0958                         ;[0953] 18 03
                    call      $1655                         ;[0955] cd 55 16
                    inc       hl                            ;[0958] 23
                    pop       bc                            ;[0959] c1
                    pop       de                            ;[095a] d1
                    ld        ($5c53),de                    ;[095b] ed 53 53 5c
                    ld        de,($5c5f)                    ;[095f] ed 5b 5f 5c
                    push      bc                            ;[0963] c5
                    push      de                            ;[0964] d5
                    ex        de,hl                         ;[0965] eb
                    ldir                                    ;[0966] ed b0
                    pop       hl                            ;[0968] e1
                    pop       bc                            ;[0969] c1
                    push      de                            ;[096a] d5
                    call      $19e8                         ;[096b] cd e8 19
                    pop       de                            ;[096e] d1
                    ret                                     ;[096f] c9

                    push      hl                            ;[0970] e5
                    ld        a,$fd                         ;[0971] 3e fd
                    call      $1601                         ;[0973] cd 01 16
                    xor       a                             ;[0976] af
                    ld        de,$09a1                      ;[0977] 11 a1 09
                    call      $0c0a                         ;[097a] cd 0a 0c
                    set       5,(iy+$02)                    ;[097d] fd cb 02 ee
                    call      $15d4                         ;[0981] cd d4 15
                    push      ix                            ;[0984] dd e5
                    ld        de,$0011                      ;[0986] 11 11 00
                    xor       a                             ;[0989] af
                    call      $04c2                         ;[098a] cd c2 04
                    pop       ix                            ;[098d] dd e1
                    ld        b,$32                         ;[098f] 06 32
                    halt                                    ;[0991] 76
                    djnz      $0991                         ;[0992] 10 fd
                    ld        e,(ix+$0b)                    ;[0994] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[0997] dd 56 0c
                    ld        a,$ff                         ;[099a] 3e ff
                    pop       ix                            ;[099c] dd e1
                    jp        $04c2                         ;[099e] c3 c2 04
                    add       b                             ;[09a1] 80
                    ld        d,e                           ;[09a2] 53
                    ld        (hl),h                        ;[09a3] 74
                    ld        h,c                           ;[09a4] 61
                    ld        (hl),d                        ;[09a5] 72
                    ld        (hl),h                        ;[09a6] 74
                    jr        nz,$0a1d                      ;[09a7] 20 74
                    ld        h,c                           ;[09a9] 61
                    ld        (hl),b                        ;[09aa] 70
                    ld        h,l                           ;[09ab] 65
                    inc       l                             ;[09ac] 2c
                    jr        nz,$0a23                      ;[09ad] 20 74
                    ld        l,b                           ;[09af] 68
                    ld        h,l                           ;[09b0] 65
                    ld        l,(hl)                        ;[09b1] 6e
                    jr        nz,$0a24                      ;[09b2] 20 70
                    ld        (hl),d                        ;[09b4] 72
                    ld        h,l                           ;[09b5] 65
                    ld        (hl),e                        ;[09b6] 73
                    ld        (hl),e                        ;[09b7] 73
                    jr        nz,$0a1b                      ;[09b8] 20 61
                    ld        l,(hl)                        ;[09ba] 6e
                    ld        a,c                           ;[09bb] 79
                    jr        nz,$0a29                      ;[09bc] 20 6b
                    ld        h,l                           ;[09be] 65
                    ld        a,c                           ;[09bf] 79
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
                    ld        a,$18                         ;[0a32] 3e 18
                    cp        b                             ;[0a34] b8
                    jr        nz,$0a3a                      ;[0a35] 20 03
                    dec       b                             ;[0a37] 05
                    ld        c,$21                         ;[0a38] 0e 21
                    jp        $0dd9                         ;[0a3a] c3 d9 0d
                    ld        a,($5c91)                     ;[0a3d] 3a 91 5c
                    push      af                            ;[0a40] f5
                    ld        (iy+$57),$01                  ;[0a41] fd 36 57 01
                    ld        a,$20                         ;[0a45] 3e 20
                    call      $0b65                         ;[0a47] cd 65 0b
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

                    sub       $a5                           ;[0b52] d6 a5
                    jr        nc,$0b5f                      ;[0b54] 30 09
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
                    bit       4,(iy+$02)                    ;[0c6c] fd cb 02 66
                    jr        z,$0c88                       ;[0c70] 28 16
                    ld        e,(iy+$2d)                    ;[0c72] fd 5e 2d
                    dec       e                             ;[0c75] 1d
                    jr        z,$0cd2                       ;[0c76] 28 5a
                    ld        a,$00                         ;[0c78] 3e 00
                    call      $1601                         ;[0c7a] cd 01 16
                    ld        sp,($5c3f)                    ;[0c7d] ed 7b 3f 5c
                    res       4,(iy+$02)                    ;[0c81] fd cb 02 a6
                    ret                                     ;[0c85] c9

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
                    set       5,(iy+$02)                    ;[0ca7] fd cb 02 ee
                    ld        hl,$5c3b                      ;[0cab] 21 3b 5c
                    set       3,(hl)                        ;[0cae] cb de
                    res       5,(hl)                        ;[0cb0] cb ae
                    exx                                     ;[0cb2] d9
                    call      $15d4                         ;[0cb3] cd d4 15
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

                    di                                      ;[0eac] f3
                    ld        b,$b0                         ;[0ead] 06 b0
                    ld        hl,$4000                      ;[0eaf] 21 00 40
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
                    di                                      ;[0ecd] f3
                    ld        hl,$5b00                      ;[0ece] 21 00 5b
                    ld        b,$08                         ;[0ed1] 06 08
                    push      bc                            ;[0ed3] c5
                    call      $0ef4                         ;[0ed4] cd f4 0e
                    pop       bc                            ;[0ed7] c1
                    djnz      $0ed3                         ;[0ed8] 10 f9
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
                    rst       $08                           ;[0fa7] cf
                    call      nc,$492a                      ;[0fa8] d4 2a 49
                    ld        e,h                           ;[0fab] 5c
                    bit       5,(iy+$37)                    ;[0fac] fd cb 37 6e
                    jp        nz,$1097                      ;[0fb0] c2 97 10
                    call      $196e                         ;[0fb3] cd 6e 19
                    call      $1695                         ;[0fb6] cd 95 16
                    ld        a,d                           ;[0fb9] 7a
                    or        e                             ;[0fba] b3
                    jp        z,$1097                       ;[0fbb] ca 97 10
                    push      hl                            ;[0fbe] e5
                    inc       hl                            ;[0fbf] 23
                    ld        c,(hl)                        ;[0fc0] 4e
                    inc       hl                            ;[0fc1] 23
                    ld        b,(hl)                        ;[0fc2] 46
                    ld        hl,$000a                      ;[0fc3] 21 0a 00
                    add       hl,bc                         ;[0fc6] 09
                    ld        b,h                           ;[0fc7] 44
                    ld        c,l                           ;[0fc8] 4d
                    call      $1f05                         ;[0fc9] cd 05 1f
                    call      $1097                         ;[0fcc] cd 97 10
                    ld        hl,($5c51)                    ;[0fcf] 2a 51 5c
                    ex        (sp),hl                       ;[0fd2] e3
                    push      hl                            ;[0fd3] e5
                    ld        a,$ff                         ;[0fd4] 3e ff
                    call      $1601                         ;[0fd6] cd 01 16
                    pop       hl                            ;[0fd9] e1
                    dec       hl                            ;[0fda] 2b
                    dec       (iy+$0f)                      ;[0fdb] fd 35 0f
                    call      $1855                         ;[0fde] cd 55 18
                    inc       (iy+$0f)                      ;[0fe1] fd 34 0f
                    ld        hl,($5c59)                    ;[0fe4] 2a 59 5c
                    inc       hl                            ;[0fe7] 23
                    inc       hl                            ;[0fe8] 23
                    inc       hl                            ;[0fe9] 23
                    inc       hl                            ;[0fea] 23
                    ld        ($5c5b),hl                    ;[0feb] 22 5b 5c
                    pop       hl                            ;[0fee] e1
                    call      $1615                         ;[0fef] cd 15 16
                    ret                                     ;[0ff2] c9

                    bit       5,(iy+$37)                    ;[0ff3] fd cb 37 6e
                    jr        nz,$1001                      ;[0ff7] 20 08
                    ld        hl,$5c49                      ;[0ff9] 21 49 5c
                    call      $190f                         ;[0ffc] cd 0f 19
                    jr        $106e                         ;[0fff] 18 6d
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

                    bit       5,(iy+$37)                    ;[1059] fd cb 37 6e
                    ret       nz                            ;[105d] c0
                    ld        hl,($5c49)                    ;[105e] 2a 49 5c
                    call      $196e                         ;[1061] cd 6e 19
                    ex        de,hl                         ;[1064] eb
                    call      $1695                         ;[1065] cd 95 16
                    ld        hl,$5c4a                      ;[1068] 21 4a 5c
                    call      $191c                         ;[106b] cd 1c 19
                    call      $1795                         ;[106e] cd 95 17
                    ld        a,$00                         ;[1071] 3e 00
                    jp        $1601                         ;[1073] c3 01 16
                    bit       7,(iy+$37)                    ;[1076] fd cb 37 7e
                    jr        z,$1024                       ;[107a] 28 a8
                    jp        $0f81                         ;[107c] c3 81 0f
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

                    bit       3,(iy+$02)                    ;[10a8] fd cb 02 5e
                    call      nz,$111d                      ;[10ac] c4 1d 11
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
                    ld        de,($5c59)                    ;[1195] ed 5b 59 5c
                    bit       5,(iy+$37)                    ;[1199] fd cb 37 6e
                    ret       z                             ;[119d] c8
                    ld        de,($5c61)                    ;[119e] ed 5b 61 5c
                    ret       c                             ;[11a2] d8
                    ld        hl,($5c63)                    ;[11a3] 2a 63 5c
                    ret                                     ;[11a6] c9

                    ld        a,(hl)                        ;[11a7] 7e
                    cp        $0e                           ;[11a8] fe 0e
                    ld        bc,$0006                      ;[11aa] 01 06 00
                    call      z,$19e8                       ;[11ad] cc e8 19
                    ld        a,(hl)                        ;[11b0] 7e
                    inc       hl                            ;[11b1] 23
                    cp        $0d                           ;[11b2] fe 0d
                    jr        nz,$11a7                      ;[11b4] 20 f1
                    ret                                     ;[11b6] c9

                    di                                      ;[11b7] f3
                    ld        a,$ff                         ;[11b8] 3e ff
                    ld        de,($5cb2)                    ;[11ba] ed 5b b2 5c
                    exx                                     ;[11be] d9
                    ld        bc,($5cb4)                    ;[11bf] ed 4b b4 5c
                    ld        de,($5c38)                    ;[11c3] ed 5b 38 5c
                    ld        hl,($5c7b)                    ;[11c7] 2a 7b 5c
                    exx                                     ;[11ca] d9
                    ld        b,a                           ;[11cb] 47
                    ld        a,$07                         ;[11cc] 3e 07
                    out       ($fe),a                       ;[11ce] d3 fe
                    ld        a,$3f                         ;[11d0] 3e 3f
                    ld        i,a                           ;[11d2] ed 47
                    nop                                     ;[11d4] 00
                    nop                                     ;[11d5] 00
                    nop                                     ;[11d6] 00
                    nop                                     ;[11d7] 00
                    nop                                     ;[11d8] 00
                    nop                                     ;[11d9] 00
                    ld        h,d                           ;[11da] 62
                    ld        l,e                           ;[11db] 6b
                    ld        (hl),$02                      ;[11dc] 36 02
                    dec       hl                            ;[11de] 2b
                    cp        h                             ;[11df] bc
                    jr        nz,$11dc                      ;[11e0] 20 fa
                    and       a                             ;[11e2] a7
                    sbc       hl,de                         ;[11e3] ed 52
                    add       hl,de                         ;[11e5] 19
                    inc       hl                            ;[11e6] 23
                    jr        nc,$11ef                      ;[11e7] 30 06
                    dec       (hl)                          ;[11e9] 35
                    jr        z,$11ef                       ;[11ea] 28 03
                    dec       (hl)                          ;[11ec] 35
                    jr        z,$11e2                       ;[11ed] 28 f3
                    dec       hl                            ;[11ef] 2b
                    exx                                     ;[11f0] d9
                    ld        ($5cb4),bc                    ;[11f1] ed 43 b4 5c
                    ld        ($5c38),de                    ;[11f5] ed 53 38 5c
                    ld        ($5c7b),hl                    ;[11f9] 22 7b 5c
                    exx                                     ;[11fc] d9
                    inc       b                             ;[11fd] 04
                    jr        z,$1219                       ;[11fe] 28 19
                    ld        ($5cb4),hl                    ;[1200] 22 b4 5c
                    ld        de,$3eaf                      ;[1203] 11 af 3e
                    ld        bc,$00a8                      ;[1206] 01 a8 00
                    ex        de,hl                         ;[1209] eb
                    lddr                                    ;[120a] ed b8
                    ex        de,hl                         ;[120c] eb
                    inc       hl                            ;[120d] 23
                    ld        ($5c7b),hl                    ;[120e] 22 7b 5c
                    dec       hl                            ;[1211] 2b
                    ld        bc,$0040                      ;[1212] 01 40 00
                    ld        ($5c38),bc                    ;[1215] ed 43 38 5c
                    ld        ($5cb2),hl                    ;[1219] 22 b2 5c
                    ld        hl,$3c00                      ;[121c] 21 00 3c
                    ld        ($5c36),hl                    ;[121f] 22 36 5c
                    ld        hl,($5cb2)                    ;[1222] 2a b2 5c
                    ld        (hl),$3e                      ;[1225] 36 3e
                    dec       hl                            ;[1227] 2b
                    ld        sp,hl                         ;[1228] f9
                    dec       hl                            ;[1229] 2b
                    dec       hl                            ;[122a] 2b
                    ld        ($5c3d),hl                    ;[122b] 22 3d 5c
                    im        1                             ;[122e] ed 56
                    ld        iy,$5c3a                      ;[1230] fd 21 3a 5c
                    ei                                      ;[1234] fb
                    ld        hl,$5cb6                      ;[1235] 21 b6 5c
                    ld        ($5c4f),hl                    ;[1238] 22 4f 5c
                    ld        de,$15af                      ;[123b] 11 af 15
                    ld        bc,$0015                      ;[123e] 01 15 00
                    ex        de,hl                         ;[1241] eb
                    ldir                                    ;[1242] ed b0
                    ex        de,hl                         ;[1244] eb
                    dec       hl                            ;[1245] 2b
                    ld        ($5c57),hl                    ;[1246] 22 57 5c
                    inc       hl                            ;[1249] 23
                    ld        ($5c53),hl                    ;[124a] 22 53 5c
                    ld        ($5c4b),hl                    ;[124d] 22 4b 5c
                    ld        (hl),$80                      ;[1250] 36 80
                    inc       hl                            ;[1252] 23
                    ld        ($5c59),hl                    ;[1253] 22 59 5c
                    ld        (hl),$0d                      ;[1256] 36 0d
                    inc       hl                            ;[1258] 23
                    ld        (hl),$80                      ;[1259] 36 80
                    inc       hl                            ;[125b] 23
                    ld        ($5c61),hl                    ;[125c] 22 61 5c
                    ld        ($5c63),hl                    ;[125f] 22 63 5c
                    ld        ($5c65),hl                    ;[1262] 22 65 5c
                    ld        a,$38                         ;[1265] 3e 38
                    ld        ($5c8d),a                     ;[1267] 32 8d 5c
                    ld        ($5c8f),a                     ;[126a] 32 8f 5c
                    ld        ($5c48),a                     ;[126d] 32 48 5c
                    ld        hl,$0523                      ;[1270] 21 23 05
                    ld        ($5c09),hl                    ;[1273] 22 09 5c
                    dec       (iy-$3a)                      ;[1276] fd 35 c6
                    dec       (iy-$36)                      ;[1279] fd 35 ca
                    ld        hl,$15c6                      ;[127c] 21 c6 15
                    ld        de,$5c10                      ;[127f] 11 10 5c
                    ld        bc,$000e                      ;[1282] 01 0e 00
                    ldir                                    ;[1285] ed b0
                    set       1,(iy+$01)                    ;[1287] fd cb 01 ce
                    call      $0edf                         ;[128b] cd df 0e
                    ld        (iy+$31),$02                  ;[128e] fd 36 31 02
                    call      $0d6b                         ;[1292] cd 6b 0d
                    xor       a                             ;[1295] af
                    ld        de,$1538                      ;[1296] 11 38 15
                    call      $0c0a                         ;[1299] cd 0a 0c
                    set       5,(iy+$02)                    ;[129c] fd cb 02 ee
                    jr        $12a9                         ;[12a0] 18 07
                    ld        (iy+$31),$02                  ;[12a2] fd 36 31 02
                    call      $1795                         ;[12a6] cd 95 17
                    call      $16b0                         ;[12a9] cd b0 16
                    ld        a,$00                         ;[12ac] 3e 00
                    call      $1601                         ;[12ae] cd 01 16
                    call      $0f2c                         ;[12b1] cd 2c 0f
                    call      $1b17                         ;[12b4] cd 17 1b
                    bit       7,(iy+$00)                    ;[12b7] fd cb 00 7e
                    jr        nz,$12cf                      ;[12bb] 20 12
                    bit       4,(iy+$30)                    ;[12bd] fd cb 30 66
                    jr        z,$1303                       ;[12c1] 28 40
                    ld        hl,($5c59)                    ;[12c3] 2a 59 5c
                    call      $11a7                         ;[12c6] cd a7 11
                    ld        (iy+$00),$ff                  ;[12c9] fd 36 00 ff
                    jr        $12ac                         ;[12cd] 18 dd
                    ld        hl,($5c59)                    ;[12cf] 2a 59 5c
                    ld        ($5c5d),hl                    ;[12d2] 22 5d 5c
                    call      $19fb                         ;[12d5] cd fb 19
                    ld        a,b                           ;[12d8] 78
                    or        c                             ;[12d9] b1
                    jp        nz,$155d                      ;[12da] c2 5d 15
                    rst       $18                           ;[12dd] df
                    cp        $0d                           ;[12de] fe 0d
                    jr        z,$12a2                       ;[12e0] 28 c0
                    bit       0,(iy+$30)                    ;[12e2] fd cb 30 46
                    call      nz,$0daf                      ;[12e6] c4 af 0d
                    call      $0d6e                         ;[12e9] cd 6e 0d
                    ld        a,$19                         ;[12ec] 3e 19
                    sub       (iy+$4f)                      ;[12ee] fd 96 4f
                    ld        ($5c8c),a                     ;[12f1] 32 8c 5c
                    set       7,(iy+$01)                    ;[12f4] fd cb 01 fe
                    ld        (iy+$00),$ff                  ;[12f8] fd 36 00 ff
                    ld        (iy+$0a),$01                  ;[12fc] fd 36 0a 01
                    call      $1b8a                         ;[1300] cd 8a 1b
                    halt                                    ;[1303] 76
                    res       5,(iy+$01)                    ;[1304] fd cb 01 ae
                    bit       1,(iy+$30)                    ;[1308] fd cb 30 4e
                    call      nz,$0ecd                      ;[130c] c4 cd 0e
                    ld        a,($5c3a)                     ;[130f] 3a 3a 5c
                    inc       a                             ;[1312] 3c
                    push      af                            ;[1313] f5
                    ld        hl,$0000                      ;[1314] 21 00 00
                    ld        (iy+$37),h                    ;[1317] fd 74 37
                    ld        (iy+$26),h                    ;[131a] fd 74 26
                    ld        ($5c0b),hl                    ;[131d] 22 0b 5c
                    ld        hl,$0001                      ;[1320] 21 01 00
                    ld        ($5c16),hl                    ;[1323] 22 16 5c
                    call      $16b0                         ;[1326] cd b0 16
                    res       5,(iy+$37)                    ;[1329] fd cb 37 ae
                    call      $0d6e                         ;[132d] cd 6e 0d
                    set       5,(iy+$02)                    ;[1330] fd cb 02 ee
                    pop       af                            ;[1334] f1
                    ld        b,a                           ;[1335] 47
                    cp        $0a                           ;[1336] fe 0a
                    jr        c,$133c                       ;[1338] 38 02
                    add       $07                           ;[133a] c6 07
                    call      $15ef                         ;[133c] cd ef 15
                    ld        a,$20                         ;[133f] 3e 20
                    rst       $10                           ;[1341] d7
                    ld        a,b                           ;[1342] 78
                    ld        de,$1391                      ;[1343] 11 91 13
                    call      $0c0a                         ;[1346] cd 0a 0c
                    xor       a                             ;[1349] af
                    ld        de,$1536                      ;[134a] 11 36 15
                    call      $0c0a                         ;[134d] cd 0a 0c
                    ld        bc,($5c45)                    ;[1350] ed 4b 45 5c
                    call      $1a1b                         ;[1354] cd 1b 1a
                    ld        a,$3a                         ;[1357] 3e 3a
                    rst       $10                           ;[1359] d7
                    ld        c,(iy+$0d)                    ;[135a] fd 4e 0d
                    ld        b,$00                         ;[135d] 06 00
                    call      $1a1b                         ;[135f] cd 1b 1a
                    call      $1097                         ;[1362] cd 97 10
                    ld        a,($5c3a)                     ;[1365] 3a 3a 5c
                    inc       a                             ;[1368] 3c
                    jr        z,$1386                       ;[1369] 28 1b
                    cp        $09                           ;[136b] fe 09
                    jr        z,$1373                       ;[136d] 28 04
                    cp        $15                           ;[136f] fe 15
                    jr        nz,$1376                      ;[1371] 20 03
                    inc       (iy+$0d)                      ;[1373] fd 34 0d
                    ld        bc,$0003                      ;[1376] 01 03 00
                    ld        de,$5c70                      ;[1379] 11 70 5c
                    ld        hl,$5c44                      ;[137c] 21 44 5c
                    bit       7,(hl)                        ;[137f] cb 7e
                    jr        z,$1384                       ;[1381] 28 01
                    add       hl,bc                         ;[1383] 09
                    lddr                                    ;[1384] ed b8
                    ld        (iy+$0a),$ff                  ;[1386] fd 36 0a ff
                    res       3,(iy+$01)                    ;[138a] fd cb 01 9e
                    jp        $12ac                         ;[138e] c3 ac 12
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
                    call      po,$103e                      ;[1554] e4 3e 10
                    ld        bc,$0000                      ;[1557] 01 00 00
                    jp        $1313                         ;[155a] c3 13 13
                    ld        ($5c49),bc                    ;[155d] ed 43 49 5c
                    ld        hl,($5c5d)                    ;[1561] 2a 5d 5c
                    ex        de,hl                         ;[1564] eb
                    ld        hl,$1555                      ;[1565] 21 55 15
                    push      hl                            ;[1568] e5
                    ld        hl,($5c61)                    ;[1569] 2a 61 5c
                    scf                                     ;[156c] 37
                    sbc       hl,de                         ;[156d] ed 52
                    push      hl                            ;[156f] e5
                    ld        h,b                           ;[1570] 60
                    ld        l,c                           ;[1571] 69
                    call      $196e                         ;[1572] cd 6e 19
                    jr        nz,$157d                      ;[1575] 20 06
                    call      $19b8                         ;[1577] cd b8 19
                    call      $19e8                         ;[157a] cd e8 19
                    pop       bc                            ;[157d] c1
                    ld        a,c                           ;[157e] 79
                    dec       a                             ;[157f] 3d
                    or        b                             ;[1580] b0
                    jr        z,$15ab                       ;[1581] 28 28
                    push      bc                            ;[1583] c5
                    inc       bc                            ;[1584] 03
                    inc       bc                            ;[1585] 03
                    inc       bc                            ;[1586] 03
                    inc       bc                            ;[1587] 03
                    dec       hl                            ;[1588] 2b
                    ld        de,($5c53)                    ;[1589] ed 5b 53 5c
                    push      de                            ;[158d] d5
                    call      $1655                         ;[158e] cd 55 16
                    pop       hl                            ;[1591] e1
                    ld        ($5c53),hl                    ;[1592] 22 53 5c
                    pop       bc                            ;[1595] c1
                    push      bc                            ;[1596] c5
                    inc       de                            ;[1597] 13
                    ld        hl,($5c61)                    ;[1598] 2a 61 5c
                    dec       hl                            ;[159b] 2b
                    dec       hl                            ;[159c] 2b
                    lddr                                    ;[159d] ed b8
                    ld        hl,($5c49)                    ;[159f] 2a 49 5c
                    ex        de,hl                         ;[15a2] eb
                    pop       bc                            ;[15a3] c1
                    ld        (hl),b                        ;[15a4] 70
                    dec       hl                            ;[15a5] 2b
                    ld        (hl),c                        ;[15a6] 71
                    dec       hl                            ;[15a7] 2b
                    ld        (hl),e                        ;[15a8] 73
                    dec       hl                            ;[15a9] 2b
                    ld        (hl),d                        ;[15aa] 72
                    pop       af                            ;[15ab] f1
                    jp        $12a2                         ;[15ac] c3 a2 12
                    call      p,$a809                       ;[15af] f4 09 a8
                    djnz      $15ff                         ;[15b2] 10 4b
                    call      p,$c409                       ;[15b4] f4 09 c4
                    dec       d                             ;[15b7] 15
                    ld        d,e                           ;[15b8] 53
                    add       c                             ;[15b9] 81
                    rrca                                    ;[15ba] 0f
                    call      nz,$5215                      ;[15bb] c4 15 52
                    call      p,$c409                       ;[15be] f4 09 c4
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
                    call      $1664                         ;[165a] cd 64 16
                    ld        hl,($5c65)                    ;[165d] 2a 65 5c
                    ex        de,hl                         ;[1660] eb
                    lddr                                    ;[1661] ed b8
                    ret                                     ;[1663] c9

                    push      af                            ;[1664] f5
                    push      hl                            ;[1665] e5
                    ld        hl,$5c4b                      ;[1666] 21 4b 5c
                    ld        a,$0e                         ;[1669] 3e 0e
                    ld        e,(hl)                        ;[166b] 5e
                    inc       hl                            ;[166c] 23
                    ld        d,(hl)                        ;[166d] 56
                    ex        (sp),hl                       ;[166e] e3
                    and       a                             ;[166f] a7
                    sbc       hl,de                         ;[1670] ed 52
                    add       hl,de                         ;[1672] 19
                    ex        (sp),hl                       ;[1673] e3
                    jr        nc,$167f                      ;[1674] 30 09
                    push      de                            ;[1676] d5
                    ex        de,hl                         ;[1677] eb
                    add       hl,bc                         ;[1678] 09
                    ex        de,hl                         ;[1679] eb
                    ld        (hl),d                        ;[167a] 72
                    dec       hl                            ;[167b] 2b
                    ld        (hl),e                        ;[167c] 73
                    inc       hl                            ;[167d] 23
                    pop       de                            ;[167e] d1
                    inc       hl                            ;[167f] 23
                    dec       a                             ;[1680] 3d
                    jr        nz,$166b                      ;[1681] 20 e8
                    ex        de,hl                         ;[1683] eb
                    pop       de                            ;[1684] d1
                    pop       af                            ;[1685] f1
                    and       a                             ;[1686] a7
                    sbc       hl,de                         ;[1687] ed 52
                    ld        b,h                           ;[1689] 44
                    ld        c,l                           ;[168a] 4d
                    inc       bc                            ;[168b] 03
                    add       hl,de                         ;[168c] 19
                    ex        de,hl                         ;[168d] eb
                    ret                                     ;[168e] c9

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

                    jr        $1725                         ;[1793] 18 90
                    ld        ($5c3f),sp                    ;[1795] ed 73 3f 5c
                    ld        (iy+$02),$10                  ;[1799] fd 36 02 10
                    call      $0daf                         ;[179d] cd af 0d
                    set       0,(iy+$02)                    ;[17a0] fd cb 02 c6
                    ld        b,(iy+$31)                    ;[17a4] fd 46 31
                    call      $0e44                         ;[17a7] cd 44 0e
                    res       0,(iy+$02)                    ;[17aa] fd cb 02 86
                    set       0,(iy+$30)                    ;[17ae] fd cb 30 c6
                    ld        hl,($5c49)                    ;[17b2] 2a 49 5c
                    ld        de,($5c6c)                    ;[17b5] ed 5b 6c 5c
                    and       a                             ;[17b9] a7
                    sbc       hl,de                         ;[17ba] ed 52
                    add       hl,de                         ;[17bc] 19
                    jr        c,$17e1                       ;[17bd] 38 22
                    push      de                            ;[17bf] d5
                    call      $196e                         ;[17c0] cd 6e 19
                    ld        de,$02c0                      ;[17c3] 11 c0 02
                    ex        de,hl                         ;[17c6] eb
                    sbc       hl,de                         ;[17c7] ed 52
                    ex        (sp),hl                       ;[17c9] e3
                    call      $196e                         ;[17ca] cd 6e 19
                    pop       bc                            ;[17cd] c1
                    push      bc                            ;[17ce] c5
                    call      $19b8                         ;[17cf] cd b8 19
                    pop       bc                            ;[17d2] c1
                    add       hl,bc                         ;[17d3] 09
                    jr        c,$17e4                       ;[17d4] 38 0e
                    ex        de,hl                         ;[17d6] eb
                    ld        d,(hl)                        ;[17d7] 56
                    inc       hl                            ;[17d8] 23
                    ld        e,(hl)                        ;[17d9] 5e
                    dec       hl                            ;[17da] 2b
                    ld        ($5c6c),de                    ;[17db] ed 53 6c 5c
                    jr        $17ce                         ;[17df] 18 ed
                    ld        ($5c6c),hl                    ;[17e1] 22 6c 5c
                    ld        hl,($5c6c)                    ;[17e4] 2a 6c 5c
                    call      $196e                         ;[17e7] cd 6e 19
                    jr        z,$17ed                       ;[17ea] 28 01
                    ex        de,hl                         ;[17ec] eb
                    call      $1833                         ;[17ed] cd 33 18
                    res       4,(iy+$02)                    ;[17f0] fd cb 02 a6
                    ret                                     ;[17f4] c9

                    ld        a,$03                         ;[17f5] 3e 03
                    jr        $17fb                         ;[17f7] 18 02
                    ld        a,$02                         ;[17f9] 3e 02
                    ld        (iy+$02),$00                  ;[17fb] fd 36 02 00
                    call      $2530                         ;[17ff] cd 30 25
                    call      nz,$1601                      ;[1802] c4 01 16
                    rst       $18                           ;[1805] df
                    call      $2070                         ;[1806] cd 70 20
                    jr        c,$181f                       ;[1809] 38 14
                    rst       $18                           ;[180b] df
                    cp        $3b                           ;[180c] fe 3b
                    jr        z,$1814                       ;[180e] 28 04
                    cp        $2c                           ;[1810] fe 2c
                    jr        nz,$181a                      ;[1812] 20 06
                    rst       $20                           ;[1814] e7
                    call      $1c82                         ;[1815] cd 82 1c
                    jr        $1822                         ;[1818] 18 08
                    call      $1ce6                         ;[181a] cd e6 1c
                    jr        $1822                         ;[181d] 18 03
                    call      $1cde                         ;[181f] cd de 1c
                    call      $1bee                         ;[1822] cd ee 1b
                    call      $1e99                         ;[1825] cd 99 1e
                    ld        a,b                           ;[1828] 78
                    and       $3f                           ;[1829] e6 3f
                    ld        h,a                           ;[182b] 67
                    ld        l,c                           ;[182c] 69
                    ld        ($5c49),hl                    ;[182d] 22 49 5c
                    call      $196e                         ;[1830] cd 6e 19
                    ld        e,$01                         ;[1833] 1e 01
                    call      $1855                         ;[1835] cd 55 18
                    rst       $10                           ;[1838] d7
                    bit       4,(iy+$02)                    ;[1839] fd cb 02 66
                    jr        z,$1835                       ;[183d] 28 f6
                    ld        a,($5c6b)                     ;[183f] 3a 6b 5c
                    sub       (iy+$4f)                      ;[1842] fd 96 4f
                    jr        nz,$1835                      ;[1845] 20 ee
                    xor       e                             ;[1847] ab
                    ret       z                             ;[1848] c8
                    push      hl                            ;[1849] e5
                    push      de                            ;[184a] d5
                    ld        hl,$5c6c                      ;[184b] 21 6c 5c
                    call      $190f                         ;[184e] cd 0f 19
                    pop       de                            ;[1851] d1
                    pop       hl                            ;[1852] e1
                    jr        $1835                         ;[1853] 18 e0
                    ld        bc,($5c49)                    ;[1855] ed 4b 49 5c
                    call      $1980                         ;[1859] cd 80 19
                    ld        d,$3e                         ;[185c] 16 3e
                    jr        z,$1865                       ;[185e] 28 05
                    ld        de,$0000                      ;[1860] 11 00 00
                    rl        e                             ;[1863] cb 13
                    ld        (iy+$2d),e                    ;[1865] fd 73 2d
                    ld        a,(hl)                        ;[1868] 7e
                    cp        $40                           ;[1869] fe 40
                    pop       bc                            ;[186b] c1
                    ret       nc                            ;[186c] d0
                    push      bc                            ;[186d] c5
                    call      $1a28                         ;[186e] cd 28 1a
                    inc       hl                            ;[1871] 23
                    inc       hl                            ;[1872] 23
                    inc       hl                            ;[1873] 23
                    res       0,(iy+$01)                    ;[1874] fd cb 01 86
                    ld        a,d                           ;[1878] 7a
                    and       a                             ;[1879] a7
                    jr        z,$1881                       ;[187a] 28 05
                    rst       $10                           ;[187c] d7
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
                    bit       5,(iy+$37)                    ;[191c] fd cb 37 6e
                    ret       nz                            ;[1920] c0
                    ld        (hl),d                        ;[1921] 72
                    dec       hl                            ;[1922] 2b
                    ld        (hl),e                        ;[1923] 73
                    ret                                     ;[1924] c9

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
                    bit       5,(iy+$37)                    ;[194c] fd cb 37 6e
                    jr        nz,$1968                      ;[1950] 20 16
                    bit       2,(iy+$30)                    ;[1952] fd cb 30 56
                    jr        z,$196c                       ;[1956] 28 14
                    jr        $1968                         ;[1958] 18 0e
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
                    push      bc                            ;[1979] c5
                    call      $19b8                         ;[197a] cd b8 19
                    ex        de,hl                         ;[197d] eb
                    jr        $1974                         ;[197e] 18 f4
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
                    ld        a,(hl)                        ;[19b9] 7e
                    cp        $40                           ;[19ba] fe 40
                    jr        c,$19d5                       ;[19bc] 38 17
                    bit       5,a                           ;[19be] cb 6f
                    jr        z,$19d6                       ;[19c0] 28 14
                    add       a                             ;[19c2] 87
                    jp        m,$19c7                       ;[19c3] fa c7 19
                    ccf                                     ;[19c6] 3f
                    ld        bc,$0005                      ;[19c7] 01 05 00
                    jr        nc,$19ce                      ;[19ca] 30 02
                    ld        c,$12                         ;[19cc] 0e 12
                    rla                                     ;[19ce] 17
                    inc       hl                            ;[19cf] 23
                    ld        a,(hl)                        ;[19d0] 7e
                    jr        nc,$19ce                      ;[19d1] 30 fb
                    jr        $19db                         ;[19d3] 18 06
                    inc       hl                            ;[19d5] 23
                    inc       hl                            ;[19d6] 23
                    ld        c,(hl)                        ;[19d7] 4e
                    inc       hl                            ;[19d8] 23
                    ld        b,(hl)                        ;[19d9] 46
                    inc       hl                            ;[19da] 23
                    add       hl,bc                         ;[19db] 09
                    pop       de                            ;[19dc] d1
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
                    call      $1664                         ;[19f0] cd 64 16
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

                    or        c                             ;[1a48] b1
                    res       7,h                           ;[1a49] cb bc
                    cp        a                             ;[1a4b] bf
                    call      nz,$b4af                      ;[1a4c] c4 af b4
                    sub       e                             ;[1a4f] 93
                    sub       c                             ;[1a50] 91
                    sub       d                             ;[1a51] 92
                    sub       l                             ;[1a52] 95
                    sbc       b                             ;[1a53] 98
                    sbc       b                             ;[1a54] 98
                    sbc       b                             ;[1a55] 98
                    sbc       b                             ;[1a56] 98
                    sbc       b                             ;[1a57] 98
                    sbc       b                             ;[1a58] 98
                    sbc       b                             ;[1a59] 98
                    ld        a,a                           ;[1a5a] 7f
                    add       c                             ;[1a5b] 81
                    ld        l,$6c                         ;[1a5c] 2e 6c
                    ld        l,(hl)                        ;[1a5e] 6e
                    ld        (hl),b                        ;[1a5f] 70
                    ld        c,b                           ;[1a60] 48
                    sub       h                             ;[1a61] 94
                    ld        d,(hl)                        ;[1a62] 56
                    ccf                                     ;[1a63] 3f
                    ld        b,c                           ;[1a64] 41
                    dec       hl                            ;[1a65] 2b
                    rla                                     ;[1a66] 17
                    rra                                     ;[1a67] 1f
                    scf                                     ;[1a68] 37
                    ld        (hl),a                        ;[1a69] 77
                    ld        b,h                           ;[1a6a] 44
                    rrca                                    ;[1a6b] 0f
                    ld        e,c                           ;[1a6c] 59
                    dec       hl                            ;[1a6d] 2b
                    ld        b,e                           ;[1a6e] 43
                    dec       l                             ;[1a6f] 2d
                    ld        d,c                           ;[1a70] 51
                    ld        a,($426d)                     ;[1a71] 3a 6d 42
                    dec       c                             ;[1a74] 0d
                    ld        c,c                           ;[1a75] 49
                    ld        e,h                           ;[1a76] 5c
                    ld        b,h                           ;[1a77] 44
                    dec       d                             ;[1a78] 15
                    ld        e,l                           ;[1a79] 5d
                    ld        bc,$023d                      ;[1a7a] 01 3d 02
                    ld        b,$00                         ;[1a7d] 06 00
                    ld        h,a                           ;[1a7f] 67
                    ld        e,$06                         ;[1a80] 1e 06
                    rlc       l                             ;[1a82] cb 05
                    ret       p                             ;[1a84] f0
                    inc       e                             ;[1a85] 1c
                    ld        b,$00                         ;[1a86] 06 00
                    nop                                     ;[1a88] ed 1e
                    nop                                     ;[1a8a] 00
                    xor       $1c                           ;[1a8b] ee 1c
                    nop                                     ;[1a8d] 00
                    inc       hl                            ;[1a8e] 23
                    rra                                     ;[1a8f] 1f
                    inc       b                             ;[1a90] 04
                    dec       a                             ;[1a91] 3d
                    ld        b,$cc                         ;[1a92] 06 cc
                    ld        b,$05                         ;[1a94] 06 05
                    inc       bc                            ;[1a96] 03
                    dec       e                             ;[1a97] 1d
                    inc       b                             ;[1a98] 04
                    nop                                     ;[1a99] 00
                    xor       e                             ;[1a9a] ab
                    dec       e                             ;[1a9b] 1d
                    dec       b                             ;[1a9c] 05
                    call      $051f                         ;[1a9d] cd 1f 05
                    adc       c                             ;[1aa0] 89
                    jr        nz,$1aa8                      ;[1aa1] 20 05
                    ld        (bc),a                        ;[1aa3] 02
                    inc       l                             ;[1aa4] 2c
                    dec       b                             ;[1aa5] 05
                    or        d                             ;[1aa6] b2
                    dec       de                            ;[1aa7] 1b
                    nop                                     ;[1aa8] 00
                    or        a                             ;[1aa9] b7
                    ld        de,$a103                      ;[1aaa] 11 03 a1
                    ld        e,$05                         ;[1aad] 1e 05
                    ld        sp,hl                         ;[1aaf] f9
                    rla                                     ;[1ab0] 17
                    ex        af,af'                        ;[1ab1] 08
                    nop                                     ;[1ab2] 00
                    add       b                             ;[1ab3] 80
                    ld        e,$03                         ;[1ab4] 1e 03
                    ld        c,a                           ;[1ab6] 4f
                    ld        e,$00                         ;[1ab7] 1e 00
                    ld        e,a                           ;[1ab9] 5f
                    ld        e,$03                         ;[1aba] 1e 03
                    xor       h                             ;[1abc] ac
                    ld        e,$00                         ;[1abd] 1e 00
                    ld        l,e                           ;[1abf] 6b
                    dec       c                             ;[1ac0] 0d
                    add       hl,bc                         ;[1ac1] 09
                    nop                                     ;[1ac2] 00
                    call      c,$0622                       ;[1ac3] dc 22 06
                    nop                                     ;[1ac6] 00
                    ld        a,($051f)                     ;[1ac7] 3a 1f 05
                    nop                                     ;[1aca] ed 1d
                    dec       b                             ;[1acc] 05
                    daa                                     ;[1acd] 27
                    ld        e,$03                         ;[1ace] 1e 03
                    ld        b,d                           ;[1ad0] 42
                    ld        e,$09                         ;[1ad1] 1e 09
                    dec       b                             ;[1ad3] 05
                    add       d                             ;[1ad4] 82
                    inc       hl                            ;[1ad5] 23
                    nop                                     ;[1ad6] 00
                    xor       h                             ;[1ad7] ac
                    ld        c,$05                         ;[1ad8] 0e 05
                    ret                                     ;[1ada] c9

                    rra                                     ;[1adb] 1f
                    dec       b                             ;[1adc] 05
                    push      af                            ;[1add] f5
                    rla                                     ;[1ade] 17
                    dec       bc                            ;[1adf] 0b
                    dec       bc                            ;[1ae0] 0b
                    dec       bc                            ;[1ae1] 0b
                    dec       bc                            ;[1ae2] 0b
                    ex        af,af'                        ;[1ae3] 08
                    nop                                     ;[1ae4] 00
                    ret       m                             ;[1ae5] f8
                    inc       bc                            ;[1ae6] 03
                    add       hl,bc                         ;[1ae7] 09
                    dec       b                             ;[1ae8] 05
                    jr        nz,$1b0e                      ;[1ae9] 20 23
                    rlca                                    ;[1aeb] 07
                    rlca                                    ;[1aec] 07
                    rlca                                    ;[1aed] 07
                    rlca                                    ;[1aee] 07
                    rlca                                    ;[1aef] 07
                    rlca                                    ;[1af0] 07
                    ex        af,af'                        ;[1af1] 08
                    nop                                     ;[1af2] 00
                    ld        a,d                           ;[1af3] 7a
                    ld        e,$06                         ;[1af4] 1e 06
                    nop                                     ;[1af6] 00
                    sub       h                             ;[1af7] 94
                    ld        ($6005),hl                    ;[1af8] 22 05 60
                    rra                                     ;[1afb] 1f
                    ld        b,$2c                         ;[1afc] 06 2c
                    ld        a,(bc)                        ;[1afe] 0a
                    nop                                     ;[1aff] 00
                    ld        (hl),$17                      ;[1b00] 36 17
                    ld        b,$00                         ;[1b02] 06 00
                    push      hl                            ;[1b04] e5
                    ld        d,$0a                         ;[1b05] 16 0a
                    nop                                     ;[1b07] 00
                    sub       e                             ;[1b08] 93
                    rla                                     ;[1b09] 17
                    ld        a,(bc)                        ;[1b0a] 0a
                    inc       l                             ;[1b0b] 2c
                    ld        a,(bc)                        ;[1b0c] 0a
                    nop                                     ;[1b0d] 00
                    sub       e                             ;[1b0e] 93
                    rla                                     ;[1b0f] 17
                    ld        a,(bc)                        ;[1b10] 0a
                    nop                                     ;[1b11] 00
                    sub       e                             ;[1b12] 93
                    rla                                     ;[1b13] 17
                    nop                                     ;[1b14] 00
                    sub       e                             ;[1b15] 93
                    rla                                     ;[1b16] 17
                    res       7,(iy+$01)                    ;[1b17] fd cb 01 be
                    call      $19fb                         ;[1b1b] cd fb 19
                    xor       a                             ;[1b1e] af
                    ld        ($5c47),a                     ;[1b1f] 32 47 5c
                    dec       a                             ;[1b22] 3d
                    ld        ($5c3a),a                     ;[1b23] 32 3a 5c
                    jr        $1b29                         ;[1b26] 18 01
                    rst       $20                           ;[1b28] e7
                    call      $16bf                         ;[1b29] cd bf 16
                    inc       (iy+$0d)                      ;[1b2c] fd 34 0d
                    jp        m,$1c8a                       ;[1b2f] fa 8a 1c
                    rst       $18                           ;[1b32] df
                    ld        b,$00                         ;[1b33] 06 00
                    cp        $0d                           ;[1b35] fe 0d
                    jr        z,$1bb3                       ;[1b37] 28 7a
                    cp        $3a                           ;[1b39] fe 3a
                    jr        z,$1b28                       ;[1b3b] 28 eb
                    ld        hl,$1b76                      ;[1b3d] 21 76 1b
                    push      hl                            ;[1b40] e5
                    ld        c,a                           ;[1b41] 4f
                    rst       $20                           ;[1b42] e7
                    ld        a,c                           ;[1b43] 79
                    sub       $ce                           ;[1b44] d6 ce
                    jp        c,$1c8a                       ;[1b46] da 8a 1c
                    ld        c,a                           ;[1b49] 4f
                    ld        hl,$1a48                      ;[1b4a] 21 48 1a
                    add       hl,bc                         ;[1b4d] 09
                    ld        c,(hl)                        ;[1b4e] 4e
                    add       hl,bc                         ;[1b4f] 09
                    jr        $1b55                         ;[1b50] 18 03
                    ld        hl,($5c74)                    ;[1b52] 2a 74 5c
                    ld        a,(hl)                        ;[1b55] 7e
                    inc       hl                            ;[1b56] 23
                    ld        ($5c74),hl                    ;[1b57] 22 74 5c
                    ld        bc,$1b52                      ;[1b5a] 01 52 1b
                    push      bc                            ;[1b5d] c5
                    ld        c,a                           ;[1b5e] 4f
                    cp        $20                           ;[1b5f] fe 20
                    jr        nc,$1b6f                      ;[1b61] 30 0c
                    ld        hl,$1c01                      ;[1b63] 21 01 1c
                    ld        b,$00                         ;[1b66] 06 00
                    add       hl,bc                         ;[1b68] 09
                    ld        c,(hl)                        ;[1b69] 4e
                    add       hl,bc                         ;[1b6a] 09
                    push      hl                            ;[1b6b] e5
                    rst       $18                           ;[1b6c] df
                    dec       b                             ;[1b6d] 05
                    ret                                     ;[1b6e] c9

                    rst       $18                           ;[1b6f] df
                    cp        c                             ;[1b70] b9
                    jp        nz,$1c8a                      ;[1b71] c2 8a 1c
                    rst       $20                           ;[1b74] e7
                    ret                                     ;[1b75] c9

                    call      $1f54                         ;[1b76] cd 54 1f
                    jr        c,$1b7d                       ;[1b79] 38 02
                    rst       $08                           ;[1b7b] cf
                    inc       d                             ;[1b7c] 14
                    bit       7,(iy+$0a)                    ;[1b7d] fd cb 0a 7e
                    jr        nz,$1bf4                      ;[1b81] 20 71
                    ld        hl,($5c42)                    ;[1b83] 2a 42 5c
                    bit       7,h                           ;[1b86] cb 7c
                    jr        z,$1b9e                       ;[1b88] 28 14
                    ld        hl,$fffe                      ;[1b8a] 21 fe ff
                    ld        ($5c45),hl                    ;[1b8d] 22 45 5c
                    ld        hl,($5c61)                    ;[1b90] 2a 61 5c
                    dec       hl                            ;[1b93] 2b
                    ld        de,($5c59)                    ;[1b94] ed 5b 59 5c
                    dec       de                            ;[1b98] 1b
                    ld        a,($5c44)                     ;[1b99] 3a 44 5c
                    jr        $1bd1                         ;[1b9c] 18 33
                    call      $196e                         ;[1b9e] cd 6e 19
                    ld        a,($5c44)                     ;[1ba1] 3a 44 5c
                    jr        z,$1bbf                       ;[1ba4] 28 19
                    and       a                             ;[1ba6] a7
                    jr        nz,$1bec                      ;[1ba7] 20 43
                    ld        b,a                           ;[1ba9] 47
                    ld        a,(hl)                        ;[1baa] 7e
                    and       $c0                           ;[1bab] e6 c0
                    ld        a,b                           ;[1bad] 78
                    jr        z,$1bbf                       ;[1bae] 28 0f
                    rst       $08                           ;[1bb0] cf
                    rst       $38                           ;[1bb1] ff
                    pop       bc                            ;[1bb2] c1
                    call      $2530                         ;[1bb3] cd 30 25
                    ret       z                             ;[1bb6] c8
                    ld        hl,($5c55)                    ;[1bb7] 2a 55 5c
                    ld        a,$c0                         ;[1bba] 3e c0
                    and       (hl)                          ;[1bbc] a6
                    ret       nz                            ;[1bbd] c0
                    xor       a                             ;[1bbe] af
                    cp        $01                           ;[1bbf] fe 01
                    adc       $00                           ;[1bc1] ce 00
                    ld        d,(hl)                        ;[1bc3] 56
                    inc       hl                            ;[1bc4] 23
                    ld        e,(hl)                        ;[1bc5] 5e
                    ld        ($5c45),de                    ;[1bc6] ed 53 45 5c
                    inc       hl                            ;[1bca] 23
                    ld        e,(hl)                        ;[1bcb] 5e
                    inc       hl                            ;[1bcc] 23
                    ld        d,(hl)                        ;[1bcd] 56
                    ex        de,hl                         ;[1bce] eb
                    add       hl,de                         ;[1bcf] 19
                    inc       hl                            ;[1bd0] 23
                    ld        ($5c55),hl                    ;[1bd1] 22 55 5c
                    ex        de,hl                         ;[1bd4] eb
                    ld        ($5c5d),hl                    ;[1bd5] 22 5d 5c
                    ld        d,a                           ;[1bd8] 57
                    ld        e,$00                         ;[1bd9] 1e 00
                    ld        (iy+$0a),$ff                  ;[1bdb] fd 36 0a ff
                    dec       d                             ;[1bdf] 15
                    ld        (iy+$0d),d                    ;[1be0] fd 72 0d
                    jp        z,$1b28                       ;[1be3] ca 28 1b
                    inc       d                             ;[1be6] 14
                    call      $198b                         ;[1be7] cd 8b 19
                    jr        z,$1bf4                       ;[1bea] 28 08
                    rst       $08                           ;[1bec] cf
                    ld        d,$cd                         ;[1bed] 16 cd
                    jr        nc,$1c16                      ;[1bef] 30 25
                    ret       nz                            ;[1bf1] c0
                    pop       bc                            ;[1bf2] c1
                    pop       bc                            ;[1bf3] c1
                    rst       $18                           ;[1bf4] df
                    cp        $0d                           ;[1bf5] fe 0d
                    jr        z,$1bb3                       ;[1bf7] 28 ba
                    cp        $3a                           ;[1bf9] fe 3a
                    jp        z,$1b28                       ;[1bfb] ca 28 1b
                    jp        $1c8a                         ;[1bfe] c3 8a 1c
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
                    jr        nz,$1c46                      ;[1c37] 20 0d
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

                    call      $28b2                         ;[1c6c] cd b2 28
                    push      af                            ;[1c6f] f5
                    ld        a,c                           ;[1c70] 79
                    or        $9f                           ;[1c71] f6 9f
                    inc       a                             ;[1c73] 3c
                    jr        nz,$1c8a                      ;[1c74] 20 14
                    pop       af                            ;[1c76] f1
                    jr        $1c22                         ;[1c77] 18 a9
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
                    jp        $0605                         ;[1cdb] c3 05 06
                    cp        $0d                           ;[1cde] fe 0d
                    jr        z,$1ce6                       ;[1ce0] 28 04
                    cp        $3a                           ;[1ce2] fe 3a
                    jr        nz,$1c82                      ;[1ce4] 20 9c
                    call      $2530                         ;[1ce6] cd 30 25
                    ret       z                             ;[1ce9] c8
                    rst       $28                           ;[1cea] ef
                    and       b                             ;[1ceb] a0
                    jr        c,$1cb7                       ;[1cec] 38 c9
                    rst       $08                           ;[1cee] cf
                    ex        af,af'                        ;[1cef] 08
                    pop       bc                            ;[1cf0] c1
                    call      $2530                         ;[1cf1] cd 30 25
                    jr        z,$1d00                       ;[1cf4] 28 0a
                    rst       $28                           ;[1cf6] ef
                    ld        (bc),a                        ;[1cf7] 02
                    jr        c,$1ce5                       ;[1cf8] 38 eb
                    call      $34e9                         ;[1cfa] cd e9 34
                    jp        c,$1bb3                       ;[1cfd] da b3 1b
                    jp        $1b29                         ;[1d00] c3 29 1b
                    cp        $cd                           ;[1d03] fe cd
                    jr        nz,$1d10                      ;[1d05] 20 09
                    rst       $20                           ;[1d07] e7
                    call      $1c82                         ;[1d08] cd 82 1c
                    call      $1bee                         ;[1d0b] cd ee 1b
                    jr        $1d16                         ;[1d0e] 18 06
                    call      $1bee                         ;[1d10] cd ee 1b
                    rst       $28                           ;[1d13] ef
                    and       c                             ;[1d14] a1
                    jr        c,$1d06                       ;[1d15] 38 ef
                    ret       nz                            ;[1d17] c0
                    ld        (bc),a                        ;[1d18] 02
                    ld        bc,$01e0                      ;[1d19] 01 e0 01
                    jr        c,$1ceb                       ;[1d1c] 38 cd
                    rst       $38                           ;[1d1e] ff
                    ld        hl,($6822)                    ;[1d1f] 2a 22 68
                    ld        e,h                           ;[1d22] 5c
                    dec       hl                            ;[1d23] 2b
                    ld        a,(hl)                        ;[1d24] 7e
                    set       7,(hl)                        ;[1d25] cb fe
                    ld        bc,$0006                      ;[1d27] 01 06 00
                    add       hl,bc                         ;[1d2a] 09
                    rlca                                    ;[1d2b] 07
                    jr        c,$1d34                       ;[1d2c] 38 06
                    ld        c,$0d                         ;[1d2e] 0e 0d
                    call      $1655                         ;[1d30] cd 55 16
                    inc       hl                            ;[1d33] 23
                    push      hl                            ;[1d34] e5
                    rst       $28                           ;[1d35] ef
                    ld        (bc),a                        ;[1d36] 02
                    ld        (bc),a                        ;[1d37] 02
                    jr        c,$1d1b                       ;[1d38] 38 e1
                    ex        de,hl                         ;[1d3a] eb
                    ld        c,$0a                         ;[1d3b] 0e 0a
                    ldir                                    ;[1d3d] ed b0
                    ld        hl,($5c45)                    ;[1d3f] 2a 45 5c
                    ex        de,hl                         ;[1d42] eb
                    ld        (hl),e                        ;[1d43] 73
                    inc       hl                            ;[1d44] 23
                    ld        (hl),d                        ;[1d45] 72
                    ld        d,(iy+$0d)                    ;[1d46] fd 56 0d
                    inc       d                             ;[1d49] 14
                    inc       hl                            ;[1d4a] 23
                    ld        (hl),d                        ;[1d4b] 72
                    call      $1dda                         ;[1d4c] cd da 1d
                    ret       nc                            ;[1d4f] d0
                    ld        b,(iy+$38)                    ;[1d50] fd 46 38
                    ld        hl,($5c45)                    ;[1d53] 2a 45 5c
                    ld        ($5c42),hl                    ;[1d56] 22 42 5c
                    ld        a,($5c47)                     ;[1d59] 3a 47 5c
                    neg                                     ;[1d5c] ed 44
                    ld        d,a                           ;[1d5e] 57
                    ld        hl,($5c5d)                    ;[1d5f] 2a 5d 5c
                    ld        e,$f3                         ;[1d62] 1e f3
                    push      bc                            ;[1d64] c5
                    ld        bc,($5c55)                    ;[1d65] ed 4b 55 5c
                    call      $1d86                         ;[1d69] cd 86 1d
                    ld        ($5c55),bc                    ;[1d6c] ed 43 55 5c
                    pop       bc                            ;[1d70] c1
                    jr        c,$1d84                       ;[1d71] 38 11
                    rst       $20                           ;[1d73] e7
                    or        $20                           ;[1d74] f6 20
                    cp        b                             ;[1d76] b8
                    jr        z,$1d7c                       ;[1d77] 28 03
                    rst       $20                           ;[1d79] e7
                    jr        $1d64                         ;[1d7a] 18 e8
                    rst       $20                           ;[1d7c] e7
                    ld        a,$01                         ;[1d7d] 3e 01
                    sub       d                             ;[1d7f] 92
                    ld        ($5c44),a                     ;[1d80] 32 44 5c
                    ret                                     ;[1d83] c9

                    rst       $08                           ;[1d84] cf
                    ld        de,$fe7e                      ;[1d85] 11 7e fe
                    ld        a,($1828)                     ;[1d88] 3a 28 18
                    inc       hl                            ;[1d8b] 23
                    ld        a,(hl)                        ;[1d8c] 7e
                    and       $c0                           ;[1d8d] e6 c0
                    scf                                     ;[1d8f] 37
                    ret       nz                            ;[1d90] c0
                    ld        b,(hl)                        ;[1d91] 46
                    inc       hl                            ;[1d92] 23
                    ld        c,(hl)                        ;[1d93] 4e
                    ld        ($5c42),bc                    ;[1d94] ed 43 42 5c
                    inc       hl                            ;[1d98] 23
                    ld        c,(hl)                        ;[1d99] 4e
                    inc       hl                            ;[1d9a] 23
                    ld        b,(hl)                        ;[1d9b] 46
                    push      hl                            ;[1d9c] e5
                    add       hl,bc                         ;[1d9d] 09
                    ld        b,h                           ;[1d9e] 44
                    ld        c,l                           ;[1d9f] 4d
                    pop       hl                            ;[1da0] e1
                    ld        d,$00                         ;[1da1] 16 00
                    push      bc                            ;[1da3] c5
                    call      $198b                         ;[1da4] cd 8b 19
                    pop       bc                            ;[1da7] c1
                    ret       nc                            ;[1da8] d0
                    jr        $1d8b                         ;[1da9] 18 e0
                    bit       1,(iy+$37)                    ;[1dab] fd cb 37 4e
                    jp        nz,$1c2e                      ;[1daf] c2 2e 1c
                    ld        hl,($5c4d)                    ;[1db2] 2a 4d 5c
                    bit       7,(hl)                        ;[1db5] cb 7e
                    jr        z,$1dd8                       ;[1db7] 28 1f
                    inc       hl                            ;[1db9] 23
                    ld        ($5c68),hl                    ;[1dba] 22 68 5c
                    rst       $28                           ;[1dbd] ef
                    ret       po                            ;[1dbe] e0
                    jp        po,$c00f                      ;[1dbf] e2 0f c0
                    ld        (bc),a                        ;[1dc2] 02
                    jr        c,$1d92                       ;[1dc3] 38 cd
                    jp        c,$d81d                       ;[1dc5] da 1d d8
                    ld        hl,($5c68)                    ;[1dc8] 2a 68 5c
                    ld        de,$000f                      ;[1dcb] 11 0f 00
                    add       hl,de                         ;[1dce] 19
                    ld        e,(hl)                        ;[1dcf] 5e
                    inc       hl                            ;[1dd0] 23
                    ld        d,(hl)                        ;[1dd1] 56
                    inc       hl                            ;[1dd2] 23
                    ld        h,(hl)                        ;[1dd3] 66
                    ex        de,hl                         ;[1dd4] eb
                    jp        $1e73                         ;[1dd5] c3 73 1e
                    rst       $08                           ;[1dd8] cf
                    nop                                     ;[1dd9] 00
                    rst       $28                           ;[1dda] ef
                    pop       hl                            ;[1ddb] e1
                    ret       po                            ;[1ddc] e0
                    jp        po,$0036                      ;[1ddd] e2 36 00
                    ld        (bc),a                        ;[1de0] 02
                    ld        bc,$3703                      ;[1de1] 01 03 37
                    nop                                     ;[1de4] 00
                    inc       b                             ;[1de5] 04
                    jr        c,$1d8f                       ;[1de6] 38 a7
                    ret                                     ;[1de8] c9

                    jr        c,$1e22                       ;[1de9] 38 37
                    ret                                     ;[1deb] c9

                    rst       $20                           ;[1dec] e7
                    call      $1c1f                         ;[1ded] cd 1f 1c
                    call      $2530                         ;[1df0] cd 30 25
                    jr        z,$1e1e                       ;[1df3] 28 29
                    rst       $18                           ;[1df5] df
                    ld        ($5c5f),hl                    ;[1df6] 22 5f 5c
                    ld        hl,($5c57)                    ;[1df9] 2a 57 5c
                    ld        a,(hl)                        ;[1dfc] 7e
                    cp        $2c                           ;[1dfd] fe 2c
                    jr        z,$1e0a                       ;[1dff] 28 09
                    ld        e,$e4                         ;[1e01] 1e e4
                    call      $1d86                         ;[1e03] cd 86 1d
                    jr        nc,$1e0a                      ;[1e06] 30 02
                    rst       $08                           ;[1e08] cf
                    dec       c                             ;[1e09] 0d
                    call      $0077                         ;[1e0a] cd 77 00
                    call      $1c56                         ;[1e0d] cd 56 1c
                    rst       $18                           ;[1e10] df
                    ld        ($5c57),hl                    ;[1e11] 22 57 5c
                    ld        hl,($5c5f)                    ;[1e14] 2a 5f 5c
                    ld        (iy+$26),$00                  ;[1e17] fd 36 26 00
                    call      $0078                         ;[1e1b] cd 78 00
                    rst       $18                           ;[1e1e] df
                    cp        $2c                           ;[1e1f] fe 2c
                    jr        z,$1dec                       ;[1e21] 28 c9
                    call      $1bee                         ;[1e23] cd ee 1b
                    ret                                     ;[1e26] c9

                    call      $2530                         ;[1e27] cd 30 25
                    jr        nz,$1e37                      ;[1e2a] 20 0b
                    call      $24fb                         ;[1e2c] cd fb 24
                    cp        $2c                           ;[1e2f] fe 2c
                    call      nz,$1bee                      ;[1e31] c4 ee 1b
                    rst       $20                           ;[1e34] e7
                    jr        $1e2c                         ;[1e35] 18 f5
                    ld        a,$e4                         ;[1e37] 3e e4
                    ld        b,a                           ;[1e39] 47
                    cpdr                                    ;[1e3a] ed b9
                    ld        de,$0200                      ;[1e3c] 11 00 02
                    jp        $198b                         ;[1e3f] c3 8b 19
                    call      $1e99                         ;[1e42] cd 99 1e
                    ld        h,b                           ;[1e45] 60
                    ld        l,c                           ;[1e46] 69
                    call      $196e                         ;[1e47] cd 6e 19
                    dec       hl                            ;[1e4a] 2b
                    ld        ($5c57),hl                    ;[1e4b] 22 57 5c
                    ret                                     ;[1e4e] c9

                    call      $1e99                         ;[1e4f] cd 99 1e
                    ld        a,b                           ;[1e52] 78
                    or        c                             ;[1e53] b1
                    jr        nz,$1e5a                      ;[1e54] 20 04
                    ld        bc,($5c78)                    ;[1e56] ed 4b 78 5c
                    ld        ($5c76),bc                    ;[1e5a] ed 43 76 5c
                    ret                                     ;[1e5e] c9

                    ld        hl,($5c6e)                    ;[1e5f] 2a 6e 5c
                    ld        d,(iy+$36)                    ;[1e62] fd 56 36
                    jr        $1e73                         ;[1e65] 18 0c
                    call      $1e99                         ;[1e67] cd 99 1e
                    ld        h,b                           ;[1e6a] 60
                    ld        l,c                           ;[1e6b] 69
                    ld        d,$00                         ;[1e6c] 16 00
                    ld        a,h                           ;[1e6e] 7c
                    cp        $f0                           ;[1e6f] fe f0
                    jr        nc,$1e9f                      ;[1e71] 30 2c
                    ld        ($5c42),hl                    ;[1e73] 22 42 5c
                    ld        (iy+$0a),d                    ;[1e76] fd 72 0a
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
                    call      $1e67                         ;[1ea1] cd 67 1e
                    ld        bc,$0000                      ;[1ea4] 01 00 00
                    call      $1e45                         ;[1ea7] cd 45 1e
                    jr        $1eaf                         ;[1eaa] 18 03
                    call      $1e99                         ;[1eac] cd 99 1e
                    ld        a,b                           ;[1eaf] 78
                    or        c                             ;[1eb0] b1
                    jr        nz,$1eb7                      ;[1eb1] 20 04
                    ld        bc,($5cb2)                    ;[1eb3] ed 4b b2 5c
                    push      bc                            ;[1eb7] c5
                    ld        de,($5c4b)                    ;[1eb8] ed 5b 4b 5c
                    ld        hl,($5c59)                    ;[1ebc] 2a 59 5c
                    dec       hl                            ;[1ebf] 2b
                    call      $19e5                         ;[1ec0] cd e5 19
                    call      $0d6b                         ;[1ec3] cd 6b 0d
                    ld        hl,($5c65)                    ;[1ec6] 2a 65 5c
                    ld        de,$0032                      ;[1ec9] 11 32 00
                    add       hl,de                         ;[1ecc] 19
                    pop       de                            ;[1ecd] d1
                    sbc       hl,de                         ;[1ece] ed 52
                    jr        nc,$1eda                      ;[1ed0] 30 08
                    ld        hl,($5cb4)                    ;[1ed2] 2a b4 5c
                    and       a                             ;[1ed5] a7
                    sbc       hl,de                         ;[1ed6] ed 52
                    jr        nc,$1edc                      ;[1ed8] 30 02
                    rst       $08                           ;[1eda] cf
                    dec       d                             ;[1edb] 15
                    ex        de,hl                         ;[1edc] eb
                    ld        ($5cb2),hl                    ;[1edd] 22 b2 5c
                    pop       de                            ;[1ee0] d1
                    pop       bc                            ;[1ee1] c1
                    ld        (hl),$3e                      ;[1ee2] 36 3e
                    dec       hl                            ;[1ee4] 2b
                    ld        sp,hl                         ;[1ee5] f9
                    push      bc                            ;[1ee6] c5
                    ld        ($5c3d),sp                    ;[1ee7] ed 73 3d 5c
                    ex        de,hl                         ;[1eeb] eb
                    jp        (hl)                          ;[1eec] e9
                    pop       de                            ;[1eed] d1
                    ld        h,(iy+$0d)                    ;[1eee] fd 66 0d
                    inc       h                             ;[1ef1] 24
                    ex        (sp),hl                       ;[1ef2] e3
                    inc       sp                            ;[1ef3] 33
                    ld        bc,($5c45)                    ;[1ef4] ed 4b 45 5c
                    push      bc                            ;[1ef8] c5
                    push      hl                            ;[1ef9] e5
                    ld        ($5c3d),sp                    ;[1efa] ed 73 3d 5c
                    push      de                            ;[1efe] d5
                    call      $1e67                         ;[1eff] cd 67 1e
                    ld        bc,$0014                      ;[1f02] 01 14 00
                    ld        hl,($5c65)                    ;[1f05] 2a 65 5c
                    add       hl,bc                         ;[1f08] 09
                    jr        c,$1f15                       ;[1f09] 38 0a
                    ex        de,hl                         ;[1f0b] eb
                    ld        hl,$0050                      ;[1f0c] 21 50 00
                    add       hl,de                         ;[1f0f] 19
                    jr        c,$1f15                       ;[1f10] 38 03
                    sbc       hl,sp                         ;[1f12] ed 72
                    ret       c                             ;[1f14] d8
                    ld        l,$03                         ;[1f15] 2e 03
                    jp        $0055                         ;[1f17] c3 55 00
                    ld        bc,$0000                      ;[1f1a] 01 00 00
                    call      $1f05                         ;[1f1d] cd 05 1f
                    ld        b,h                           ;[1f20] 44
                    ld        c,l                           ;[1f21] 4d
                    ret                                     ;[1f22] c9

                    pop       bc                            ;[1f23] c1
                    pop       hl                            ;[1f24] e1
                    pop       de                            ;[1f25] d1
                    ld        a,d                           ;[1f26] 7a
                    cp        $3e                           ;[1f27] fe 3e
                    jr        z,$1f36                       ;[1f29] 28 0b
                    dec       sp                            ;[1f2b] 3b
                    ex        (sp),hl                       ;[1f2c] e3
                    ex        de,hl                         ;[1f2d] eb
                    ld        ($5c3d),sp                    ;[1f2e] ed 73 3d 5c
                    push      bc                            ;[1f32] c5
                    jp        $1e73                         ;[1f33] c3 73 1e
                    push      de                            ;[1f36] d5
                    push      hl                            ;[1f37] e5
                    rst       $08                           ;[1f38] cf
                    ld        b,$cd                         ;[1f39] 06 cd
                    sbc       c                             ;[1f3b] 99
                    ld        e,$76                         ;[1f3c] 1e 76
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
                    ret                                     ;[1f5f] c9

                    call      $2530                         ;[1f60] cd 30 25
                    jr        z,$1f6a                       ;[1f63] 28 05
                    ld        a,$ce                         ;[1f65] 3e ce
                    jp        $1e39                         ;[1f67] c3 39 1e
                    set       6,(iy+$01)                    ;[1f6a] fd cb 01 f6
                    call      $2c8d                         ;[1f6e] cd 8d 2c
                    jr        nc,$1f89                      ;[1f71] 30 16
                    rst       $20                           ;[1f73] e7
                    cp        $24                           ;[1f74] fe 24
                    jr        nz,$1f7d                      ;[1f76] 20 05
                    res       6,(iy+$01)                    ;[1f78] fd cb 01 b6
                    rst       $20                           ;[1f7c] e7
                    cp        $28                           ;[1f7d] fe 28
                    jr        nz,$1fbd                      ;[1f7f] 20 3c
                    rst       $20                           ;[1f81] e7
                    cp        $29                           ;[1f82] fe 29
                    jr        z,$1fa6                       ;[1f84] 28 20
                    call      $2c8d                         ;[1f86] cd 8d 2c
                    jp        nc,$1c8a                      ;[1f89] d2 8a 1c
                    ex        de,hl                         ;[1f8c] eb
                    rst       $20                           ;[1f8d] e7
                    cp        $24                           ;[1f8e] fe 24
                    jr        nz,$1f94                      ;[1f90] 20 02
                    ex        de,hl                         ;[1f92] eb
                    rst       $20                           ;[1f93] e7
                    ex        de,hl                         ;[1f94] eb
                    ld        bc,$0006                      ;[1f95] 01 06 00
                    call      $1655                         ;[1f98] cd 55 16
                    inc       hl                            ;[1f9b] 23
                    inc       hl                            ;[1f9c] 23
                    ld        (hl),$0e                      ;[1f9d] 36 0e
                    cp        $2c                           ;[1f9f] fe 2c
                    jr        nz,$1fa6                      ;[1fa1] 20 03
                    rst       $20                           ;[1fa3] e7
                    jr        $1f86                         ;[1fa4] 18 e0
                    cp        $29                           ;[1fa6] fe 29
                    jr        nz,$1fbd                      ;[1fa8] 20 13
                    rst       $20                           ;[1faa] e7
                    cp        $3d                           ;[1fab] fe 3d
                    jr        nz,$1fbd                      ;[1fad] 20 0e
                    rst       $20                           ;[1faf] e7
                    ld        a,($5c3b)                     ;[1fb0] 3a 3b 5c
                    push      af                            ;[1fb3] f5
                    call      $24fb                         ;[1fb4] cd fb 24
                    pop       af                            ;[1fb7] f1
                    xor       (iy+$01)                      ;[1fb8] fd ae 01
                    and       $40                           ;[1fbb] e6 40
                    jp        nz,$1c8a                      ;[1fbd] c2 8a 1c
                    call      $1bee                         ;[1fc0] cd ee 1b
                    call      $2530                         ;[1fc3] cd 30 25
                    pop       hl                            ;[1fc6] e1
                    ret       z                             ;[1fc7] c8
                    jp        (hl)                          ;[1fc8] e9
                    ld        a,$03                         ;[1fc9] 3e 03
                    jr        $1fcf                         ;[1fcb] 18 02
                    ld        a,$02                         ;[1fcd] 3e 02
                    call      $2530                         ;[1fcf] cd 30 25
                    call      nz,$1601                      ;[1fd2] c4 01 16
                    call      $0d4d                         ;[1fd5] cd 4d 0d
                    call      $1fdf                         ;[1fd8] cd df 1f
                    call      $1bee                         ;[1fdb] cd ee 1b
                    ret                                     ;[1fde] c9

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

                    call      $2530                         ;[2089] cd 30 25
                    jr        z,$2096                       ;[208c] 28 08
                    ld        a,$01                         ;[208e] 3e 01
                    call      $1601                         ;[2090] cd 01 16
                    call      $0d6e                         ;[2093] cd 6e 0d
                    ld        (iy+$02),$01                  ;[2096] fd 36 02 01
                    call      $20c1                         ;[209a] cd c1 20
                    call      $1bee                         ;[209d] cd ee 1b
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
                    call      $204e                         ;[20c1] cd 4e 20
                    jr        z,$20c1                       ;[20c4] 28 fb
                    cp        $28                           ;[20c6] fe 28
                    jr        nz,$20d8                      ;[20c8] 20 0e
                    rst       $20                           ;[20ca] e7
                    call      $1fdf                         ;[20cb] cd df 1f
                    rst       $18                           ;[20ce] df
                    cp        $29                           ;[20cf] fe 29
                    jp        nz,$1c8a                      ;[20d1] c2 8a 1c
                    rst       $20                           ;[20d4] e7
                    jp        $21b2                         ;[20d5] c3 b2 21
                    cp        $ca                           ;[20d8] fe ca
                    jr        nz,$20ed                      ;[20da] 20 11
                    rst       $20                           ;[20dc] e7
                    call      $1c1f                         ;[20dd] cd 1f 1c
                    set       7,(iy+$37)                    ;[20e0] fd cb 37 fe
                    bit       6,(iy+$01)                    ;[20e4] fd cb 01 76
                    jp        nz,$1c8a                      ;[20e8] c2 8a 1c
                    jr        $20fa                         ;[20eb] 18 0d
                    call      $2c8d                         ;[20ed] cd 8d 2c
                    jp        nc,$21af                      ;[20f0] d2 af 21
                    call      $1c1f                         ;[20f3] cd 1f 1c
                    res       7,(iy+$37)                    ;[20f6] fd cb 37 be
                    call      $2530                         ;[20fa] cd 30 25
                    jp        z,$21b2                       ;[20fd] ca b2 21
                    call      $16bf                         ;[2100] cd bf 16
                    ld        hl,$5c71                      ;[2103] 21 71 5c
                    res       6,(hl)                        ;[2106] cb b6
                    set       5,(hl)                        ;[2108] cb ee
                    ld        bc,$0001                      ;[210a] 01 01 00
                    bit       7,(hl)                        ;[210d] cb 7e
                    jr        nz,$211c                      ;[210f] 20 0b
                    ld        a,($5c3b)                     ;[2111] 3a 3b 5c
                    and       $40                           ;[2114] e6 40
                    jr        nz,$211a                      ;[2116] 20 02
                    ld        c,$03                         ;[2118] 0e 03
                    or        (hl)                          ;[211a] b6
                    ld        (hl),a                        ;[211b] 77
                    rst       $30                           ;[211c] f7
                    ld        (hl),$0d                      ;[211d] 36 0d
                    ld        a,c                           ;[211f] 79
                    rrca                                    ;[2120] 0f
                    rrca                                    ;[2121] 0f
                    jr        nc,$2129                      ;[2122] 30 05
                    ld        a,$22                         ;[2124] 3e 22
                    ld        (de),a                        ;[2126] 12
                    dec       hl                            ;[2127] 2b
                    ld        (hl),a                        ;[2128] 77
                    ld        ($5c5b),hl                    ;[2129] 22 5b 5c
                    bit       7,(iy+$37)                    ;[212c] fd cb 37 7e
                    jr        nz,$215e                      ;[2130] 20 2c
                    ld        hl,($5c5d)                    ;[2132] 2a 5d 5c
                    push      hl                            ;[2135] e5
                    ld        hl,($5c3d)                    ;[2136] 2a 3d 5c
                    push      hl                            ;[2139] e5
                    ld        hl,$213a                      ;[213a] 21 3a 21
                    push      hl                            ;[213d] e5
                    bit       4,(iy+$30)                    ;[213e] fd cb 30 66
                    jr        z,$2148                       ;[2142] 28 04
                    ld        ($5c3d),sp                    ;[2144] ed 73 3d 5c
                    ld        hl,($5c61)                    ;[2148] 2a 61 5c
                    call      $11a7                         ;[214b] cd a7 11
                    ld        (iy+$00),$ff                  ;[214e] fd 36 00 ff
                    call      $0f2c                         ;[2152] cd 2c 0f
                    res       7,(iy+$01)                    ;[2155] fd cb 01 be
                    call      $21b9                         ;[2159] cd b9 21
                    jr        $2161                         ;[215c] 18 03
                    call      $0f2c                         ;[215e] cd 2c 0f
                    ld        (iy+$22),$00                  ;[2161] fd 36 22 00
                    call      $21d6                         ;[2165] cd d6 21
                    jr        nz,$2174                      ;[2168] 20 0a
                    call      $111d                         ;[216a] cd 1d 11
                    ld        bc,($5c82)                    ;[216d] ed 4b 82 5c
                    call      $0dd9                         ;[2171] cd d9 0d
                    ld        hl,$5c71                      ;[2174] 21 71 5c
                    res       5,(hl)                        ;[2177] cb ae
                    bit       7,(hl)                        ;[2179] cb 7e
                    res       7,(hl)                        ;[217b] cb be
                    jr        nz,$219b                      ;[217d] 20 1c
                    pop       hl                            ;[217f] e1
                    pop       hl                            ;[2180] e1
                    ld        ($5c3d),hl                    ;[2181] 22 3d 5c
                    pop       hl                            ;[2184] e1
                    ld        ($5c5f),hl                    ;[2185] 22 5f 5c
                    set       7,(iy+$01)                    ;[2188] fd cb 01 fe
                    call      $21b9                         ;[218c] cd b9 21
                    ld        hl,($5c5f)                    ;[218f] 2a 5f 5c
                    ld        (iy+$26),$00                  ;[2192] fd 36 26 00
                    ld        ($5c5d),hl                    ;[2196] 22 5d 5c
                    jr        $21b2                         ;[2199] 18 17
                    ld        hl,($5c63)                    ;[219b] 2a 63 5c
                    ld        de,($5c61)                    ;[219e] ed 5b 61 5c
                    scf                                     ;[21a2] 37
                    sbc       hl,de                         ;[21a3] ed 52
                    ld        b,h                           ;[21a5] 44
                    ld        c,l                           ;[21a6] 4d
                    call      $2ab2                         ;[21a7] cd b2 2a
                    call      $2aff                         ;[21aa] cd ff 2a
                    jr        $21b2                         ;[21ad] 18 03
                    call      $1ffc                         ;[21af] cd fc 1f
                    call      $204e                         ;[21b2] cd 4e 20
                    jp        z,$20c1                       ;[21b5] ca c1 20
                    ret                                     ;[21b8] c9

                    ld        hl,($5c61)                    ;[21b9] 2a 61 5c
                    ld        ($5c5d),hl                    ;[21bc] 22 5d 5c
                    rst       $18                           ;[21bf] df
                    cp        $e2                           ;[21c0] fe e2
                    jr        z,$21d0                       ;[21c2] 28 0c
                    ld        a,($5c71)                     ;[21c4] 3a 71 5c
                    call      $1c59                         ;[21c7] cd 59 1c
                    rst       $18                           ;[21ca] df
                    cp        $0d                           ;[21cb] fe 0d
                    ret       z                             ;[21cd] c8
                    rst       $08                           ;[21ce] cf
                    dec       bc                            ;[21cf] 0b
                    call      $2530                         ;[21d0] cd 30 25
                    ret       z                             ;[21d3] c8
                    rst       $08                           ;[21d4] cf
                    djnz      $2201                         ;[21d5] 10 2a
                    ld        d,c                           ;[21d7] 51
                    ld        e,h                           ;[21d8] 5c
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
                    ld        b,$00                         ;[24fc] 06 00
                    push      bc                            ;[24fe] c5
                    ld        c,a                           ;[24ff] 4f
                    ld        hl,$2596                      ;[2500] 21 96 25
                    call      $16dc                         ;[2503] cd dc 16
                    ld        a,c                           ;[2506] 79
                    jp        nc,$2684                      ;[2507] d2 84 26
                    ld        b,$00                         ;[250a] 06 00
                    ld        c,(hl)                        ;[250c] 4e
                    add       hl,bc                         ;[250d] 09
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
                    ld        de,$0100                      ;[253b] 11 00 01
                    add       hl,de                         ;[253e] 19
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
                    jr        nz,$2573                      ;[2557] 20 1a
                    dec       a                             ;[2559] 3d
                    ld        c,a                           ;[255a] 4f
                    ld        b,$07                         ;[255b] 06 07
                    inc       d                             ;[255d] 14
                    inc       hl                            ;[255e] 23
                    ld        a,(de)                        ;[255f] 1a
                    xor       (hl)                          ;[2560] ae
                    xor       c                             ;[2561] a9
                    jr        nz,$2573                      ;[2562] 20 0f
                    djnz      $255d                         ;[2564] 10 f7
                    pop       bc                            ;[2566] c1
                    pop       bc                            ;[2567] c1
                    pop       bc                            ;[2568] c1
                    ld        a,$80                         ;[2569] 3e 80
                    sub       b                             ;[256b] 90
                    ld        bc,$0001                      ;[256c] 01 01 00
                    rst       $30                           ;[256f] f7
                    ld        (de),a                        ;[2570] 12
                    jr        $257d                         ;[2571] 18 0a
                    pop       hl                            ;[2573] e1
                    ld        de,$0008                      ;[2574] 11 08 00
                    add       hl,de                         ;[2577] 19
                    pop       de                            ;[2578] d1
                    pop       bc                            ;[2579] c1
                    djnz      $254f                         ;[257a] 10 d3
                    ld        c,b                           ;[257c] 48
                    jp        $2ab2                         ;[257d] c3 b2 2a
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
                    ld        ($281c),hl                    ;[2596] 22 1c 28
                    ld        c,a                           ;[2599] 4f
                    ld        l,$f2                         ;[259a] 2e f2
                    dec       hl                            ;[259c] 2b
                    ld        (de),a                        ;[259d] 12
                    xor       b                             ;[259e] a8
                    ld        d,(hl)                        ;[259f] 56
                    and       l                             ;[25a0] a5
                    ld        d,a                           ;[25a1] 57
                    and       a                             ;[25a2] a7
                    add       h                             ;[25a3] 84
                    and       (hl)                          ;[25a4] a6
                    adc       a                             ;[25a5] 8f
                    call      nz,$aae6                      ;[25a6] c4 e6 aa
                    cp        a                             ;[25a9] bf
                    xor       e                             ;[25aa] ab
                    rst       $00                           ;[25ab] c7
                    xor       c                             ;[25ac] a9
                    adc       $00                           ;[25ad] ce 00
                    rst       $20                           ;[25af] e7
                    jp        $24ff                         ;[25b0] c3 ff 24
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
                    jp        $27bd                         ;[25f5] c3 bd 27
                    call      $2530                         ;[25f8] cd 30 25
                    jr        z,$2625                       ;[25fb] 28 28
                    ld        bc,($5c76)                    ;[25fd] ed 4b 76 5c
                    call      $2d2b                         ;[2601] cd 2b 2d
                    rst       $28                           ;[2604] ef
                    and       c                             ;[2605] a1
                    rrca                                    ;[2606] 0f
                    inc       (hl)                          ;[2607] 34
                    scf                                     ;[2608] 37
                    ld        d,$04                         ;[2609] 16 04
                    inc       (hl)                          ;[260b] 34
                    add       b                             ;[260c] 80
                    ld        b,c                           ;[260d] 41
                    nop                                     ;[260e] 00
                    nop                                     ;[260f] 00
                    add       b                             ;[2610] 80
                    ld        ($a102),a                     ;[2611] 32 02 a1
                    inc       bc                            ;[2614] 03
                    ld        sp,$cd38                      ;[2615] 31 38 cd
                    and       d                             ;[2618] a2
                    dec       l                             ;[2619] 2d
                    ld        ($5c76),bc                    ;[261a] ed 43 76 5c
                    ld        a,(hl)                        ;[261e] 7e
                    and       a                             ;[261f] a7
                    jr        z,$2625                       ;[2620] 28 03
                    sub       $10                           ;[2622] d6 10
                    ld        (hl),a                        ;[2624] 77
                    jr        $2630                         ;[2625] 18 09
                    call      $2530                         ;[2627] cd 30 25
                    jr        z,$2630                       ;[262a] 28 04
                    rst       $28                           ;[262c] ef
                    and       e                             ;[262d] a3
                    jr        c,$2664                       ;[262e] 38 34
                    rst       $20                           ;[2630] e7
                    jp        $26c3                         ;[2631] c3 c3 26
                    ld        bc,$105a                      ;[2634] 01 5a 10
                    rst       $20                           ;[2637] e7
                    cp        $23                           ;[2638] fe 23
                    jp        z,$270d                       ;[263a] ca 0d 27
                    ld        hl,$5c3b                      ;[263d] 21 3b 5c
                    res       6,(hl)                        ;[2640] cb b6
                    bit       7,(hl)                        ;[2642] cb 7e
                    jr        z,$2665                       ;[2644] 28 1f
                    call      $028e                         ;[2646] cd 8e 02
                    ld        c,$00                         ;[2649] 0e 00
                    jr        nz,$2660                      ;[264b] 20 13
                    call      $031e                         ;[264d] cd 1e 03
                    jr        nc,$2660                      ;[2650] 30 0e
                    dec       d                             ;[2652] 15
                    ld        e,a                           ;[2653] 5f
                    call      $0333                         ;[2654] cd 33 03
                    push      af                            ;[2657] f5
                    ld        bc,$0001                      ;[2658] 01 01 00
                    rst       $30                           ;[265b] f7
                    pop       af                            ;[265c] f1
                    ld        (de),a                        ;[265d] 12
                    ld        c,$01                         ;[265e] 0e 01
                    ld        b,$00                         ;[2660] 06 00
                    call      $2ab2                         ;[2662] cd b2 2a
                    jp        $2712                         ;[2665] c3 12 27
                    call      $2522                         ;[2668] cd 22 25
                    call      nz,$2535                      ;[266b] c4 35 25
                    rst       $20                           ;[266e] e7
                    jp        $25db                         ;[266f] c3 db 25
                    call      $2522                         ;[2672] cd 22 25
                    call      nz,$2580                      ;[2675] c4 80 25
                    rst       $20                           ;[2678] e7
                    jr        $26c3                         ;[2679] 18 48
                    call      $2522                         ;[267b] cd 22 25
                    call      nz,$22cb                      ;[267e] c4 cb 22
                    rst       $20                           ;[2681] e7
                    jr        $26c3                         ;[2682] 18 3f
                    call      $2c88                         ;[2684] cd 88 2c
                    jr        nc,$26df                      ;[2687] 30 56
                    cp        $41                           ;[2689] fe 41
                    jr        nc,$26c9                      ;[268b] 30 3c
                    call      $2530                         ;[268d] cd 30 25
                    jr        nz,$26b5                      ;[2690] 20 23
                    call      $2c9b                         ;[2692] cd 9b 2c
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
                    call      $0077                         ;[26b0] cd 77 00
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
                    jr        $26dd                         ;[26c7] 18 14
                    call      $28b2                         ;[26c9] cd b2 28
                    jp        c,$1c2e                       ;[26cc] da 2e 1c
                    call      z,$2996                       ;[26cf] cc 96 29
                    ld        a,($5c3b)                     ;[26d2] 3a 3b 5c
                    cp        $c0                           ;[26d5] fe c0
                    jr        c,$26dd                       ;[26d7] 38 04
                    inc       hl                            ;[26d9] 23
                    call      $33b4                         ;[26da] cd b4 33
                    jr        $2712                         ;[26dd] 18 33
                    ld        bc,$09db                      ;[26df] 01 db 09
                    cp        $2d                           ;[26e2] fe 2d
                    jr        z,$270d                       ;[26e4] 28 27
                    ld        bc,$1018                      ;[26e6] 01 18 10
                    cp        $ae                           ;[26e9] fe ae
                    jr        z,$270d                       ;[26eb] 28 20
                    sub       $af                           ;[26ed] d6 af
                    jp        c,$1c8a                       ;[26ef] da 8a 1c
                    ld        bc,$04f0                      ;[26f2] 01 f0 04
                    cp        $14                           ;[26f5] fe 14
                    jr        z,$270d                       ;[26f7] 28 14
                    jp        nc,$1c8a                      ;[26f9] d2 8a 1c
                    ld        b,$10                         ;[26fc] 06 10
                    add       $dc                           ;[26fe] c6 dc
                    ld        c,a                           ;[2700] 4f
                    cp        $df                           ;[2701] fe df
                    jr        nc,$2707                      ;[2703] 30 02
                    res       6,c                           ;[2705] cb b1
                    cp        $ee                           ;[2707] fe ee
                    jr        c,$270d                       ;[2709] 38 02
                    res       7,c                           ;[270b] cb b9
                    push      bc                            ;[270d] c5
                    rst       $20                           ;[270e] e7
                    jp        $24ff                         ;[270f] c3 ff 24
                    rst       $18                           ;[2712] df
                    cp        $28                           ;[2713] fe 28
                    jr        nz,$2723                      ;[2715] 20 0c
                    bit       6,(iy+$01)                    ;[2717] fd cb 01 76
                    jr        nz,$2734                      ;[271b] 20 17
                    call      $2a52                         ;[271d] cd 52 2a
                    rst       $20                           ;[2720] e7
                    jr        $2713                         ;[2721] 18 f0
                    ld        b,$00                         ;[2723] 06 00
                    ld        c,a                           ;[2725] 4f
                    ld        hl,$2795                      ;[2726] 21 95 27
                    call      $16dc                         ;[2729] cd dc 16
                    jr        nc,$2734                      ;[272c] 30 06
                    ld        c,(hl)                        ;[272e] 4e
                    ld        hl,$26ed                      ;[272f] 21 ed 26
                    add       hl,bc                         ;[2732] 09
                    ld        b,(hl)                        ;[2733] 46
                    pop       de                            ;[2734] d1
                    ld        a,d                           ;[2735] 7a
                    cp        b                             ;[2736] b8
                    jr        c,$2773                       ;[2737] 38 3a
                    and       a                             ;[2739] a7
                    jp        z,$0018                       ;[273a] ca 18 00
                    push      bc                            ;[273d] c5
                    ld        hl,$5c3b                      ;[273e] 21 3b 5c
                    ld        a,e                           ;[2741] 7b
                    cp        $ed                           ;[2742] fe ed
                    jr        nz,$274c                      ;[2744] 20 06
                    bit       6,(hl)                        ;[2746] cb 76
                    jr        nz,$274c                      ;[2748] 20 02
                    ld        e,$99                         ;[274a] 1e 99
                    push      de                            ;[274c] d5
                    call      $2530                         ;[274d] cd 30 25
                    jr        z,$275b                       ;[2750] 28 09
                    ld        a,e                           ;[2752] 7b
                    and       $3f                           ;[2753] e6 3f
                    ld        b,a                           ;[2755] 47
                    rst       $28                           ;[2756] ef
                    dec       sp                            ;[2757] 3b
                    jr        c,$2772                       ;[2758] 38 18
                    add       hl,bc                         ;[275a] 09
                    ld        a,e                           ;[275b] 7b
                    xor       (iy+$01)                      ;[275c] fd ae 01
                    and       $40                           ;[275f] e6 40
                    jp        nz,$1c8a                      ;[2761] c2 8a 1c
                    pop       de                            ;[2764] d1
                    ld        hl,$5c3b                      ;[2765] 21 3b 5c
                    set       6,(hl)                        ;[2768] cb f6
                    bit       7,e                           ;[276a] cb 7b
                    jr        nz,$2770                      ;[276c] 20 02
                    res       6,(hl)                        ;[276e] cb b6
                    pop       bc                            ;[2770] c1
                    jr        $2734                         ;[2771] 18 c1
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
                    jr        c,$2761                       ;[2788] 38 d7
                    cp        $17                           ;[278a] fe 17
                    jr        z,$2790                       ;[278c] 28 02
                    set       7,c                           ;[278e] cb f9
                    push      bc                            ;[2790] c5
                    rst       $20                           ;[2791] e7
                    jp        $24ff                         ;[2792] c3 ff 24
                    dec       hl                            ;[2795] 2b
                    rst       $08                           ;[2796] cf
                    dec       l                             ;[2797] 2d
                    jp        $c42a                         ;[2798] c3 2a c4
                    cpl                                     ;[279b] 2f
                    push      bc                            ;[279c] c5
                    ld        e,(hl)                        ;[279d] 5e
                    add       $3d                           ;[279e] c6 3d
                    adc       $3e                           ;[27a0] ce 3e
                    call      z,$cd3c                       ;[27a2] cc 3c cd
                    rst       $00                           ;[27a5] c7
                    ret                                     ;[27a6] c9

                    ret       z                             ;[27a7] c8
                    jp        z,$cbc9                       ;[27a8] ca c9 cb
                    push      bc                            ;[27ab] c5
                    rst       $00                           ;[27ac] c7
                    add       $c8                           ;[27ad] c6 c8
                    nop                                     ;[27af] 00
                    ld        b,$08                         ;[27b0] 06 08
                    ex        af,af'                        ;[27b2] 08
                    ld        a,(bc)                        ;[27b3] 0a
                    ld        (bc),a                        ;[27b4] 02
                    inc       bc                            ;[27b5] 03
                    dec       b                             ;[27b6] 05
                    dec       b                             ;[27b7] 05
                    dec       b                             ;[27b8] 05
                    dec       b                             ;[27b9] 05
                    dec       b                             ;[27ba] 05
                    dec       b                             ;[27bb] 05
                    ld        b,$cd                         ;[27bc] 06 cd
                    jr        nc,$27e5                      ;[27be] 30 25
                    jr        nz,$27f7                      ;[27c0] 20 35
                    rst       $20                           ;[27c2] e7
                    call      $2c8d                         ;[27c3] cd 8d 2c
                    jp        nc,$1c8a                      ;[27c6] d2 8a 1c
                    rst       $20                           ;[27c9] e7
                    cp        $24                           ;[27ca] fe 24
                    push      af                            ;[27cc] f5
                    jr        nz,$27d0                      ;[27cd] 20 01
                    rst       $20                           ;[27cf] e7
                    cp        $28                           ;[27d0] fe 28
                    jr        nz,$27e6                      ;[27d2] 20 12
                    rst       $20                           ;[27d4] e7
                    cp        $29                           ;[27d5] fe 29
                    jr        z,$27e9                       ;[27d7] 28 10
                    call      $24fb                         ;[27d9] cd fb 24
                    rst       $18                           ;[27dc] df
                    cp        $2c                           ;[27dd] fe 2c
                    jr        nz,$27e4                      ;[27df] 20 03
                    rst       $20                           ;[27e1] e7
                    jr        $27d9                         ;[27e2] 18 f5
                    cp        $29                           ;[27e4] fe 29
                    jp        nz,$1c8a                      ;[27e6] c2 8a 1c
                    rst       $20                           ;[27e9] e7
                    ld        hl,$5c3b                      ;[27ea] 21 3b 5c
                    res       6,(hl)                        ;[27ed] cb b6
                    pop       af                            ;[27ef] f1
                    jr        z,$27f4                       ;[27f0] 28 02
                    set       6,(hl)                        ;[27f2] cb f6
                    jp        $2712                         ;[27f4] c3 12 27
                    rst       $20                           ;[27f7] e7
                    and       $df                           ;[27f8] e6 df
                    ld        b,a                           ;[27fa] 47
                    rst       $20                           ;[27fb] e7
                    sub       $24                           ;[27fc] d6 24
                    ld        c,a                           ;[27fe] 4f
                    jr        nz,$2802                      ;[27ff] 20 01
                    rst       $20                           ;[2801] e7
                    rst       $20                           ;[2802] e7
                    push      hl                            ;[2803] e5
                    ld        hl,($5c53)                    ;[2804] 2a 53 5c
                    dec       hl                            ;[2807] 2b
                    ld        de,$00ce                      ;[2808] 11 ce 00
                    push      bc                            ;[280b] c5
                    call      $1d86                         ;[280c] cd 86 1d
                    pop       bc                            ;[280f] c1
                    jr        nc,$2814                      ;[2810] 30 02
                    rst       $08                           ;[2812] cf
                    jr        $27fa                         ;[2813] 18 e5
                    call      $28ab                         ;[2815] cd ab 28
                    and       $df                           ;[2818] e6 df
                    cp        b                             ;[281a] b8
                    jr        nz,$2825                      ;[281b] 20 08
                    call      $28ab                         ;[281d] cd ab 28
                    sub       $24                           ;[2820] d6 24
                    cp        c                             ;[2822] b9
                    jr        z,$2831                       ;[2823] 28 0c
                    pop       hl                            ;[2825] e1
                    dec       hl                            ;[2826] 2b
                    ld        de,$0200                      ;[2827] 11 00 02
                    push      bc                            ;[282a] c5
                    call      $198b                         ;[282b] cd 8b 19
                    pop       bc                            ;[282e] c1
                    jr        $2808                         ;[282f] 18 d7
                    and       a                             ;[2831] a7
                    call      z,$28ab                       ;[2832] cc ab 28
                    pop       de                            ;[2835] d1
                    pop       de                            ;[2836] d1
                    ld        ($5c5d),de                    ;[2837] ed 53 5d 5c
                    call      $28ab                         ;[283b] cd ab 28
                    push      hl                            ;[283e] e5
                    cp        $29                           ;[283f] fe 29
                    jr        z,$2885                       ;[2841] 28 42
                    inc       hl                            ;[2843] 23
                    ld        a,(hl)                        ;[2844] 7e
                    cp        $0e                           ;[2845] fe 0e
                    ld        d,$40                         ;[2847] 16 40
                    jr        z,$2852                       ;[2849] 28 07
                    dec       hl                            ;[284b] 2b
                    call      $28ab                         ;[284c] cd ab 28
                    inc       hl                            ;[284f] 23
                    ld        d,$00                         ;[2850] 16 00
                    inc       hl                            ;[2852] 23
                    push      hl                            ;[2853] e5
                    push      de                            ;[2854] d5
                    call      $24fb                         ;[2855] cd fb 24
                    pop       af                            ;[2858] f1
                    xor       (iy+$01)                      ;[2859] fd ae 01
                    and       $40                           ;[285c] e6 40
                    jr        nz,$288b                      ;[285e] 20 2b
                    pop       hl                            ;[2860] e1
                    ex        de,hl                         ;[2861] eb
                    ld        hl,($5c65)                    ;[2862] 2a 65 5c
                    ld        bc,$0005                      ;[2865] 01 05 00
                    sbc       hl,bc                         ;[2868] ed 42
                    ld        ($5c65),hl                    ;[286a] 22 65 5c
                    ldir                                    ;[286d] ed b0
                    ex        de,hl                         ;[286f] eb
                    dec       hl                            ;[2870] 2b
                    call      $28ab                         ;[2871] cd ab 28
                    cp        $29                           ;[2874] fe 29
                    jr        z,$2885                       ;[2876] 28 0d
                    push      hl                            ;[2878] e5
                    rst       $18                           ;[2879] df
                    cp        $2c                           ;[287a] fe 2c
                    jr        nz,$288b                      ;[287c] 20 0d
                    rst       $20                           ;[287e] e7
                    pop       hl                            ;[287f] e1
                    call      $28ab                         ;[2880] cd ab 28
                    jr        $2843                         ;[2883] 18 be
                    push      hl                            ;[2885] e5
                    rst       $18                           ;[2886] df
                    cp        $29                           ;[2887] fe 29
                    jr        z,$288d                       ;[2889] 28 02
                    rst       $08                           ;[288b] cf
                    add       hl,de                         ;[288c] 19
                    pop       de                            ;[288d] d1
                    ex        de,hl                         ;[288e] eb
                    ld        ($5c5d),hl                    ;[288f] 22 5d 5c
                    ld        hl,($5c0b)                    ;[2892] 2a 0b 5c
                    ex        (sp),hl                       ;[2895] e3
                    ld        ($5c0b),hl                    ;[2896] 22 0b 5c
                    push      de                            ;[2899] d5
                    rst       $20                           ;[289a] e7
                    rst       $20                           ;[289b] e7
                    call      $24fb                         ;[289c] cd fb 24
                    pop       hl                            ;[289f] e1
                    ld        ($5c5d),hl                    ;[28a0] 22 5d 5c
                    pop       hl                            ;[28a3] e1
                    ld        ($5c0b),hl                    ;[28a4] 22 0b 5c
                    rst       $20                           ;[28a7] e7
                    jp        $2712                         ;[28a8] c3 12 27
                    inc       hl                            ;[28ab] 23
                    ld        a,(hl)                        ;[28ac] 7e
                    cp        $21                           ;[28ad] fe 21
                    jr        c,$28ab                       ;[28af] 38 fa
                    ret                                     ;[28b1] c9

                    set       6,(iy+$01)                    ;[28b2] fd cb 01 f6
                    rst       $18                           ;[28b6] df
                    call      $2c8d                         ;[28b7] cd 8d 2c
                    jp        nc,$1c8a                      ;[28ba] d2 8a 1c
                    push      hl                            ;[28bd] e5
                    and       $1f                           ;[28be] e6 1f
                    ld        c,a                           ;[28c0] 4f
                    rst       $20                           ;[28c1] e7
                    push      hl                            ;[28c2] e5
                    cp        $28                           ;[28c3] fe 28
                    jr        z,$28ef                       ;[28c5] 28 28
                    set       6,c                           ;[28c7] cb f1
                    cp        $24                           ;[28c9] fe 24
                    jr        z,$28de                       ;[28cb] 28 11
                    set       5,c                           ;[28cd] cb e9
                    call      $2c88                         ;[28cf] cd 88 2c
                    jr        nc,$28e3                      ;[28d2] 30 0f
                    call      $2c88                         ;[28d4] cd 88 2c
                    jr        nc,$28ef                      ;[28d7] 30 16
                    res       6,c                           ;[28d9] cb b1
                    rst       $20                           ;[28db] e7
                    jr        $28d4                         ;[28dc] 18 f6
                    rst       $20                           ;[28de] e7
                    res       6,(iy+$01)                    ;[28df] fd cb 01 b6
                    ld        a,($5c0c)                     ;[28e3] 3a 0c 5c
                    and       a                             ;[28e6] a7
                    jr        z,$28ef                       ;[28e7] 28 06
                    call      $2530                         ;[28e9] cd 30 25
                    jp        nz,$2951                      ;[28ec] c2 51 29
                    ld        b,c                           ;[28ef] 41
                    call      $2530                         ;[28f0] cd 30 25
                    jr        nz,$28fd                      ;[28f3] 20 08
                    ld        a,c                           ;[28f5] 79
                    and       $e0                           ;[28f6] e6 e0
                    set       7,a                           ;[28f8] cb ff
                    ld        c,a                           ;[28fa] 4f
                    jr        $2934                         ;[28fb] 18 37
                    ld        hl,($5c4b)                    ;[28fd] 2a 4b 5c
                    ld        a,(hl)                        ;[2900] 7e
                    and       $7f                           ;[2901] e6 7f
                    jr        z,$2932                       ;[2903] 28 2d
                    cp        c                             ;[2905] b9
                    jr        nz,$292a                      ;[2906] 20 22
                    rla                                     ;[2908] 17
                    add       a                             ;[2909] 87
                    jp        p,$293f                       ;[290a] f2 3f 29
                    jr        c,$293f                       ;[290d] 38 30
                    pop       de                            ;[290f] d1
                    push      de                            ;[2910] d5
                    push      hl                            ;[2911] e5
                    inc       hl                            ;[2912] 23
                    ld        a,(de)                        ;[2913] 1a
                    inc       de                            ;[2914] 13
                    cp        $20                           ;[2915] fe 20
                    jr        z,$2913                       ;[2917] 28 fa
                    or        $20                           ;[2919] f6 20
                    cp        (hl)                          ;[291b] be
                    jr        z,$2912                       ;[291c] 28 f4
                    or        $80                           ;[291e] f6 80
                    cp        (hl)                          ;[2920] be
                    jr        nz,$2929                      ;[2921] 20 06
                    ld        a,(de)                        ;[2923] 1a
                    call      $2c88                         ;[2924] cd 88 2c
                    jr        nc,$293e                      ;[2927] 30 15
                    pop       hl                            ;[2929] e1
                    push      bc                            ;[292a] c5
                    call      $19b8                         ;[292b] cd b8 19
                    ex        de,hl                         ;[292e] eb
                    pop       bc                            ;[292f] c1
                    jr        $2900                         ;[2930] 18 ce
                    set       7,b                           ;[2932] cb f8
                    pop       de                            ;[2934] d1
                    rst       $18                           ;[2935] df
                    cp        $28                           ;[2936] fe 28
                    jr        z,$2943                       ;[2938] 28 09
                    set       5,b                           ;[293a] cb e8
                    jr        $294b                         ;[293c] 18 0d
                    pop       de                            ;[293e] d1
                    pop       de                            ;[293f] d1
                    pop       de                            ;[2940] d1
                    push      hl                            ;[2941] e5
                    rst       $18                           ;[2942] df
                    call      $2c88                         ;[2943] cd 88 2c
                    jr        nc,$294b                      ;[2946] 30 03
                    rst       $20                           ;[2948] e7
                    jr        $2943                         ;[2949] 18 f8
                    pop       hl                            ;[294b] e1
                    rl        b                             ;[294c] cb 10
                    bit       6,b                           ;[294e] cb 70
                    ret                                     ;[2950] c9

                    ld        hl,($5c0b)                    ;[2951] 2a 0b 5c
                    ld        a,(hl)                        ;[2954] 7e
                    cp        $29                           ;[2955] fe 29
                    jp        z,$28ef                       ;[2957] ca ef 28
                    ld        a,(hl)                        ;[295a] 7e
                    or        $60                           ;[295b] f6 60
                    ld        b,a                           ;[295d] 47
                    inc       hl                            ;[295e] 23
                    ld        a,(hl)                        ;[295f] 7e
                    cp        $0e                           ;[2960] fe 0e
                    jr        z,$296b                       ;[2962] 28 07
                    dec       hl                            ;[2964] 2b
                    call      $28ab                         ;[2965] cd ab 28
                    inc       hl                            ;[2968] 23
                    res       5,b                           ;[2969] cb a8
                    ld        a,b                           ;[296b] 78
                    cp        c                             ;[296c] b9
                    jr        z,$2981                       ;[296d] 28 12
                    inc       hl                            ;[296f] 23
                    inc       hl                            ;[2970] 23
                    inc       hl                            ;[2971] 23
                    inc       hl                            ;[2972] 23
                    inc       hl                            ;[2973] 23
                    call      $28ab                         ;[2974] cd ab 28
                    cp        $29                           ;[2977] fe 29
                    jp        z,$28ef                       ;[2979] ca ef 28
                    call      $28ab                         ;[297c] cd ab 28
                    jr        $295a                         ;[297f] 18 d9
                    bit       5,c                           ;[2981] cb 69
                    jr        nz,$2991                      ;[2983] 20 0c
                    inc       hl                            ;[2985] 23
                    ld        de,($5c65)                    ;[2986] ed 5b 65 5c
                    call      $33c0                         ;[298a] cd c0 33
                    ex        de,hl                         ;[298d] eb
                    ld        ($5c65),hl                    ;[298e] 22 65 5c
                    pop       de                            ;[2991] d1
                    pop       de                            ;[2992] d1
                    xor       a                             ;[2993] af
                    inc       a                             ;[2994] 3c
                    ret                                     ;[2995] c9

                    xor       a                             ;[2996] af
                    ld        b,a                           ;[2997] 47
                    bit       7,c                           ;[2998] cb 79
                    jr        nz,$29e7                      ;[299a] 20 4b
                    bit       7,(hl)                        ;[299c] cb 7e
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
                    jp        $2a49                         ;[29ab] c3 49 2a
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
                    jr        nz,$2a20                      ;[29bd] 20 61
                    ex        de,hl                         ;[29bf] eb
                    ex        de,hl                         ;[29c0] eb
                    jr        $29e7                         ;[29c1] 18 24
                    push      hl                            ;[29c3] e5
                    rst       $18                           ;[29c4] df
                    pop       hl                            ;[29c5] e1
                    cp        $2c                           ;[29c6] fe 2c
                    jr        z,$29ea                       ;[29c8] 28 20
                    bit       7,c                           ;[29ca] cb 79
                    jr        z,$2a20                       ;[29cc] 28 52
                    bit       6,c                           ;[29ce] cb 71
                    jr        nz,$29d8                      ;[29d0] 20 06
                    cp        $29                           ;[29d2] fe 29
                    jr        nz,$2a12                      ;[29d4] 20 3c
                    rst       $20                           ;[29d6] e7
                    ret                                     ;[29d7] c9

                    cp        $29                           ;[29d8] fe 29
                    jr        z,$2a48                       ;[29da] 28 6c
                    cp        $cc                           ;[29dc] fe cc
                    jr        nz,$2a12                      ;[29de] 20 32
                    rst       $18                           ;[29e0] df
                    dec       hl                            ;[29e1] 2b
                    ld        ($5c5d),hl                    ;[29e2] 22 5d 5c
                    jr        $2a45                         ;[29e5] 18 5e
                    ld        hl,$0000                      ;[29e7] 21 00 00
                    push      hl                            ;[29ea] e5
                    rst       $20                           ;[29eb] e7
                    pop       hl                            ;[29ec] e1
                    ld        a,c                           ;[29ed] 79
                    cp        $c0                           ;[29ee] fe c0
                    jr        nz,$29fb                      ;[29f0] 20 09
                    rst       $18                           ;[29f2] df
                    cp        $29                           ;[29f3] fe 29
                    jr        z,$2a48                       ;[29f5] 28 51
                    cp        $cc                           ;[29f7] fe cc
                    jr        z,$29e0                       ;[29f9] 28 e5
                    push      bc                            ;[29fb] c5
                    push      hl                            ;[29fc] e5
                    call      $2aee                         ;[29fd] cd ee 2a
                    ex        (sp),hl                       ;[2a00] e3
                    ex        de,hl                         ;[2a01] eb
                    call      $2acc                         ;[2a02] cd cc 2a
                    jr        c,$2a20                       ;[2a05] 38 19
                    dec       bc                            ;[2a07] 0b
                    call      $2af4                         ;[2a08] cd f4 2a
                    add       hl,bc                         ;[2a0b] 09
                    pop       de                            ;[2a0c] d1
                    pop       bc                            ;[2a0d] c1
                    djnz      $29c3                         ;[2a0e] 10 b3
                    bit       7,c                           ;[2a10] cb 79
                    jr        nz,$2a7a                      ;[2a12] 20 66
                    push      hl                            ;[2a14] e5
                    bit       6,c                           ;[2a15] cb 71
                    jr        nz,$2a2c                      ;[2a17] 20 13
                    ld        b,d                           ;[2a19] 42
                    ld        c,e                           ;[2a1a] 4b
                    rst       $18                           ;[2a1b] df
                    cp        $29                           ;[2a1c] fe 29
                    jr        z,$2a22                       ;[2a1e] 28 02
                    rst       $08                           ;[2a20] cf
                    ld        (bc),a                        ;[2a21] 02
                    rst       $20                           ;[2a22] e7
                    pop       hl                            ;[2a23] e1
                    ld        de,$0005                      ;[2a24] 11 05 00
                    call      $2af4                         ;[2a27] cd f4 2a
                    add       hl,bc                         ;[2a2a] 09
                    ret                                     ;[2a2b] c9

                    call      $2aee                         ;[2a2c] cd ee 2a
                    ex        (sp),hl                       ;[2a2f] e3
                    call      $2af4                         ;[2a30] cd f4 2a
                    pop       bc                            ;[2a33] c1
                    add       hl,bc                         ;[2a34] 09
                    inc       hl                            ;[2a35] 23
                    ld        b,d                           ;[2a36] 42
                    ld        c,e                           ;[2a37] 4b
                    ex        de,hl                         ;[2a38] eb
                    call      $2ab1                         ;[2a39] cd b1 2a
                    rst       $18                           ;[2a3c] df
                    cp        $29                           ;[2a3d] fe 29
                    jr        z,$2a48                       ;[2a3f] 28 07
                    cp        $2c                           ;[2a41] fe 2c
                    jr        nz,$2a20                      ;[2a43] 20 db
                    call      $2a52                         ;[2a45] cd 52 2a
                    rst       $20                           ;[2a48] e7
                    cp        $28                           ;[2a49] fe 28
                    jr        z,$2a45                       ;[2a4b] 28 f8
                    res       6,(iy+$01)                    ;[2a4d] fd cb 01 b6
                    ret                                     ;[2a51] c9

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
                    jp        m,$2a20                       ;[2aa3] fa 20 2a
                    ld        b,h                           ;[2aa6] 44
                    ld        c,l                           ;[2aa7] 4d
                    pop       de                            ;[2aa8] d1
                    res       6,(iy+$01)                    ;[2aa9] fd cb 01 b6
                    call      $2530                         ;[2aad] cd 30 25
                    ret       z                             ;[2ab0] c8
                    xor       a                             ;[2ab1] af
                    res       6,(iy+$01)                    ;[2ab2] fd cb 01 b6
                    push      bc                            ;[2ab6] c5
                    call      $33a9                         ;[2ab7] cd a9 33
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
                    bit       1,(iy+$37)                    ;[2b02] fd cb 37 4e
                    jr        z,$2b66                       ;[2b06] 28 5e
                    ld        bc,$0005                      ;[2b08] 01 05 00
                    inc       bc                            ;[2b0b] 03
                    inc       hl                            ;[2b0c] 23
                    ld        a,(hl)                        ;[2b0d] 7e
                    cp        $20                           ;[2b0e] fe 20
                    jr        z,$2b0c                       ;[2b10] 28 fa
                    jr        nc,$2b1f                      ;[2b12] 30 0b
                    cp        $10                           ;[2b14] fe 10
                    jr        c,$2b29                       ;[2b16] 38 11
                    cp        $16                           ;[2b18] fe 16
                    jr        nc,$2b29                      ;[2b1a] 30 0d
                    inc       hl                            ;[2b1c] 23
                    jr        $2b0c                         ;[2b1d] 18 ed
                    call      $2c88                         ;[2b1f] cd 88 2c
                    jr        c,$2b0b                       ;[2b22] 38 e7
                    cp        $24                           ;[2b24] fe 24
                    jp        z,$2bc0                       ;[2b26] ca c0 2b
                    ld        a,c                           ;[2b29] 79
                    ld        hl,($5c59)                    ;[2b2a] 2a 59 5c
                    dec       hl                            ;[2b2d] 2b
                    call      $1655                         ;[2b2e] cd 55 16
                    inc       hl                            ;[2b31] 23
                    inc       hl                            ;[2b32] 23
                    ex        de,hl                         ;[2b33] eb
                    push      de                            ;[2b34] d5
                    ld        hl,($5c4d)                    ;[2b35] 2a 4d 5c
                    dec       de                            ;[2b38] 1b
                    sub       $06                           ;[2b39] d6 06
                    ld        b,a                           ;[2b3b] 47
                    jr        z,$2b4f                       ;[2b3c] 28 11
                    inc       hl                            ;[2b3e] 23
                    ld        a,(hl)                        ;[2b3f] 7e
                    cp        $21                           ;[2b40] fe 21
                    jr        c,$2b3e                       ;[2b42] 38 fa
                    or        $20                           ;[2b44] f6 20
                    inc       de                            ;[2b46] 13
                    ld        (de),a                        ;[2b47] 12
                    djnz      $2b3e                         ;[2b48] 10 f4
                    or        $80                           ;[2b4a] f6 80
                    ld        (de),a                        ;[2b4c] 12
                    ld        a,$c0                         ;[2b4d] 3e c0
                    ld        hl,($5c4d)                    ;[2b4f] 2a 4d 5c
                    xor       (hl)                          ;[2b52] ae
                    or        $20                           ;[2b53] f6 20
                    pop       hl                            ;[2b55] e1
                    call      $2bea                         ;[2b56] cd ea 2b
                    push      hl                            ;[2b59] e5
                    rst       $28                           ;[2b5a] ef
                    ld        (bc),a                        ;[2b5b] 02
                    jr        c,$2b3f                       ;[2b5c] 38 e1
                    ld        bc,$0005                      ;[2b5e] 01 05 00
                    and       a                             ;[2b61] a7
                    sbc       hl,bc                         ;[2b62] ed 42
                    jr        $2ba6                         ;[2b64] 18 40
                    bit       6,(iy+$01)                    ;[2b66] fd cb 01 76
                    jr        z,$2b72                       ;[2b6a] 28 06
                    ld        de,$0006                      ;[2b6c] 11 06 00
                    add       hl,de                         ;[2b6f] 19
                    jr        $2b59                         ;[2b70] 18 e7
                    ld        hl,($5c4d)                    ;[2b72] 2a 4d 5c
                    ld        bc,($5c72)                    ;[2b75] ed 4b 72 5c
                    bit       0,(iy+$37)                    ;[2b79] fd cb 37 46
                    jr        nz,$2baf                      ;[2b7d] 20 30
                    ld        a,b                           ;[2b7f] 78
                    or        c                             ;[2b80] b1
                    ret       z                             ;[2b81] c8
                    push      hl                            ;[2b82] e5
                    rst       $30                           ;[2b83] f7
                    push      de                            ;[2b84] d5
                    push      bc                            ;[2b85] c5
                    ld        d,h                           ;[2b86] 54
                    ld        e,l                           ;[2b87] 5d
                    inc       hl                            ;[2b88] 23
                    ld        (hl),$20                      ;[2b89] 36 20
                    lddr                                    ;[2b8b] ed b8
                    push      hl                            ;[2b8d] e5
                    call      $2bf1                         ;[2b8e] cd f1 2b
                    pop       hl                            ;[2b91] e1
                    ex        (sp),hl                       ;[2b92] e3
                    and       a                             ;[2b93] a7
                    sbc       hl,bc                         ;[2b94] ed 42
                    add       hl,bc                         ;[2b96] 09
                    jr        nc,$2b9b                      ;[2b97] 30 02
                    ld        b,h                           ;[2b99] 44
                    ld        c,l                           ;[2b9a] 4d
                    ex        (sp),hl                       ;[2b9b] e3
                    ex        de,hl                         ;[2b9c] eb
                    ld        a,b                           ;[2b9d] 78
                    or        c                             ;[2b9e] b1
                    jr        z,$2ba3                       ;[2b9f] 28 02
                    ldir                                    ;[2ba1] ed b0
                    pop       bc                            ;[2ba3] c1
                    pop       de                            ;[2ba4] d1
                    pop       hl                            ;[2ba5] e1
                    ex        de,hl                         ;[2ba6] eb
                    ld        a,b                           ;[2ba7] 78
                    or        c                             ;[2ba8] b1
                    ret       z                             ;[2ba9] c8
                    push      de                            ;[2baa] d5
                    ldir                                    ;[2bab] ed b0
                    pop       hl                            ;[2bad] e1
                    ret                                     ;[2bae] c9

                    dec       hl                            ;[2baf] 2b
                    dec       hl                            ;[2bb0] 2b
                    dec       hl                            ;[2bb1] 2b
                    ld        a,(hl)                        ;[2bb2] 7e
                    push      hl                            ;[2bb3] e5
                    push      bc                            ;[2bb4] c5
                    call      $2bc6                         ;[2bb5] cd c6 2b
                    pop       bc                            ;[2bb8] c1
                    pop       hl                            ;[2bb9] e1
                    inc       bc                            ;[2bba] 03
                    inc       bc                            ;[2bbb] 03
                    inc       bc                            ;[2bbc] 03
                    jp        $19e8                         ;[2bbd] c3 e8 19
                    ld        a,$df                         ;[2bc0] 3e df
                    ld        hl,($5c4d)                    ;[2bc2] 2a 4d 5c
                    and       (hl)                          ;[2bc5] a6
                    push      af                            ;[2bc6] f5
                    call      $2bf1                         ;[2bc7] cd f1 2b
                    ex        de,hl                         ;[2bca] eb
                    add       hl,bc                         ;[2bcb] 09
                    push      bc                            ;[2bcc] c5
                    dec       hl                            ;[2bcd] 2b
                    ld        ($5c4d),hl                    ;[2bce] 22 4d 5c
                    inc       bc                            ;[2bd1] 03
                    inc       bc                            ;[2bd2] 03
                    inc       bc                            ;[2bd3] 03
                    ld        hl,($5c59)                    ;[2bd4] 2a 59 5c
                    dec       hl                            ;[2bd7] 2b
                    call      $1655                         ;[2bd8] cd 55 16
                    ld        hl,($5c4d)                    ;[2bdb] 2a 4d 5c
                    pop       bc                            ;[2bde] c1
                    push      bc                            ;[2bdf] c5
                    inc       bc                            ;[2be0] 03
                    lddr                                    ;[2be1] ed b8
                    ex        de,hl                         ;[2be3] eb
                    inc       hl                            ;[2be4] 23
                    pop       bc                            ;[2be5] c1
                    ld        (hl),b                        ;[2be6] 70
                    dec       hl                            ;[2be7] 2b
                    ld        (hl),c                        ;[2be8] 71
                    pop       af                            ;[2be9] f1
                    dec       hl                            ;[2bea] 2b
                    ld        (hl),a                        ;[2beb] 77
                    ld        hl,($5c59)                    ;[2bec] 2a 59 5c
                    dec       hl                            ;[2bef] 2b
                    ret                                     ;[2bf0] c9

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

                    call      $28b2                         ;[2c02] cd b2 28
                    jp        nz,$1c8a                      ;[2c05] c2 8a 1c
                    call      $2530                         ;[2c08] cd 30 25
                    jr        nz,$2c15                      ;[2c0b] 20 08
                    res       6,c                           ;[2c0d] cb b1
                    call      $2996                         ;[2c0f] cd 96 29
                    call      $1bee                         ;[2c12] cd ee 1b
                    jr        c,$2c1f                       ;[2c15] 38 08
                    push      bc                            ;[2c17] c5
                    call      $19b8                         ;[2c18] cd b8 19
                    call      $19e8                         ;[2c1b] cd e8 19
                    pop       bc                            ;[2c1e] c1
                    set       7,c                           ;[2c1f] cb f9
                    ld        b,$00                         ;[2c21] 06 00
                    push      bc                            ;[2c23] c5
                    ld        hl,$0001                      ;[2c24] 21 01 00
                    bit       6,c                           ;[2c27] cb 71
                    jr        nz,$2c2d                      ;[2c29] 20 02
                    ld        l,$05                         ;[2c2b] 2e 05
                    ex        de,hl                         ;[2c2d] eb
                    rst       $20                           ;[2c2e] e7
                    ld        h,$ff                         ;[2c2f] 26 ff
                    call      $2acc                         ;[2c31] cd cc 2a
                    jp        c,$2a20                       ;[2c34] da 20 2a
                    pop       hl                            ;[2c37] e1
                    push      bc                            ;[2c38] c5
                    inc       h                             ;[2c39] 24
                    push      hl                            ;[2c3a] e5
                    ld        h,b                           ;[2c3b] 60
                    ld        l,c                           ;[2c3c] 69
                    call      $2af4                         ;[2c3d] cd f4 2a
                    ex        de,hl                         ;[2c40] eb
                    rst       $18                           ;[2c41] df
                    cp        $2c                           ;[2c42] fe 2c
                    jr        z,$2c2e                       ;[2c44] 28 e8
                    cp        $29                           ;[2c46] fe 29
                    jr        nz,$2c05                      ;[2c48] 20 bb
                    rst       $20                           ;[2c4a] e7
                    pop       bc                            ;[2c4b] c1
                    ld        a,c                           ;[2c4c] 79
                    ld        l,b                           ;[2c4d] 68
                    ld        h,$00                         ;[2c4e] 26 00
                    inc       hl                            ;[2c50] 23
                    inc       hl                            ;[2c51] 23
                    add       hl,hl                         ;[2c52] 29
                    add       hl,de                         ;[2c53] 19
                    jp        c,$1f15                       ;[2c54] da 15 1f
                    push      de                            ;[2c57] d5
                    push      bc                            ;[2c58] c5
                    push      hl                            ;[2c59] e5
                    ld        b,h                           ;[2c5a] 44
                    ld        c,l                           ;[2c5b] 4d
                    ld        hl,($5c59)                    ;[2c5c] 2a 59 5c
                    dec       hl                            ;[2c5f] 2b
                    call      $1655                         ;[2c60] cd 55 16
                    inc       hl                            ;[2c63] 23
                    ld        (hl),a                        ;[2c64] 77
                    pop       bc                            ;[2c65] c1
                    dec       bc                            ;[2c66] 0b
                    dec       bc                            ;[2c67] 0b
                    dec       bc                            ;[2c68] 0b
                    inc       hl                            ;[2c69] 23
                    ld        (hl),c                        ;[2c6a] 71
                    inc       hl                            ;[2c6b] 23
                    ld        (hl),b                        ;[2c6c] 70
                    pop       bc                            ;[2c6d] c1
                    ld        a,b                           ;[2c6e] 78
                    inc       hl                            ;[2c6f] 23
                    ld        (hl),a                        ;[2c70] 77
                    ld        h,d                           ;[2c71] 62
                    ld        l,e                           ;[2c72] 6b
                    dec       de                            ;[2c73] 1b
                    ld        (hl),$00                      ;[2c74] 36 00
                    bit       6,c                           ;[2c76] cb 71
                    jr        z,$2c7c                       ;[2c78] 28 02
                    ld        (hl),$20                      ;[2c7a] 36 20
                    pop       bc                            ;[2c7c] c1
                    lddr                                    ;[2c7d] ed b8
                    pop       bc                            ;[2c7f] c1
                    ld        (hl),b                        ;[2c80] 70
                    dec       hl                            ;[2c81] 2b
                    ld        (hl),c                        ;[2c82] 71
                    dec       hl                            ;[2c83] 2b
                    dec       a                             ;[2c84] 3d
                    jr        nz,$2c7f                      ;[2c85] 20 f8
                    ret                                     ;[2c87] c9

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

                    cp        $c4                           ;[2c9b] fe c4
                    jr        nz,$2cb8                      ;[2c9d] 20 19
                    ld        de,$0000                      ;[2c9f] 11 00 00
                    rst       $20                           ;[2ca2] e7
                    sub       $31                           ;[2ca3] d6 31
                    adc       $00                           ;[2ca5] ce 00
                    jr        nz,$2cb3                      ;[2ca7] 20 0a
                    ex        de,hl                         ;[2ca9] eb
                    ccf                                     ;[2caa] 3f
                    adc       hl,hl                         ;[2cab] ed 6a
                    jp        c,$31ad                       ;[2cad] da ad 31
                    ex        de,hl                         ;[2cb0] eb
                    jr        $2ca2                         ;[2cb1] 18 ef
                    ld        b,d                           ;[2cb3] 42
                    ld        c,e                           ;[2cb4] 4b
                    jp        $2d2b                         ;[2cb5] c3 2b 2d
                    cp        $2e                           ;[2cb8] fe 2e
                    jr        z,$2ccb                       ;[2cba] 28 0f
                    call      $2d3b                         ;[2cbc] cd 3b 2d
                    cp        $2e                           ;[2cbf] fe 2e
                    jr        nz,$2ceb                      ;[2cc1] 20 28
                    rst       $20                           ;[2cc3] e7
                    call      $2d1b                         ;[2cc4] cd 1b 2d
                    jr        c,$2ceb                       ;[2cc7] 38 22
                    jr        $2cd5                         ;[2cc9] 18 0a
                    rst       $20                           ;[2ccb] e7
                    call      $2d1b                         ;[2ccc] cd 1b 2d
                    jp        c,$1c8a                       ;[2ccf] da 8a 1c
                    rst       $28                           ;[2cd2] ef
                    and       b                             ;[2cd3] a0
                    jr        c,$2cc5                       ;[2cd4] 38 ef
                    and       c                             ;[2cd6] a1
                    ret       nz                            ;[2cd7] c0
                    ld        (bc),a                        ;[2cd8] 02
                    jr        c,$2cba                       ;[2cd9] 38 df
                    call      $2d22                         ;[2cdb] cd 22 2d
                    jr        c,$2ceb                       ;[2cde] 38 0b
                    rst       $28                           ;[2ce0] ef
                    ret       po                            ;[2ce1] e0
                    and       h                             ;[2ce2] a4
                    dec       b                             ;[2ce3] 05
                    ret       nz                            ;[2ce4] c0
                    inc       b                             ;[2ce5] 04
                    rrca                                    ;[2ce6] 0f
                    jr        c,$2cd0                       ;[2ce7] 38 e7
                    jr        $2cda                         ;[2ce9] 18 ef
                    cp        $45                           ;[2ceb] fe 45
                    jr        z,$2cf2                       ;[2ced] 28 03
                    cp        $65                           ;[2cef] fe 65
                    ret       nz                            ;[2cf1] c0
                    ld        b,$ff                         ;[2cf2] 06 ff
                    rst       $20                           ;[2cf4] e7
                    cp        $2b                           ;[2cf5] fe 2b
                    jr        z,$2cfe                       ;[2cf7] 28 05
                    cp        $2d                           ;[2cf9] fe 2d
                    jr        nz,$2cff                      ;[2cfb] 20 02
                    inc       b                             ;[2cfd] 04
                    rst       $20                           ;[2cfe] e7
                    call      $2d1b                         ;[2cff] cd 1b 2d
                    jr        c,$2ccf                       ;[2d02] 38 cb
                    push      bc                            ;[2d04] c5
                    call      $2d3b                         ;[2d05] cd 3b 2d
                    call      $2dd5                         ;[2d08] cd d5 2d
                    pop       bc                            ;[2d0b] c1
                    jp        c,$31ad                       ;[2d0c] da ad 31
                    and       a                             ;[2d0f] a7
                    jp        m,$31ad                       ;[2d10] fa ad 31
                    inc       b                             ;[2d13] 04
                    jr        z,$2d18                       ;[2d14] 28 02
                    neg                                     ;[2d16] ed 44
                    jp        $2d4f                         ;[2d18] c3 4f 2d
                    cp        $30                           ;[2d1b] fe 30
                    ret       c                             ;[2d1d] d8
                    cp        $3a                           ;[2d1e] fe 3a
                    ccf                                     ;[2d20] 3f
                    ret                                     ;[2d21] c9

                    call      $2d1b                         ;[2d22] cd 1b 2d
                    ret       c                             ;[2d25] d8
                    sub       $30                           ;[2d26] d6 30
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

                    push      af                            ;[2d3b] f5
                    rst       $28                           ;[2d3c] ef
                    and       b                             ;[2d3d] a0
                    jr        c,$2d31                       ;[2d3e] 38 f1
                    call      $2d22                         ;[2d40] cd 22 2d
                    ret       c                             ;[2d43] d8
                    rst       $28                           ;[2d44] ef
                    ld        bc,$04a4                      ;[2d45] 01 a4 04
                    rrca                                    ;[2d48] 0f
                    jr        c,$2d18                       ;[2d49] 38 cd
                    ld        (hl),h                        ;[2d4b] 74
                    nop                                     ;[2d4c] 00
                    jr        $2d40                         ;[2d4d] 18 f1
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
                    jp        po,$7e38                      ;[2e25] e2 38 7e
                    sub       $7e                           ;[2e28] d6 7e
                    call      $2dc1                         ;[2e2a] cd c1 2d
                    ld        d,a                           ;[2e2d] 57
                    ld        a,($5cac)                     ;[2e2e] 3a ac 5c
                    sub       d                             ;[2e31] 92
                    ld        ($5cac),a                     ;[2e32] 32 ac 5c
                    ld        a,d                           ;[2e35] 7a
                    call      $2d4f                         ;[2e36] cd 4f 2d
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
                    ld        a,c                           ;[2f01] 79
                    ld        c,(iy+$71)                    ;[2f02] fd 4e 71
                    add       hl,bc                         ;[2f05] 09
                    ld        (hl),a                        ;[2f06] 77
                    inc       (iy+$71)                      ;[2f07] fd 34 71
                    jr        $2edf                         ;[2f0a] 18 d3
                    push      af                            ;[2f0c] f5
                    ld        hl,$5ca1                      ;[2f0d] 21 a1 5c
                    ld        c,(iy+$71)                    ;[2f10] fd 4e 71
                    ld        b,$00                         ;[2f13] 06 00
                    add       hl,bc                         ;[2f15] 09
                    ld        b,c                           ;[2f16] 41
                    pop       af                            ;[2f17] f1
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
                    add       hl,bc                         ;[3029] 09
                    ex        de,hl                         ;[302a] eb
                    adc       (hl)                          ;[302b] 8e
                    rrca                                    ;[302c] 0f
                    adc       $00                           ;[302d] ce 00
                    jr        nz,$303c                      ;[302f] 20 0b
                    sbc       a                             ;[3031] 9f
                    ld        (hl),a                        ;[3032] 77
                    inc       hl                            ;[3033] 23
                    ld        (hl),e                        ;[3034] 73
                    inc       hl                            ;[3035] 23
                    ld        (hl),d                        ;[3036] 72
                    dec       hl                            ;[3037] 2b
                    dec       hl                            ;[3038] 2b
                    dec       hl                            ;[3039] 2b
                    pop       de                            ;[303a] d1
                    ret                                     ;[303b] c9

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
                    call      $30a9                         ;[30de] cd a9 30
                    ex        de,hl                         ;[30e1] eb
                    pop       hl                            ;[30e2] e1
                    jr        c,$30ef                       ;[30e3] 38 0a
                    ld        a,d                           ;[30e5] 7a
                    or        e                             ;[30e6] b3
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
                    jr        z,$31e2                       ;[31ff] 28 e1
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
                    jr        nz,$323f                      ;[3223] 20 1a
                    inc       hl                            ;[3225] 23
                    inc       hl                            ;[3226] 23
                    inc       hl                            ;[3227] 23
                    ld        a,$80                         ;[3228] 3e 80
                    and       (hl)                          ;[322a] a6
                    dec       hl                            ;[322b] 2b
                    or        (hl)                          ;[322c] b6
                    dec       hl                            ;[322d] 2b
                    jr        nz,$3233                      ;[322e] 20 03
                    ld        a,$80                         ;[3230] 3e 80
                    xor       (hl)                          ;[3232] ae
                    dec       hl                            ;[3233] 2b
                    jr        nz,$326c                      ;[3234] 20 36
                    ld        (hl),a                        ;[3236] 77
                    inc       hl                            ;[3237] 23
                    ld        (hl),$ff                      ;[3238] 36 ff
                    dec       hl                            ;[323a] 2b
                    ld        a,$18                         ;[323b] 3e 18
                    jr        $3272                         ;[323d] 18 33
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

                    nop                                     ;[32c5] 00
                    or        b                             ;[32c6] b0
                    nop                                     ;[32c7] 00
                    ld        b,b                           ;[32c8] 40
                    or        b                             ;[32c9] b0
                    nop                                     ;[32ca] 00
                    ld        bc,$0030                      ;[32cb] 01 30 00
                    pop       af                            ;[32ce] f1
                    ld        c,c                           ;[32cf] 49
                    rrca                                    ;[32d0] 0f
                    jp        c,$40a2                       ;[32d1] da a2 40
                    or        b                             ;[32d4] b0
                    nop                                     ;[32d5] 00
                    ld        a,(bc)                        ;[32d6] 0a
                    adc       a                             ;[32d7] 8f
                    ld        (hl),$3c                      ;[32d8] 36 3c
                    inc       (hl)                          ;[32da] 34
                    and       c                             ;[32db] a1
                    inc       sp                            ;[32dc] 33
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
                    ld        bc,$c035                      ;[3337] 01 35 c0
                    inc       sp                            ;[333a] 33
                    and       b                             ;[333b] a0
                    ld        (hl),$86                      ;[333c] 36 86
                    ld        (hl),$c6                      ;[333e] 36 c6
                    inc       sp                            ;[3340] 33
                    ld        a,d                           ;[3341] 7a
                    ld        (hl),$06                      ;[3342] 36 06
                    dec       (hl)                          ;[3344] 35
                    ld        sp,hl                         ;[3345] f9
                    inc       (hl)                          ;[3346] 34
                    sbc       e                             ;[3347] 9b
                    ld        (hl),$83                      ;[3348] 36 83
                    scf                                     ;[334a] 37
                    inc       d                             ;[334b] 14
                    ld        ($33a2),a                     ;[334c] 32 a2 33
                    ld        c,a                           ;[334f] 4f
                    dec       l                             ;[3350] 2d
                    sub       a                             ;[3351] 97
                    ld        ($3449),a                     ;[3352] 32 49 34
                    dec       de                            ;[3355] 1b
                    inc       (hl)                          ;[3356] 34
                    dec       l                             ;[3357] 2d
                    inc       (hl)                          ;[3358] 34
                    rrca                                    ;[3359] 0f
                    inc       (hl)                          ;[335a] 34
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
                    rrca                                    ;[3374] 0f
                    rrca                                    ;[3375] 0f
                    rrca                                    ;[3376] 0f
                    rrca                                    ;[3377] 0f
                    add       $7c                           ;[3378] c6 7c
                    ld        l,a                           ;[337a] 6f
                    ld        a,d                           ;[337b] 7a
                    and       $1f                           ;[337c] e6 1f
                    jr        $338e                         ;[337e] 18 0e
                    cp        $18                           ;[3380] fe 18
                    jr        nc,$338c                      ;[3382] 30 08
                    exx                                     ;[3384] d9
                    ld        bc,$fffb                      ;[3385] 01 fb ff
                    ld        d,h                           ;[3388] 54
                    ld        e,l                           ;[3389] 5d
                    add       hl,bc                         ;[338a] 09
                    exx                                     ;[338b] d9
                    rlca                                    ;[338c] 07
                    ld        l,a                           ;[338d] 6f
                    ld        de,$32d7                      ;[338e] 11 d7 32
                    ld        h,$00                         ;[3391] 26 00
                    add       hl,de                         ;[3393] 19
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
                    call      $33c0                         ;[33b8] cd c0 33
                    ld        ($5c65),de                    ;[33bb] ed 53 65 5c
                    ret                                     ;[33bf] c9

                    call      $33a9                         ;[33c0] cd a9 33
                    ldir                                    ;[33c3] ed b0
                    ret                                     ;[33c5] c9

                    ld        h,d                           ;[33c6] 62
                    ld        l,e                           ;[33c7] 6b
                    call      $33a9                         ;[33c8] cd a9 33
                    exx                                     ;[33cb] d9
                    push      hl                            ;[33cc] e5
                    exx                                     ;[33cd] d9
                    ex        (sp),hl                       ;[33ce] e3
                    push      bc                            ;[33cf] c5
                    ld        a,(hl)                        ;[33d0] 7e
                    and       $c0                           ;[33d1] e6 c0
                    rlca                                    ;[33d3] 07
                    rlca                                    ;[33d4] 07
                    ld        c,a                           ;[33d5] 4f
                    inc       c                             ;[33d6] 0c
                    ld        a,(hl)                        ;[33d7] 7e
                    and       $3f                           ;[33d8] e6 3f
                    jr        nz,$33de                      ;[33da] 20 02
                    inc       hl                            ;[33dc] 23
                    ld        a,(hl)                        ;[33dd] 7e
                    add       $50                           ;[33de] c6 50
                    ld        (de),a                        ;[33e0] 12
                    ld        a,$05                         ;[33e1] 3e 05
                    sub       c                             ;[33e3] 91
                    inc       hl                            ;[33e4] 23
                    inc       de                            ;[33e5] 13
                    ld        b,$00                         ;[33e6] 06 00
                    ldir                                    ;[33e8] ed b0
                    pop       bc                            ;[33ea] c1
                    ex        (sp),hl                       ;[33eb] e3
                    exx                                     ;[33ec] d9
                    pop       hl                            ;[33ed] e1
                    exx                                     ;[33ee] d9
                    ld        b,a                           ;[33ef] 47
                    xor       a                             ;[33f0] af
                    dec       b                             ;[33f1] 05
                    ret       z                             ;[33f2] c8
                    ld        (de),a                        ;[33f3] 12
                    inc       de                            ;[33f4] 13
                    jr        $33f1                         ;[33f5] 18 fa
                    and       a                             ;[33f7] a7
                    ret       z                             ;[33f8] c8
                    push      af                            ;[33f9] f5
                    push      de                            ;[33fa] d5
                    ld        de,$0000                      ;[33fb] 11 00 00
                    call      $33c8                         ;[33fe] cd c8 33
                    pop       de                            ;[3401] d1
                    pop       af                            ;[3402] f1
                    dec       a                             ;[3403] 3d
                    jr        $33f8                         ;[3404] 18 f2
                    ld        c,a                           ;[3406] 4f
                    rlca                                    ;[3407] 07
                    rlca                                    ;[3408] 07
                    add       c                             ;[3409] 81
                    ld        c,a                           ;[340a] 4f
                    ld        b,$00                         ;[340b] 06 00
                    add       hl,bc                         ;[340d] 09
                    ret                                     ;[340e] c9

                    push      de                            ;[340f] d5
                    ld        hl,($5c68)                    ;[3410] 2a 68 5c
                    call      $3406                         ;[3413] cd 06 34
                    call      $33c0                         ;[3416] cd c0 33
                    pop       hl                            ;[3419] e1
                    ret                                     ;[341a] c9

                    ld        h,d                           ;[341b] 62
                    ld        l,e                           ;[341c] 6b
                    exx                                     ;[341d] d9
                    push      hl                            ;[341e] e5
                    ld        hl,$32c5                      ;[341f] 21 c5 32
                    exx                                     ;[3422] d9
                    call      $33f7                         ;[3423] cd f7 33
                    call      $33c8                         ;[3426] cd c8 33
                    exx                                     ;[3429] d9
                    pop       hl                            ;[342a] e1
                    exx                                     ;[342b] d9
                    ret                                     ;[342c] c9

                    push      hl                            ;[342d] e5
                    ex        de,hl                         ;[342e] eb
                    ld        hl,($5c68)                    ;[342f] 2a 68 5c
                    call      $3406                         ;[3432] cd 06 34
                    ex        de,hl                         ;[3435] eb
                    call      $33c0                         ;[3436] cd c0 33
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
                    call      $1e99                         ;[34b3] cd 99 1e
                    ld        hl,$2d2b                      ;[34b6] 21 2b 2d
                    push      hl                            ;[34b9] e5
                    push      bc                            ;[34ba] c5
                    ret                                     ;[34bb] c9

                    call      $2bf1                         ;[34bc] cd f1 2b
                    dec       bc                            ;[34bf] 0b
                    ld        a,b                           ;[34c0] 78
                    or        c                             ;[34c1] b1
                    jr        nz,$34e7                      ;[34c2] 20 23
                    ld        a,(de)                        ;[34c4] 1a
                    call      $2c8d                         ;[34c5] cd 8d 2c
                    jr        c,$34d3                       ;[34c8] 38 09
                    sub       $90                           ;[34ca] d6 90
                    jr        c,$34e7                       ;[34cc] 38 19
                    cp        $15                           ;[34ce] fe 15
                    jr        nc,$34e7                      ;[34d0] 30 15
                    inc       a                             ;[34d2] 3c
                    dec       a                             ;[34d3] 3d
                    add       a                             ;[34d4] 87
                    add       a                             ;[34d5] 87
                    add       a                             ;[34d6] 87
                    cp        $a8                           ;[34d7] fe a8
                    jr        nc,$34e7                      ;[34d9] 30 0c
                    ld        bc,($5c7b)                    ;[34db] ed 4b 7b 5c
                    add       c                             ;[34df] 81
                    ld        c,a                           ;[34e0] 4f
                    jr        nc,$34e4                      ;[34e1] 30 01
                    inc       b                             ;[34e3] 04
                    jp        $2d2b                         ;[34e4] c3 2b 2d
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
                    ld        de,$fffb                      ;[35c2] 11 fb ff
                    push      hl                            ;[35c5] e5
                    add       hl,de                         ;[35c6] 19
                    pop       de                            ;[35c7] d1
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
                    ld        bc,$0001                      ;[361f] 01 01 00
                    rst       $30                           ;[3622] f7
                    ld        ($5c5b),hl                    ;[3623] 22 5b 5c
                    push      hl                            ;[3626] e5
                    ld        hl,($5c51)                    ;[3627] 2a 51 5c
                    push      hl                            ;[362a] e5
                    ld        a,$ff                         ;[362b] 3e ff
                    call      $1601                         ;[362d] cd 01 16
                    call      $2de3                         ;[3630] cd e3 2d
                    pop       hl                            ;[3633] e1
                    call      $1615                         ;[3634] cd 15 16
                    pop       de                            ;[3637] d1
                    ld        hl,($5c5b)                    ;[3638] 2a 5b 5c
                    and       a                             ;[363b] a7
                    sbc       hl,de                         ;[363c] ed 52
                    ld        b,h                           ;[363e] 44
                    ld        c,l                           ;[363f] 4d
                    call      $2ab2                         ;[3640] cd b2 2a
                    ex        de,hl                         ;[3643] eb
                    ret                                     ;[3644] c9

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
                    jp        $2d2b                         ;[3677] c3 2b 2d
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
                    rst       $38                           ;[386e] ff
                    rst       $38                           ;[386f] ff
                    rst       $38                           ;[3870] ff
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
                    rst       $38                           ;[387e] ff
                    rst       $38                           ;[387f] ff
                    rst       $38                           ;[3880] ff
                    rst       $38                           ;[3881] ff
                    rst       $38                           ;[3882] ff
                    rst       $38                           ;[3883] ff
                    rst       $38                           ;[3884] ff
                    rst       $38                           ;[3885] ff
                    rst       $38                           ;[3886] ff
                    rst       $38                           ;[3887] ff
                    rst       $38                           ;[3888] ff
                    rst       $38                           ;[3889] ff
                    rst       $38                           ;[388a] ff
                    rst       $38                           ;[388b] ff
                    rst       $38                           ;[388c] ff
                    rst       $38                           ;[388d] ff
                    rst       $38                           ;[388e] ff
                    rst       $38                           ;[388f] ff
                    rst       $38                           ;[3890] ff
                    rst       $38                           ;[3891] ff
                    rst       $38                           ;[3892] ff
                    rst       $38                           ;[3893] ff
                    rst       $38                           ;[3894] ff
                    rst       $38                           ;[3895] ff
                    rst       $38                           ;[3896] ff
                    rst       $38                           ;[3897] ff
                    rst       $38                           ;[3898] ff
                    rst       $38                           ;[3899] ff
                    rst       $38                           ;[389a] ff
                    rst       $38                           ;[389b] ff
                    rst       $38                           ;[389c] ff
                    rst       $38                           ;[389d] ff
                    rst       $38                           ;[389e] ff
                    rst       $38                           ;[389f] ff
                    rst       $38                           ;[38a0] ff
                    rst       $38                           ;[38a1] ff
                    rst       $38                           ;[38a2] ff
                    rst       $38                           ;[38a3] ff
                    rst       $38                           ;[38a4] ff
                    rst       $38                           ;[38a5] ff
                    rst       $38                           ;[38a6] ff
                    rst       $38                           ;[38a7] ff
                    rst       $38                           ;[38a8] ff
                    rst       $38                           ;[38a9] ff
                    rst       $38                           ;[38aa] ff
                    rst       $38                           ;[38ab] ff
                    rst       $38                           ;[38ac] ff
                    rst       $38                           ;[38ad] ff
                    rst       $38                           ;[38ae] ff
                    rst       $38                           ;[38af] ff
                    rst       $38                           ;[38b0] ff
                    rst       $38                           ;[38b1] ff
                    rst       $38                           ;[38b2] ff
                    rst       $38                           ;[38b3] ff
                    rst       $38                           ;[38b4] ff
                    rst       $38                           ;[38b5] ff
                    rst       $38                           ;[38b6] ff
                    rst       $38                           ;[38b7] ff
                    rst       $38                           ;[38b8] ff
                    rst       $38                           ;[38b9] ff
                    rst       $38                           ;[38ba] ff
                    rst       $38                           ;[38bb] ff
                    rst       $38                           ;[38bc] ff
                    rst       $38                           ;[38bd] ff
                    rst       $38                           ;[38be] ff
                    rst       $38                           ;[38bf] ff
                    rst       $38                           ;[38c0] ff
                    rst       $38                           ;[38c1] ff
                    rst       $38                           ;[38c2] ff
                    rst       $38                           ;[38c3] ff
                    rst       $38                           ;[38c4] ff
                    rst       $38                           ;[38c5] ff
                    rst       $38                           ;[38c6] ff
                    rst       $38                           ;[38c7] ff
                    rst       $38                           ;[38c8] ff
                    rst       $38                           ;[38c9] ff
                    rst       $38                           ;[38ca] ff
                    rst       $38                           ;[38cb] ff
                    rst       $38                           ;[38cc] ff
                    rst       $38                           ;[38cd] ff
                    rst       $38                           ;[38ce] ff
                    rst       $38                           ;[38cf] ff
                    rst       $38                           ;[38d0] ff
                    rst       $38                           ;[38d1] ff
                    rst       $38                           ;[38d2] ff
                    rst       $38                           ;[38d3] ff
                    rst       $38                           ;[38d4] ff
                    rst       $38                           ;[38d5] ff
                    rst       $38                           ;[38d6] ff
                    rst       $38                           ;[38d7] ff
                    rst       $38                           ;[38d8] ff
                    rst       $38                           ;[38d9] ff
                    rst       $38                           ;[38da] ff
                    rst       $38                           ;[38db] ff
                    rst       $38                           ;[38dc] ff
                    rst       $38                           ;[38dd] ff
                    rst       $38                           ;[38de] ff
                    rst       $38                           ;[38df] ff
                    rst       $38                           ;[38e0] ff
                    rst       $38                           ;[38e1] ff
                    rst       $38                           ;[38e2] ff
                    rst       $38                           ;[38e3] ff
                    rst       $38                           ;[38e4] ff
                    rst       $38                           ;[38e5] ff
                    rst       $38                           ;[38e6] ff
                    rst       $38                           ;[38e7] ff
                    rst       $38                           ;[38e8] ff
                    rst       $38                           ;[38e9] ff
                    rst       $38                           ;[38ea] ff
                    rst       $38                           ;[38eb] ff
                    rst       $38                           ;[38ec] ff
                    rst       $38                           ;[38ed] ff
                    rst       $38                           ;[38ee] ff
                    rst       $38                           ;[38ef] ff
                    rst       $38                           ;[38f0] ff
                    rst       $38                           ;[38f1] ff
                    rst       $38                           ;[38f2] ff
                    rst       $38                           ;[38f3] ff
                    rst       $38                           ;[38f4] ff
                    rst       $38                           ;[38f5] ff
                    rst       $38                           ;[38f6] ff
                    rst       $38                           ;[38f7] ff
                    rst       $38                           ;[38f8] ff
                    rst       $38                           ;[38f9] ff
                    rst       $38                           ;[38fa] ff
                    rst       $38                           ;[38fb] ff
                    rst       $38                           ;[38fc] ff
                    rst       $38                           ;[38fd] ff
                    rst       $38                           ;[38fe] ff
                    rst       $38                           ;[38ff] ff
                    rst       $38                           ;[3900] ff
                    rst       $38                           ;[3901] ff
                    rst       $38                           ;[3902] ff
                    rst       $38                           ;[3903] ff
                    rst       $38                           ;[3904] ff
                    rst       $38                           ;[3905] ff
                    rst       $38                           ;[3906] ff
                    rst       $38                           ;[3907] ff
                    rst       $38                           ;[3908] ff
                    rst       $38                           ;[3909] ff
                    rst       $38                           ;[390a] ff
                    rst       $38                           ;[390b] ff
                    rst       $38                           ;[390c] ff
                    rst       $38                           ;[390d] ff
                    rst       $38                           ;[390e] ff
                    rst       $38                           ;[390f] ff
                    rst       $38                           ;[3910] ff
                    rst       $38                           ;[3911] ff
                    rst       $38                           ;[3912] ff
                    rst       $38                           ;[3913] ff
                    rst       $38                           ;[3914] ff
                    rst       $38                           ;[3915] ff
                    rst       $38                           ;[3916] ff
                    rst       $38                           ;[3917] ff
                    rst       $38                           ;[3918] ff
                    rst       $38                           ;[3919] ff
                    rst       $38                           ;[391a] ff
                    rst       $38                           ;[391b] ff
                    rst       $38                           ;[391c] ff
                    rst       $38                           ;[391d] ff
                    rst       $38                           ;[391e] ff
                    rst       $38                           ;[391f] ff
                    rst       $38                           ;[3920] ff
                    rst       $38                           ;[3921] ff
                    rst       $38                           ;[3922] ff
                    rst       $38                           ;[3923] ff
                    rst       $38                           ;[3924] ff
                    rst       $38                           ;[3925] ff
                    rst       $38                           ;[3926] ff
                    rst       $38                           ;[3927] ff
                    rst       $38                           ;[3928] ff
                    rst       $38                           ;[3929] ff
                    rst       $38                           ;[392a] ff
                    rst       $38                           ;[392b] ff
                    rst       $38                           ;[392c] ff
                    rst       $38                           ;[392d] ff
                    rst       $38                           ;[392e] ff
                    rst       $38                           ;[392f] ff
                    rst       $38                           ;[3930] ff
                    rst       $38                           ;[3931] ff
                    rst       $38                           ;[3932] ff
                    rst       $38                           ;[3933] ff
                    rst       $38                           ;[3934] ff
                    rst       $38                           ;[3935] ff
                    rst       $38                           ;[3936] ff
                    rst       $38                           ;[3937] ff
                    rst       $38                           ;[3938] ff
                    rst       $38                           ;[3939] ff
                    rst       $38                           ;[393a] ff
                    rst       $38                           ;[393b] ff
                    rst       $38                           ;[393c] ff
                    rst       $38                           ;[393d] ff
                    rst       $38                           ;[393e] ff
                    rst       $38                           ;[393f] ff
                    rst       $38                           ;[3940] ff
                    rst       $38                           ;[3941] ff
                    rst       $38                           ;[3942] ff
                    rst       $38                           ;[3943] ff
                    rst       $38                           ;[3944] ff
                    rst       $38                           ;[3945] ff
                    rst       $38                           ;[3946] ff
                    rst       $38                           ;[3947] ff
                    rst       $38                           ;[3948] ff
                    rst       $38                           ;[3949] ff
                    rst       $38                           ;[394a] ff
                    rst       $38                           ;[394b] ff
                    rst       $38                           ;[394c] ff
                    rst       $38                           ;[394d] ff
                    rst       $38                           ;[394e] ff
                    rst       $38                           ;[394f] ff
                    rst       $38                           ;[3950] ff
                    rst       $38                           ;[3951] ff
                    rst       $38                           ;[3952] ff
                    rst       $38                           ;[3953] ff
                    rst       $38                           ;[3954] ff
                    rst       $38                           ;[3955] ff
                    rst       $38                           ;[3956] ff
                    rst       $38                           ;[3957] ff
                    rst       $38                           ;[3958] ff
                    rst       $38                           ;[3959] ff
                    rst       $38                           ;[395a] ff
                    rst       $38                           ;[395b] ff
                    rst       $38                           ;[395c] ff
                    rst       $38                           ;[395d] ff
                    rst       $38                           ;[395e] ff
                    rst       $38                           ;[395f] ff
                    rst       $38                           ;[3960] ff
                    rst       $38                           ;[3961] ff
                    rst       $38                           ;[3962] ff
                    rst       $38                           ;[3963] ff
                    rst       $38                           ;[3964] ff
                    rst       $38                           ;[3965] ff
                    rst       $38                           ;[3966] ff
                    rst       $38                           ;[3967] ff
                    rst       $38                           ;[3968] ff
                    rst       $38                           ;[3969] ff
                    rst       $38                           ;[396a] ff
                    rst       $38                           ;[396b] ff
                    rst       $38                           ;[396c] ff
                    rst       $38                           ;[396d] ff
                    rst       $38                           ;[396e] ff
                    rst       $38                           ;[396f] ff
                    rst       $38                           ;[3970] ff
                    rst       $38                           ;[3971] ff
                    rst       $38                           ;[3972] ff
                    rst       $38                           ;[3973] ff
                    rst       $38                           ;[3974] ff
                    rst       $38                           ;[3975] ff
                    rst       $38                           ;[3976] ff
                    rst       $38                           ;[3977] ff
                    rst       $38                           ;[3978] ff
                    rst       $38                           ;[3979] ff
                    rst       $38                           ;[397a] ff
                    rst       $38                           ;[397b] ff
                    rst       $38                           ;[397c] ff
                    rst       $38                           ;[397d] ff
                    rst       $38                           ;[397e] ff
                    rst       $38                           ;[397f] ff
                    rst       $38                           ;[3980] ff
                    rst       $38                           ;[3981] ff
                    rst       $38                           ;[3982] ff
                    rst       $38                           ;[3983] ff
                    rst       $38                           ;[3984] ff
                    rst       $38                           ;[3985] ff
                    rst       $38                           ;[3986] ff
                    rst       $38                           ;[3987] ff
                    rst       $38                           ;[3988] ff
                    rst       $38                           ;[3989] ff
                    rst       $38                           ;[398a] ff
                    rst       $38                           ;[398b] ff
                    rst       $38                           ;[398c] ff
                    rst       $38                           ;[398d] ff
                    rst       $38                           ;[398e] ff
                    rst       $38                           ;[398f] ff
                    rst       $38                           ;[3990] ff
                    rst       $38                           ;[3991] ff
                    rst       $38                           ;[3992] ff
                    rst       $38                           ;[3993] ff
                    rst       $38                           ;[3994] ff
                    rst       $38                           ;[3995] ff
                    rst       $38                           ;[3996] ff
                    rst       $38                           ;[3997] ff
                    rst       $38                           ;[3998] ff
                    rst       $38                           ;[3999] ff
                    rst       $38                           ;[399a] ff
                    rst       $38                           ;[399b] ff
                    rst       $38                           ;[399c] ff
                    rst       $38                           ;[399d] ff
                    rst       $38                           ;[399e] ff
                    rst       $38                           ;[399f] ff
                    rst       $38                           ;[39a0] ff
                    rst       $38                           ;[39a1] ff
                    rst       $38                           ;[39a2] ff
                    rst       $38                           ;[39a3] ff
                    rst       $38                           ;[39a4] ff
                    rst       $38                           ;[39a5] ff
                    rst       $38                           ;[39a6] ff
                    rst       $38                           ;[39a7] ff
                    rst       $38                           ;[39a8] ff
                    rst       $38                           ;[39a9] ff
                    rst       $38                           ;[39aa] ff
                    rst       $38                           ;[39ab] ff
                    rst       $38                           ;[39ac] ff
                    rst       $38                           ;[39ad] ff
                    rst       $38                           ;[39ae] ff
                    rst       $38                           ;[39af] ff
                    rst       $38                           ;[39b0] ff
                    rst       $38                           ;[39b1] ff
                    rst       $38                           ;[39b2] ff
                    rst       $38                           ;[39b3] ff
                    rst       $38                           ;[39b4] ff
                    rst       $38                           ;[39b5] ff
                    rst       $38                           ;[39b6] ff
                    rst       $38                           ;[39b7] ff
                    rst       $38                           ;[39b8] ff
                    rst       $38                           ;[39b9] ff
                    rst       $38                           ;[39ba] ff
                    rst       $38                           ;[39bb] ff
                    rst       $38                           ;[39bc] ff
                    rst       $38                           ;[39bd] ff
                    rst       $38                           ;[39be] ff
                    rst       $38                           ;[39bf] ff
                    rst       $38                           ;[39c0] ff
                    rst       $38                           ;[39c1] ff
                    rst       $38                           ;[39c2] ff
                    rst       $38                           ;[39c3] ff
                    rst       $38                           ;[39c4] ff
                    rst       $38                           ;[39c5] ff
                    rst       $38                           ;[39c6] ff
                    rst       $38                           ;[39c7] ff
                    rst       $38                           ;[39c8] ff
                    rst       $38                           ;[39c9] ff
                    rst       $38                           ;[39ca] ff
                    rst       $38                           ;[39cb] ff
                    rst       $38                           ;[39cc] ff
                    rst       $38                           ;[39cd] ff
                    rst       $38                           ;[39ce] ff
                    rst       $38                           ;[39cf] ff
                    rst       $38                           ;[39d0] ff
                    rst       $38                           ;[39d1] ff
                    rst       $38                           ;[39d2] ff
                    rst       $38                           ;[39d3] ff
                    rst       $38                           ;[39d4] ff
                    rst       $38                           ;[39d5] ff
                    rst       $38                           ;[39d6] ff
                    rst       $38                           ;[39d7] ff
                    rst       $38                           ;[39d8] ff
                    rst       $38                           ;[39d9] ff
                    rst       $38                           ;[39da] ff
                    rst       $38                           ;[39db] ff
                    rst       $38                           ;[39dc] ff
                    rst       $38                           ;[39dd] ff
                    rst       $38                           ;[39de] ff
                    rst       $38                           ;[39df] ff
                    rst       $38                           ;[39e0] ff
                    rst       $38                           ;[39e1] ff
                    rst       $38                           ;[39e2] ff
                    rst       $38                           ;[39e3] ff
                    rst       $38                           ;[39e4] ff
                    rst       $38                           ;[39e5] ff
                    rst       $38                           ;[39e6] ff
                    rst       $38                           ;[39e7] ff
                    rst       $38                           ;[39e8] ff
                    rst       $38                           ;[39e9] ff
                    rst       $38                           ;[39ea] ff
                    rst       $38                           ;[39eb] ff
                    rst       $38                           ;[39ec] ff
                    rst       $38                           ;[39ed] ff
                    rst       $38                           ;[39ee] ff
                    rst       $38                           ;[39ef] ff
                    rst       $38                           ;[39f0] ff
                    rst       $38                           ;[39f1] ff
                    rst       $38                           ;[39f2] ff
                    rst       $38                           ;[39f3] ff
                    rst       $38                           ;[39f4] ff
                    rst       $38                           ;[39f5] ff
                    rst       $38                           ;[39f6] ff
                    rst       $38                           ;[39f7] ff
                    rst       $38                           ;[39f8] ff
                    rst       $38                           ;[39f9] ff
                    rst       $38                           ;[39fa] ff
                    rst       $38                           ;[39fb] ff
                    rst       $38                           ;[39fc] ff
                    rst       $38                           ;[39fd] ff
                    rst       $38                           ;[39fe] ff
                    rst       $38                           ;[39ff] ff
                    rst       $38                           ;[3a00] ff
                    rst       $38                           ;[3a01] ff
                    rst       $38                           ;[3a02] ff
                    rst       $38                           ;[3a03] ff
                    rst       $38                           ;[3a04] ff
                    rst       $38                           ;[3a05] ff
                    rst       $38                           ;[3a06] ff
                    rst       $38                           ;[3a07] ff
                    rst       $38                           ;[3a08] ff
                    rst       $38                           ;[3a09] ff
                    rst       $38                           ;[3a0a] ff
                    rst       $38                           ;[3a0b] ff
                    rst       $38                           ;[3a0c] ff
                    rst       $38                           ;[3a0d] ff
                    rst       $38                           ;[3a0e] ff
                    rst       $38                           ;[3a0f] ff
                    rst       $38                           ;[3a10] ff
                    rst       $38                           ;[3a11] ff
                    rst       $38                           ;[3a12] ff
                    rst       $38                           ;[3a13] ff
                    rst       $38                           ;[3a14] ff
                    rst       $38                           ;[3a15] ff
                    rst       $38                           ;[3a16] ff
                    rst       $38                           ;[3a17] ff
                    rst       $38                           ;[3a18] ff
                    rst       $38                           ;[3a19] ff
                    rst       $38                           ;[3a1a] ff
                    rst       $38                           ;[3a1b] ff
                    rst       $38                           ;[3a1c] ff
                    rst       $38                           ;[3a1d] ff
                    rst       $38                           ;[3a1e] ff
                    rst       $38                           ;[3a1f] ff
                    rst       $38                           ;[3a20] ff
                    rst       $38                           ;[3a21] ff
                    rst       $38                           ;[3a22] ff
                    rst       $38                           ;[3a23] ff
                    rst       $38                           ;[3a24] ff
                    rst       $38                           ;[3a25] ff
                    rst       $38                           ;[3a26] ff
                    rst       $38                           ;[3a27] ff
                    rst       $38                           ;[3a28] ff
                    rst       $38                           ;[3a29] ff
                    rst       $38                           ;[3a2a] ff
                    rst       $38                           ;[3a2b] ff
                    rst       $38                           ;[3a2c] ff
                    rst       $38                           ;[3a2d] ff
                    rst       $38                           ;[3a2e] ff
                    rst       $38                           ;[3a2f] ff
                    rst       $38                           ;[3a30] ff
                    rst       $38                           ;[3a31] ff
                    rst       $38                           ;[3a32] ff
                    rst       $38                           ;[3a33] ff
                    rst       $38                           ;[3a34] ff
                    rst       $38                           ;[3a35] ff
                    rst       $38                           ;[3a36] ff
                    rst       $38                           ;[3a37] ff
                    rst       $38                           ;[3a38] ff
                    rst       $38                           ;[3a39] ff
                    rst       $38                           ;[3a3a] ff
                    rst       $38                           ;[3a3b] ff
                    rst       $38                           ;[3a3c] ff
                    rst       $38                           ;[3a3d] ff
                    rst       $38                           ;[3a3e] ff
                    rst       $38                           ;[3a3f] ff
                    rst       $38                           ;[3a40] ff
                    rst       $38                           ;[3a41] ff
                    rst       $38                           ;[3a42] ff
                    rst       $38                           ;[3a43] ff
                    rst       $38                           ;[3a44] ff
                    rst       $38                           ;[3a45] ff
                    rst       $38                           ;[3a46] ff
                    rst       $38                           ;[3a47] ff
                    rst       $38                           ;[3a48] ff
                    rst       $38                           ;[3a49] ff
                    rst       $38                           ;[3a4a] ff
                    rst       $38                           ;[3a4b] ff
                    rst       $38                           ;[3a4c] ff
                    rst       $38                           ;[3a4d] ff
                    rst       $38                           ;[3a4e] ff
                    rst       $38                           ;[3a4f] ff
                    rst       $38                           ;[3a50] ff
                    rst       $38                           ;[3a51] ff
                    rst       $38                           ;[3a52] ff
                    rst       $38                           ;[3a53] ff
                    rst       $38                           ;[3a54] ff
                    rst       $38                           ;[3a55] ff
                    rst       $38                           ;[3a56] ff
                    rst       $38                           ;[3a57] ff
                    rst       $38                           ;[3a58] ff
                    rst       $38                           ;[3a59] ff
                    rst       $38                           ;[3a5a] ff
                    rst       $38                           ;[3a5b] ff
                    rst       $38                           ;[3a5c] ff
                    rst       $38                           ;[3a5d] ff
                    rst       $38                           ;[3a5e] ff
                    rst       $38                           ;[3a5f] ff
                    rst       $38                           ;[3a60] ff
                    rst       $38                           ;[3a61] ff
                    rst       $38                           ;[3a62] ff
                    rst       $38                           ;[3a63] ff
                    rst       $38                           ;[3a64] ff
                    rst       $38                           ;[3a65] ff
                    rst       $38                           ;[3a66] ff
                    rst       $38                           ;[3a67] ff
                    rst       $38                           ;[3a68] ff
                    rst       $38                           ;[3a69] ff
                    rst       $38                           ;[3a6a] ff
                    rst       $38                           ;[3a6b] ff
                    rst       $38                           ;[3a6c] ff
                    rst       $38                           ;[3a6d] ff
                    rst       $38                           ;[3a6e] ff
                    rst       $38                           ;[3a6f] ff
                    rst       $38                           ;[3a70] ff
                    rst       $38                           ;[3a71] ff
                    rst       $38                           ;[3a72] ff
                    rst       $38                           ;[3a73] ff
                    rst       $38                           ;[3a74] ff
                    rst       $38                           ;[3a75] ff
                    rst       $38                           ;[3a76] ff
                    rst       $38                           ;[3a77] ff
                    rst       $38                           ;[3a78] ff
                    rst       $38                           ;[3a79] ff
                    rst       $38                           ;[3a7a] ff
                    rst       $38                           ;[3a7b] ff
                    rst       $38                           ;[3a7c] ff
                    rst       $38                           ;[3a7d] ff
                    rst       $38                           ;[3a7e] ff
                    rst       $38                           ;[3a7f] ff
                    rst       $38                           ;[3a80] ff
                    rst       $38                           ;[3a81] ff
                    rst       $38                           ;[3a82] ff
                    rst       $38                           ;[3a83] ff
                    rst       $38                           ;[3a84] ff
                    rst       $38                           ;[3a85] ff
                    rst       $38                           ;[3a86] ff
                    rst       $38                           ;[3a87] ff
                    rst       $38                           ;[3a88] ff
                    rst       $38                           ;[3a89] ff
                    rst       $38                           ;[3a8a] ff
                    rst       $38                           ;[3a8b] ff
                    rst       $38                           ;[3a8c] ff
                    rst       $38                           ;[3a8d] ff
                    rst       $38                           ;[3a8e] ff
                    rst       $38                           ;[3a8f] ff
                    rst       $38                           ;[3a90] ff
                    rst       $38                           ;[3a91] ff
                    rst       $38                           ;[3a92] ff
                    rst       $38                           ;[3a93] ff
                    rst       $38                           ;[3a94] ff
                    rst       $38                           ;[3a95] ff
                    rst       $38                           ;[3a96] ff
                    rst       $38                           ;[3a97] ff
                    rst       $38                           ;[3a98] ff
                    rst       $38                           ;[3a99] ff
                    rst       $38                           ;[3a9a] ff
                    rst       $38                           ;[3a9b] ff
                    rst       $38                           ;[3a9c] ff
                    rst       $38                           ;[3a9d] ff
                    rst       $38                           ;[3a9e] ff
                    rst       $38                           ;[3a9f] ff
                    rst       $38                           ;[3aa0] ff
                    rst       $38                           ;[3aa1] ff
                    rst       $38                           ;[3aa2] ff
                    rst       $38                           ;[3aa3] ff
                    rst       $38                           ;[3aa4] ff
                    rst       $38                           ;[3aa5] ff
                    rst       $38                           ;[3aa6] ff
                    rst       $38                           ;[3aa7] ff
                    rst       $38                           ;[3aa8] ff
                    rst       $38                           ;[3aa9] ff
                    rst       $38                           ;[3aaa] ff
                    rst       $38                           ;[3aab] ff
                    rst       $38                           ;[3aac] ff
                    rst       $38                           ;[3aad] ff
                    rst       $38                           ;[3aae] ff
                    rst       $38                           ;[3aaf] ff
                    rst       $38                           ;[3ab0] ff
                    rst       $38                           ;[3ab1] ff
                    rst       $38                           ;[3ab2] ff
                    rst       $38                           ;[3ab3] ff
                    rst       $38                           ;[3ab4] ff
                    rst       $38                           ;[3ab5] ff
                    rst       $38                           ;[3ab6] ff
                    rst       $38                           ;[3ab7] ff
                    rst       $38                           ;[3ab8] ff
                    rst       $38                           ;[3ab9] ff
                    rst       $38                           ;[3aba] ff
                    rst       $38                           ;[3abb] ff
                    rst       $38                           ;[3abc] ff
                    rst       $38                           ;[3abd] ff
                    rst       $38                           ;[3abe] ff
                    rst       $38                           ;[3abf] ff
                    rst       $38                           ;[3ac0] ff
                    rst       $38                           ;[3ac1] ff
                    rst       $38                           ;[3ac2] ff
                    rst       $38                           ;[3ac3] ff
                    rst       $38                           ;[3ac4] ff
                    rst       $38                           ;[3ac5] ff
                    rst       $38                           ;[3ac6] ff
                    rst       $38                           ;[3ac7] ff
                    rst       $38                           ;[3ac8] ff
                    rst       $38                           ;[3ac9] ff
                    rst       $38                           ;[3aca] ff
                    rst       $38                           ;[3acb] ff
                    rst       $38                           ;[3acc] ff
                    rst       $38                           ;[3acd] ff
                    rst       $38                           ;[3ace] ff
                    rst       $38                           ;[3acf] ff
                    rst       $38                           ;[3ad0] ff
                    rst       $38                           ;[3ad1] ff
                    rst       $38                           ;[3ad2] ff
                    rst       $38                           ;[3ad3] ff
                    rst       $38                           ;[3ad4] ff
                    rst       $38                           ;[3ad5] ff
                    rst       $38                           ;[3ad6] ff
                    rst       $38                           ;[3ad7] ff
                    rst       $38                           ;[3ad8] ff
                    rst       $38                           ;[3ad9] ff
                    rst       $38                           ;[3ada] ff
                    rst       $38                           ;[3adb] ff
                    rst       $38                           ;[3adc] ff
                    rst       $38                           ;[3add] ff
                    rst       $38                           ;[3ade] ff
                    rst       $38                           ;[3adf] ff
                    rst       $38                           ;[3ae0] ff
                    rst       $38                           ;[3ae1] ff
                    rst       $38                           ;[3ae2] ff
                    rst       $38                           ;[3ae3] ff
                    rst       $38                           ;[3ae4] ff
                    rst       $38                           ;[3ae5] ff
                    rst       $38                           ;[3ae6] ff
                    rst       $38                           ;[3ae7] ff
                    rst       $38                           ;[3ae8] ff
                    rst       $38                           ;[3ae9] ff
                    rst       $38                           ;[3aea] ff
                    rst       $38                           ;[3aeb] ff
                    rst       $38                           ;[3aec] ff
                    rst       $38                           ;[3aed] ff
                    rst       $38                           ;[3aee] ff
                    rst       $38                           ;[3aef] ff
                    rst       $38                           ;[3af0] ff
                    rst       $38                           ;[3af1] ff
                    rst       $38                           ;[3af2] ff
                    rst       $38                           ;[3af3] ff
                    rst       $38                           ;[3af4] ff
                    rst       $38                           ;[3af5] ff
                    rst       $38                           ;[3af6] ff
                    rst       $38                           ;[3af7] ff
                    rst       $38                           ;[3af8] ff
                    rst       $38                           ;[3af9] ff
                    rst       $38                           ;[3afa] ff
                    rst       $38                           ;[3afb] ff
                    rst       $38                           ;[3afc] ff
                    rst       $38                           ;[3afd] ff
                    rst       $38                           ;[3afe] ff
                    rst       $38                           ;[3aff] ff
                    rst       $38                           ;[3b00] ff
                    rst       $38                           ;[3b01] ff
                    rst       $38                           ;[3b02] ff
                    rst       $38                           ;[3b03] ff
                    rst       $38                           ;[3b04] ff
                    rst       $38                           ;[3b05] ff
                    rst       $38                           ;[3b06] ff
                    rst       $38                           ;[3b07] ff
                    rst       $38                           ;[3b08] ff
                    rst       $38                           ;[3b09] ff
                    rst       $38                           ;[3b0a] ff
                    rst       $38                           ;[3b0b] ff
                    rst       $38                           ;[3b0c] ff
                    rst       $38                           ;[3b0d] ff
                    rst       $38                           ;[3b0e] ff
                    rst       $38                           ;[3b0f] ff
                    rst       $38                           ;[3b10] ff
                    rst       $38                           ;[3b11] ff
                    rst       $38                           ;[3b12] ff
                    rst       $38                           ;[3b13] ff
                    rst       $38                           ;[3b14] ff
                    rst       $38                           ;[3b15] ff
                    rst       $38                           ;[3b16] ff
                    rst       $38                           ;[3b17] ff
                    rst       $38                           ;[3b18] ff
                    rst       $38                           ;[3b19] ff
                    rst       $38                           ;[3b1a] ff
                    rst       $38                           ;[3b1b] ff
                    rst       $38                           ;[3b1c] ff
                    rst       $38                           ;[3b1d] ff
                    rst       $38                           ;[3b1e] ff
                    rst       $38                           ;[3b1f] ff
                    rst       $38                           ;[3b20] ff
                    rst       $38                           ;[3b21] ff
                    rst       $38                           ;[3b22] ff
                    rst       $38                           ;[3b23] ff
                    rst       $38                           ;[3b24] ff
                    rst       $38                           ;[3b25] ff
                    rst       $38                           ;[3b26] ff
                    rst       $38                           ;[3b27] ff
                    rst       $38                           ;[3b28] ff
                    rst       $38                           ;[3b29] ff
                    rst       $38                           ;[3b2a] ff
                    rst       $38                           ;[3b2b] ff
                    rst       $38                           ;[3b2c] ff
                    rst       $38                           ;[3b2d] ff
                    rst       $38                           ;[3b2e] ff
                    rst       $38                           ;[3b2f] ff
                    rst       $38                           ;[3b30] ff
                    rst       $38                           ;[3b31] ff
                    rst       $38                           ;[3b32] ff
                    rst       $38                           ;[3b33] ff
                    rst       $38                           ;[3b34] ff
                    rst       $38                           ;[3b35] ff
                    rst       $38                           ;[3b36] ff
                    rst       $38                           ;[3b37] ff
                    rst       $38                           ;[3b38] ff
                    rst       $38                           ;[3b39] ff
                    rst       $38                           ;[3b3a] ff
                    rst       $38                           ;[3b3b] ff
                    rst       $38                           ;[3b3c] ff
                    rst       $38                           ;[3b3d] ff
                    rst       $38                           ;[3b3e] ff
                    rst       $38                           ;[3b3f] ff
                    rst       $38                           ;[3b40] ff
                    rst       $38                           ;[3b41] ff
                    rst       $38                           ;[3b42] ff
                    rst       $38                           ;[3b43] ff
                    rst       $38                           ;[3b44] ff
                    rst       $38                           ;[3b45] ff
                    rst       $38                           ;[3b46] ff
                    rst       $38                           ;[3b47] ff
                    rst       $38                           ;[3b48] ff
                    rst       $38                           ;[3b49] ff
                    rst       $38                           ;[3b4a] ff
                    rst       $38                           ;[3b4b] ff
                    rst       $38                           ;[3b4c] ff
                    rst       $38                           ;[3b4d] ff
                    rst       $38                           ;[3b4e] ff
                    rst       $38                           ;[3b4f] ff
                    rst       $38                           ;[3b50] ff
                    rst       $38                           ;[3b51] ff
                    rst       $38                           ;[3b52] ff
                    rst       $38                           ;[3b53] ff
                    rst       $38                           ;[3b54] ff
                    rst       $38                           ;[3b55] ff
                    rst       $38                           ;[3b56] ff
                    rst       $38                           ;[3b57] ff
                    rst       $38                           ;[3b58] ff
                    rst       $38                           ;[3b59] ff
                    rst       $38                           ;[3b5a] ff
                    rst       $38                           ;[3b5b] ff
                    rst       $38                           ;[3b5c] ff
                    rst       $38                           ;[3b5d] ff
                    rst       $38                           ;[3b5e] ff
                    rst       $38                           ;[3b5f] ff
                    rst       $38                           ;[3b60] ff
                    rst       $38                           ;[3b61] ff
                    rst       $38                           ;[3b62] ff
                    rst       $38                           ;[3b63] ff
                    rst       $38                           ;[3b64] ff
                    rst       $38                           ;[3b65] ff
                    rst       $38                           ;[3b66] ff
                    rst       $38                           ;[3b67] ff
                    rst       $38                           ;[3b68] ff
                    rst       $38                           ;[3b69] ff
                    rst       $38                           ;[3b6a] ff
                    rst       $38                           ;[3b6b] ff
                    rst       $38                           ;[3b6c] ff
                    rst       $38                           ;[3b6d] ff
                    rst       $38                           ;[3b6e] ff
                    rst       $38                           ;[3b6f] ff
                    rst       $38                           ;[3b70] ff
                    rst       $38                           ;[3b71] ff
                    rst       $38                           ;[3b72] ff
                    rst       $38                           ;[3b73] ff
                    rst       $38                           ;[3b74] ff
                    rst       $38                           ;[3b75] ff
                    rst       $38                           ;[3b76] ff
                    rst       $38                           ;[3b77] ff
                    rst       $38                           ;[3b78] ff
                    rst       $38                           ;[3b79] ff
                    rst       $38                           ;[3b7a] ff
                    rst       $38                           ;[3b7b] ff
                    rst       $38                           ;[3b7c] ff
                    rst       $38                           ;[3b7d] ff
                    rst       $38                           ;[3b7e] ff
                    rst       $38                           ;[3b7f] ff
                    rst       $38                           ;[3b80] ff
                    rst       $38                           ;[3b81] ff
                    rst       $38                           ;[3b82] ff
                    rst       $38                           ;[3b83] ff
                    rst       $38                           ;[3b84] ff
                    rst       $38                           ;[3b85] ff
                    rst       $38                           ;[3b86] ff
                    rst       $38                           ;[3b87] ff
                    rst       $38                           ;[3b88] ff
                    rst       $38                           ;[3b89] ff
                    rst       $38                           ;[3b8a] ff
                    rst       $38                           ;[3b8b] ff
                    rst       $38                           ;[3b8c] ff
                    rst       $38                           ;[3b8d] ff
                    rst       $38                           ;[3b8e] ff
                    rst       $38                           ;[3b8f] ff
                    rst       $38                           ;[3b90] ff
                    rst       $38                           ;[3b91] ff
                    rst       $38                           ;[3b92] ff
                    rst       $38                           ;[3b93] ff
                    rst       $38                           ;[3b94] ff
                    rst       $38                           ;[3b95] ff
                    rst       $38                           ;[3b96] ff
                    rst       $38                           ;[3b97] ff
                    rst       $38                           ;[3b98] ff
                    rst       $38                           ;[3b99] ff
                    rst       $38                           ;[3b9a] ff
                    rst       $38                           ;[3b9b] ff
                    rst       $38                           ;[3b9c] ff
                    rst       $38                           ;[3b9d] ff
                    rst       $38                           ;[3b9e] ff
                    rst       $38                           ;[3b9f] ff
                    rst       $38                           ;[3ba0] ff
                    rst       $38                           ;[3ba1] ff
                    rst       $38                           ;[3ba2] ff
                    rst       $38                           ;[3ba3] ff
                    rst       $38                           ;[3ba4] ff
                    rst       $38                           ;[3ba5] ff
                    rst       $38                           ;[3ba6] ff
                    rst       $38                           ;[3ba7] ff
                    rst       $38                           ;[3ba8] ff
                    rst       $38                           ;[3ba9] ff
                    rst       $38                           ;[3baa] ff
                    rst       $38                           ;[3bab] ff
                    rst       $38                           ;[3bac] ff
                    rst       $38                           ;[3bad] ff
                    rst       $38                           ;[3bae] ff
                    rst       $38                           ;[3baf] ff
                    rst       $38                           ;[3bb0] ff
                    rst       $38                           ;[3bb1] ff
                    rst       $38                           ;[3bb2] ff
                    rst       $38                           ;[3bb3] ff
                    rst       $38                           ;[3bb4] ff
                    rst       $38                           ;[3bb5] ff
                    rst       $38                           ;[3bb6] ff
                    rst       $38                           ;[3bb7] ff
                    rst       $38                           ;[3bb8] ff
                    rst       $38                           ;[3bb9] ff
                    rst       $38                           ;[3bba] ff
                    rst       $38                           ;[3bbb] ff
                    rst       $38                           ;[3bbc] ff
                    rst       $38                           ;[3bbd] ff
                    rst       $38                           ;[3bbe] ff
                    rst       $38                           ;[3bbf] ff
                    rst       $38                           ;[3bc0] ff
                    rst       $38                           ;[3bc1] ff
                    rst       $38                           ;[3bc2] ff
                    rst       $38                           ;[3bc3] ff
                    rst       $38                           ;[3bc4] ff
                    rst       $38                           ;[3bc5] ff
                    rst       $38                           ;[3bc6] ff
                    rst       $38                           ;[3bc7] ff
                    rst       $38                           ;[3bc8] ff
                    rst       $38                           ;[3bc9] ff
                    rst       $38                           ;[3bca] ff
                    rst       $38                           ;[3bcb] ff
                    rst       $38                           ;[3bcc] ff
                    rst       $38                           ;[3bcd] ff
                    rst       $38                           ;[3bce] ff
                    rst       $38                           ;[3bcf] ff
                    rst       $38                           ;[3bd0] ff
                    rst       $38                           ;[3bd1] ff
                    rst       $38                           ;[3bd2] ff
                    rst       $38                           ;[3bd3] ff
                    rst       $38                           ;[3bd4] ff
                    rst       $38                           ;[3bd5] ff
                    rst       $38                           ;[3bd6] ff
                    rst       $38                           ;[3bd7] ff
                    rst       $38                           ;[3bd8] ff
                    rst       $38                           ;[3bd9] ff
                    rst       $38                           ;[3bda] ff
                    rst       $38                           ;[3bdb] ff
                    rst       $38                           ;[3bdc] ff
                    rst       $38                           ;[3bdd] ff
                    rst       $38                           ;[3bde] ff
                    rst       $38                           ;[3bdf] ff
                    rst       $38                           ;[3be0] ff
                    rst       $38                           ;[3be1] ff
                    rst       $38                           ;[3be2] ff
                    rst       $38                           ;[3be3] ff
                    rst       $38                           ;[3be4] ff
                    rst       $38                           ;[3be5] ff
                    rst       $38                           ;[3be6] ff
                    rst       $38                           ;[3be7] ff
                    rst       $38                           ;[3be8] ff
                    rst       $38                           ;[3be9] ff
                    rst       $38                           ;[3bea] ff
                    rst       $38                           ;[3beb] ff
                    rst       $38                           ;[3bec] ff
                    rst       $38                           ;[3bed] ff
                    rst       $38                           ;[3bee] ff
                    rst       $38                           ;[3bef] ff
                    rst       $38                           ;[3bf0] ff
                    rst       $38                           ;[3bf1] ff
                    rst       $38                           ;[3bf2] ff
                    rst       $38                           ;[3bf3] ff
                    rst       $38                           ;[3bf4] ff
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
                    rst       $38                           ;[3c02] ff
                    rst       $38                           ;[3c03] ff
                    rst       $38                           ;[3c04] ff
                    rst       $38                           ;[3c05] ff
                    rst       $38                           ;[3c06] ff
                    rst       $38                           ;[3c07] ff
                    rst       $38                           ;[3c08] ff
                    rst       $38                           ;[3c09] ff
                    rst       $38                           ;[3c0a] ff
                    rst       $38                           ;[3c0b] ff
                    rst       $38                           ;[3c0c] ff
                    rst       $38                           ;[3c0d] ff
                    rst       $38                           ;[3c0e] ff
                    rst       $38                           ;[3c0f] ff
                    rst       $38                           ;[3c10] ff
                    rst       $38                           ;[3c11] ff
                    rst       $38                           ;[3c12] ff
                    rst       $38                           ;[3c13] ff
                    rst       $38                           ;[3c14] ff
                    rst       $38                           ;[3c15] ff
                    rst       $38                           ;[3c16] ff
                    rst       $38                           ;[3c17] ff
                    rst       $38                           ;[3c18] ff
                    rst       $38                           ;[3c19] ff
                    rst       $38                           ;[3c1a] ff
                    rst       $38                           ;[3c1b] ff
                    rst       $38                           ;[3c1c] ff
                    rst       $38                           ;[3c1d] ff
                    rst       $38                           ;[3c1e] ff
                    rst       $38                           ;[3c1f] ff
                    rst       $38                           ;[3c20] ff
                    rst       $38                           ;[3c21] ff
                    rst       $38                           ;[3c22] ff
                    rst       $38                           ;[3c23] ff
                    rst       $38                           ;[3c24] ff
                    rst       $38                           ;[3c25] ff
                    rst       $38                           ;[3c26] ff
                    rst       $38                           ;[3c27] ff
                    rst       $38                           ;[3c28] ff
                    rst       $38                           ;[3c29] ff
                    rst       $38                           ;[3c2a] ff
                    rst       $38                           ;[3c2b] ff
                    rst       $38                           ;[3c2c] ff
                    rst       $38                           ;[3c2d] ff
                    rst       $38                           ;[3c2e] ff
                    rst       $38                           ;[3c2f] ff
                    rst       $38                           ;[3c30] ff
                    rst       $38                           ;[3c31] ff
                    rst       $38                           ;[3c32] ff
                    rst       $38                           ;[3c33] ff
                    rst       $38                           ;[3c34] ff
                    rst       $38                           ;[3c35] ff
                    rst       $38                           ;[3c36] ff
                    rst       $38                           ;[3c37] ff
                    rst       $38                           ;[3c38] ff
                    rst       $38                           ;[3c39] ff
                    rst       $38                           ;[3c3a] ff
                    rst       $38                           ;[3c3b] ff
                    rst       $38                           ;[3c3c] ff
                    rst       $38                           ;[3c3d] ff
                    rst       $38                           ;[3c3e] ff
                    rst       $38                           ;[3c3f] ff
                    rst       $38                           ;[3c40] ff
                    rst       $38                           ;[3c41] ff
                    rst       $38                           ;[3c42] ff
                    rst       $38                           ;[3c43] ff
                    rst       $38                           ;[3c44] ff
                    rst       $38                           ;[3c45] ff
                    rst       $38                           ;[3c46] ff
                    rst       $38                           ;[3c47] ff
                    rst       $38                           ;[3c48] ff
                    rst       $38                           ;[3c49] ff
                    rst       $38                           ;[3c4a] ff
                    rst       $38                           ;[3c4b] ff
                    rst       $38                           ;[3c4c] ff
                    rst       $38                           ;[3c4d] ff
                    rst       $38                           ;[3c4e] ff
                    rst       $38                           ;[3c4f] ff
                    rst       $38                           ;[3c50] ff
                    rst       $38                           ;[3c51] ff
                    rst       $38                           ;[3c52] ff
                    rst       $38                           ;[3c53] ff
                    rst       $38                           ;[3c54] ff
                    rst       $38                           ;[3c55] ff
                    rst       $38                           ;[3c56] ff
                    rst       $38                           ;[3c57] ff
                    rst       $38                           ;[3c58] ff
                    rst       $38                           ;[3c59] ff
                    rst       $38                           ;[3c5a] ff
                    rst       $38                           ;[3c5b] ff
                    rst       $38                           ;[3c5c] ff
                    rst       $38                           ;[3c5d] ff
                    rst       $38                           ;[3c5e] ff
                    rst       $38                           ;[3c5f] ff
                    rst       $38                           ;[3c60] ff
                    rst       $38                           ;[3c61] ff
                    rst       $38                           ;[3c62] ff
                    rst       $38                           ;[3c63] ff
                    rst       $38                           ;[3c64] ff
                    rst       $38                           ;[3c65] ff
                    rst       $38                           ;[3c66] ff
                    rst       $38                           ;[3c67] ff
                    rst       $38                           ;[3c68] ff
                    rst       $38                           ;[3c69] ff
                    rst       $38                           ;[3c6a] ff
                    rst       $38                           ;[3c6b] ff
                    rst       $38                           ;[3c6c] ff
                    rst       $38                           ;[3c6d] ff
                    rst       $38                           ;[3c6e] ff
                    rst       $38                           ;[3c6f] ff
                    rst       $38                           ;[3c70] ff
                    rst       $38                           ;[3c71] ff
                    rst       $38                           ;[3c72] ff
                    rst       $38                           ;[3c73] ff
                    rst       $38                           ;[3c74] ff
                    rst       $38                           ;[3c75] ff
                    rst       $38                           ;[3c76] ff
                    rst       $38                           ;[3c77] ff
                    rst       $38                           ;[3c78] ff
                    rst       $38                           ;[3c79] ff
                    rst       $38                           ;[3c7a] ff
                    rst       $38                           ;[3c7b] ff
                    rst       $38                           ;[3c7c] ff
                    rst       $38                           ;[3c7d] ff
                    rst       $38                           ;[3c7e] ff
                    rst       $38                           ;[3c7f] ff
                    rst       $38                           ;[3c80] ff
                    rst       $38                           ;[3c81] ff
                    rst       $38                           ;[3c82] ff
                    rst       $38                           ;[3c83] ff
                    rst       $38                           ;[3c84] ff
                    rst       $38                           ;[3c85] ff
                    rst       $38                           ;[3c86] ff
                    rst       $38                           ;[3c87] ff
                    rst       $38                           ;[3c88] ff
                    rst       $38                           ;[3c89] ff
                    rst       $38                           ;[3c8a] ff
                    rst       $38                           ;[3c8b] ff
                    rst       $38                           ;[3c8c] ff
                    rst       $38                           ;[3c8d] ff
                    rst       $38                           ;[3c8e] ff
                    rst       $38                           ;[3c8f] ff
                    rst       $38                           ;[3c90] ff
                    rst       $38                           ;[3c91] ff
                    rst       $38                           ;[3c92] ff
                    rst       $38                           ;[3c93] ff
                    rst       $38                           ;[3c94] ff
                    rst       $38                           ;[3c95] ff
                    rst       $38                           ;[3c96] ff
                    rst       $38                           ;[3c97] ff
                    rst       $38                           ;[3c98] ff
                    rst       $38                           ;[3c99] ff
                    rst       $38                           ;[3c9a] ff
                    rst       $38                           ;[3c9b] ff
                    rst       $38                           ;[3c9c] ff
                    rst       $38                           ;[3c9d] ff
                    rst       $38                           ;[3c9e] ff
                    rst       $38                           ;[3c9f] ff
                    rst       $38                           ;[3ca0] ff
                    rst       $38                           ;[3ca1] ff
                    rst       $38                           ;[3ca2] ff
                    rst       $38                           ;[3ca3] ff
                    rst       $38                           ;[3ca4] ff
                    rst       $38                           ;[3ca5] ff
                    rst       $38                           ;[3ca6] ff
                    rst       $38                           ;[3ca7] ff
                    rst       $38                           ;[3ca8] ff
                    rst       $38                           ;[3ca9] ff
                    rst       $38                           ;[3caa] ff
                    rst       $38                           ;[3cab] ff
                    rst       $38                           ;[3cac] ff
                    rst       $38                           ;[3cad] ff
                    rst       $38                           ;[3cae] ff
                    rst       $38                           ;[3caf] ff
                    rst       $38                           ;[3cb0] ff
                    rst       $38                           ;[3cb1] ff
                    rst       $38                           ;[3cb2] ff
                    rst       $38                           ;[3cb3] ff
                    rst       $38                           ;[3cb4] ff
                    rst       $38                           ;[3cb5] ff
                    rst       $38                           ;[3cb6] ff
                    rst       $38                           ;[3cb7] ff
                    rst       $38                           ;[3cb8] ff
                    rst       $38                           ;[3cb9] ff
                    rst       $38                           ;[3cba] ff
                    rst       $38                           ;[3cbb] ff
                    rst       $38                           ;[3cbc] ff
                    rst       $38                           ;[3cbd] ff
                    rst       $38                           ;[3cbe] ff
                    rst       $38                           ;[3cbf] ff
                    rst       $38                           ;[3cc0] ff
                    rst       $38                           ;[3cc1] ff
                    rst       $38                           ;[3cc2] ff
                    rst       $38                           ;[3cc3] ff
                    rst       $38                           ;[3cc4] ff
                    rst       $38                           ;[3cc5] ff
                    rst       $38                           ;[3cc6] ff
                    rst       $38                           ;[3cc7] ff
                    rst       $38                           ;[3cc8] ff
                    rst       $38                           ;[3cc9] ff
                    rst       $38                           ;[3cca] ff
                    rst       $38                           ;[3ccb] ff
                    rst       $38                           ;[3ccc] ff
                    rst       $38                           ;[3ccd] ff
                    rst       $38                           ;[3cce] ff
                    rst       $38                           ;[3ccf] ff
                    rst       $38                           ;[3cd0] ff
                    rst       $38                           ;[3cd1] ff
                    rst       $38                           ;[3cd2] ff
                    rst       $38                           ;[3cd3] ff
                    rst       $38                           ;[3cd4] ff
                    rst       $38                           ;[3cd5] ff
                    rst       $38                           ;[3cd6] ff
                    rst       $38                           ;[3cd7] ff
                    rst       $38                           ;[3cd8] ff
                    rst       $38                           ;[3cd9] ff
                    rst       $38                           ;[3cda] ff
                    rst       $38                           ;[3cdb] ff
                    rst       $38                           ;[3cdc] ff
                    rst       $38                           ;[3cdd] ff
                    rst       $38                           ;[3cde] ff
                    rst       $38                           ;[3cdf] ff
                    rst       $38                           ;[3ce0] ff
                    rst       $38                           ;[3ce1] ff
                    rst       $38                           ;[3ce2] ff
                    rst       $38                           ;[3ce3] ff
                    rst       $38                           ;[3ce4] ff
                    rst       $38                           ;[3ce5] ff
                    rst       $38                           ;[3ce6] ff
                    rst       $38                           ;[3ce7] ff
                    rst       $38                           ;[3ce8] ff
                    rst       $38                           ;[3ce9] ff
                    rst       $38                           ;[3cea] ff
                    rst       $38                           ;[3ceb] ff
                    rst       $38                           ;[3cec] ff
                    rst       $38                           ;[3ced] ff
                    rst       $38                           ;[3cee] ff
                    rst       $38                           ;[3cef] ff
                    rst       $38                           ;[3cf0] ff
                    rst       $38                           ;[3cf1] ff
                    rst       $38                           ;[3cf2] ff
                    rst       $38                           ;[3cf3] ff
                    rst       $38                           ;[3cf4] ff
                    rst       $38                           ;[3cf5] ff
                    rst       $38                           ;[3cf6] ff
                    rst       $38                           ;[3cf7] ff
                    rst       $38                           ;[3cf8] ff
                    rst       $38                           ;[3cf9] ff
                    rst       $38                           ;[3cfa] ff
                    rst       $38                           ;[3cfb] ff
                    rst       $38                           ;[3cfc] ff
                    rst       $38                           ;[3cfd] ff
                    rst       $38                           ;[3cfe] ff
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
