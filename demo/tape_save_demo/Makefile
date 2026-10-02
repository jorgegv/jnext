# tape_save_demo (GH #89): a 48K raw binary for --inject, not a NEX or TAP.
# Needs z88dk on PATH (z88dk-z80asm).
Z80ASM ?= z88dk-z80asm
STEM    = $(notdir $(CURDIR))
BINDIR  = ../../test/00regression/bin

.PHONY: all nex tap install clean

all: $(STEM).bin

# Nothing to build for the demo tree's nex/tap passes: this is a raw binary.
nex tap:

$(STEM).bin: $(STEM).asm
	$(Z80ASM) -b -o=$@ $<

# Install the built binary as the tape-save-mic-func regression fixture.
install: all
	cp $(STEM).bin $(BINDIR)/

clean:
	rm -f *.bin *.o *.lis *.map *.sym
