# z88dk `-debug` map fixture (GH #352)

Maps of one small program (`main.c`, `util.c`, `util.h`, `asmfn.asm`, all ours,
in this directory), built with z88dk commit `4d530b6eb779ad0f2a1d13c0bb670cf717477501`:

    zcc +zxn -clib=new -debug -m -s --list -subtype=nex main.c util.c asmfn.asm -o prog -create-app   # debug.map
    zcc +zxn -clib=new        -m -s --list -subtype=nex main.c util.c asmfn.asm -o prog -create-app   # nodebug.map

`prog.map` of each build was TRIMMED with `grep`, keeping the lines verbatim and
in their original order:

- `nodebug.map`: the six user symbols (`_asm_bump _counter _main _square _table _util_twice`).
- `debug.map`: the same six (their addresses differ, `-debug` links more code),
  the three symbols `-debug` itself adds to the link (`__debug_framepointer`,
  `l_debug_pop_frame`, `l_debug_push_frame`), the `__C_LINE_` / `__ASM_LINE_`
  line markers of `main.c`, `util.c`, `asmfn.asm`, the `__C_LINE_0_..stdint_2eh`
  marker, and all nine `__CDBINFO__` records.

Rows: `SYM-14`..`SYM-18` in `test/debug/debugger_backend_test.cpp`.
