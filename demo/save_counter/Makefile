# esxDOS needs NextZXOS, so there is no TAP target here: a 48K TAP has no esxDOS.
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
