# https://stackoverflow.com/a/30142139


# basename for binary
BASEFN = test03sprite
# Final binary
BIN = $(BASEFN).nex
# Put all auto generated stuff to this build dir.
BLDIR = ./build
# Path to CSPECT emulator
CSPECT = $(HOME)/z88dk/cspect/CSpect.exe

# Compiler options
C_OPTS := +zxn -SO3 -compiler=sdcc -clib=sdcc_iy --list --c-code-in-asm -I../nexlib
# Linker options
L_OPTS := +zxn -subtype=nex -v -O3 -m -s -clib=sdcc_iy -L../nexlib -lnexlib -startup=31 -create-app -pragma-include:zpragma.inc

# List of all .c source files.
C_SRCS := $(wildcard *.c)
# List of all .cpp source files.
ASM_SRCS := $(wildcard *.asm)

# All .c.o files go to build dir.
C_OBJS := $(C_SRCS:%.c=$(BLDIR)/%.c.o)
# All .c.o files go to build dir.
ASM_OBJS := $(ASM_SRCS:%.asm=$(BLDIR)/%.asm.o)

# Build target for every single object file.
$(BLDIR)/%.c.o : %.c
	zcc $(C_OPTS) -c $< -o $@

$(BLDIR)/%.asm.o : %.asm
	zcc $(C_OPTS) -c $< -o $@

# Actual target of the binary - depends on all .o files.
$(BLDIR)/$(BIN) : build_external $(C_OBJS) $(ASM_OBJS)
	zcc $(L_OPTS) $(C_OBJS) $(ASM_OBJS) -o $(BLDIR)/$(BASEFN)

build_external :
	make -C ../nexlib

all : $(BLDIR)/$(BIN)
	@du -b $(BLDIR)/*.bin $(BLDIR)/*.nex

re : clean all

run : all
	$(CSPECT) -w3 -vsync -zxnext -mmc=./assets/ $(BLDIR)/$(BIN)

rerun : clean run

.PHONY : clean
clean :
#	rm -f $(BLDIR)/$(BIN) $(BLDIR)/$(BASEFN).* $(C_OBJS) $(ASM_OBJS)
	rm -f $(BLDIR)/*.o $(BLDIR)/$(BASEFN)*

cleanasm :
	rm -f $(BLDIR)/*.asm.o

cleanres :
	rm -f $(BLDIR)/palette_data.asm.o $(BLDIR)/sprite_defs.asm.o
	rm -f $(BLDIR)/tilemap_manager.asm.o $(BLDIR)/tileset.asm.o

cleanlib :
	make -C ../nexlib clean

cleanall : clean cleanlib
