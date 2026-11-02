# Prints every label a user (or ImportSymbolsScript.py) created, one per line:
#   JNEXT-ROW LABEL <name> <address>      e.g.  JNEXT-ROW LABEL banked P04::8000
# and a line `JNEXT-ROW DUMP <tag>` first (the script's one argument).
# GH #389, test/00regression/scripts/ghidra-foreign-func.sh. Runs under Ghidra's
# Jython extension. Part of jnext's test suite, GPLv3 like the rest of jnext.
# @runtime Jython
args = getScriptArgs()
print("JNEXT-ROW DUMP %s" % (args[0] if len(args) else ""))
for s in currentProgram.getSymbolTable().getAllSymbols(False):
    if s.getSource().toString() == "USER_DEFINED":
        print("JNEXT-ROW LABEL %s %s" % (s.getName(), s.getAddress()))
