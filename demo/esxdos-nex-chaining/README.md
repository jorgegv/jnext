# esxDOS sibling-NEX chaining demo (PR #15)

Three bare z88dk `+zxn` NEX programs demonstrating esxDOS `M_EXECCMD "run <name>.nex"`
(the `--esxdos-stub` `.RUN` sibling-chaining path):

- `menu.nex` — white screen. Press **1** -> chain-load `red.nex`; **2** -> `blue.nex`.
- `red.nex`  — red screen.   Press **M** -> chain-load `menu.nex`.
- `blue.nex` — blue screen.  Press **M** -> chain-load `menu.nex`.

The only special part is the inline-asm esxDOS call: `ld ix,<cmd>` / `rst 8` / `defb 0x8f`.

## Build / install
    make            # build menu.nex red.nex blue.nex
    make install    # copy them into test/00regression/nex/ (regression fixtures)

## Run (GUI)
    jnext --machine next --sdcard <sdimage> --load menu.nex --esxdos-stub

The chaining is exercised headlessly by the `esxdos-chain-*-func` functional tests.
