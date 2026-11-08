# Makefile for the HALT-idle bench workloads (GH #452). One source, three NEX.
# The NEX are committed under test/bench/nex/ (our own code, licence-clean);
# `make install` copies a fresh build there.
ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix
NEX      = halt_idle_35.nex halt_idle_28.nex ctc_dac_28.nex

.PHONY: all nex tap install clean

all: nex

nex: $(NEX)

# The workloads are bench NEX only: no TAP is produced.
tap:
	@true

halt_idle_35.nex: halt_bench.asm
	$(ZCC) $(ZCCFLAGS) $< -o halt_idle_35.bin -subtype=nex -create-app -Ca-DSPEED=0

halt_idle_28.nex: halt_bench.asm
	$(ZCC) $(ZCCFLAGS) $< -o halt_idle_28.bin -subtype=nex -create-app -Ca-DSPEED=3

ctc_dac_28.nex: halt_bench.asm
	$(ZCC) $(ZCCFLAGS) $< -o ctc_dac_28.bin -subtype=nex -create-app -Ca-DSPEED=3 -Ca-DCTCDAC=1

install: nex
	cp $(NEX) ../../test/bench/nex/

clean:
	rm -f *.nex *.bin *.lis *.map *.sym *.o *.reloc
