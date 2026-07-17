# esxDOS sibling-NEX chaining demo (PR #15).
# Bare z88dk +zxn NEX programs demonstrating M_EXECCMD "run <name>.nex".
# Needs z88dk on PATH (zcc) + ZCCCFG set.
ZCC   ?= zcc
COPTS := +zxn -subtype=nex -clib=sdcc_iy -SO3 -compiler=sdcc -startup=31 -create-app
NEXDIR := ../../test/00regression/nex
PROGS := menu red blue

all: $(addsuffix .nex,$(PROGS))

%.nex: %.c
	$(ZCC) $(COPTS) $< -o $*

# Install the built NEX as regression fixtures (siblings — chaining resolves in-dir)
install: all
	cp $(addsuffix .nex,$(PROGS)) $(NEXDIR)/

clean:
	rm -f *.nex *.map *.lis zcc_opt.def
