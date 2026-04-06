# Makefile for JNEXT demo/test programs
# Compiles all .c files to .nex and .tap using z88dk

ZCC      = zcc
ZCCFLAGS = +zxn -vn -startup=31 -clib=sdcc_iy -SO3

SOURCES  = $(wildcard *.c)
NEXFILES = $(SOURCES:.c=.nex)
TAPFILES = $(SOURCES:.c=.tap)

.PHONY: all nex tap clean

all: nex tap

nex: $(NEXFILES)

tap: $(TAPFILES)

%.nex: %.c
	$(ZCC) $(ZCCFLAGS) $< -o $* -subtype=nex -create-app

%.tap: %.c
	$(ZCC) $(ZCCFLAGS) $< -o $* -subtype=tap -create-app

clean:
	rm -f $(NEXFILES) $(TAPFILES)
	rm -f *_CODE.bin *.lis *.c.lis *.map *.sym *.o *.reloc
	rm -f $(SOURCES:.c=)
