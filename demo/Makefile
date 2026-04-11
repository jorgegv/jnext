# Makefile for JNEXT demo/test programs
# Compiles all .c files to .nex and .tap using z88dk

ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_ix -SO3

SOURCES  = $(wildcard *.c)
STEMS    = $(SOURCES:.c=)
NEXFILES = $(SOURCES:.c=.nex)
TAPFILES = $(SOURCES:.c=.tap)

.PHONY: all nex tap clean

all: nex tap

nex: $(NEXFILES)

tap: $(TAPFILES)

# z88dk -o produces a raw binary (no extension by default).
# We use .bin extension to keep the directory clean.
%.nex: %.c
	$(ZCC) $(ZCCFLAGS) $< -o $*.bin -subtype=nex -create-app

%.tap: %.c
	$(ZCC) $(ZCCFLAGS) $< -o $*.bin -subtype=tap -create-app

clean:
	rm -f $(NEXFILES) $(TAPFILES)
	rm -f *.bin *.lis *.c.lis *.map *.sym *.o *.reloc
	rm -f $(STEMS)
