# Makefile for JNEXT demo/test programs
# Delegates to per-demo subdirectory Makefiles

DEMOS = $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))

.PHONY: all nex tap clean $(DEMOS)

all: $(DEMOS)

$(DEMOS):
	$(MAKE) -C $@ all

nex:
	@for d in $(DEMOS); do $(MAKE) -C $$d nex; done

tap:
	@for d in $(DEMOS); do $(MAKE) -C $$d tap; done

clean:
	@for d in $(DEMOS); do $(MAKE) -C $$d clean; done
