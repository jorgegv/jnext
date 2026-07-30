ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix -SO3

STEM = $(notdir $(CURDIR))

.PHONY: all nex tap clean

# NEX only: the CLI buffer is a NEX V1.3 header feature, so a TAP of this
# program would have nothing to print.
all: nex

nex: $(STEM).nex

tap:

$(STEM).nex: $(STEM).c make-v13.py
	$(ZCC) $(ZCCFLAGS) $< -o $(STEM).bin -subtype=nex -create-app
	./make-v13.py $(STEM).nex

clean:
	rm -f *.nex *.tap *.bin *.lis *.c.lis *.map *.sym *.o *.reloc $(STEM)
