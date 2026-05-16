10 REM G46(b) - NR registers + sysvars + slot 7 dump
20 REM Type at NextZXOS BASIC prompt after welcome screen.
30 REM Saves 3 files to SD root: nrdump.bin, sysvars.bin, slot7.bin
40 REM
50 REM First: snapshot current NR_57 (slot 7 mapping) before anything.
60 OUT 9275,87: LET cur57 = IN 9531
70 SAVE "/slot7.bin" CODE 57344,8192
80 REM Read all 256 NextRegs into $7000-$70FF.
90 FOR n=0 TO 255: OUT 9275,n: POKE 28672+n,IN 9531: NEXT n
100 SAVE "/nrdump.bin" CODE 28672,256
110 REM Save NextZXOS + ZX sysvars block ($5B00-$5CFF, 512 bytes).
120 SAVE "/sysvars.bin" CODE 23296,512
130 REM Save 4 KB at $4000 (BASIC display + attributes - useful context).
140 SAVE "/screen.bin" CODE 16384,6912 : REM screen + attrs context
150 PRINT "DONE. NR_57 was ";cur57
160 PRINT "Files: /slot7.bin /nrdump.bin /sysvars.bin /screen.bin"
