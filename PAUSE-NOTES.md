# PAUSE NOTES: gh304-bc-colour-width (GH #304 Parts B and C), NOT MERGED

Base: main @ 473ca0fcb (v1.1.14, Part A already in). Branch is ready for the owner's decision; nothing pushed.

## What is in it (commits, oldest first)

1. `c4d33eeb0` B fix: NR 0x4A fallback expands to 9 bits (blue LSB = B1|B0), `src/video/renderer.cpp` + `PaletteManager::rrrgggbb_to_rgb333` made public. New rows FB9-01/02.
2. `298528c23` **owner-gated**: LR-140, LR-127a expected value 0xFF240055 -> 0xFF24006D.
3. `7ab4a69b0` C fix: blend modes 6/7 and the stencil AND use 3 blue bits (`argb_b3`, clamp 7, `rgb333_to_argb8888`). New rows BLC-01..03.
4. `cd4d06e87` **owner-gated**: BL-10/16/20/21/22/40/42/52/60 and LMASK-C11 rewritten to palette-producible 3-bit-blue stimuli and VHDL expected words.
5. plan doc rows, regenerated traceability matrix and dashboard, manifest `compositor_test` 242 -> 247.

`rrrgggbb_to_argb` itself is untouched (debugger API, REQ-qt-27c).

## What the owner must decide

- Approve the 12 existing-row changes (commits 2 and 4; table in `/home/jorgegv/tmp/autorun/2026-10-09/gh304bc-report.md`).
- Approve regenerating ONE reference: `copper-demo` (40960 px, blue 0x55->0x6D and 0xAA->0xB6 only). No other screenshot row changes (regression 216 pass / 1 fail = copper-demo / 0 skip).
- Until the reference is regenerated, `make regression` FAILS on copper-demo by design.

## To land it once approved

    cd /home/jorgegv/tmp/worktrees/gh304bc
    LANG=C make gui-release
    bash test/00regression/generate-references.sh copper-demo
    git -C . diff --stat   # only test/00regression/img/copper-demo-reference.png
    git add test/00regression/img/copper-demo-reference.png && git commit -m "copper-demo reference: 9-bit fallback blue (GH #304 B)"
    LANG=C make regression   # must be 217/0/0 and stamp
    # then independent review, merge to main, bump-patch (CLAUDE.md protocol)

Suggested ChangeLog Bug Fixes line: "Fallback colour (NR 0x4A) and blend/stencil blue now use the hardware's 9-bit colour width."

## Evidence

`/home/jorgegv/tmp/autorun/2026-10-09/gh304bc-evidence/` : `copper-demo/` (reference | new | diff montage, histogram), `plotit/` (main vs branch, border #0000AA -> #0000B6).
Branch binary for GUI side-by-side with main's: `/home/jorgegv/tmp/worktrees/gh304bc/build/gui-release/jnext`.
