# The program drives the Next's SPI ports (0xE7/0xEB), so it is a Next-only NEX: there is no TAP target.
ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix -SO3

STEM = $(notdir $(CURDIR))

.PHONY: all nex tap clean

all: nex

nex: $(STEM).nex

# Nothing to build: see the comment at the top.
tap:

$(STEM).nex: $(STEM).c
	$(ZCC) $(ZCCFLAGS) $< -o $(STEM).bin -subtype=nex -create-app

clean:
	rm -f *.nex *.tap *.bin *.lis *.c.lis *.map *.sym *.o *.reloc $(STEM)
