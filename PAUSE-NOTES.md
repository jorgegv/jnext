# GH #305 stage B: PAUSED for the owner's reference decision

Branch `gh305-ula-beam-pixels` (base main v1.1.15 = 23d27c794). Stage A (CPU write tags at any CPU
speed) is already on main. This branch is stage B: every byte the ULA fetches (pixels, attributes,
Timex second plane / alt screen, bank 7) is replayed at the beam instant the VHDL fetches it
(`zxula.vhd:270-303`). Not merged, not pushed.

## State
- Gate on the final tree: `make clean && make gui-release` ok; `make unit-test` 12449/12449, no
  DASHBOARD STALE; `make unit-test-sdl` 11550/11550; FUSE 1356/1356 (plain and PGO);
  `make regression`: 218 declared, 216 pass, **2 FAIL, 0 skip**: `nirvana-128k` (380 px = 95
  emulated px) and `nirvana-plus3` (540 px = 135 emulated px). No other row fails, including
  `boot-nextzxos-cpm` (the page-0x0B write the plan flagged is harmless). No stamp is written while
  those two fail.
- New rows: `ula_test` VMUX-01..12 (148), `mmu_test` G12-MUX-12/13 (261), functional
  `editmenu-beam-func` (it fails on main's binary with exactly the 624/254 px frames of stage A).

## What the owner must decide
Regenerate the two references or not. Evidence says the new output is right: FUSE (beam-accurate
for 128K/+3) agrees with the NEW output at 0 px on all 16 frames compared, and disagrees with the
OLD output (= the committed references) on the 5 frames where old and new differ.

## To accept (owner only; this is the single regeneration)
    cd /home/jorgegv/tmp/worktrees/gh305b
    bash test/00regression/generate-references.sh nirvana-128k nirvana-plus3
    # review `git diff --stat` (only those two PNGs), commit, then:
    LANG=C make regression        # expect 218/218 and a stamp
Then the branch is landable by the usual protocol (independent review, merge, `make bump-patch`).
The ChangeLog line is already in Unreleased. Draft close note is in the report.

## Evidence
`/home/jorgegv/tmp/autorun/2026-10-09/gh305b-evidence/` (README.md): per-row reference | new | diff
montages, zoom, FUSE frame and FUSE diffs, the 16-frame FUSE sweep with the SZX snapshots that
reproduce each frame, and the NextZXOS EDIT-menu before/after frames. Full report:
`/home/jorgegv/tmp/autorun/2026-10-09/gh305b-report.md`.

## Binary to run next to main's
`/home/jorgegv/tmp/worktrees/gh305b/build/gui-release/jnext` (PGO, this branch). Main's equivalent
non-PGO: `/home/jorgegv/tmp/worktrees/gh305b-base/build/gui-release-non-pgo/jnext` (scratch
worktree of 23d27c794; remove with `git worktree remove` when done).
