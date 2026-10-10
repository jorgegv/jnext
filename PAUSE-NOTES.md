# PAUSE NOTES: gh304-bc-colour-width (GH #304 Parts B and C), NOT MERGED

Base: main @ 473ca0fcb (v1.1.14, Part A already in). Branch is ready for the owner's decision; nothing pushed.

## What is in it (commits, oldest first)

(Round 2 after review: see the end of this file.)

1. `c4d33eeb0` B fix: NR 0x4A fallback expands to 9 bits (blue LSB = B1|B0), `src/video/renderer.cpp` + `PaletteManager::rrrgggbb_to_rgb333` made public. New rows FB9-01/02.
2. `298528c23` **owner-gated**: LR-140, LR-127a expected value 0xFF240055 -> 0xFF24006D.
3. `7ab4a69b0` C fix: blend modes 6/7 and the stencil AND use 3 blue bits (`argb_b3`, clamp 7, `rgb333_to_argb8888`). New rows BLC-01..03.
4. `cd4d06e87` **owner-gated**: BL-10/16/20/21/22/40/42/52/60 and LMASK-C11 rewritten to palette-producible 3-bit-blue stimuli and VHDL expected words.
5. plan doc rows, regenerated traceability matrix and dashboard, manifest `compositor_test` 242 -> 247.

`rrrgggbb_to_argb` itself is untouched (debugger API, REQ-qt-27c).

## What the owner must decide

- Approve the existing-row changes: the "owner-gated:" commits (LR-140, LR-127a; BL-10/16/20/21/22/40/41/42/52/60, LMASK-C11; FB-10..12/17, LR-143/144, LMASK-C09-00 oracles). Full table with VHDL citations: `/home/jorgegv/tmp/autorun/2026-10-09/gh304bc-report.md` (Round 2 section for the added ones).
- Approve regenerating ONE reference: `copper-demo` (40960 px, blue 0x55->0x6D and 0xAA->0xB6 only). No other screenshot row changes (regression 216 pass / 1 fail = copper-demo / 0 skip).
- Until the reference is regenerated, `make regression` FAILS on copper-demo by design.

## To land it once approved

    cd /home/jorgegv/tmp/worktrees/gh304bc
    LANG=C make gui-release
    bash test/00regression/generate-references.sh copper-demo
    git status --short   # only test/00regression/img/copper-demo-reference.png
    git add test/00regression/img/copper-demo-reference.png && git commit -m "copper-demo reference: 9-bit fallback blue (GH #304 B)"
    git rm PAUSE-NOTES.md && git commit -m "remove PAUSE-NOTES"   # MUST happen before merge: main never carries it
    LANG=C make regression   # must be 217/0/0 and stamp
    # then independent review, merge to main, bump-patch (CLAUDE.md protocol)

Suggested ChangeLog Bug Fixes line: "Fallback colour (NR 0x4A) and blend/stencil blue now use the hardware's 9-bit colour width."

## Evidence

`/home/jorgegv/tmp/autorun/2026-10-09/gh304bc-evidence/` : `copper-demo/` (reference | new | diff montage, histogram), `plotit/` (main vs branch, border #0000AA -> #0000B6).
Branch binary for GUI side-by-side with main's: `/home/jorgegv/tmp/worktrees/gh304bc/build/gui-release/jnext`.

## Round 2 (review round 1 fixes)

- F1: `Renderer::fallback_to_argb` is the single NR 0x4A expansion; render_row and the debugger Background/ULA views use it. Rows INS-14-22/23 (debugger_backend_test, all 256 values; manifest 1444 -> 1446).
- F2/F5: BL-41 and BL-22 (now (3,4,5), its plan row) in an owner-gated commit.
- F3/F4: owner-gated commit for FB-10..12/17, LR-143/144, LMASK-C09-00 (oracles/stimuli to 3-bit blue / 9-bit fallback); BL-11 comment-only commit.
- F6: the removal step is in the landing list above.
