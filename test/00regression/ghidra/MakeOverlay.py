# Creates an overlay memory block in the default address space of the program
# (what the user does by hand before importing jnext's per-page symbol files):
#   MakeOverlay.py NAME START SIZE      e.g.  MakeOverlay.py P04 0x8000 0x2000
# GH #389, test/00regression/scripts/ghidra-foreign-func.sh. Runs under Ghidra's
# Jython extension. Part of jnext's test suite, GPLv3 like the rest of jnext.
# @runtime Jython
args = getScriptArgs()
name = args[0]
start = int(args[1], 0)
size = int(args[2], 0)
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
currentProgram.getMemory().createInitializedBlock(name, space.getAddress(start), size, 0, monitor, True)
print("JNEXT-ROW overlay %s created" % name)
