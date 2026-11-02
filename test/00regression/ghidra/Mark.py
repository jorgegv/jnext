# Prints `JNEXT-ROW MARK <argument>`, to split one headless run's log into the
# part before and the part after it.
# GH #389, test/00regression/scripts/ghidra-foreign-func.sh. Runs under Ghidra's
# Jython extension. Part of jnext's test suite, GPLv3 like the rest of jnext.
# @runtime Jython
args = getScriptArgs()
print("JNEXT-ROW MARK %s" % (args[0] if len(args) else ""))
