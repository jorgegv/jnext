# Obsolete documents

**Nothing in this directory is maintained, and nothing here describes JNEXT as
it is today.** These are documents that were superseded, describe something that
was removed or never built, or were background notes that the implementation
and the user and developer guides have since replaced. They are kept, not
deleted, so the history they record and the links into them stay readable.

The sub-directories mirror where each file used to live (`analysis/`,
`design/`); `git log --follow <file>` shows its full history.

| File | Why it is here |
|------|----------------|
| `nextzxos-boot-menu.png` | Orphaned screenshot. The regression index now shows `test/00regression/img/boot-nextzxos-menu-reference.png` instead. |
| `analysis/WAY-FORWARD-2026-03-31.md` | Strategy note of 2026-03-31, superseded by everything since; NextZXOS boots natively since 2026-07-10. |
| `analysis/SDCARD-IMAGE-BOOTING.md` | Early boot analysis. Its diagnosis (a missing `config.ini`) was not the boot blocker: the FAT32 cluster count and a bank-7 aliasing bug were. NextZXOS boots natively since 2026-07-10. |
| `design/FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` | Design for `--bypass-tbblue-fw`, which was built and then removed on 2026-07-11. Native boot is the only path. |
| `design/INTERNAL-Z80N-CORE-PLAN.md` | Plan for a VHDL-derived Z80N core, declined (WONT, Task 65, 2026-07-17). |
| `design/PROFILING-OPTIMIZATION-PLAN.md` | Self-declared obsolete; superseded by the Task 27 plan and reports in `doc/design/`. |
| `design/SCRIPTABLE-DEBUGGER.md` | DSL design of 2026-04-09, superseded by `doc/design/debug-subsystem/dsl-frontend.md` (epic #276). |
| `design/REQUIREMENTS-DATABASE.md` | Proposal (2026-04-20) for a requirements database, never implemented; the generated traceability matrix (GH #196) took its place. |
| `design/TASK89-DRAFT-ISSUE.md` | Draft of an issue that was filed as GH #31. |
| `design/LAYER-COMPOSITION-RESOLUTION.md` | Background note, not derived from the VHDL. The VHDL and the developer guide are the authority. |
| `design/MAGIC-BREAKPOINT.md` | Background note on `ED FF`; the feature shipped and is documented in the user guide. |
| `design/VIDEO-RECORDING.md` | Background note on FFmpeg capture; the recorder shipped and is documented in the user guide. |
