# dsl_demo (GH #26 WP7): the program the debugger-script suite runs against.
# Needs z88dk on PATH (zcc) + ZCCCFG set. Builds the good and the BUGGY NEX and
# the MAP, and refuses if the two builds disagree on any ADDRESS (the scripts
# use one MAP; only the `const` lines — the BUGGY operands — may differ).
ZCC      ?= zcc
ZCCFLAGS  = +zxn -vn -startup=31 -clib=sdcc_ix -subtype=nex -create-app -m
NEXDIR    = ../../test/00regression/nex

.PHONY: all nex tap install clean

all: nex

nex: dsl_demo.nex dsl_demo_buggy.nex

tap:

dsl_demo.nex: dsl_demo.asm
	$(ZCC) $(ZCCFLAGS) $< -o dsl_demo

dsl_demo_buggy.nex: dsl_demo.asm dsl_demo.nex
	$(ZCC) $(ZCCFLAGS) -Ca-DBUGGY=1 $< -o dsl_demo_buggy
	grep '; addr' dsl_demo.map > dsl_demo.addr
	grep '; addr' dsl_demo_buggy.map > dsl_demo_buggy.addr
	cmp dsl_demo.addr dsl_demo_buggy.addr

install: all
	cp dsl_demo.nex dsl_demo_buggy.nex dsl_demo.map $(NEXDIR)/

clean:
	rm -f *.nex *.bin *.map *.addr *.lis *.sym *.o dsl_demo dsl_demo_buggy
