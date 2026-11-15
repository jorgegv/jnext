# The program needs esxDOS (RST $08), so it is a NEX only: there is no TAP target.
# One source, two NEX files: VARIANT=1 -> esx_save_counter.nex, VARIANT=2 -> esx_save_counter_b.nex.
ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix -SO3

STEM = $(notdir $(CURDIR))

.PHONY: all nex tap clean

all: nex

nex: $(STEM).nex $(STEM)_b.nex

# Nothing to build: see the comment at the top.
tap:

$(STEM).nex: $(STEM).c
	$(ZCC) $(ZCCFLAGS) -DVARIANT=1 $< -o $(STEM).bin -subtype=nex -create-app

$(STEM)_b.nex: $(STEM).c
	$(ZCC) $(ZCCFLAGS) -DVARIANT=2 $< -o $(STEM)_b.bin -subtype=nex -create-app

clean:
	rm -f *.nex *.tap *.bin *.lis *.c.lis *.map *.sym *.o *.reloc $(STEM)
