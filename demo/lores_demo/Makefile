ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix -SO3

STEM = $(notdir $(CURDIR))

.PHONY: all nex tap clean

all: nex tap

nex: $(STEM).nex

tap: $(STEM).tap

$(STEM).nex: $(STEM).c
	$(ZCC) $(ZCCFLAGS) $< -o $(STEM).bin -subtype=nex -create-app

$(STEM).tap: $(STEM).c
	$(ZCC) $(ZCCFLAGS) $< -o $(STEM).bin -subtype=tap -create-app

clean:
	rm -f *.nex *.tap *.bin *.lis *.c.lis *.map *.sym *.o *.reloc $(STEM)
