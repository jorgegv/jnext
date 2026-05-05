10 REM G46(b) v2 - dump page 0x30 (NR_57=10) via inline Z80 routine
20 REM Routine at 32768 ($8000):
30 REM    LD A,B          ; B = NR_57 value to switch to
40 REM    NEXTREG 87,A
50 REM    LD HL,$E000
60 REM    LD DE,$A000
70 REM    LD BC,$2000
80 REM    LDIR             ; copy 8KB slot7 -> slot5
90 REM    LD A,1
100 REM   NEXTREG 87,A    ; restore
110 REM   RET
120 REM Then SAVE the slot 5 buffer ($A000) to file.
130 LET a=32768
140 RESTORE 300
150 FOR i=0 TO 21: READ b: POKE a+i,b: NEXT i
160 REM Run with B=$10 (page 0x30) - parameter via system var; simpler:
170 REM Hard-code NR_57 = $10 in routine; second routine variant for $11.
180 RANDOMIZE USR a
190 SAVE "/page30.bin" CODE 40960,8192
200 PRINT "/page30.bin saved (slot 7 with NR_57=10 copied)"
210 STOP
220 :
300 REM Z80 bytes: LD A,$10; NEXTREG 87,A; LD HL,$E000; LD DE,$A000;
310 REM            LD BC,$2000; LDIR; LD A,$01; NEXTREG 87,A; RET
320 DATA 62,16,237,146,87
330 DATA 33,0,224,17,0,160,1,0,32,237,176
340 DATA 62,1,237,146,87,201
