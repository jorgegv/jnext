# GH #345: paused at the plan (owner wind-down, 2026-10-30)

- State: plan written, GO conditional on Q1, nothing implemented. Branch = epic tip 6ed07e811 + this note.
- Plan: ~/tmp/autorun/jnext/2026-10-29/gh345-plan.md; evidence (z88dk-gdb sessions A-E, prototype diffs): ~/tmp/autorun/jnext/2026-10-29/gh345-evidence/. Scratch builds: ~/tmp/gh345-z88, ~/tmp/gh345-gdb.
- Owner questions:
  - Q1 (blocking): may SRCDBG-CS-08 change from "the esxdos shim leaves 1 frame" to "the shim pops the RST frame" (plan D5)? Existing row, expected value changes.
  - Q2 (non-blocking): report upstream to z88dk that newlib +zxn __banked with defaults calls page 0 and loses its stack, and that z88dk-gdb segfaults on a 24-bit __C_LINE_ map?
- Overlap with #375 stage 3 (parked plan): D3 reads a 24-bit __PAGE_n_head by its low 16 bits (z88dk_source_map.cpp:186-190); #375 s3 rewrites read_z88dk_map on the same parse helpers. Land one, then re-check the other plan.
- Row delta: source_debug_test 263->279, z88dk_map_test 28->29. #375 s3 also moves both: recount at merge.
- build/ was last built from the reverted prototype (first build recompiles 4 files).
- Resume: owner answers Q1, merge the epic tip, dispatch an implementer with the plan, then an independent reviewer. Delete this file in the first implementation commit.
