; GH #375 fixture: a label used in SIZEOF() is an ordinary label (trait +sizeof).
            DEVICE ZXSPECTRUMNEXT
            ORG $8000
start:      ld bc,SIZEOF(msg)
            ret
msg:        db "hello world"
