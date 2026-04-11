# https://stackoverflow.com/a/30142139


# basename for binary
LIBNAME = nexlib.lib
# Put all auto generated stuff to this build dir.
BLDIR = ./build
# Path to CSPECT emulator
CSPECT = $(HOME)/z88dk/cspect/CSpect.exe

# Compiler options
C_OPTS := +zxn -SO3 -compiler=sdcc -clib=sdcc_iy --list --c-code-in-asm -I.

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
$(LIBNAME) : $(C_OBJS) $(ASM_OBJS)
	z88dk-z80asm -m=z80n $(C_OBJS) $(ASM_OBJS) -x$(LIBNAME)

all : $(LIBNAME)
	@du -b $(BLDIR)/*.bin $(BLDIR)/*.nex

re : clean all

run : all
	$(CSPECT) -w3 -vsync -zxnext -mmc=./assets/ $(LIBNAME)

rerun : clean run

.PHONY : clean
clean :
	rm -f $(BLDIR)/*.o $(LIBNAME)

cleanasm :
	rm -f $(BLDIR)/*.asm.o

cleanres :
	rm -f $(BLDIR)/palette_data.asm.o $(BLDIR)/sprite_defs.asm.o
	rm -f $(BLDIR)/tilemap_manager.asm.o $(BLDIR)/tileset.asm.o

