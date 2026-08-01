# Test Plan Traceability Matrix

> Updated 2026-04-20 from main at HEAD. This document is the canonical map from plan row → test ID → VHDL citation → test location for the 16 jnext subsystem unit test suites. See doc/testing/UNIT-TEST-PLAN-EXECUTION.md for the authoring process and doc/design/EMULATOR-DESIGN-PLAN.md §Phase 9 for the task tree.
>
> Hand-maintained rows (e.g. a cross-file pointer to a suite outside the section's own test source, like NR-C0-02) carry an explicit `<!-- protected: reason -->` HTML comment after the row's closing `|`; `test/refresh-traceability-matrix.pl` leaves such rows byte-identical on regen (GH #105).


## Summary

<!-- BEGIN GENERATED SUMMARY — written by test/refresh-traceability-matrix.pl; do not edit by hand -->
| Section                                    |  Rows | pass | fail | skip | missing | unrecorded |
|--------------------------------------------|------:|-----:|-----:|-----:|--------:|-----------:|
| Memory/MMU                                 |   201 |  172 |    0 |    0 |      29 |         63 |
| ULA Video                                  |   125 |  114 |    0 |    0 |      11 |          6 |
| Layer2                                     |   122 |  114 |    0 |    0 |       8 |         25 |
| Sprites                                    |   194 |  187 |    0 |    0 |       7 |         18 |
| Tilemap                                    |    69 |   59 |    0 |    0 |      10 |         13 |
| Copper                                     |    82 |   79 |    0 |    0 |       3 |          1 |
| Compositor                                 |   144 |  141 |    0 |    0 |       3 |         77 |
| Audio                                      |   208 |  185 |    0 |    0 |      23 |          9 |
| DMA                                        |   158 |  150 |    0 |    0 |       8 |          7 |
| DivMMC+SPI                                 |   127 |   99 |    0 |    0 |      28 |         47 |
| Multiface                                  |    55 |   55 |    0 |    0 |       0 |          0 |
| CTC+Interrupts                             |   180 |  149 |    0 |    0 |      31 |          0 |
| UART+I2C/RTC                               |   112 |  109 |    0 |    0 |       3 |          6 |
| NextREG                                    |   107 |   58 |    0 |    0 |      49 |          0 |
| IO Port Dispatch                           |    97 |   93 |    0 |    0 |       4 |         26 |
| Input                                      |   185 |  175 |    0 |    0 |      10 |        169 |
| Rewind                                     |    21 |    0 |    0 |    0 |      21 |          0 |
| Floating Bus                               |    27 |   25 |    0 |    0 |       2 |          6 |
| VideoTiming                                |    25 |   22 |    0 |    0 |       3 |         21 |
| Contention                                 |    76 |   70 |    0 |    0 |       6 |         57 |
| LoRes                                      |    48 |   48 |    0 |    0 |       0 |          0 |
| SD Card                                    |    21 |   16 |    0 |    0 |       5 |         34 |
| NMI Source Pipeline                        |    56 |   50 |    0 |    0 |       6 |          7 |
| CPU interrupt pulse                        |    11 |   11 |    0 |    0 |       0 |          0 |
| CPU/Z80N/IM2 regressions                   |    24 |   24 |    0 |    0 |       0 |          0 |
| ESP-01 socket transport                    |   130 |  128 |    0 |    2 |       0 |         17 |
| ESP-01 AT engine                           |   137 |  137 |    0 |    0 |       0 |         10 |
| ESP-01 jnext UART adapter                  |    30 |   30 |    0 |    0 |       0 |          0 |
| Companion: mmu_integration_test            |    59 |   59 |    0 |    0 |       0 |          0 |
| Companion: ula_integration_test            |     8 |    8 |    0 |    0 |       0 |          6 |
| Companion: compositor_integration_test     |     2 |    2 |    0 |    0 |       0 |          2 |
| Companion: copper_integration_test         |     3 |    3 |    0 |    0 |       0 |          4 |
| Companion: tilemap_fetch_split_test        |     4 |    4 |    0 |    0 |       0 |          0 |
| Companion: lores_integration_test          |     2 |    2 |    0 |    0 |       0 |          0 |
| Companion: ctc_interrupts_test             |    10 |   10 |    0 |    0 |       0 |         28 |
| Companion: nextreg_integration_test        |    80 |   80 |    0 |    0 |       0 |        224 |
| Companion: nmi_integration_test            |     9 |    9 |    0 |    0 |       0 |          0 |
| Companion: input_integration_test          |    17 |   16 |    0 |    0 |       1 |          1 |
| Companion: uart_integration_test           |    22 |   22 |    0 |    0 |       0 |          0 |
| **Total**                                  |  2988 | 2715 |    0 |    2 |     271 |        884 |

Rows the sections above carry: **2988**. Distinct row IDs recorded anywhere in this document (every table, including "Extra coverage"): **2972**. Rows the 90 suites declared in `test/unit-tests.conf` run live: **6566**.

The `Rows` column counts rows that publish a **`Status`**, so it equals pass+fail+skip+missing by construction. A further **85** rows live in the 4-column "Extra coverage (not in plan)" tables, which have no `Status` column: their `VHDL file:line` and `Test file:line` ARE recomputed on every run (they were not, for two years — GH #192), and a row asserted nowhere reads `missing` in the location column exactly as it would in a main table. A further **13** rows sit in **2** tables that carry neither column and are therefore not refreshed at all; each says so above itself.

**`missing`** = a row this document lists that its suite's test source no longer asserts. **`unrecorded`** = the reverse: a row the test source asserts that this document does not list **in the owning subsystem's section** — asked per section, not globally, so an ID string reused by another subsystem cannot vouch for it (GH #118). Both are real gaps; neither is auto-repaired, because the description that makes a row worth recording cannot be derived from the source (GH #117).

**One deliberate looseness remains, so treat this column as a floor.** *Sub-letter aliasing*: a source row `X-01b` counts as recorded by matrix row `X-01`, matching how the Status lookup resolves sub-rows. It is kept because `resolve_ids()` uses the same mapping in the other direction — drop it and row `X-01` would read `pass` *because* `X-01a` proves it while `X-01a` was reported as recorded nowhere. All 102 IDs it was hiding were triaged (GH #118): 90 are decompositions of their parent plan row, and 12 were distinct assertions that now have rows of their own — `NA-01b`, `NA-01c`, `NR-12a`, `NR-12b`, `HK-07b`, `MF-G162-01b`, `REG-01b`, `REG-02b`, `REG-03a/b/c`, `S5.10c` — joining the earlier `FB-04b`, `IORQ-02b` and `IORQ-02c`. The set is now printed on every run (the `ALIASED` report), so the next one that is not a sub-case is visible instead of inferred. The second looseness — *cross-section ID collision* — is closed: recording is asked against the owning `##` subsystem section rather than globally, which surfaced 29 rows that an identically-named row in a different subsystem had been vouching for (`SD-16..SD-23` by Audio, `PR-01..PR-05` by IO Port Dispatch, the `G108-*` set by ULA Video, `NR-10/11/13/14` + `PRI-01/02/04` by Audio and Memory/MMU, `SD2-01/02` by Memory/MMU). A `###` companion sub-section is judged against its parent `##`, not separately: its rows are part of the same subsystem's coverage story and several are recorded in the parent's own table (GH #118).

### Suites with no section here, and why

Every suite `test/unit-tests.conf` declares is accounted for: it is either traced by a section above or listed below with the authority it is actually written against. **Anything else is a hard failure** — `test/refresh-traceability-matrix.pl` refuses to run (exit 2) and rewrites nothing, in the manner of `test/run-unit-tests.sh` refusing when its manifest and CMake disagree. That refusal is the anti-drift mechanism: the traced-suite count sat at 28 for the whole v0.98 series while the manifest grew 49 → 80, because each of the ~31 additions arrived as one more name on a warning line that already listed fifty.

These 49 suites (2869 live rows) have no VHDL-derived plan row to map, so they have no section here. They are still declared, counted and run; their runtime view is `test/SUBSYSTEM-TESTS-STATUS.md`.

| Suite | Rows | Authority it is written against |
|-------|-----:|---------------------------------|
| `fuse_z80_test` | 1356 | data-driven FUSE runner, no per-row IDs |
| `z80n_test` | 85 | data-driven FUSE-style runner, opcode names not row IDs |
| `esxdos_stub_test` | 46 | esxDOS API surface + jnext trap policy, not core logic |
| `phantom_typist_test` | 22 | jnext auto-typing state machine (host keystroke injection) |
| `esp_wiring_test` | 60 | jnext host ESP policy/visibility/wiring, no core counterpart |
| `sd_rom_extractor_test` | 26 | FAT32 + TBBlue SD path layout (host ROM extraction) |
| `fat32_image_test` | 16 | FAT32 on-disk format (host image reader) |
| `sdcard_provisioner_test` | 57 | jnext SD-image download/patch policy (host side) |
| `audio_pacing_test` | 43 | host SDL audio pacing/underrun policy, downstream of the mixer |
| `audio_capture_test` | 17 | host WAV capture of the mixer output |
| `audio_gain_test` | 11 | host output-gain control (a user setting, not a core register) |
| `subsystem_gain_test` | 26 | host per-subsystem gain control (a user setting) |
| `present_cadence_test` | 34 | host present cadence policy (wall-clock, not core timing) |
| `render_policy_test` | 10 | host render/skip policy (wall-clock, not core timing) |
| `emulator_boot_test` | 31 | host cold-boot choreography (GH #40 contract, no VHDL oracle) |
| `preferences_apply_policy_test` | 20 | Preferences apply/revert policy (host GUI) |
| `window_attach_test` | 32 | host window-attach geometry (GH #39 contract, no VHDL oracle) |
| `pointer_capture_test` | 12 | host mouse-capture policy (window-manager behaviour) |
| `frame_deadline_test` | 38 | host frame-deadline scheduling (wall-clock) |
| `frame_sequencer_test` | 103 | host frame sequencer (wall-clock run/present ordering) |
| `tick_stats_test` | 32 | host tick accounting for the status bar |
| `speed_report_test` | 36 | host speed-percentage reporting |
| `host_key_latch_test` | 69 | host key latch/debounce compensation; guest matrix is `## Input` |
| `log_test` | 13 | jnext logging façade (spdlog wiring) |
| `log_gate_test` | 3 | jnext log-level gating |
| `cli_options_test` | 13 | CLI flag table vs the man page (see `make cli-check`) |
| `video_recorder_cmd_test` | 33 | FFmpeg command-line construction (host encoder) |
| `nex_loader_test` | 98 | NEX file-format spec (host loader), no core counterpart |
| `nex_v13_test` | 78 | NEX V1.3 file-format spec + nexload2.asm (host loader), no core counterpart |
| `extended_nex_test` | 28 | narrative section, ID ranges not per-row IDs |
| `atic_atac_nmi_test` | 4 | narrative section, hand-maintained (feeds protected NR-C0-02) |
| `profiler_test` | 32 | jnext profiler output format (a developer tool) |
| `resume_guard_test` | 11 | debugger resume-confirmation policy (jnext-internal) |
| `app_config_test` | 57 | jnext.conf schema/precedence (host settings file) |
| `audio_gain_config_test` | 22 | gain settings persistence (host settings file) |
| `audio_gain_preferences_test` | 10 | gain controls in the Preferences dialog (host GUI) |
| `present_count_test` | 17 | host present accounting (wall-clock, not core timing) |
| `esp_status_test` | 15 | host status-bar ESP indicator (GUI), no core counterpart |
| `esc_break_test` | 6 | host ESC->BREAK binding; guest matrix is `## Input` |
| `host_hotkey_test` | 33 | host hotkey bindings (Alt vs the guest Symbol Shift) |
| `shifted_keys_test` | 22 | host shifted-scancode translation; guest matrix is `## Input` |
| `quit_cleanup_test` | 7 | host shutdown ordering (GUI lifecycle) |
| `preferences_apply_test` | 40 | Preferences dialog wiring (host GUI) |
| `debugger_video_panel_test` | 92 | debugger panel RENDERING; the hardware it displays is traced in `## Compositor`/`## Layer2`/`## ULA Video` (GUI-gated build) |
| `debugger_audio_panel_test` | 15 | debugger panel RENDERING; the hardware it displays is traced in `## Audio` (GUI-gated build) |
| `debugger_quit_gate_test` | 5 | debugger quit gating (host GUI lifecycle) |
| `debugger_window_size_test` | 21 | debugger window geometry (host GUI) |
| `debugger_window_grow_test` | 4 | debugger window geometry (host GUI) |
| `debugger_accel_test` | 8 | debugger keyboard accelerators (host GUI) |

The runtime pass/fail view of all declared suites lives in `test/SUBSYSTEM-TESTS-STATUS.md` (`make unit-test-dashboard`), which is its canonical source; this table is the *document's own* view — what the matrix records and what it misses.
<!-- END GENERATED SUMMARY -->

This table is regenerated by `test/refresh-traceability-matrix.pl` from the same run that rewrites the rows below, and only between the markers above. It was hand-maintained until GH #117 and had drifted three months and ~2000 rows out of date; typing fresh numbers into it by hand only restarts that drift, so do not.

### The `VHDL file:line` column

Script-filled since 2026-07-20 (Task 78). Until then the script only rewrote
Status and Test `file:line`, so ~1600 rows showed `—` even though their
citation was sitting in the test source next to the assertion. The extractor
now recovers it from four **row-local** evidence tiers, in order:

1. the `check()`/`skip()` call carrying the row's own ID;
2. a comment block naming that ID;
3. the first `check()`/`skip()` after the ID literal — but **only when the row
   has no call of its own**, which is the table-driven signature (the ID sits
   in an initialiser and the shared assertion is in the loop below it);
4. the subsystem plan doc's row.

Tier 3's restriction is the load-bearing one. A row whose own `check()` simply
embeds no citation must stay uncited: the following call belongs to the *next*
row, and borrowing from it publishes a plausible-but-wrong citation that is
self-consistent on every later run, so it never surfaces as drift either.
Category banner comments and the nearest *unrelated* preceding comment are
rejected for the same reason — both were prototyped, and both attribute a
neighbour's VHDL lines to this row. An honest `—` beats a confident wrong
answer. `test/traceability-citations-selftest.pl` pins all of this, including
the refusals.

Every filled citation is validated against the real FPGA source tree, so a
typo'd or renamed VHDL filename is reported, not published.

A published citation may be SHORTER than its source. The line list stops at
the first interrupting prose, so a detail spelled `multiface.vhd:158 (clear),
:165 (eff)` publishes `:158` alone: reaching across `(clear)` means consuming
English, and that regex's failure mode is a confidently wrong citation, which
this document ranks strictly worse than an incomplete one. 13 cells are short
of their source this way today. None names a line the source does not, and each
names the row's primary evidence; the fix is to re-spell the detail as a plain
list (`:158,165`) in the test source and re-run.

Reading the column:

- `file.vhd:lines` — the row's VHDL basis.
- `—` — no row-local citation exists yet. A real, visible gap: the fix is to
  cite the VHDL in the test source (where it belongs), then re-run the script.
- `(jnext-internal)` / `(SD SPI spec)` — tombstones. These suites have no VHDL
  counterpart at all; their spec is a jnext contract (rewind/replay) or an
  external standard (SD SPI mode). Nothing to cite, permanently.

A cell that already held a hand-written citation is never overwritten. When it
disagrees with what the source says, the script reports the pair so the drift
is visible and can be adjudicated by hand.

## Z80N — `test/z80n_test.cpp`

Last-touch commit: `8d0cf05a15f77099a6a7ac35bcd5cc5ad223019f` (`8d0cf05a15`)

| Test ID  | Plan row title    | VHDL file:line | Status  | Test file:line |
|----------|-------------------|----------------|---------|----------------|
| ED 23    | ED 23 SWAPNIB     | —              | missing | missing        |
| ED 24    | ED 24 MIRROR A    | —              | missing | missing        |
| ED 27 nn | ED 27 nn TEST n   | —              | missing | missing        |
| ED 28    | ED 28 BSLA DE,B   | —              | missing | missing        |
| ED 29    | ED 29 BSRA DE,B   | —              | missing | missing        |
| ED 2A    | ED 2A BSRL DE,B   | —              | missing | missing        |
| ED 2B    | ED 2B BSRF DE,B   | —              | missing | missing        |
| ED 2C    | ED 2C BRLC DE,B   | —              | missing | missing        |
| ED 30    | ED 30 MUL DE      | —              | missing | missing        |
| ED 31    | ED 31 ADD HL,A    | —              | missing | missing        |
| ED 32    | ED 32 ADD DE,A    | —              | missing | missing        |
| ED 33    | ED 33 ADD BC,A    | —              | missing | missing        |
| ED 34    | ED 34 ADD HL,nn   | —              | missing | missing        |
| ED 35    | ED 35 ADD DE,nn   | —              | missing | missing        |
| ED 36    | ED 36 ADD BC,nn   | —              | missing | missing        |
| ED 8A    | ED 8A PUSH nn     | —              | missing | missing        |
| ED 90    | ED 90 OUTINB      | —              | missing | missing        |
| ED 91    | ED 91 NEXTREG n,v | —              | missing | missing        |
| ED 92    | ED 92 NEXTREG n,A | —              | missing | missing        |
| ED 93    | ED 93 PIXELDN     | —              | missing | missing        |
| ED 94    | ED 94 PIXELAD     | —              | missing | missing        |
| ED 95    | ED 95 SETAE       | —              | missing | missing        |
| ED 98    | ED 98 JP (C)      | —              | missing | missing        |
| ED A4    | ED A4 LDIX        | —              | missing | missing        |
| ED A5    | ED A5 LDWS        | —              | missing | missing        |
| ED AC    | ED AC LDDX        | —              | missing | missing        |
| ED B4    | ED B4 LDIRX       | —              | missing | missing        |
| ED B6    | ED B6 LDIRSCALE   | —              | missing | missing        |
| ED B7    | ED B7 LDPIRX      | —              | missing | missing        |
| ED BC    | ED BC LDDRX       | —              | missing | missing        |

## CPU/Z80N/IM2 regressions — `test/cpu/cpu_z80n_im2_regressions_test.cpp`

Discriminative regressions for the Z80N opcodes and the IM2 fabric, each added
alongside the fix it guards and each written to FAIL against the pre-fix
emulator. Not a plan-derived compliance suite: the `## Z80N` section above
records the FUSE-driven opcode sweep, and these are the individual behaviours
that sweep does not reach (IncDecZ latching, `BSLA`/`BSRA`/`BSRF` shift
saturation, OUTINB's extended M1, the IM2 daisy-chain's mode gating and RETI
teardown).

**24 of this suite's 52 rows are recorded below.** The other 28 do not carry a
row ID at all: their names embed a commit reference (`Z80N-TSTATES-MUL
(65b5918+86128d5)`) or are a sentence (`soft-reset-preserves-regfile: BC/DE/HL
survive`), so there is nothing to key a matrix row on. That is a real gap in
this suite, not a silent one — closing it means renaming those rows in the test
source, which is an edit for the branch that owns that file.

The `VHDL file:line` column is `—` throughout, and that too is recoverable
rather than absent: the file cites `t80n.vhd`, `t80n_mcode.vhd`,
`im2_control.vhd`, `im2_peripheral.vhd`, `im2_device.vhd` and `zxnext.vhd`
lines in the comment block above every row, but keys them to the SHORT plan ID
(`V17-Z80N-01a`) while the assertion uses the long one
(`V17-Z80N-01a-BSRF-UB-FREE-VHDL-1006-1014`). The extractor only trusts
row-local evidence that names the row's own ID — deliberately, because the
alternative is attributing a neighbour's citation — so the fix is to put the
citation in the `check()` call, exactly as `multiface_test.cpp` does.

| Test ID                                                  | Assertion description                                                                                        | VHDL file:line | Status  | Test file:line                                  |
|----------------------------------------------------------|--------------------------------------------------------------------------------------------------------------|----------------|---------|-------------------------------------------------|
| V11-CPU-01-IM2-DDFD-ED-NO-RETI                           | `DD ED 4D` must not pulse reti_seen: after S_DDFD_T4 any non-DD/FD byte returns the decoder to S_0           | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1494 |
| V11-CPU-02-Z80N-PIXELDN-BAND3-WRAP-PRESERVES-H-HIGH      | PIXELDN band-3 wrap preserves H[7:5] (the third-of-screen bits)                                              | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1558 |
| V12-CPU-NIT-02-Z80N-OUTINB-EXTENDED-M1-CONTEND-NO-MREQ   | OUTINB's extended 5T inner M1 emits per-T-state contend-no-MREQ on IR, not a bare `tstates += 1`             | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1716 |
| V13-CPU-01-Z80N-LDWS-INCDECZ-FROM-DJNZ-NOT-TAKEN         | DJNZ with B=1 (branch NOT taken) still updates the IncDecZ latch that LDWS reads as F.P                      | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1337 |
| V14-CPU-01-DEC-BC-UPDATES-INCDECZ-VHDL-1361              | plain `DEC BC` updates IncDecZ                                                                               | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1869 |
| V14-CPU-01-INC-BC-UPDATES-INCDECZ-VHDL-1361              | plain `INC BC` updates IncDecZ (DPair=00 satisfies the latch gate)                                           | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1811 |
| V14-CPU-01-INC-HL-MUST-NOT-UPDATE-INCDECZ                | negative case: `INC HL` (DPair!=00) must NOT update IncDecZ                                                  | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:1937 |
| V14-CPU-NIT-01-A-DD-INC-BC-UPDATES-INCDECZ-VHDL-1361     | DD-prefixed `INC BC` still updates IncDecZ (the prefix walk must not reclassify it)                          | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2024 |
| V14-CPU-NIT-01-B-FD-INC-BC-UPDATES-INCDECZ-VHDL-1361     | FD-prefixed `INC BC` still updates IncDecZ                                                                   | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2084 |
| V14-CPU-NIT-01-C-DD-DEC-BC-UPDATES-INCDECZ-VHDL-1361     | DD-prefixed `DEC BC` still updates IncDecZ                                                                   | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2132 |
| V14-CPU-NIT-01-D-FD-DEC-BC-UPDATES-INCDECZ-VHDL-1361     | FD-prefixed `DEC BC` still updates IncDecZ                                                                   | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2178 |
| V14-CPU-NIT-01-E-DD-DJNZ-UPDATES-INCDECZ-VHDL-1359       | DD-prefixed DJNZ still updates IncDecZ                                                                       | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2242 |
| V14-CPU-NIT-01-F-FD-DJNZ-UPDATES-INCDECZ-VHDL-1359       | FD-prefixed DJNZ still updates IncDecZ                                                                       | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2290 |
| V17-CPU-01-IM2-INT-REQ-HELD-IN-PULSE-MODE-VHDL-170       | im2_int_req is held at 0 while NR 0xC0 selects pulse mode, so no stale latch survives the switch to IM2 mode | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2358 |
| V17-CPU-NIT-04-BSRA-UB-FREE-VHDL-1006-1014               | BSRA with shift >= 16 replicates the sign bit across the whole result                                        | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2546 |
| V17-Z80N-01a-BSRF-UB-FREE-VHDL-1006-1014                 | BSRF with shift >= 16 fills with 1s (and does not invoke a C++ out-of-range shift)                           | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2419 |
| V17-Z80N-01b-BSLA-UB-FREE-VHDL-992                       | BSLA with shift >= 16 returns 0 (no bits left)                                                               | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2459 |
| V18R-CPU-02-DMA-RAISE-NO-POLLUTE-CTC7                    | raise(Im2Level::DMA) must not light CTC7's int_status — CTC4..7 i_int_req is hardwired to '0'              | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2650 |
| V18R-CPU-02-DMA-RAISE-NO-POLLUTE-ULA                     | the same DMA alias path must not pollute the ULA frame interrupt's status either                             | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2680 |
| V18R-CPU-NIT-01-LDPIRX-MEMPTR-LO-STROBE                  | LDPIRX strobes only MEMPTR-lo, matching the VHDL microcode's WZ write                                        | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2598 |
| V19-IM2-03-INT-UNQ-ONE-SHOT-AFTER-ISR                    | NR 0x20 int_unq is a ONE-cycle pulse: it must not re-arm im2_int_req on ticks after the ISR                  | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2772 |
| V19R-CPU-01-INT-REQ-PULSE-SYNTHESIS-MULTI-FRAME-VHDL-101 | a level-modelled raise_req() must still synthesise a one-cycle edge every frame, not only the first          | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2884 |
| V21-IM2-01-INT-LINE-GATED-ON-IM-MODE-VHDL-150-1974       | int_line_asserted()/ack_vector() gate on the Z80 being in IM 2, not only on NR 0xC0 bit 0                    | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:2982 |
| V22-IM2-01-ON-RETI-CLEARS-IM2-INT-REQ-LATCH-VHDL-175     | on_reti() clears im2_int_req in lock-step with the S_ISR->S_0 transition, never leaving it stale             | —            | pass    | test/cpu/cpu_z80n_im2_regressions_test.cpp:3169 |

## CPU interrupt pulse — `test/cpu/int_pulse_test.cpp`

The /INT pulse window in `Z80Cpu`: how long the interrupt line stays asserted
before an unaccepted request is discarded, and the fact that the width is
machine-dependent (32 T-states on 48K/+3, 36 on 128K/Pentagon/Next). The rows
bracket each boundary from both sides, cover the 4-T-state band where the two
widths disagree, and pin the setter/getter round-trip that the FUSE Z80 runner
depends on.

| Test ID                                          | Assertion description                                                              | VHDL file:line            | Status  | Test file:line                  |
|--------------------------------------------------|------------------------------------------------------------------------------------|---------------------------|---------|---------------------------------|
| INT-PULSE-128K-edge-32                           | 128K/Pent/Next (width=36): delta=32 must NOT discard                               | zxnext.vhd:2033,2014-2015 | pass    | test/cpu/int_pulse_test.cpp:152 |
| INT-PULSE-128K-past-33                           | 128K/Pent/Next (width=36): delta=33 must discard                                   | zxnext.vhd:2033,2014-2015 | pass    | test/cpu/int_pulse_test.cpp:155 |
| INT-PULSE-48K-edge-28                            | 48K/+3 (width=32): delta=28 must NOT discard /INT                                  | zxnext.vhd:2033,2014-2015 | pass    | test/cpu/int_pulse_test.cpp:139 |
| INT-PULSE-48K-past-29                            | 48K/+3 (width=32): delta=29 must discard /INT                                      | zxnext.vhd:2033,2014-2015 | pass    | test/cpu/int_pulse_test.cpp:142 |
| INT-PULSE-DELTA-DIVERGENCE-128K-live             | 128K/Pent/Next pulse must still be live at delta=32                                | zxnext.vhd:2033           | pass    | test/cpu/int_pulse_test.cpp:170 |
| INT-PULSE-DELTA-DIVERGENCE-48K-expires           | 48K/+3 pulse must expire across delta=29 boundary                                  | zxnext.vhd:2033           | pass    | test/cpu/int_pulse_test.cpp:167 |
| INT-PULSE-V18R-CPU-01-128K-pulse-expired-iff1-up | V18R-CPU-01: 128K/Pent/Next delta=34 must DROP at 2nd execute() even though IFF1=1 | zxnext.vhd:2017-2033      | pass    | test/cpu/int_pulse_test.cpp:196 |
| INT-PULSE-V18R-CPU-01-48K-pulse-expired-iff1-up  | V18R-CPU-01: 48K/+3 delta=30 must DROP at 2nd execute() even though IFF1=1         | zxnext.vhd:2017-2033      | pass    | test/cpu/int_pulse_test.cpp:191 |
| INT-PULSE-default-is-true                        | Z80Cpu default must keep machine_48_or_p3_=true (32-cycle width)                   | —                       | pass    | test/cpu/int_pulse_test.cpp:209 |
| INT-PULSE-setter-false                           | setter(false) must take effect                                                     | —                       | pass    | test/cpu/int_pulse_test.cpp:213 |
| INT-PULSE-setter-true                            | setter(true) must take effect                                                      | —                       | pass    | test/cpu/int_pulse_test.cpp:217 |

## Memory/MMU — `test/mmu/mmu_test.cpp`

Last-touch commit: `9fcc5802146a4e6a56bc2ad9abf19c0b202e680c` (`9fcc580214`)

| Test ID | Plan row title                                               | VHDL file:line  | Status  | Test file:line            |
|---------|--------------------------------------------------------------|-----------------|---------|---------------------------|
| MMU-01  | Write NR 0x50 = 0x00                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:202 |
| MMU-02  | Write NR 0x51 = 0x01                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:203 |
| MMU-03  | Write NR 0x52 = 0x04                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:204 |
| MMU-04  | Write NR 0x53 = 0x05                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:205 |
| MMU-05  | Write NR 0x54 = 0x0A                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:206 |
| MMU-06  | Write NR 0x55 = 0x0B                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:207 |
| MMU-07  | Write NR 0x56 = 0x0E                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:210 |
| MMU-08  | Write NR 0x57 = 0x0F                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:211 |
| MMU-09  | Write NR 0x50 = 0xFF                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:234 |
| MMU-10  | High page (NR 0x54 = 0x40)                                   | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:249 |
| MMU-11  | Max page (NR 0x54 = 0xDF)                                    | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:264 |
| MMU-12  | Page 0xE0 overflows to ROM                                   | zxnext.vhd:3060-3061 | pass    | test/mmu/mmu_test.cpp:287 |
| MMU-13  | Read-back NR 0x50-0x57                                       | zxnext.vhd:4880   | pass    | test/mmu/mmu_test.cpp:306 |
| MMU-14  | Write/read pattern all slots                                 | zxnext.vhd:4880   | pass    | test/mmu/mmu_test.cpp:324 |
| MMU-15  | Slot boundary (0x1FFF/0x2000)                                | zxnext.vhd:2952-2959 | pass    | test/mmu/mmu_test.cpp:342 |
| RST-01  | MMU0 after reset                                             | zxnext.vhd:4610-4618 | pass    | test/mmu/mmu_test.cpp:366 |
| RST-02  | MMU1 after reset                                             | zxnext.vhd:4610-4618 | pass    | test/mmu/mmu_test.cpp:367 |
| RST-03  | MMU2 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:368 |
| RST-04  | MMU3 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:369 |
| RST-05  | MMU4 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:370 |
| RST-06  | MMU5 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:371 |
| RST-07  | MMU6 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:372 |
| RST-08  | MMU7 after reset                                             | zxnext.vhd:4611-4618 | pass    | test/mmu/mmu_test.cpp:373 |
| P7F-01  | Bank 0 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:395 |
| P7F-02  | Bank 1 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:396 |
| P7F-03  | Bank 2 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:397 |
| P7F-04  | Bank 3 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:398 |
| P7F-05  | Bank 4 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:399 |
| P7F-06  | Bank 5 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:400 |
| P7F-07  | Bank 6 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:401 |
| P7F-08  | Bank 7 select                                                | zxnext.vhd:3640-3814 | pass    | test/mmu/mmu_test.cpp:402 |
| P7F-09  | ROM 0 select                                                 | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:423 |
| P7F-10  | ROM 1 select (bit 4)                                         | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:435 |
| P7F-11  | Shadow screen (bit 3)                                        | —               | missing | missing                   |
| P7F-12  | Lock bit (bit 5)                                             | zxnext.vhd:3814   | pass    | test/mmu/mmu_test.cpp:459 |
| P7F-13  | Locked write rejected                                        | zxnext.vhd:3814   | pass    | test/mmu/mmu_test.cpp:477 |
| P7F-14  | NR 0x08 bit 7 unlocks                                        | zxnext.vhd:3654 | pass    | test/mmu/mmu_test.cpp:505 |
| P7F-15  | Full register preserved                                      | zxnext.vhd:3640   | pass    | test/mmu/mmu_test.cpp:521 |
| DFF-01  | Extra bit 0                                                  | zxnext.vhd:3764,4679-4680 | pass    | test/mmu/mmu_test.cpp:618 |
| DFF-02  | Extra bit 1                                                  | zxnext.vhd:3764,4679-4680 | pass    | test/mmu/mmu_test.cpp:632 |
| DFF-03  | Extra bit 2                                                  | zxnext.vhd:3765,4679-4680 | pass    | test/mmu/mmu_test.cpp:646 |
| DFF-04  | Extra bit 3                                                  | zxnext.vhd:3766,4679-4680 | pass    | test/mmu/mmu_test.cpp:660 |
| DFF-05  | Max bank (DFFD=0x0F,7FFD=7)                                  | zxnext.vhd:3763-3766,4679-4680 | pass    | test/mmu/mmu_test.cpp:675 |
| DFF-06  | Locked by 7FFD bit 5                                         | zxnext.vhd:3691   | pass    | test/mmu/mmu_test.cpp:694 |
| DFF-07  | Bit 4 (Profi DFFD override)                                  | zxnext.vhd:3693,3797 | pass    | test/mmu/mmu_test.cpp:715 |
| DFF-08  | Soft reset preserves DFFD + MMU6/7                           | zxnext.vhd:3687 | pass    | test/mmu/mmu_test.cpp:750 |
| P1F-01  | ROM bank 0 (+3 mode)                                         | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:825 |
| P1F-02  | ROM bank 1 (+3 mode)                                         | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:837 |
| P1F-03  | ROM bank 2 (+3 mode)                                         | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:849 |
| P1F-04  | ROM bank 3 (+3 mode)                                         | zxnext.vhd:4619-4670 | pass    | test/mmu/mmu_test.cpp:861 |
| P1F-05  | Special mode enable                                          | zxnext.vhd:4623-4632 | pass    | test/mmu/mmu_test.cpp:882 |
| P1F-06  | Locked by 7FFD bit 5                                         | zxnext.vhd:3814   | pass    | test/mmu/mmu_test.cpp:896 |
| P1F-07  | Motor bit independent                                        | —               | missing | missing                   |
| SPE-01  | 00 (1FFD=0x01)                                               | zxnext.vhd:4623-4632 | pass    | test/mmu/mmu_test.cpp:926 |
| SPE-02  | 01 (1FFD=0x03)                                               | zxnext.vhd:4623-4632 | pass    | test/mmu/mmu_test.cpp:928 |
| SPE-03  | 10 (1FFD=0x05)                                               | zxnext.vhd:4623-4632 | pass    | test/mmu/mmu_test.cpp:930 |
| SPE-04  | 11 (1FFD=0x07)                                               | zxnext.vhd:4623-4632 | pass    | test/mmu/mmu_test.cpp:932 |
| SPE-05  | Exit special mode                                            | zxnext.vhd:4634   | pass    | test/mmu/mmu_test.cpp:962 |
| LCK-01  | 7FFD bit 5 locks 7FFD writes                                 | zxnext.vhd:3814   | pass    | test/mmu/mmu_test.cpp:983 |
| LCK-02  | 7FFD bit 5 locks 1FFD writes                                 | zxnext.vhd:3814   | pass    | test/mmu/mmu_test.cpp:996 |
| LCK-03  | 7FFD bit 5 locks DFFD writes                                 | zxnext.vhd:3691   | pass    | test/mmu/mmu_test.cpp:1017 |
| LCK-04  | NR 0x08 bit 7 clears lock                                    | zxnext.vhd:3654 | pass    | test/mmu/mmu_test.cpp:1041 |
| LCK-05  | Pentagon-1024 overrides lock                                 | zxnext.vhd:3769,3801 | pass    | test/mmu/mmu_test.cpp:1072 |
| LCK-06  | MMU writes bypass lock                                       | zxnext.vhd:4880   | pass    | test/mmu/mmu_test.cpp:1087 |
| LCK-07  | NR 0x8E bypasses lock                                        | zxnext.vhd:3662,3696,3726 | pass    | test/mmu/mmu_test.cpp:1114 |
| N8E-01  | Bank select (bit 3=1)                                        | zxnext.vhd:3662-3670,3696-3704 | pass    | test/mmu/mmu_test.cpp:1146 |
| N8E-02  | ROM select (bit 3=0, bit 2=0)                                | zxnext.vhd:3668-3670 | pass    | test/mmu/mmu_test.cpp:1167 |
| N8E-03  | Special mode via 8E                                          | zxnext.vhd:3734   | pass    | test/mmu/mmu_test.cpp:1187 |
| N8E-04  | Special + config bits                                        | zxnext.vhd:3732-3734 | pass    | test/mmu/mmu_test.cpp:1204 |
| N8E-05  | Read-back format                                             | zxnext.vhd:6158-6159 | pass    | test/mmu/mmu_test.cpp:1238 |
| N8E-06  | Bank select clears DFFD(3)                                   | zxnext.vhd:3698-3700 | pass    | test/mmu/mmu_test.cpp:1274 |
| N8F-01  | Standard mode (default)                                      | zxnext.vhd:3764-3766 | pass    | test/mmu/mmu_test.cpp:1304 |
| N8F-02  | Pentagon 512K                                                | zxnext.vhd:3764   | pass    | test/mmu/mmu_test.cpp:1324 |
| N8F-03  | Pentagon 1024K                                               | zxnext.vhd:3765,3801 | pass    | test/mmu/mmu_test.cpp:1346 |
| N8F-04  | Pentagon 1024K disabled by EFF7                              | zxnext.vhd:3798,3801 | pass    | test/mmu/mmu_test.cpp:1373 |
| N8F-05  | Pentagon bank(6) always 0                                    | zxnext.vhd:3766   | pass    | test/mmu/mmu_test.cpp:1396 |
| EF7-01  | Bit 3 = RAM at 0x0000                                        | zxnext.vhd:4636-4644 | pass    | test/mmu/mmu_test.cpp:1427 |
| EF7-02  | Bit 3 = 0 → ROM at 0x0000                                    | zxnext.vhd:4641-4644 | pass    | test/mmu/mmu_test.cpp:1450 |
| EF7-03  | Bit 2 = 1 disables Pent-1024                                 | zxnext.vhd:3781,3801 | pass    | test/mmu/mmu_test.cpp:1470 |
| EF7-04  | Reset state                                                  | zxnext.vhd:3777-3779 | pass    | test/mmu/mmu_test.cpp:1494 |
| EF7-05  | Soft reset preserves EFF7 + RAM-at-0                         | zxnext.vhd:3777 | pass    | test/mmu/mmu_test.cpp:1524 |
| ROM-01  | 48K always ROM 0                                             | zxnext.vhd:2984 | pass    | test/mmu/mmu_test.cpp:1559 |
| ROM-02  | 128K ROM 0                                                   | zxnext.vhd:3003 | pass    | test/mmu/mmu_test.cpp:1573 |
| ROM-03  | 128K ROM 1                                                   | zxnext.vhd:3003 | pass    | test/mmu/mmu_test.cpp:1585 |
| ROM-04  | +3 ROM 0                                                     | zxnext.vhd:2993 | pass    | test/mmu/mmu_test.cpp:1599 |
| ROM-05  | +3 ROM 1                                                     | zxnext.vhd:2993 | pass    | test/mmu/mmu_test.cpp:1612 |
| ROM-06  | +3 ROM 2                                                     | zxnext.vhd:2993 | pass    | test/mmu/mmu_test.cpp:1626 |
| ROM-07  | +3 ROM 3                                                     | zxnext.vhd:2993 | pass    | test/mmu/mmu_test.cpp:1639 |
| ROM-08  | ROM is read-only                                             | zxnext.vhd:2933-3133 | pass    | test/mmu/mmu_test.cpp:1660 |
| ROM-09  | ROM with altrom_rw = 1                                       | zxnext.vhd:3056,3078,3117 | pass    | test/mmu/mmu_test.cpp:1690 |
| ALT-01  | Enable altrom                                                | zxnext.vhd:2262 | pass    | test/mmu/mmu_test.cpp:2495 |
| ALT-02  | Disable altrom                                               | zxnext.vhd:2262 | pass    | test/mmu/mmu_test.cpp:2510 |
| ALT-03  | Altrom read/write enable                                     | zxnext.vhd:2263 | pass    | test/mmu/mmu_test.cpp:2521 |
| ALT-04  | Altrom read-only                                             | zxnext.vhd:2263 | pass    | test/mmu/mmu_test.cpp:2533 |
| ALT-05  | Lock ROM1                                                    | zxnext.vhd:2264 | pass    | test/mmu/mmu_test.cpp:2546 |
| ALT-06  | Lock ROM0                                                    | zxnext.vhd:2265 | pass    | test/mmu/mmu_test.cpp:2558 |
| ALT-07  | Reset preserves bits 3:0                                     | zxnext.vhd:2254 | pass    | test/mmu/mmu_test.cpp:2576 |
| ALT-08  | Altrom address 128K                                          | zxnext.vhd:2981-3001,3021,3078,3117 | pass    | test/mmu/mmu_test.cpp:2601 |
| ALT-09  | Read-back                                                    | zxnext.vhd:6156 | pass    | test/mmu/mmu_test.cpp:2616 |
| MMU-CFG-01 | Config mode maps ROMRAM, writeably                           | zxnext.vhd:3044-3050 | pass    | test/mmu/mmu_test.cpp:2649 |
| MMU-CFG-02 | Config mode read path returns SRAM bank contents             | zxnext.vhd:3044-3050 | pass    | test/mmu/mmu_test.cpp:2669 |
| MMU-CFG-03 | MMU-RAM mapping wins over config mode                        | zxnext.vhd:3037   | pass    | test/mmu/mmu_test.cpp:2690 |
| MMU-CFG-04 | Config mode off → normal ROM; ROM-slot writes drop           | zxnext.vhd:3044-3050 | pass    | test/mmu/mmu_test.cpp:2710 |
| ADR-01  | 0x00                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2922 |
| ADR-02  | 0x01                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2923 |
| ADR-03  | 0x0A                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2924 |
| ADR-04  | 0x0B                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2925 |
| ADR-05  | 0x0E                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2928 |
| ADR-06  | 0x10                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2929 |
| ADR-07  | 0x20                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2930 |
| ADR-08  | 0xDF                                                         | zxnext.vhd:2964   | pass    | test/mmu/mmu_test.cpp:2931 |
| ADR-09  | 0xE0                                                         | zxnext.vhd:3060-3061 | pass    | test/mmu/mmu_test.cpp:2959 |
| ADR-10  | 0xFF                                                         | zxnext.vhd:3060-3061 | pass    | test/mmu/mmu_test.cpp:2969 |
| BNK-01  | Page 0x0A → bank5 path                                       | zxnext.vhd:2961   | pass    | test/mmu/mmu_test.cpp:3007 |
| BNK-02  | Page 0x0B → bank5 path                                       | zxnext.vhd:2961   | pass    | test/mmu/mmu_test.cpp:3026 |
| BNK-03  | Page 0x0E → bank7 path                                       | zxnext.vhd:2962   | pass    | test/mmu/mmu_test.cpp:3050 |
| BNK-04  | Page 0x0F → normal SRAM                                      | zxnext.vhd:2961-2962 | pass    | test/mmu/mmu_test.cpp:3070 |
| BNK-05  | Bank5 read/write functional                                  | zxnext.vhd:2933-3133 | pass    | test/mmu/mmu_test.cpp:3087 |
| BNK-06  | Bank7 read/write functional                                  | zxnext.vhd:2933-3133 | pass    | test/mmu/mmu_test.cpp:3100 |
| CON-01  | 48K: bank 5 contended                                        | —               | missing | missing                   |
| CON-02  | 48K: bank 5 hi contended                                     | —               | missing | missing                   |
| CON-03  | 48K: bank 0 not contended                                    | —               | missing | missing                   |
| CON-04  | 48K: bank 7 not contended                                    | —               | missing | missing                   |
| CON-05  | 128K: odd banks contended                                    | —               | missing | missing                   |
| CON-06  | 128K: even banks not contended                               | —               | missing | missing                   |
| CON-07  | +3: banks >= 4 contended                                     | —               | missing | missing                   |
| CON-08  | +3: banks < 4 not contended                                  | —               | missing | missing                   |
| CON-09  | High page never contended                                    | —               | missing | missing                   |
| CON-10  | NR 0x08 bit 6 disables contention                            | —               | missing | missing                   |
| CON-11  | Speed > 3.5 MHz no contention                                | —               | missing | missing                   |
| CON-12  | Pentagon timing no contention (**RETIRED 2026-05-04** — standalone Pentagon machine type dropped, Wave 0.3; superseded in the plan doc by CON-12a/CON-12b, both also RETIRED; no `check()` row exists) | —               | missing | missing                   |
| L2M-01  | L2 write-over routes writes to L2 bank, not to unrelated MM… | zxnext.vhd:2969,3077 | pass    | test/mmu/mmu_test.cpp:3138 |
| L2M-01b | L2 bank 8 physically aliases MMU page 0x10 (hw collision)    | zxnext.vhd:2964 | pass    | test/mmu/mmu_test.cpp:3157 |
| L2M-02  | L2 read-enable maps 0-16K                                    | zxnext.vhd:2969,3077,3100 | pass    | test/mmu/mmu_test.cpp:3176 |
| L2M-03  | L2 auto segment follows A(15:14)                             | zxnext.vhd:3100-3107 | pass    | test/mmu/mmu_test.cpp:3219 |
| L2M-04  | L2 does NOT map 48K-64K                                      | zxnext.vhd:3077   | pass    | test/mmu/mmu_test.cpp:3237 |
| L2M-05  | L2 bank from NR 0x12                                         | —               | missing | missing                   |
| L2M-06  | L2 shadow bank from NR 0x13                                  | —               | missing | missing                   |
| PRI-01  | DivMMC ROM overrides MMU                                     | —               | missing | missing                   |
| PRI-02  | DivMMC RAM overrides MMU                                     | —               | missing | missing                   |
| PRI-03  | L2 overrides MMU in 0-16K                                    | zxnext.vhd:3077   | pass    | test/mmu/mmu_test.cpp:3296 |
| PRI-04  | L2 does not override DivMMC                                  | —               | missing | missing                   |
| PRI-05  | MMU page in upper 48K                                        | zxnext.vhd:2933-3133 | pass    | test/mmu/mmu_test.cpp:3317 |
| PRI-06  | Altrom overrides normal ROM                                  | zxnext.vhd:3078   | pass    | test/mmu/mmu_test.cpp:3342 |
| PRI-07  | Config mode overrides ROM                                    | zxnext.vhd:3044-3052 | pass    | test/mmu/mmu_test.cpp:3366 |
| P7F-16      | Shadow disables Timex `screen_mode`                                   | zxnext.vhd:3652,3768 | pass    | test/mmu/mmu_test.cpp:554  |
| P7F-17      | Bit 3 -> `Ula::set_shadow_screen_en` routing                          | zxnext.vhd:4453      | pass    | test/mmu/mmu_test.cpp:583  |
| DFF-09      | DFFD bit 6 round-trip via Multiface readback                          | zxnext.vhd:877,3694  | pass    | test/mmu/mmu_test.cpp:793  |
| EF7-06      | NR 0x85 b2 (`port_eff7_io_en`) gates EFF7 writes (G143 closed; RE-HOMED to mmu_integration_test; mapping corrected NR 0x84→0x85 2026-05-04) | zxnext.vhd:2604,2441,2392 | missing | missing                           |
| CON-12a     | Pentagon timing: machine type falls through switch (**RETIRED 2026-05-04** — standalone Pentagon type dropped, Wave 0.3; `ContentionModel` no longer exposes `pentagon_timing`; CT-GATE-01/07/08 + CT-M48-\*/CT-M128-\*/CT-MP3-\* cover 48K/128K/+3/Next) | —                    | missing | missing                    |
| CON-12b     | Pentagon timing: gate zeros 48K bank 5 contention (**RETIRED 2026-05-04** — same removal, Wave 0.3; CT-GATE-01/02/07/08 cover the surviving enable-gate terms) | —                    | missing | missing                    |
| L2M-02a     | L2 read-enable redirects 0x0000-0x3FFF reads to L2 bank               | zxnext.vhd:2969,3077 | pass    | test/mmu/mmu_test.cpp:3176 |
| L2M-02b     | L2 read-enable OFF -> MMU slot wins (discriminative)                  | zxnext.vhd:3077      | pass    | test/mmu/mmu_test.cpp:3195 |
| BOOT-OVL-01 | 8 KB boot ROM overlays full 16 KB at 0x0000-0x3FFF                    | zxnext.vhd:3199-3204 | pass    | test/mmu/mmu_test.cpp:3403 |
| BOOT-OVL-02 | Boot ROM does not leak past 0x3FFF                                    | zxnext.vhd:1856      | pass    | test/mmu/mmu_test.cpp:3424 |
| BOOT-OVL-03 | Wrong-sized boot ROM blob raises a diagnostic                         | zxnext.vhd:3199-3204 | pass    | test/mmu/mmu_test.cpp:3451 |
| SD2-01      | SD2-on suppresses 0xF1FD paging                                       | zxnext.vhd:2708      | missing | missing                    |
| SD2-02      | SD2-off lets 0xF1FD paging through                                    | —                    | missing | missing                    |
| BOOT-NEX-01 | Loader rejects NEX whose `ram_required` exceeds installed RAM         | —                    | pass    | test/mmu/mmu_test.cpp:3499 |
| BOOT-NEX-02 | Loader accepts NEX when `ram_required` <= installed RAM               | —                    | pass    | test/mmu/mmu_test.cpp:3520 |
| BOOT-NEX-03 | Per-bank loading bar rendered                                         | —                    | pass    | test/mmu/mmu_test.cpp:3567 |
| BOOT-NEX-04 | Inter-bank `loading_delay` honoured                                   | —                    | pass    | test/mmu/mmu_test.cpp:3613 |
| BOOT-NEX-05 | `start_delay` before code-entry                                       | —                    | pass    | test/mmu/mmu_test.cpp:3629 |
| BOOT-NEX-06 | Loading-bar colour honoured                                           | —                    | pass    | test/mmu/mmu_test.cpp:3597 |
| BOOT-NEX-07 | NEX loader writes to physical bank 5 do NOT leak ULA attributes       | —                    | pass    | test/mmu/mmu_test.cpp:3709 |
| BOOT-SD-01  | mount -> unmount -> re-mount round-trip                               | —                    | missing | missing                    |
| BOOT-SD-02  | unmount mid-transfer is safe                                          | —                    | missing | missing                    |
| ROM-10           | 48K-mode sram_rom3 hardwire path untested                        | zxnext.vhd:2985      | pass    | test/mmu/mmu_test.cpp:1721 |
| ROM-11           | NR 0x8C altrom factor in current_rom_bank                        | zxnext.vhd:3138      | pass    | test/mmu/mmu_test.cpp:1746 |
| ROM-12           | port_1ffd b2 spurious ROM3 in Next mode                          | zxnext.vhd:3814      | pass    | test/mmu/mmu_test.cpp:1794 |
| G12-MUX-01       | Ram::set_write_observer hook not implemented (Nirvana)           | —                    | pass    | test/mmu/mmu_test.cpp:2025 |
| G12-MUX-02       | Per-write callback signature absent                              | —                    | pass    | test/mmu/mmu_test.cpp:2052 |
| G12-MUX-03       | ULA mid-row recolour mux unwired                                 | zxula.vhd:192          | pass    | test/mmu/mmu_test.cpp:2106 |
| SHA-01           | NR 0x69 b6 alias to shadow-screen unwired                        | zxnext.vhd:3622      | pass    | test/mmu/mmu_test.cpp:2389 |
| SHA-02           | Shadow read-side bank 7 routing untested                         | zxnext.vhd:3653,3658   | pass    | test/mmu/mmu_test.cpp:2415 |
| SHA-03           | Shadow + Timex cross-state discriminative                        | zxnext.vhd:3763-3766,4453 | pass    | test/mmu/mmu_test.cpp:2457 |
| BOOT-TAPESAVE-01 | ROM SAVE captures EAR pulses to .tap                             | —                    | pass    | test/mmu/mmu_test.cpp:3770 |
| BOOT-TAPESAVE-02 | ROM SAVE captures EAR pulses to .tzx                             | —                    | pass    | test/mmu/mmu_test.cpp:3839 |
| BOOT-TAPESAVE-03 | ROM SAVE captures EAR pulses to .wav (PCM)                       | —                    | pass    | test/mmu/mmu_test.cpp:3887 |
| BOOT-Z80-01      | v1 (uncompressed) .z80 round-trip                                | —                    | pass    | test/mmu/mmu_test.cpp:3954 |
| BOOT-Z80-02      | v2 (RLE-compressed) .z80 round-trip                              | —                    | pass    | test/mmu/mmu_test.cpp:4027 |
| BOOT-Z80-03      | v3 (extended-header, 128K) .z80                                  | —                    | pass    | test/mmu/mmu_test.cpp:4098 |
| BOOT-Z80-04      | Unsupported / corrupt .z80 file rejected                         | —                    | pass    | test/mmu/mmu_test.cpp:4135 |
| BOOT-Z80-05      | Structurally-valid .z80 with only foreign page numbers rejected  | —                    | pass    | test/mmu/mmu_test.cpp:4184 |
| BOOT-SNAPSAVE-02 | `.szx` save round-trip via GUI/CLI                               | —                    | pass    | test/mmu/mmu_test.cpp:4329 |
| BOOT-SNAPSAVE-03 | `.nex` save round-trip via GUI/CLI                               | —                    | pass    | test/mmu/mmu_test.cpp:4552 |
| BOOT-DECI-01     | TZX 0x15 Direct-Recording block decoded                          | —                    | pass    | test/mmu/mmu_test.cpp:4667 |
| BOOT-DECI-02     | TZX 0x15 unknown / malformed block tolerated                     | —                    | pass    | test/mmu/mmu_test.cpp:4760 |
| BOOT-DECI-03     | WAV real-time DeciLoad loads via custom loader                   | —                    | pass    | test/mmu/mmu_test.cpp:4815 |
| BOOT-DECI-04     | WAV resampling preserves pulse-edge timing within tolerance      | —                    | pass    | test/mmu/mmu_test.cpp:4871 |
| BOOT-FDC-01      | `.dsk` (CPCEMU/EDSK) image mounted on +3 drive                   | —                    | missing | missing                    |
| BOOT-FDC-02      | uPD765 motor-on / read-id behaviour                              | —                    | missing | missing                    |
| BOOT-FDC-03      | NR 0x81 b3 (`fdc` clken) gates motor-on                          | NR 0x81 b3           | missing | missing                    |
| MMU-CFG-05 | Addr bit 13 picks upper/lower 8 KB of the NR 0x04 bank           | zxnext.vhd:5060      | pass    | test/mmu/mmu_test.cpp:2729 |
| MMU-CFG-06 | Mmu::reset() PRESERVES the NR 0x04 bank; config_mode untouched   | zxnext.vhd:4930-5111 | pass | test/mmu/mmu_test.cpp:2765 |
| MMU-CFG-07 | Out-of-range NR 0x04 bank: read 0xFF, write dropped              | zxnext.vhd:1126      | pass    | test/mmu/mmu_test.cpp:2818 |
| CFG-08           | config_mode / NR 0x04 setters toggle drop vs route               | zxnext.vhd:1127      | pass    | test/mmu/mmu_test.cpp:2841 |
| CFG-09           | rom_in_sram=1 routes ROM-slot reads to SRAM pages 0-7            | zxnext.vhd:5099      | pass    | test/mmu/mmu_test.cpp:2864 |
| CFG-10           | rom_in_sram + config_mode=0: ROM-slot writes still drop          | zxnext.vhd:5100      | pass    | test/mmu/mmu_test.cpp:2883 |
| CFG-11           | set_rom_in_sram 1->0 restores ROM-slot reads to rom_             | zxnext.vhd:5050-5057 | pass    | test/mmu/mmu_test.cpp:2903 |
| CFG-12           | Mmu::reset(hard=false) also preserves the NR 0x04 bank           | zxnext_top_issue2.vhd:840, zxnext.vhd:1730 | pass | test/mmu/mmu_test.cpp:2795 |

### Extra coverage (not in plan)

| Test ID | Assertion description                       | VHDL file:line | Test file:line            |
|---------|---------------------------------------------|----------------|---------------------------|
| RST-09  | MMU0 is ROM after reset                     | —              | missing                   |
| RST-10  | MMU1 is ROM after reset                     | —              | missing                   |
| RW-01   | Write 0x42 to 0x8000 (page 0x10), read back | —              | missing                   |
| RW-02   | Independent writes to two slots             | —              | missing                   |
| RW-03   | Same page in two slots shares data          | —              | missing                   |
| RW-04   | Write across slot 4/5 boundary              | —              | missing                   |
| RW-05   | All 8 slots independently writable          | —              | missing                   |

### Companion integration suite — `test/mmu/mmu_integration_test.cpp`

Memory/MMU rows that need a whole `Emulator` rather than a bare `Mmu`: the
NR-fan-out paths (`V12-MEM-*`, `V13-MEM-*`), the `0xEFF7` port gate, the
machine-type default, the Multiface SRAM window seen from the memory side
(`MF-SRAM-*`), the boot-hold frame counter (`G156-HOLD-*`), the SA-BYTES tape
trap gate (`MMU-G33-TRAP-*`), the live machine switch (`SWITCH-*`) and the
snapshot save round-trips (`SNAPSAVE-*`).

The `SNAPSAVE-*` and `G156-HOLD-*` rows read `—` in the VHDL column and always
will: a .szx/.nex round-trip is a file-format contract and the boot hold is a
jnext-internal host-side delay, neither of which the FPGA core contains.

| Test ID                             | Assertion description                                                                                                                  | VHDL file:line                 | Status  | Test file:line                         |
|-------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------|--------------------------------|---------|----------------------------------------|
| G156-HOLD-01                        | boot_hold_frames_remaining() reflects set_boot_hold_frames()                                                                           | —                            | pass    | test/mmu/mmu_integration_test.cpp:811  |
| G156-HOLD-02                        | boot_hold_frames_remaining() decrements by exactly 1 per run_frame()                                                                   | —                            | pass    | test/mmu/mmu_integration_test.cpp:818  |
| G156-HOLD-03                        | boot_hold_frames_remaining() reaches exactly 0 after the full hold count of run_frame() calls                                          | —                            | pass    | test/mmu/mmu_integration_test.cpp:828  |
| G156-HOLD-04                        | PC and R are frozen across every held frame (no instruction executed while boot_hold_frames_remaining_ > 0)                            | —                            | pass    | test/mmu/mmu_integration_test.cpp:841  |
| G156-HOLD-05                        | CPU resumes real execution once the hold ends — PC/R change over post-hold frames (the hold is not permanent)                        | —                            | pass    | test/mmu/mmu_integration_test.cpp:854  |
| G156-HOLD-06                        | pre-save remaining is genuinely mid-hold (neither the initial value nor zero)                                                          | —                            | pass    | test/mmu/mmu_integration_test.cpp:875  |
| G156-HOLD-07                        | save_state()/load_state() round-trip preserves boot_hold_frames_remaining_ exactly                                                     | —                            | pass    | test/mmu/mmu_integration_test.cpp:902  |
| G156-HOLD-08                        | the restored hold correctly resumes: exactly the restored remaining count of run_frame() calls exhausts it to 0                        | —                            | pass    | test/mmu/mmu_integration_test.cpp:910  |
| G156-HOLD-09                        | PC/R stayed frozen for the entire restored hold — no instruction executed while resuming a mid-hold snapshot                         | —                            | pass    | test/mmu/mmu_integration_test.cpp:917  |
| MF-SRAM-01                          | Next MF window reads external SRAM pages 0x0A (ROM half) / 0x0B (RAM half) per VHDL :3029-3036                                         | —                            | pass    | test/mmu/mmu_integration_test.cpp:756  |
| MF-SRAM-02                          | Next MF RAM half writes reach SRAM page 0x0B; ROM half is read-only (page 0x0A unchanged)                                              | —                            | pass    | test/mmu/mmu_integration_test.cpp:761  |
| MF-SRAM-03                          | standalone (128K) MF window is unaffected by SRAM pages 0x0A/0x0B — reads the private buffer, not page 0x0A                          | —                            | pass    | test/mmu/mmu_integration_test.cpp:787  |
| MF-SRAM-04                          | standalone (128K) MF RAM write stays in the private buffer, does NOT reach SRAM page 0x0B                                              | —                            | pass    | test/mmu/mmu_integration_test.cpp:792  |
| MMU-EF7-IO-EN-00                    | baseline: gate-open + EFF7=0x00 clears disable_p1024 + ram_at_0000                                                                     | zxnext.vhd:3780-3782           | pass    | test/mmu/mmu_integration_test.cpp:160  |
| MMU-EF7-IO-EN-01                    | NR 0x85 b2=0 — write 0x0C to 0xEFF7 dropped                                                                                          | zxnext.vhd:2604                | pass    | test/mmu/mmu_integration_test.cpp:177  |
| MMU-EF7-IO-EN-02                    | NR 0x85 b2=1 — write 0x0C to 0xEFF7 sets disable_p1024 + ram_at_0000                                                                 | zxnext.vhd:2604                | pass    | test/mmu/mmu_integration_test.cpp:194  |
| MMU-G33-TRAP-01                     | handle_sa_bytes_trap: A/IX/DE -> TAP block on file; exit PC=popped ret, SP+=2, IX+=DE, DE=0, carry set                                 | —                            | pass    | test/mmu/mmu_integration_test.cpp:1410 |
| MMU-G33-TRAP-02                     | run_frame gate positive: SA-BYTES signature + PC=0x04C2 + armed saver -> trap fires once, CPU parked at return                         | —                            | pass    | test/mmu/mmu_integration_test.cpp:1445 |
| MMU-G33-TRAP-03                     | run_frame gate negative: non-48K ROM bytes at 0x04C2 -> trap does NOT fire (the ungated trap corrupted a NextZXOS boot)                | —                            | pass    | test/mmu/mmu_integration_test.cpp:1476 |
| MT-DEF-01                           | Next (ZXN_ISSUE2) cold-boot NR $03 machine-type = 011 (+3), the signal initialiser's power-on value                                    | zxnext.vhd:1103                | pass    | test/mmu/mmu_integration_test.cpp:692  |
| MT-DEF-02                           | +3 (ZX_PLUS3) cold-boot NR $03 machine-type = 011 (+3)                                                                                 | —                            | pass    | test/mmu/mmu_integration_test.cpp:704  |
| SNAPSAVE-NEX-RT-00                  | NexSaver::save() returns a non-empty buffer                                                                                            | —                            | pass    | test/mmu/mmu_integration_test.cpp:1271 |
| SNAPSAVE-NEX-RT-01                  | saved .nex bytes written to disk                                                                                                       | —                            | pass    | test/mmu/mmu_integration_test.cpp:1276 |
| SNAPSAVE-NEX-RT-02                  | Emulator::load_nex() accepts the saved file                                                                                            | —                            | pass    | test/mmu/mmu_integration_test.cpp:1287 |
| SNAPSAVE-NEX-RT-BORDER              | border colour round-trips via the .nex header                                                                                          | —                            | pass    | test/mmu/mmu_integration_test.cpp:1313 |
| SNAPSAVE-NEX-RT-ENTRYBANK           | entry_bank re-establishes the CPU-executable mapping at 0xC000-0xFFFF (MMU slots 6/7) in the freshly loaded Emulator                   | —                            | pass    | test/mmu/mmu_integration_test.cpp:1318 |
| SNAPSAVE-NEX-RT-PCSP                | PC/SP round-trip through save()->file->Emulator::load_nex() (the only two registers NEX's header carries)                              | —                            | pass    | test/mmu/mmu_integration_test.cpp:1294 |
| SNAPSAVE-NEX-RT-RAM                 | bank-20 (pages 40/41) content round-trips byte-for-byte through the .nex bank payload                                                  | —                            | pass    | test/mmu/mmu_integration_test.cpp:1307 |
| SNAPSAVE-SZX-RT-00                  | SzxSaver::save() returns a non-empty buffer and reports success for a supported machine (+3)                                           | —                            | pass    | test/mmu/mmu_integration_test.cpp:1042 |
| SNAPSAVE-SZX-RT-01                  | saved .szx bytes written to disk                                                                                                       | —                            | pass    | test/mmu/mmu_integration_test.cpp:1048 |
| SNAPSAVE-SZX-RT-02                  | Emulator::load_szx() accepts the saved file                                                                                            | —                            | pass    | test/mmu/mmu_integration_test.cpp:1059 |
| SNAPSAVE-SZX-RT-48K-00              | SzxSaver::save() succeeds for 48K                                                                                                      | —                            | pass    | test/mmu/mmu_integration_test.cpp:1164 |
| SNAPSAVE-SZX-RT-48K-01              | saved 48K .szx bytes written to disk                                                                                                   | —                            | pass    | test/mmu/mmu_integration_test.cpp:1185 |
| SNAPSAVE-SZX-RT-48K-02              | Emulator::load_szx() accepts the saved 48K file                                                                                        | —                            | pass    | test/mmu/mmu_integration_test.cpp:1196 |
| SNAPSAVE-SZX-RT-48K-BANK1-UNTOUCHED | bank 1 (not part of a 48K's RAM) is never written by load_szx() — reads back as reset()'s zero fill                                  | —                            | pass    | test/mmu/mmu_integration_test.cpp:1233 |
| SNAPSAVE-SZX-RT-48K-BORDER          | border colour round-trips via ZXSTSPECREGS.chFe for 48K                                                                                | —                            | pass    | test/mmu/mmu_integration_test.cpp:1240 |
| SNAPSAVE-SZX-RT-48K-PAGESET         | the SAVED FILE's ZXSTRAMPAGE chPageNo set is exactly {0,2,5} — independently scanned from raw bytes, not via SzxLoader               | —                            | pass    | test/mmu/mmu_integration_test.cpp:1173 |
| SNAPSAVE-SZX-RT-48K-RAM             | banks 0/2/5 (48K's real RAM) round-trip byte-for-byte via ZXSTRAMPAGE                                                                  | —                            | pass    | test/mmu/mmu_integration_test.cpp:1215 |
| SNAPSAVE-SZX-RT-48K-REGS            | register set round-trips through save()->file->Emulator::load_szx() for 48K                                                            | —                            | pass    | test/mmu/mmu_integration_test.cpp:1202 |
| SNAPSAVE-SZX-RT-BORDER              | border colour round-trips via ZXSTSPECREGS.chFe                                                                                        | —                            | pass    | test/mmu/mmu_integration_test.cpp:1098 |
| SNAPSAVE-SZX-RT-PAGING              | classic paging ports (0x7FFD/0x1FFD) round-trip via ZXSTSPECREGS                                                                       | —                            | pass    | test/mmu/mmu_integration_test.cpp:1076 |
| SNAPSAVE-SZX-RT-RAM                 | all 8 physical RAM banks (0-7) round-trip byte-for-byte via ZXSTRAMPAGE — a +3 save now carries its full RAM, not a truncated subset | —                            | pass    | test/mmu/mmu_integration_test.cpp:1091 |
| SNAPSAVE-SZX-RT-REFUSED             | SzxSaver::save() refuses outright for a Next machine: ok=false, no data written, a non-empty error explaining why                      | —                            | pass    | test/mmu/mmu_integration_test.cpp:1122 |
| SNAPSAVE-SZX-RT-REGS                | full register set (both AF/BC/DE/HL sets, IX/IY/SP/PC, I/R/IFF/IM/halted) round-trips through save()->file->Emulator::load_szx()       | —                            | pass    | test/mmu/mmu_integration_test.cpp:1068 |
| SWITCH-01                           | live Next→128K machine switch clears Mmu::rom_in_sram                                                                                | —                            | pass    | test/mmu/mmu_integration_test.cpp:647  |
| SWITCH-02                           | post-switch standalone bank-7 writes land in flat RAM, not the Next-only BRAM buffer                                                   | —                            | pass    | test/mmu/mmu_integration_test.cpp:659  |
| V12-MEM-01-A                        | NR 0x50 read-back returns verbatim 0xE5 after high-page write                                                                          | zxnext.vhd:4686-4699,6059-6060 | pass    | test/mmu/mmu_integration_test.cpp:237  |
| V12-MEM-01-B                        | NR 0x8C write does NOT clobber NR 0x50 verbatim value                                                                                  | zxnext.vhd:3813                | pass    | test/mmu/mmu_integration_test.cpp:249  |
| V12-MEM-02-A                        | NR 0x08 bit 6 (contention_disable) reads back 1 after write+commit                                                                     | zxnext.vhd:5176,5800-5823,5906 | pass    | test/mmu/mmu_integration_test.cpp:306  |
| V12-MEM-02-B                        | ContentionModel.contention_disable() is true post-commit on a live emulator                                                            | zxnext.vhd:5822-5823           | pass    | test/mmu/mmu_integration_test.cpp:314  |
| V12-MEM-02-C                        | NR 0x08 bit 6 survives save/load round-trip [ContentionModel re-sync from Mmu.contention_disabled() in load_state]                     | —                            | pass    | test/mmu/mmu_integration_test.cpp:351  |
| V12-MEM-02-D                        | ContentionModel.contention_disable() (effective) is true post-load                                                                     | zxnext.vhd:5823                | pass    | test/mmu/mmu_integration_test.cpp:361  |
| V12-MEM-03-A                        | Mmu.machine_type() round-trips ZX48K through save/load                                                                                 | —                            | pass    | test/mmu/mmu_integration_test.cpp:459  |
| V12-MEM-03-B                        | ContentionModel.type_ tracks Mmu.machine_type() across load_state — ZX48K + page=0x0A (bank 5) contends                              | zxnext.vhd:4490                | pass    | test/mmu/mmu_integration_test.cpp:485  |
| V13-MEM-01-A                        | Baseline port 0x123B bit 1 = 0 after clearing both NR 0x69 and port 0x123B                                                             | zxnext.vhd:3933                | pass    | test/mmu/mmu_integration_test.cpp:545  |
| V13-MEM-01-B                        | NR 0x69 bit 7 = 1 fans out into port 0x123B bit 1 = 1                                                                                  | zxnext.vhd:3924-3925           | pass    | test/mmu/mmu_integration_test.cpp:556  |
| V13-MEM-01-C                        | NR 0x69 bit 7 read-back = 1 after NR 0x69 = 0x80 write (Layer2 mirror — pre-fix path, regression guard)                              | zxnext.vhd:6095-6096           | pass    | test/mmu/mmu_integration_test.cpp:567  |
| V13-MEM-01-D                        | NR 0x69 bit 7 = 0 clears port 0x123B bit 1 (sweep guard — fix must not be a one-shot raise)                                          | zxnext.vhd:3924-3925           | pass    | test/mmu/mmu_integration_test.cpp:578  |
| V13-MEM-01-E                        | NR 0x69 fan-out only touches port 0x123B bit 1 (other bits unchanged)                                                                  | zxnext.vhd:3924-3925           | pass    | test/mmu/mmu_integration_test.cpp:607  |

## ULA Video — `test/ula/ula_test.cpp` + `test/ula/ula_integration_test.cpp`

Last-touch commit: HEAD (Phase 4 dashboard refresh 2026-04-23; Phase 3 merge at `94ccaf3`)

Task 3 SKIP-reduction plan (`doc/design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md`) landed 2026-04-23 Phase 0 → 4 (final state after post-closure walkback + NR 0x68 bit 3 follow-up). `ula_test.cpp` moved from `123/48/0/75` to `110/81/0/29`; 13 rows migrated from `skip()`/`check()` to `// G:` source comments (status `missing` below) — 10 Phase-0 unobservable-at-this-abstraction reclassifications plus the 3 Wave E rows (S14.04/05/06) walked back post-closure because they validated `VideoTiming` interrupt-class logic with no production consumer. 33 rows flipped from `skip()` to live `check()` passes via five parallel Phase-2 waves. 7 integration rows now live as passes in the companion suite `test/ula/ula_integration_test.cpp` (7/7/0/0 — scroll, ULA+, ULAnext, alt-file, NR 0x68 bit 3 ungated ulap_en). Remaining 29 skips are all F-blocked to named subsystem plans: Emulator floating-bus (5), ContentionModel (12), Compositor NR 0x68 blend-mode (3), VideoTiming per-machine + int-position (7), Emulator/MMU shadow-screen routing (2). See `doc/testing/audits/task3-ula-phase4.md` for row-by-row rationale.

| Test ID | Plan row title                                  | VHDL file:line | Status  | Test file:line            |
|---------|-------------------------------------------------|----------------|---------|---------------------------|
| S1.01  | Top-left pixel                                  | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:215 |
| S1.02  | First char row, col 1                           | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:216 |
| S1.03  | Pixel row 1 in char row 0                       | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:217 |
| S1.04  | Pixel row 7 in char row 0                       | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:218 |
| S1.05  | Char row 1, pixel row 0                         | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:219 |
| S1.06  | Third of screen (py=64)                         | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:220 |
| S1.07  | Bottom-right pixel                              | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:221 |
| S1.08  | Alternate display file (mode(0)=1)              | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:222 |
| S1.09  | Middle of screen (py=96, px=128)                | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:223 |
| S1.10  | Wrap within third (py=63)                       | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:224 |
| S1.11  | Second third start+1 row                        | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:225 |
| S1.12  | Last pixel row of last char                     | zxula.vhd:218-263 | pass    | test/ula/ula_test.cpp:226 |
| S2.01  | Ink, no bright, colour 0                        | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:250 |
| S2.02  | Paper, no bright, colour 0                      | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:251 |
| S2.03  | Ink, bright, red (2)                            | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:252 |
| S2.04  | Paper, bright, green (4)                        | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:253 |
| S2.05  | Ink white, no bright                            | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:254 |
| S2.06  | Paper white, bright                             | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:255 |
| S2.07  | Ink cyan (5), bright                            | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:256 |
| S2.09  | Full white on black, bright                     | zxula.vhd:543-554 | pass    | test/ula/ula_test.cpp:257 |
| S3.01  | Black border                                    | zxula.vhd:418    | pass    | test/ula/ula_test.cpp:285 |
| S3.02  | Blue border                                     | zxula.vhd:418    | pass    | test/ula/ula_test.cpp:286 |
| S3.03  | Red border                                      | zxula.vhd:418    | pass    | test/ula/ula_test.cpp:287 |
| S3.04  | White border                                    | zxula.vhd:418    | pass    | test/ula/ula_test.cpp:288 |
| S3.05  | Green border                                    | zxula.vhd:418    | pass    | test/ula/ula_test.cpp:289 |
| S3.06  | Timex border, port_ff(5:3)=0                    | zxula.vhd:419    | pass    | test/ula/ula_test.cpp:303 |
| S3.07  | Timex border, port_ff(5:3)=7                    | zxula.vhd:419    | pass    | test/ula/ula_test.cpp:311 |
| S4.01  | Flash period = 32 frames                        | zxula.vhd:474-481 | pass    | test/ula/ula_test.cpp:499 |
| S4.02  | Flash attr bit=0: no inversion                  | zxula.vhd:470    | pass    | test/ula/ula_test.cpp:514 |
| S4.03  | Flash attr bit=1, counter bit4=0                | zxula.vhd:470    | pass    | test/ula/ula_test.cpp:528 |
| S4.04  | Flash attr bit=1, counter bit4=1                | zxula.vhd:470    | pass    | test/ula/ula_test.cpp:543 |
| S4.05  | Flash disabled in ULAnext mode                  | zxula.vhd:470    | pass    | test/ula/ula_test.cpp:564 |
| S4.06  | Flash disabled in ULA+ mode                     | zxula.vhd:470    | pass    | test/ula/ula_test.cpp:583 |
| S5.01  | Standard mode (000)                             | zxula.vhd:191/384-393 | pass    | test/ula/ula_test.cpp:601 |
| S5.02  | Alt display file (001)                          | zxula.vhd:191    | pass    | test/ula/ula_test.cpp:611 |
| S5.03  | Hi-colour mode (010)                            | zxula.vhd:386-392 | pass    | test/ula/ula_test.cpp:626 |
| S5.04  | Hi-colour + alt file (011)                      | zxula.vhd:218,235  | pass    | test/ula/ula_test.cpp:653 |
| S5.05  | Hi-res mode (100)                               | zxula.vhd:191/389/419/426-427 | pass    | test/ula/ula_test.cpp:681 |
| S5.06  | Hi-res uses timex border colour                 | zxula.vhd:419,426-427,543-553 | pass    | test/ula/ula_test.cpp:714 |
| S5.07  | Shadow screen forces mode "000"                 | zxula.vhd:191    | pass    | test/ula/ula_test.cpp:748 |
| S6.01  | Ink, format 0x07                                | zxnext.vhd:5394  | pass    | test/ula/ula_test.cpp:1526 |
| S6.02  | Paper, format 0x07                              | zxula.vhd:520    | pass    | test/ula/ula_test.cpp:1540 |
| S6.03  | Ink, format 0x0F                                | zxula.vhd:510    | pass    | test/ula/ula_test.cpp:1554 |
| S6.04  | Paper, format 0x0F                              | zxula.vhd:521    | pass    | test/ula/ula_test.cpp:1568 |
| S6.05  | Ink, format 0xFF                                | zxula.vhd:510    | pass    | test/ula/ula_test.cpp:1584 |
| S6.06  | Paper, format 0xFF                              | zxula.vhd:525    | pass    | test/ula/ula_test.cpp:1597 |
| S6.07  | Border, format 0x07                             | zxula.vhd:504    | pass    | test/ula/ula_test.cpp:1612 |
| S6.08  | Border, format 0xFF                             | zxula.vhd:500-504 | pass    | test/ula/ula_test.cpp:1627 |
| S6.09  | Ink, format 0x01                                | zxula.vhd:518    | pass    | test/ula/ula_test.cpp:1641 |
| S6.10  | Paper, format 0x01                              | zxula.vhd:518    | pass    | test/ula/ula_test.cpp:1658 |
| S6.11  | Ink, format 0x3F                                | zxula.vhd:523    | pass    | test/ula/ula_test.cpp:1672 |
| S6.12  | Non-standard format (e.g. 0x05)                 | zxula.vhd:525    | pass    | test/ula/ula_test.cpp:1685 |
| S6.14  | ULA palette reset content, indices 0x20-0xFF    | zxnext.vhd:6960-6965 | pass    | test/ula/ula_test.cpp:1731 |
| S6.15  | Unwritten ULAnext paper/border render           | zxula.vhd:520    | pass    | test/ula/ula_test.cpp:1796 |
| S6.16  | STANDARD strip border, ULAnext (GH #96)         | zxula.vhd:494-504 | pass    | test/ula/ula_test.cpp:1854 |
| S6.17  | STANDARD strip border, format 0xFF (GH #96)     | zxula.vhd:500-502 | pass    | test/ula/ula_test.cpp:1880 |
| S6.18  | HI_COLOUR strip border, ULAnext (GH #96)        | zxula.vhd:494-504 | pass    | test/ula/ula_test.cpp:1910 |
| S6.19  | HI_COLOUR strip border, format 0xFF (GH #96)    | zxula.vhd:500-502 | pass    | test/ula/ula_test.cpp:1936 |
| S6.20  | Scrolled-path paper select_bgnd (GH #97)        | zxula.vhd:525    | pass    | test/ula/ula_test.cpp:1964 |
| S6.21  | HI_COLOUR-path paper select_bgnd (GH #97)       | zxula.vhd:525    | pass    | test/ula/ula_test.cpp:1989 |
| S6.22  | HI_RES-path paper select_bgnd (GH #97)          | zxula.vhd:525    | pass    | test/ula/ula_test.cpp:2017 |
| S6.23  | TMX border-row select_bgnd (GH #97)             | zxula.vhd:500-502 | pass    | test/ula/ula_test.cpp:2045 |
| S6.24  | STANDARD full border row, ULAnext (GH #103)     | zxula.vhd:494-504,414-415 | pass    | test/ula/ula_test.cpp:2094 |
| S6.25  | STANDARD full border row, format 0xFF (GH #103) | zxula.vhd:500-502 | pass    | test/ula/ula_test.cpp:2122 |
| S6.26  | HI_COLOUR full border row, ULAnext (GH #103)    | zxula.vhd:494-504,414-415,426 | pass    | test/ula/ula_test.cpp:2155 |
| S7.01  | Ink, group 0                                    | zxnext.vhd:4547-4554 | pass    | test/ula/ula_test.cpp:2190 |
| S7.02  | Paper, group 0                                  | zxula.vhd:531    | pass    | test/ula/ula_test.cpp:2206 |
| S7.03  | Ink, group 3                                    | zxula.vhd:531    | pass    | test/ula/ula_test.cpp:2231 |
| S7.04  | Paper, group 3                                  | zxula.vhd:531-541 | pass    | test/ula/ula_test.cpp:2255 |
| S7.05  | Hi-res forces bit 3 high                        | zxula.vhd:531    | pass    | test/ula/ula_test.cpp:2271 |
| S7.06  | Flash bit NOT used (attr bit 7 = palette group) | zxula.vhd:531    | pass    | test/ula/ula_test.cpp:2301 |
| S7.07  | STANDARD strip border, ULA+ (GH #104)           | zxula.vhd:535-540,418 | pass    | test/ula/ula_test.cpp:2344 |
| S7.08  | HI_COLOUR strip border, ULA+ (GH #104)          | zxula.vhd:535-540,418 | pass    | test/ula/ula_test.cpp:2372 |
| S7.09  | Full border row, ULA+ (GH #104)                 | zxula.vhd:535-540,414-415,418 | pass    | test/ula/ula_test.cpp:2401 |
| S8.01  | Default window, inside                          | zxula.vhd:562    | pass    | test/ula/ula_test.cpp:2421 |
| S8.02  | Narrow window, inside                           | zxula.vhd:562    | pass    | test/ula/ula_test.cpp:2425 |
| S8.03  | Narrow window, outside left                     | zxula.vhd:562    | pass    | test/ula/ula_test.cpp:2429 |
| S8.04  | Narrow window, outside right                    | zxula.vhd:562    | pass    | test/ula/ula_test.cpp:2433 |
| S8.05  | Narrow window, outside top                      | zxula.vhd:562    | pass    | test/ula/ula_test.cpp:2443 |
| S8.08  | y2 >= 0xC0 clamped to 0xBF                      | zxnext.vhd:6779-6783 | pass    | test/ula/ula_test.cpp:2474 |
| S9.02  | Scroll Y by 1                                   | zxula.vhd:192,206 | pass    | test/ula/ula_test.cpp:2542 |
| S9.03  | Scroll Y by 191                                 | zxula.vhd:203-204 | pass    | test/ula/ula_test.cpp:2560 |
| S9.04  | Scroll Y wraps at 192                           | zxula.vhd:203-204 | pass    | test/ula/ula_test.cpp:2579 |
| S9.05  | Scroll X by 8 (1 char)                          | zxula.vhd:199    | pass    | test/ula/ula_test.cpp:2610 |
| S9.06  | Scroll X by 1 (fine)                            | zxula.vhd:199,216 | pass    | test/ula/ula_test.cpp:2635 |
| S9.07  | Scroll X by 255                                 | zxula.vhd:199    | pass    | test/ula/ula_test.cpp:2662 |
| S9.08  | Fine scroll X enabled                           | zxula.vhd:199    | pass    | test/ula/ula_test.cpp:2689 |
| S9.09  | Combined X+Y scroll                             | zxula.vhd:193-216 | pass    | test/ula/ula_test.cpp:2718 |
| S9.10  | Y scroll wraps mid-third                        | zxula.vhd:206,223 | pass    | test/ula/ula_test.cpp:2740 |
| S12.01  | ULA enabled (default)                           | zxnext.vhd:5445  | pass    | test/ula/ula_test.cpp:2951 |
| S13.01  | 48K frame length                                | zxula_timing.vhd | pass    | test/ula/ula_test.cpp:2983 |
| S13.02  | 128K frame length                               | zxula_timing.vhd | pass    | test/ula/ula_test.cpp:2993 |
| S13.03  | Pentagon frame length                           | —              | missing | missing                    |
| S13.04  | Active display start 48K                        | zxula_timing.vhd | pass    | test/ula/ula_test.cpp:3004 |
| S14.04  | Interrupt disabled                              | —              | missing | missing                                                                                          |
| S14.05  | Line interrupt fires                            | —              | missing | missing                                                                                         |
| S14.06  | Line interrupt 0 = last line                    | —              | missing | missing                                                                                         |
| S15.01  | Normal screen (shadow=0)                        | zxnext.vhd:4453  | pass    | test/ula/ula_test.cpp:3087 |
| S15.02  | Shadow screen (shadow=1)                        | zxnext.vhd:4453  | pass    | test/ula/ula_test.cpp:3106 |
| S5.10      | Hi-res renders at 512 px wide (mode=100)                                                                    | zxula.vhd:389,419                 | pass   | test/ula/ula_test.cpp:866       |
| S5.10c     | HI_RES top-border row fills all 640 FB cells with TMX border colour                                         | —                             | pass   | test/ula/ula_test.cpp:981       |
| S5.11      | Hi-res border uses 6-bit `border_clr_tmx` field (mode=100)                                                  | zxula.vhd:419,504                 | pass   | test/ula/ula_test.cpp:1075      |
| S5.12      | HI_RES display path dispatches through ULAnext encoder (G167)                                               | zxula.vhd:485-528               | pass   | test/ula/ula_test.cpp:1173      |
| S5.13      | HI_RES display path dispatches through ULA+ encoder (G167)                                                  | zxula.vhd:531-541               | pass   | test/ula/ula_test.cpp:1266      |
| G108-NR69-MODE          | NR 0x69 bits 5:0 fan out to port_ff_reg(5:0) + Ula::screen_mode_reg_; mode re-decoded         | zxnext.vhd:3617-3618 + zxula.vhd:191 | missing | missing                                        |
| G108-NR69-PRESERVE-BIT7 | NR 0x69 fan-out only touches bits 5:0; bit 6 from prior port-FF write preserved              | zxnext.vhd:3617-3618 | missing | missing                                        |
| G108-NR22-INTDIS-SET    | NR 0x22 bit 2 → port_ff_reg(6); bits 5:0 preserved; Ula propagated                            | zxnext.vhd:3619-3620 | missing | missing                                        |
| G108-NR22-INTDIS-CLR    | NR 0x22 bit 2 = 0 clears port_ff_reg(6); bits 5:0 preserved                                   | zxnext.vhd:3619-3620 | missing | missing                                        |
| G108-NRC4-INTDIS-CLR    | NR 0xC4 bit 0 = 1 → port_ff_reg(6) ← 0 (inverted polarity)                                    | zxnext.vhd:3621-3622 | missing | missing                                        |
| G108-NRC4-INTDIS-SET    | NR 0xC4 bit 0 = 0 → port_ff_reg(6) ← 1 (inverted polarity)                                    | zxnext.vhd:3621-3622 | missing | missing                                        |
| G108-PORTFF-WINS        | Direct port-FF write supersedes accumulated NR-side fan-out (elsif priority)                  | zxnext.vhd:3614-3622 | missing | missing                                        |
| S5-PSL.01  | Two port-0xFF writes mid-frame at lines L1 < L2 captured separately                                          | zxula.vhd:259-266; zxnext.vhd:2397,2713,2813 | pass | test/ula/ula_test.cpp:1305 |
| S5-PSL.02  | Render at line L: STANDARD pixels for L < split, HI_COLOUR pixels for L >= split                             | zxula.vhd:259-266; zxnext.vhd:2397,2713,2813 | pass | test/ula/ula_test.cpp:1358 |
| S5-PSL.03  | Mid-frame HI_RES->STANDARD switch at line L: lines >= L revert to 256-px attribute path                      | zxula.vhd:259-266; zxnext.vhd:2397,2713,2813 | pass | test/ula/ula_test.cpp:1398 |
| S5-PSL.04  | `Ula::start_frame()` rewinds the per-scanline change-log; line-0 baseline reflects last-frame closing value  | zxula.vhd:259-266; zxnext.vhd:2397,2713,2813 | pass | test/ula/ula_test.cpp:1427 |
| S5-PSL.05  | Save-state snapshot includes the per-scanline change-log; round-trip preserves split rendering               | zxula.vhd:259-266; zxnext.vhd:2397,2713,2813 | pass | test/ula/ula_test.cpp:1479 |
| S9-PSL.01  | Two NR 0x26 writes at scanlines L1 < L2 captured separately                                                  | zxula.vhd:193-216, 199          | pass   | test/ula/ula_test.cpp:2778      |
| S9-PSL.02  | Mid-frame NR 0x27 (scroll Y) split renders top/bottom with different scroll                                  | zxula.vhd:193-216, 199          | pass   | test/ula/ula_test.cpp:2826      |
| S9-PSL.03  | Mid-frame NR 0x26 fine-scroll (NR 0x68 b2) flip at line L                                                    | zxula.vhd:193-216, 199          | pass   | test/ula/ula_test.cpp:2866      |
| S9-PSL.04  | `Ula::start_frame()` rewinds NR 0x26 / NR 0x27 change-log; line-0 baseline correct                            | zxula.vhd:193-216, 199          | pass   | test/ula/ula_test.cpp:2897      |
| S16.01     | NR 0xFF write commits ULA palette entry at the slot indexed by `port_bf3b_ulap_index`                        | zxnext.vhd:6957                 | pass   | test/ula/ula_test.cpp:3193      |
| S17.01     | Two NR 0x43 b1-3 writes mid-frame at lines L1 < L2 captured separately                                        | zxnext.vhd:6957                 | pass   | test/ula/ula_test.cpp:3252      |
| S17.02     | Mid-frame NR 0x6B b4 flip at line L re-routes tilemap palette select for lines >= L                           | zxnext.vhd:5462,6826              | pass   | test/ula/ula_test.cpp:3282      |
| S17.03     | NR 0x43 b1-3 selector and NR 0x6B b4 are independent — flipping one does not perturb the other                | zxnext.vhd:6957, 3614+          | pass   | test/ula/ula_test.cpp:3324      |
| S17.04     | `PaletteManager::start_frame()` rewinds the selector change-log; line-0 baseline reflects last-frame          | zxnext.vhd:5391-5393,5462         | pass   | test/ula/ula_test.cpp:3364      |

### Extra coverage (not in plan)

| Test ID | Assertion description                         | VHDL file:line | Test file:line            |
|---------|-----------------------------------------------|----------------|---------------------------|
| S2.11  | Rendered paper pixel (0x00 pixels, 0x47 attr) | —              | missing                   |
| S13.09  | Pentagon T-states/frame = 71680               | —              | missing                   |
| S13.10  | Display left = 128                            | —              | missing                   |
| S13.11  | Display top = 64                              | —              | missing                   |
| S13.12  | Display width = 256                           | —              | missing                   |
| S13.13  | Display height = 192                          | —              | missing                   |
| S13.14  | Frame complete after full T-states            | zxula_timing.vhd | test/ula/ula_test.cpp:3031 |
| SR.01   | rrrgggbb 0x00 -> black                        | —              | missing                   |
| SR.02   | rrrgggbb 0xFF -> white                        | —              | missing                   |
| SR.03   | rrrgggbb 0xE0 -> red                          | —              | missing                   |
| SR.04   | FB_WIDTH = 320                                | —              | missing                   |
| SR.05   | FB_HEIGHT = 256                               | —              | missing                   |
| SR.06   | DISP_X = 32                                   | —              | missing                   |
| SR.07   | DISP_Y = 32                                   | —              | missing                   |
| SD.01   | ULA FB_WIDTH = 320                            | —              | missing                   |
| SD.02   | ULA FB_HEIGHT = 256                           | —              | missing                   |
| SD.03   | ULA DISP_X = 32 (left border)                 | —              | missing                   |
| SD.04   | ULA DISP_Y = 32 (top border)                  | —              | missing                   |
| SD.05   | ULA DISP_W = 256                              | —              | missing                   |
| SD.06   | ULA DISP_H = 192                              | —              | missing                   |
| SD.07   | Border widths sum correctly (32+256+32=320)   | —              | missing                   |
| SD.08   | Border heights sum correctly (32+192+32=256)  | —              | missing                   |
| S03P.01 | Init fills all lines with current border      | —              | missing                   |
| S03P.02 | Per-line snapshot at line 100                 | —              | missing                   |
| S03P.03 | Other lines unchanged                         | —              | missing                   |
| S03P.04 | Out-of-range line returns current border      | —              | missing                   |

### Companion integration suite — `test/ula/ula_integration_test.cpp`

Created 2026-04-23 (commit `08a4296`, renamed and merged at `94ccaf3`) to host end-to-end integration coverage of the Phase-2 flips that require the full `Emulator` fixture (NR 0x26/0x27 scroll composition, port 0xFF3B ULA+ palette, NR 0x43 ULAnext, and HI_COLOUR vs alt-file discrimination). Runtime: `Total:    6  Passed:    6  Failed:    0  Skipped:    0`. No skips; each row is a live pass.

| Test ID               | Plan row title                                                 | VHDL file:line | Status | Test file                                    |
|-----------------------|----------------------------------------------------------------|----------------|--------|----------------------------------------------|
| INT-SCROLL-01         | NR 0x26 coarse scroll: pixels shift by whole chars             | zxula.vhd:199  | pass    | test/ula/ula_integration_test.cpp:236        |
| INT-SCROLL-02         | NR 0x27 vertical scroll: wraps modulo 192 per :193-207         | zxula.vhd:193-207 | pass    | test/ula/ula_integration_test.cpp:345        |
| INT-SCROLL-03         | NR 0x68 bit 2 fine scroll X: sub-char offset                   | zxula.vhd:199  | pass    | test/ula/ula_integration_test.cpp:294        |
| INT-ULAPLUS-01        | Port 0xFF3B enable: palette group 3 picks correct indices      | zxula.vhd:531  | pass    | test/ula/ula_integration_test.cpp:435        |
| INT-ULANEXT-01        | NR 0x43 bit 0 + NR 0x42=0x0F: paper index uses format lookup   | zxula.vhd:503-515 | pass    | test/ula/ula_integration_test.cpp:909        |
| INT-STANDARD-ALT-01   | Alt-file bit: standard-screen mode 001 selects alt display     | zxula.vhd:218  | pass    | test/ula/ula_integration_test.cpp:1449       |
| INT-ULAPLUS-03  | Port 0xBF3B ULA+ index write commits palette entry at `port_bf3b_ulap_index` slot    | zxnext.vhd:4525-4538                 | pass    | test/ula/ula_integration_test.cpp:696         |
| INT-ULANEXT-02  | Runtime renderer integration: NR 0x43 ULAnext encoder routed to scanline output       | zxula.vhd:485-528; zxnext.vhd:6981   | pass    | test/ula/ula_integration_test.cpp:1040        |

## Layer2 — `test/layer2/layer2_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID | Plan row title                                               | VHDL file:line       | Status  | Test file:line                   |
|---------|--------------------------------------------------------------|----------------------|---------|----------------------------------|
| G1-01   | NR 0x12 default                                              | zxnext.vhd:4943      | pass    | test/layer2/layer2_test.cpp:250  |
| G1-02   | NR 0x13 default                                              | zxnext.vhd:4944      | pass    | test/layer2/layer2_test.cpp:255  |
| G1-03   | NR 0x14 default                                              | zxnext.vhd:4946      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-04   | NR 0x16 default                                              | zxnext.vhd:4955      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-05   | NR 0x17 default                                              | zxnext.vhd:4957      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-06   | NR 0x18 defaults                                             | zxnext.vhd:4959-4962 | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-07   | NR 0x43[2] default                                           | zxnext.vhd:5007      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-08   | NR 0x4A default                                              | zxnext.vhd:5014      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-09   | NR 0x70 default                                              | zxnext.vhd:5047-5048 | pass    | test/layer2/layer2_test.cpp:261  |
| G1-10   | NR 0x71[0] default                                           | zxnext.vhd:5050      | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-11   | port 0x123B default                                          | zxnext.vhd:3908-3913 | pass    | test/layer2/layer2_test.cpp:1398 |
| G1-12   | Layer 2 off after reset                                      | zxnext.vhd:3908      | pass    | test/layer2/layer2_test.cpp:268  |
| G2-01   | 256x192 row-major address                                    | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:316  |
| G2-02   | 256x192 row pitch = 256                                      | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:346  |
| G2-03   | 256x192 y≥192 invisible                                      | layer2.vhd:165       | pass    | test/layer2/layer2_test.cpp:362  |
| G2-04   | 256x192 x wraparound at 256 is impossible (no stimulus rout… | layer2.vhd:164       | pass    | test/layer2/layer2_test.cpp:1400 |
| G2-05   | 320x256 column-major address                                 | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:380  |
| G2-06   | 320x256 column pitch = 256                                   | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:403  |
| G2-07   | 320x256 x in [320,383] invisible                             | layer2.vhd:164       | pass    | test/layer2/layer2_test.cpp:1400 |
| G2-08   | 320x256 y=255 visible                                        | layer2.vhd:165       | pass    | test/layer2/layer2_test.cpp:413  |
| G2-09   | 640x256 high nibble = left pixel                             | layer2.vhd:202       | pass    | test/layer2/layer2_test.cpp:427  |
| G2-10   | 640x256 only 4-bit index pre-offset                          | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:456  |
| G2-11   | 640x256 shares 320 column layout                             | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:1400 |
| G2-12   | Lookahead one pixel                                          | layer2.vhd:148       | pass    | test/layer2/layer2_test.cpp:1400 |
| G3-01   | 256x192 scroll X=128                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:496  |
| G3-02   | 256x192 scroll X=255                                         | layer2.vhd:152       | pass    | test/layer2/layer2_test.cpp:511  |
| G3-03   | 256x192 scroll Y wrap from 192                               | layer2.vhd:156-158   | pass    | test/layer2/layer2_test.cpp:526  |
| G3-04   | 256x192 scroll Y wrap from 193                               | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:536  |
| G3-05   | 256x192 scroll Y=96                                          | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:546  |
| G3-06   | Scroll X MSB (nr_71[0]) in 256 mode                          | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:562  |
| G3-07   | 320x256 scroll X=160                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:581  |
| G3-08   | 320x256 scroll X=319                                         | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:596  |
| G3-09   | 320x256 scroll X wrap arithmetic                             | layer2.vhd:153       | pass    | test/layer2/layer2_test.cpp:1402 |
| G3-10   | 320x256 scroll Y=128                                         | layer2.vhd:157       | pass    | test/layer2/layer2_test.cpp:610  |
| G3-11   | 640x256 scroll X=160 byte-level                              | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1402 |
| G3-12   | Negative path: 320x256 scroll X wrap branch skipped when x_… | layer2.vhd:153       | pass    | test/layer2/layer2_test.cpp:622  |
| G4-01a  | Auto-index advances — slot 0 observable                      | zxnext.vhd:5243-5249 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-01b  | Auto-index advances — slot 1 observable                      | zxnext.vhd:5243-5249 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-01c  | Auto-index advances — slot 2 observable                      | zxnext.vhd:5243-5249 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-01d  | Auto-index advances — slot 3 observable and wraps            | zxnext.vhd:5243-5249 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-02   | Auto-index wraps at 4                                        | zxnext.vhd:5249      | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-03   | NR 0x1C[0] resets L2 clip index                              | zxnext.vhd:5278-5281 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-04   | NR 0x1C[0]=0 leaves L2 index alone                           | zxnext.vhd:5278-5281 | pass    | test/layer2/layer2_test.cpp:1404 |
| G4-05   | 256x192 default clip covers full area                        | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:670  |
| G4-06   | 256x192 clip to centre 64x64                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:683  |
| G4-07   | 256x192 clip x1==x2 single column                            | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:702  |
| G4-08   | 256x192 clip x1 > x2 → empty                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:716  |
| G4-09   | 320x256 clip X is doubled                                    | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:730  |
| G4-10   | 320x256 clip Y is not doubled                                | layer2.vhd:137-138   | pass    | test/layer2/layer2_test.cpp:744  |
| G4-11   | 320x256 clip `x1=0,x2=0` gives 2-pixel-wide strip            | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:764  |
| G4-12   | 640x256 clip uses same doubling as 320                       | layer2.vhd:133-134   | pass    | test/layer2/layer2_test.cpp:786  |
| G4-13   | Clip is inclusive on both edges                              | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:806  |
| G5-01   | Offset 0 identity                                            | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:842  |
| G5-02   | Offset 1 shifts high nibble                                  | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:852  |
| G5-03   | Offset 15, high nibble 0                                     | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:861  |
| G5-04   | Offset 15, high nibble 1 → wraps to 0                        | layer2.vhd:203       | pass    | test/layer2/layer2_test.cpp:869  |
| G5-05   | 4-bit mode high nibble is pre-offset zero                    | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:883  |
| G5-06   | 4-bit mode offset shifts into upper nibble                   | layer2.vhd:202-203   | pass    | test/layer2/layer2_test.cpp:892  |
| G5-07   | 4-bit mode low nibble is right pixel                         | layer2.vhd:202       | pass    | test/layer2/layer2_test.cpp:902  |
| G5-08   | Palette 0 vs Palette 1                                       | zxnext.vhd:6827      | pass    | test/layer2/layer2_test.cpp:923  |
| G5-09   | Palette select does not affect sprite/ula palette            | zxnext.vhd:6827      | pass    | test/layer2/layer2_test.cpp:1406 |
| G6-01   | Index ≠ 0xE3, RGB = 0xE3 → transparent (would catch "index…  | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:969  |
| G6-02   | Index = 0xE3, RGB ≠ 0xE3 → opaque (would catch "index check… | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:977  |
| G6-03   | Identity palette, default NR 0x14                            | zxnext.vhd:7121      | pass    | test/layer2/layer2_test.cpp:989  |
| G6-04   | Change NR 0x14 to 0x00                                       | zxnext.vhd:5226      | pass    | test/layer2/layer2_test.cpp:999  |
| G6-05   | Clip outside ⇒ transparent regardless of colour              | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:1014 |
| G6-06   | L2 disabled ⇒ all transparent                                | layer2.vhd:175       | pass    | test/layer2/layer2_test.cpp:1025 |
| G6-07   | Fallback 0xE3 visible when every layer transparent           | zxnext.vhd:5014      | pass    | test/layer2/layer2_test.cpp:1408 |
| G6-08   | Fallback colour follows NR 0x4A write                        | zxnext.vhd:5407      | pass    | test/layer2/layer2_test.cpp:1408 |
| G6-09   | Priority bit gated by transparency                           | zxnext.vhd:7123      | pass    | test/layer2/layer2_test.cpp:1408 |
| G6-10   | NR 0x44 second-write captures palette priority into bit 9    | zxnext.vhd:4920      | pass    | test/layer2/layer2_test.cpp:1044 |
| G6-11   | Renderer populates `layer2_priority_[]` from palette priority slot | zxnext.vhd:7039-7050 | pass    | test/layer2/layer2_test.cpp:1066 |
| G7-01   | Bank `+1` transform on default bank                          | layer2.vhd:172       | missing | missing                          |
| G7-02   | Bank `+1` transform, nonzero high 3 bits                     | layer2.vhd:172       | missing | missing                          |
| G7-03   | Bank `+1` transform, max legal                               | layer2.vhd:172-175   | missing | missing                          |
| G7-04   | Out-of-range bank → no pixel                                 | layer2.vhd:173-175   | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-05   | Address bits 16:14 select 16K page within 48K                | layer2.vhd:173       | missing | missing                          |
| G7-06   | 320x256 uses 5 pages                                         | layer2.vhd:160       | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-07   | Port 0x123B bit 0 enables CPU writes                         | zxnext.vhd:3917      | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-08   | Port 0x123B bit 2 enables CPU reads                          | zxnext.vhd:3918      | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-09   | Port 0x123B bit 1 enables display                            | zxnext.vhd:3916      | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-10   | Port 0x123B bit 1 and NR 0x69 bit 7 target same flop         | zxnext.vhd:3924-3925 | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-11   | Port 0x123B bit 3 selects shadow bank for mapping only       | zxnext.vhd:2968      | pass    | test/layer2/layer2_test.cpp:1410 |
| G7-12   | Shadow bank data becomes visible after NR 0x12 rewrite       | layer2.vhd:172       | pass    | test/layer2/layer2_test.cpp:1411 |
| G7-13   | Port 0x123B bits 7:6 select segment                          | zxnext.vhd:2966-2967 | pass    | test/layer2/layer2_test.cpp:1411 |
| G7-14   | Port 0x123B segment=11 ⇒ A15:A14 selects page                | zxnext.vhd:2966      | pass    | test/layer2/layer2_test.cpp:1411 |
| G7-15   | Port 0x123B bit 4 (offset latch)                             | zxnext.vhd:3922      | pass    | test/layer2/layer2_test.cpp:1411 |
| G7-16   | Port 0x123B read-back formatting                             | zxnext.vhd:3933      | pass    | test/layer2/layer2_test.cpp:1411 |
| G7-17   | port 0x123B bit-4 latches offset, leaves enable/wr_en/rd_en/segment unchanged | zxnext.vhd:3914-3923 | pass    | test/layer2/layer2_test.cpp:1175 |
| G7-18   | port 0x123B bit 3 routes CPU writes/reads to NR 0x13 shadow bank | zxnext.vhd:2968   | pass    | test/layer2/layer2_test.cpp:1200 |
| G7-19   | port 0x123B read returns formatted control word, not 0xFF default | zxnext.vhd:3933, emulator.cpp:1178 | pass | test/layer2/layer2_test.cpp:1213 |
| G8-01   | NR 0x15 priority SLU with L2 opaque over ULA                 | zxnext.vhd:7216      | pass    | test/layer2/layer2_test.cpp:1413 |
| G8-02   | L2 transparent ⇒ ULA shows through in SLU                    | zxnext.vhd:7121-7122 | pass    | test/layer2/layer2_test.cpp:1413 |
| G8-03   | L2 priority bit promotes over sprite                         | zxnext.vhd:7050      | pass    | test/layer2/layer2_test.cpp:1413 |
| G8-04   | Priority bit suppressed when L2 pixel transparent            | zxnext.vhd:7123      | pass    | test/layer2/layer2_test.cpp:1413 |
| G8-05   | `layer2_rgb` zeroed when transparent                         | zxnext.vhd:7122      | pass    | test/layer2/layer2_test.cpp:1413 |
| G9-01   | Disable then re-enable via NR 0x69                           | zxnext.vhd:3924      | pass    | test/layer2/layer2_test.cpp:1415 |
| G9-02   | Cold-reset port 0x123B read is 0x00                          | zxnext.vhd:3908-3913 | pass    | test/layer2/layer2_test.cpp:1415 |
| G9-03   | Clip y1 > y2 empties display                                 | layer2.vhd:167       | pass    | test/layer2/layer2_test.cpp:1318 |
| G9-04   | Scroll X with wide branch NOT fired                          | —                    | missing | missing                          |
| G9-05   | Wide mode clip `x2=0xFF` ⇒ effective 511                     | layer2.vhd:134       | pass    | test/layer2/layer2_test.cpp:1340 |
| G9-06   | `hc_eff = hc + 1` cannot be detected as a pure scroll (non-… | layer2.vhd:148       | missing | missing                          |
| L2-G17-01  | Parallax.nex side-by-side duplication root cause (post-LoRes) | PARALLAX-NEX-INVESTIGATION.md | missing | missing                          |
| G9-G28-01  | `hc_eff = hc + 1` per-column observable (cycle-accurate gate) | layer2.vhd:148      | missing | missing                          |
| G10-01  | start_frame baseline captures scroll_x_/y_                   | zxnext.vhd:5232      | pass    | test/layer2/layer2_test.cpp:1452 |
| G10-02  | Three scroll writes recorded in change log                   | zxnext.vhd:5232      | pass    | test/layer2/layer2_test.cpp:1463 |
| G10-03  | rewind_to_baseline restores live scroll_x to baseline        | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1469 |
| G10-04a | apply_changes_for_line(0): no change applied → scroll_x baseline | layer2.vhd:152-154 | pass | test/layer2/layer2_test.cpp:1477 |
| G10-04b | apply_changes_for_line(49) before first change: scroll_x baseline | layer2.vhd:152-154 | pass | test/layer2/layer2_test.cpp:1481 |
| G10-04c | apply_changes_for_line(50) first change: scroll_x updated    | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1485 |
| G10-04d | apply_changes_for_line(99) between changes: scroll_x held    | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1488 |
| G10-04e | apply_changes_for_line(100): scroll_x advances to next       | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1492 |
| G10-04f | apply_changes_for_line(150): scroll_x advances to last       | layer2.vhd:152-154   | pass    | test/layer2/layer2_test.cpp:1495 |
| G10-05  | Change log capped at MAX_CHANGES_PER_FRAME (overflow drop)   | —                    | pass    | test/layer2/layer2_test.cpp:1504 |
| L2P-G02-01 | NR 0x15 write logged with current scanline (sprite_en + priority) | zxnext.vhd:5232, 6799 | pass | test/layer2/layer2_test.cpp:1532 |
| L2P-G02-02 | apply_changes_for_line replays NR 0x15 entries per scanline  | zxnext.vhd:6799      | pass    | test/layer2/layer2_test.cpp:1575 |
| G10-G05-01 | Layer2 clip-window 4-coord snapshot logged with current scanline | zxnext.vhd:5243, 5278 | pass | test/layer2/layer2_test.cpp:1611 |
| G10-G05-02 | Renderer replays clip-window snapshot per scanline           | layer2.vhd:134, 167  | pass    | test/layer2/layer2_test.cpp:1690 |
| G10-G09-01 | Layer2 NR 0x12 active-bank write logged with current scanline | zxnext.vhd:5220, 1135 | pass   | test/layer2/layer2_test.cpp:1719 |
| G10-G09-02 | Renderer fetches L2 from old bank above flip-line, new below | layer2.vhd:172       | pass    | test/layer2/layer2_test.cpp:1779 |
| G10-G14-01 | Layer2 set_enabled write logged with current scanline        | zxnext.vhd:3916, 3924-3925 | pass | test/layer2/layer2_test.cpp:1806 |
| G10-G14-02 | Renderer applies enable per scanline (above hidden, mid visible, post hidden) | layer2.vhd:175, 197-198 | pass | test/layer2/layer2_test.cpp:1876 |

## Sprites — `test/sprites/sprites_test.cpp`

Last-touch commit: `44c21eed8032671965579e470d332ac4ce3b6ce0` (`44c21eed80`)

| Test ID    | Plan row title                                               | VHDL file:line  | Status  | Test file:line                     |
|------------|--------------------------------------------------------------|-----------------|---------|------------------------------------|
| G6.CL-01   | `check(..., true)` — no clip semantics verified              | —               | pass    | test/sprites/sprites_test.cpp:1505 |
| G6.CL-02   | `check(..., true)` — setters only                            | —               | pass    | test/sprites/sprites_test.cpp:1525 |
| G6.CL-03   | `check(..., true)` — setter only, wrong group                | —               | pass    | test/sprites/sprites_test.cpp:1550 |
| G6.CL-04   | `check(..., true)` — setter, misnamed as clip                | —               | pass    | test/sprites/sprites_test.cpp:1565 |
| G14.RST-04 | `check(..., true)` — no getter, no assertion                 | —               | pass    | test/sprites/sprites_test.cpp:2888 |
| G14.RST-05 | `check(..., true)` — same                                    | —               | pass    | test/sprites/sprites_test.cpp:2901 |
| G1.AT-01   | 4-byte write auto-skips to next sprite attr0                 | sprites.vhd:639-667,594-612,653-657 | pass    | test/sprites/sprites_test.cpp:301  |
| G1.AT-02   | 5-byte write advances through attr4                          | sprites.vhd:639-664 | pass    | test/sprites/sprites_test.cpp:319  |
| G1.AT-03   | 0x303B sets `attr_index = d(6:0) & "000"`                    | sprites.vhd:655-657 | pass    | test/sprites/sprites_test.cpp:331  |
| G1.AT-04   | 0x303B sets `pattern_index = d(5:0)&d(7)&"0000000"`          | sprites.vhd:735-736 | pass    | test/sprites/sprites_test.cpp:348  |
| G1.AT-05   | Attr2 bitfields readable as (paloff, xm, ym, rot, xmsb)      | sprites.vhd:381   | pass    | test/sprites/sprites_test.cpp:358  |
| G1.AT-06   | Attr4 bitfields (H, N6, type, xscale, yscale, ymsb)          | sprites.vhd:437   | pass    | test/sprites/sprites_test.cpp:375  |
| G1.AT-07   | Sprite 127 is the last slot                                  | sprites.vhd:655   | pass    | test/sprites/sprites_test.cpp:391  |
| G1.AT-08   | Attr write via NR 0x34 mirror path                           | sprites.vhd:704-715 | pass    | test/sprites/sprites_test.cpp:401  |
| G1.AT-09   | Mirror `index="111"` sets sprite number                      | sprites.vhd:600-602 | pass    | test/sprites/sprites_test.cpp:411  |
| G1.AT-10   | `mirror_inc_i` increments within 7 bits                      | sprites.vhd:603-605 | pass    | test/sprites/sprites_test.cpp:424  |
| G1.AT-11   | Legacy set_attr_slot moves 0x57 cursor unconditionally (GH … | sprites.vhd:653-654 | pass    | test/sprites/sprites_test.cpp:440  |
| G1.AT-12   | Mirror write takes priority over pending CPU write           | —               | missing | missing                            |
| G1.AT-22   | Reverse tie sync via port 0x303B (slot + pattern_index(7))   | sprites.vhd:607-609,655-657 | pass    | test/sprites/sprites_test.cpp:704  |
| G1.AT-23   | Reverse tie sync via 0x57 slot advance; non-boundary no-op   | sprites.vhd:607-609,639,658-663 | pass    | test/sprites/sprites_test.cpp:741  |
| G1.AT-24   | Tie clear: 0x303B / 0x57 advance leave mirror_sprite_q alone | sprites.vhd:607-609 | pass    | test/sprites/sprites_test.cpp:771  |
| G2.PL-01   | 256-byte pattern upload targets bytes 0..255 of pattern 0    | —               | pass    | test/sprites/sprites_test.cpp:825  |
| G2.PL-02   | Last pattern (63) writable                                   | —               | pass    | test/sprites/sprites_test.cpp:840  |
| G2.PL-03   | Auto-increment crosses pattern boundary                      | —               | pass    | test/sprites/sprites_test.cpp:859  |
| G2.PL-04   | `pattern_index(7)` half-pattern offset for 4bpp              | sprites.vhd:736   | pass    | test/sprites/sprites_test.cpp:881  |
| G2.PL-05   | 14-bit pattern address does not spill above 0x3FFF           | —               | pass    | test/sprites/sprites_test.cpp:902  |
| G2.PL-09   | Tie clear: NR 0x34 leaves pattern_index(7) alone (end-to-end) | sprites.vhd:728-741,603-605,733-734 | pass    | test/sprites/sprites_test.cpp:1000 |
| G2.PL-10   | Tie set: NR 0x34 re-bases pattern_index incl. bit 7          | sprites.vhd:733-734,603-605 | pass    | test/sprites/sprites_test.cpp:1028 |
| G3.PX-01   | 8bpp opaque pixel, paloff=0, no mirror/rotate/scale          | —               | pass    | test/sprites/sprites_test.cpp:1059 |
| G3.PX-02   | 8bpp paloff applies to upper nibble only                     | —               | pass    | test/sprites/sprites_test.cpp:1073 |
| G3.PX-03   | 8bpp paloff upper nibble wraps mod 16                        | —               | pass    | test/sprites/sprites_test.cpp:1087 |
| G3.PX-04   | 4bpp (H=1), even addr, upper nibble selected                 | —               | pass    | test/sprites/sprites_test.cpp:1105 |
| G3.PX-05   | 4bpp, odd addr, lower nibble selected                        | —               | pass    | test/sprites/sprites_test.cpp:1119 |
| G3.PX-06   | 4bpp addr remap: `pat_addr_b = addr(13:8) & n6 & addr(7:1)`  | —               | pass    | test/sprites/sprites_test.cpp:1138 |
| G3.TR-01   | 8bpp transparent pixel (full byte) not written               | —               | pass    | test/sprites/sprites_test.cpp:1152 |
| G3.TR-02   | 4bpp transparent nibble not written, other nibble of same b… | —               | pass    | test/sprites/sprites_test.cpp:1172 |
| G3.TR-03   | Transparent compare is on palette **index**, not ARGB        | —               | pass    | test/sprites/sprites_test.cpp:1199 |
| G3.TR-04   | 8bpp paloff change does not make the transparency check com… | sprites.vhd:971   | pass    | test/sprites/sprites_test.cpp:1217 |
| G3.PA-01   | 4bpp replaces upper nibble with paloff                       | —               | pass    | test/sprites/sprites_test.cpp:1233 |
| G3.PA-02   | Line buffer bit 8 set on any sprite write                    | —               | pass    | test/sprites/sprites_test.cpp:1249 |
| G4.XY-01   | Sprite at (0,0) opaque fills [0..15] on line 0               | —               | pass    | test/sprites/sprites_test.cpp:1277 |
| G4.XY-02   | X MSB (attr2(0)=1) gives x=256+attr0                         | —               | pass    | test/sprites/sprites_test.cpp:1289 |
| G4.XY-03   | Y MSB requires 5th byte; else forced to 0                    | —               | pass    | test/sprites/sprites_test.cpp:1308 |
| G4.XY-04   | *(removed: y=256 always clipped — clip_y2_i 8-bit, sprites.vhd:1053)* | —      | missing | missing                            |
| G4.XY-05   | x=319 renders last valid column                              | —               | pass    | test/sprites/sprites_test.cpp:1329 |
| G4.XY-06   | x=320 fully off-screen, x-wrap 1× (mask 11111) still render… | —               | pass    | test/sprites/sprites_test.cpp:1346 |
| G4.XY-07   | 2× scale wrap-around, sprite starts at x=300                 | —               | pass    | test/sprites/sprites_test.cpp:1368 |
| G5.VIS-01  | `attr3(7)=1` and on-scanline ⇒ renders                       | —               | pass    | test/sprites/sprites_test.cpp:1426 |
| G5.VIS-02  | `attr3(7)=0` ⇒ S_QUALIFY→S_QUALIFY (skipped)                 | —               | pass    | test/sprites/sprites_test.cpp:1437 |
| G5.VIS-03  | Y not on this line ⇒ `spr_cur_yoff≠0` ⇒ skipped              | —               | pass    | test/sprites/sprites_test.cpp:1448 |
| G5.VIS-04  | `spr_cur_hcount_valid=0` at entry and no x-wrap ⇒ no write   | —               | pass    | test/sprites/sprites_test.cpp:1461 |
| G5.VIS-05  | Invisible sprite still latches its anchor context for a lat… | —               | pass    | test/sprites/sprites_test.cpp:1483 |
| G6.CL-01   | Reset defaults {0,0xFF,0,0xBF} pass full display window      | —               | pass    | test/sprites/sprites_test.cpp:1505 |
| G6.CL-02   | Non-over-border x transform `(({0,x1(7:5)}+1) & x1(4:0))`    | —               | pass    | test/sprites/sprites_test.cpp:1525 |
| G6.CL-03   | Non-over-border x2 transform same formula                    | —               | pass    | test/sprites/sprites_test.cpp:1550 |
| G6.CL-04   | Over-border, clip_en=0 ⇒ full 320×256                        | —               | pass    | test/sprites/sprites_test.cpp:1565 |
| G6.CL-05   | Over-border, clip_en=1 ⇒ (x1*2, x2*2+1, y1, y2)              | sprites.vhd:1049-1053 | pass    | test/sprites/sprites_test.cpp:1586 |
| G6.CL-06   | Sprite pixel suppressed when `(h,v)` outside (x_s..x_e, y_s… | —               | pass    | test/sprites/sprites_test.cpp:1602 |
| G6.CL-07   | Sprite pixel emitted when inside clip and non-zero line-buf… | —               | pass    | test/sprites/sprites_test.cpp:1615 |
| G7.PR-01   | `zero_on_top=0`: higher-index sprite wins overlap            | —               | pass    | test/sprites/sprites_test.cpp:1639 |
| G7.PR-02   | `zero_on_top=1`: lower-index sprite wins                     | —               | pass    | test/sprites/sprites_test.cpp:1655 |
| G7.PR-03   | bit 8 of line-buffer entry cleared each scanline by video p… | —               | pass    | test/sprites/sprites_test.cpp:1675 |
| G7.PR-04   | Collision flag set regardless of `zero_on_top`               | —               | pass    | test/sprites/sprites_test.cpp:1692 |
| G9.MI-01   | Plain sprite, pattern byte 0 renders at x=0, byte 15 at x=15 | sprites.vhd:811-820 | pass    | test/sprites/sprites_test.cpp:1724 |
| G9.MI-02   | X-mirror flips columns: byte 15 at x=0, byte 0 at x=15       | sprites.vhd:813,817-820 | pass    | test/sprites/sprites_test.cpp:1738 |
| G9.MI-03   | Y-mirror on row 0 reads pattern row 15                       | sprites.vhd:811   | pass    | test/sprites/sprites_test.cpp:1758 |
| G9.MI-04   | Both mirrors                                                 | sprites.vhd:811,813 | pass    | test/sprites/sprites_test.cpp:1775 |
| G9.RO-01   | Rotate swaps row/col in address                              | sprites.vhd:816   | pass    | test/sprites/sprites_test.cpp:1798 |
| G9.RO-02   | `x_mirr_eff = xmirror XOR rotate`                            | sprites.vhd:813   | pass    | test/sprites/sprites_test.cpp:1818 |
| G9.RO-03   | Rotate + x-mirror produces delta = -16 (0x3FF0)              | —               | missing | missing                            |
| G9.RO-04   | Rotate without mirror: delta = +16                           | —               | missing | missing                            |
| G10.SC-01  | X 1× renders 16 px, advances addr every pixel                | —               | pass    | test/sprites/sprites_test.cpp:1863 |
| G10.SC-02  | X 2× renders 32 px, each byte repeated twice                 | —               | pass    | test/sprites/sprites_test.cpp:1875 |
| G10.SC-03  | X 4× renders 64 px, each byte×4                              | —               | pass    | test/sprites/sprites_test.cpp:1890 |
| G10.SC-04  | X 8× renders 128 px, each byte×8                             | —               | pass    | test/sprites/sprites_test.cpp:1904 |
| G10.SC-05  | Y 2× shows row 0 on 2 consecutive scanlines                  | —               | pass    | test/sprites/sprites_test.cpp:1922 |
| G10.SC-06  | Y 4× repeats 4×                                              | —               | pass    | test/sprites/sprites_test.cpp:1941 |
| G10.SC-07  | Y 8× repeats 8×                                              | —               | pass    | test/sprites/sprites_test.cpp:1961 |
| G10.SC-08  | 5th byte absent ⇒ scale forced 1× regardless of attr4 bits   | —               | pass    | test/sprites/sprites_test.cpp:1977 |
| G10.SC-09  | Combined X=4×, Y=2× covers 64×32 rectangle                   | —               | pass    | test/sprites/sprites_test.cpp:1994 |
| G10.SC-10  | X wrap mask for 2× is 11110                                  | —               | pass    | test/sprites/sprites_test.cpp:2008 |
| G11.OB-01  | `over_border=0`: sprite at y=200 not emitted (clip via non-… | —               | pass    | test/sprites/sprites_test.cpp:2035 |
| G11.OB-02  | `over_border=1, border_clip_en=0`: sprite at y=200 emitted   | —               | pass    | test/sprites/sprites_test.cpp:2046 |
| G11.OB-03  | `over_border=1, border_clip_en=1`: sprite at y=200, clip_y2… | sprites.vhd:1049-1053 | pass    | test/sprites/sprites_test.cpp:2071 |
| G11.OB-04  | `pixel_en_o` also requires `vcounter < 224` when `over_bord… | —               | pass    | test/sprites/sprites_test.cpp:2092 |
| G12.AN-01  | Sprite with `attr4(7:6)≠"01"` and attr3(6)=1 is an anchor;…  | —               | pass    | test/sprites/sprites_test.cpp:2114 |
| G12.AN-02  | Anchor type=1 additionally latches rotate/mirror/scale       | —               | pass    | test/sprites/sprites_test.cpp:2130 |
| G12.AN-03  | Anchor type=0 zeroes latched transforms                      | —               | pass    | test/sprites/sprites_test.cpp:2150 |
| G12.AN-04  | 4-byte (attr3(6)=0) sprite does **not** update anchor        | —               | pass    | test/sprites/sprites_test.cpp:2164 |
| G12.AN-05  | `anchor_vis` is `attr3(7)` of anchor                         | —               | pass    | test/sprites/sprites_test.cpp:2192 |
| G12.RE-01  | Relative with no transforms renders at `anchor_pos + (signe… | —               | pass    | test/sprites/sprites_test.cpp:2203 |
| G12.RE-02  | Relative inherits visibility: anchor invisible ⇒ relative i… | —               | pass    | test/sprites/sprites_test.cpp:2228 |
| G12.RE-03  | Relative palette: attr2(0)=0 ⇒ direct paloff                 | —               | pass    | test/sprites/sprites_test.cpp:2244 |
| G12.RE-04  | Relative palette: attr2(0)=1 ⇒ `anchor_paloff + attr2(7:4)`… | —               | pass    | test/sprites/sprites_test.cpp:2262 |
| G12.RE-05  | Anchor rotate swaps rel's offset axes (x0↔y0)                | —               | pass    | test/sprites/sprites_test.cpp:2278 |
| G12.RE-06  | Anchor xmirror XOR rotate negates rel X offset (note: subtr… | sprites.vhd:762 | pass    | test/sprites/sprites_test.cpp:2295 |
| G12.RE-07  | Anchor ymirror negates rel Y offset                          | —               | pass    | test/sprites/sprites_test.cpp:2310 |
| G12.RE-08  | Anchor xscale=01 doubles rel X offset (shift left 1)         | —               | pass    | test/sprites/sprites_test.cpp:2324 |
| G12.RE-09  | Anchor yscale=10 quadruples rel Y offset                     | —               | pass    | test/sprites/sprites_test.cpp:2338 |
| G12.RE-10  | Anchor xscale=11 × 8                                         | —               | pass    | test/sprites/sprites_test.cpp:2351 |
| G12.RT-01  | Type 0 relative: own mirror/rotate used directly             | —               | pass    | test/sprites/sprites_test.cpp:2371 |
| G12.RT-02  | Type 1 relative: `mirror = anchor XOR rel`                   | —               | pass    | test/sprites/sprites_test.cpp:2392 |
| G12.RT-03  | Type 1 relative: `rotate = anchor XOR rel`                   | —               | pass    | test/sprites/sprites_test.cpp:2417 |
| G12.RT-04  | Type 1 relative scale from anchor, not relative              | —               | pass    | test/sprites/sprites_test.cpp:2438 |
| G12.RP-01  | Rel pattern without add (attr4(0)=0): uses own name          | —               | pass    | test/sprites/sprites_test.cpp:2453 |
| G12.RP-02  | Rel pattern with add (attr4(0)=1): anchor_pattern + rel pat… | —               | pass    | test/sprites/sprites_test.cpp:2466 |
| G12.RP-03  | Rel pattern with N6 bit (from rel's attr4(6) AND anchor_h)   | sprites.vhd:802   | pass    | test/sprites/sprites_test.cpp:2511 |
| G12.RP-04  | 4bpp relative inherits H from anchor (`anchor_h`)            | sprites.vhd:785   | pass    | test/sprites/sprites_test.cpp:2554 |
| G12.NG-01  | Relative sprite with no prior anchor ⇒ `anchor_*` all zero…  | sprites.vhd:893-897 | pass    | test/sprites/sprites_test.cpp:2577 |
| G12.NG-02  | Two consecutive anchors: second replaces first               | —               | pass    | test/sprites/sprites_test.cpp:2591 |
| G12.NG-03  | Invisible anchor between visible anchor and relative leaves… | —               | pass    | test/sprites/sprites_test.cpp:2605 |
| G13.CO-01  | No overlap ⇒ collision bit stays 0                           | —               | pass    | test/sprites/sprites_test.cpp:2630 |
| G13.CO-02  | Two opaque sprites overlap ⇒ bit 0 = 1                       | —               | pass    | test/sprites/sprites_test.cpp:2644 |
| G13.CO-03  | Collision fires even when `zero_on_top=1` blocks the write   | —               | pass    | test/sprites/sprites_test.cpp:2659 |
| G13.CO-04  | Transparent pixel does not count (spr_line_we=0)             | —               | pass    | test/sprites/sprites_test.cpp:2675 |
| G13.CO-05  | Read of 0x303B clears status                                 | —               | pass    | test/sprites/sprites_test.cpp:2691 |
| G13.CO-06  | Collision bit is sticky across frames until read             | —               | pass    | test/sprites/sprites_test.cpp:2710 |
| G13.OT-01  | Few sprites ⇒ `state_s` returns to S_IDLE before next `line… | —               | pass    | test/sprites/sprites_test.cpp:2724 |
| G13.OT-02  | 128 visible anchors all on same Y, 1× scale ⇒ overtime       | sprites.vhd:977   | pass    | test/sprites/sprites_test.cpp:2743 |
| G13.OT-03  | Overtime bit independent of collision bit                    | —               | pass    | test/sprites/sprites_test.cpp:2763 |
| G13.OT-04  | Both flags can accumulate                                    | sprites.vhd:990-991 | pass    | test/sprites/sprites_test.cpp:2781 |
| G13.SR-01  | Status bits 7:2 always 0                                     | —               | pass    | test/sprites/sprites_test.cpp:2798 |
| G13.SR-02  | Read captures then clears in same cycle                      | —               | pass    | test/sprites/sprites_test.cpp:2815 |
| G13.SR-03  | Status bits update via OR while unread                       | —               | pass    | test/sprites/sprites_test.cpp:2831 |
| G14.RST-01 | `anchor_vis` cleared on reset                                | —               | pass    | test/sprites/sprites_test.cpp:2860 |
| G14.RST-02 | `spr_cur_index` reset to 0                                   | —               | pass    | test/sprites/sprites_test.cpp:2872 |
| G14.RST-03 | `status_reg_s` and `status_reg_read` zeroed                  | —               | pass    | test/sprites/sprites_test.cpp:2879 |
| G14.RST-04 | `mirror_sprite_q` zeroed                                     | —               | pass    | test/sprites/sprites_test.cpp:2888 |
| G14.RST-05 | `line_buf_sel` starts at 0                                   | —               | pass    | test/sprites/sprites_test.cpp:2901 |
| G14.RST-06 | `attr_index` and `pattern_index` zeroed                      | —               | pass    | test/sprites/sprites_test.cpp:2930 |
| G15.NG-01  | Pattern index 64..255 inaccessible via attr3                 | —               | pass    | test/sprites/sprites_test.cpp:2961 |
| G15.NG-02  | Sprite fully off-screen (x=500, y=500) produces zero writes  | —               | pass    | test/sprites/sprites_test.cpp:2975 |
| G15.NG-03  | Sprite at `(x=0, y=0)` with `attr3(7)=1, attr3(6)=0` (no 5t… | —               | pass    | test/sprites/sprites_test.cpp:2986 |
| G15.NG-04  | Palette offset wrap: `paloff=0xF, pat(7:4)=0x1` ⇒ (0xF+0x1)… | —               | pass    | test/sprites/sprites_test.cpp:2999 |
| G15.NG-05  | Zero-size pattern (all bytes = transp colour) ⇒ zero pixels… | —               | pass    | test/sprites/sprites_test.cpp:3015 |
| G15.NG-06  | Relative sprite whose computed `spr_rel_x3(8)=1` but attr3(… | —               | missing | missing                            |
| G15.NG-07  | Negative offset wraps in 9-bit arithmetic: anchor_x=5, rel…  | sprites.vhd:762 | pass    | test/sprites/sprites_test.cpp:3044 |
| G1.AT-13        | NR 0x09 b4 sprite_tie syncs attr_index to mirror sprite_num                    | sprites.vhd:594-612         | pass   | test/sprites/sprites_test.cpp:476    |
| G1.AT-14        | NR 0x35-0x39 must not auto-increment sprite slot                               | zxnext.vhd:4855-4877,4916   | pass   | test/sprites/sprites_test.cpp:503    |
| G1.AT-15        | NR 0x75-0x79 must increment slot after every byte                              | zxnext.vhd:4916             | pass   | test/sprites/sprites_test.cpp:541    |
| G1.AT-16        | NR 0x19 read returns indexed sprite-clip reg (mux at 5956)                     | zxnext.vhd:5956-5970        | missing | missing                              |
| G1.AT-17        | NR 0x1A read returns indexed ULA-clip reg (mux at 5956)                        | zxnext.vhd:5956-5970        | missing | missing                              |
| G16.PSL-01a     | rewind restores baseline (slot 0 cleared)                                      | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3084   |
| G16.PSL-01b     | apply_changes_for_line(0) restores write                                       | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3090   |
| G16.PSL-02a     | Line 0: baseline visible (X=10, Y=20)                                          | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3129   |
| G16.PSL-02b     | Lines 1..99: still baseline (X=10, Y=20)                                       | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3141   |
| G16.PSL-02c     | Line 100: mid-frame write applied (X=200, Y=200)                               | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3150   |
| G16.PSL-02d     | Lines 101..255: post-write state retained                                      | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3161   |
| G16.PSL-03a     | After rewind: slot 0 X back to 0                                               | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3193   |
| G16.PSL-03b     | Lines 0..49: slot 0 X = 0 (baseline)                                           | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3198   |
| G16.PSL-03c     | Line 50: slot 0 X = 50                                                         | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3203   |
| G16.PSL-03d     | Lines 51..149: slot 0 X = 50 (carried)                                         | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3208   |
| G16.PSL-03e     | Line 150: slot 0 X = 150 (second write)                                        | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3213   |
| G16.PSL-04a     | Write logged (count > 0)                                                       | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3226   |
| G16.PSL-04b     | After reset: change_log_size == 0                                              | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3231   |
| G16.PSL-05a     | rewind restores baseline X=42                                                  | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3260   |
| G16.PSL-05b     | start_frame clears log                                                         | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3267   |
| G16.PSL-06      | log saturates at MAX_CHANGES_PER_FRAME                                         | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3283   |
| G16.PSL-07a     | Lines 0..99: byte4 == 0x00 (baseline)                                          | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3323   |
| G16.PSL-07b     | Line 100: byte4 replayed (0xD0)                                                | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3330   |
| G16.PSL-07c     | Lines 101..255: byte4 carries 0xD0                                             | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3337   |
| G16.PSL-08a     | Lines 0..49: NR-0x75-path writes not yet visible                               | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3376   |
| G16.PSL-08b     | Line 50: write_attr_byte mid-frame replayed                                    | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3385   |
| G16.PSL-09a     | Render line 20: sprite pixel at X=10 (baseline)                                | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3445   |
| G16.PSL-09b     | Render line 20: NO sprite pixel at X=200                                       | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3449   |
| G16.PSL-09c     | Render line 100: sprite pixel at X=200 (mid-frame)                             | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3456   |
| G16.PSL-09d     | Render line 100: NO sprite pixel at X=10 (sprite moved)                        | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3460   |
| G16.OVF-01      | Cap-overflow rendering consequence (writes that fit replay; >cap drop)         | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3534   |
| G16.OVF-02      | Overflow warn fires once-per-frame; clears at next start_frame                 | sprites.vhd:327-470         | pass   | test/sprites/sprites_test.cpp:3565   |
| G16.OVF-03      | Z80N-DMA 32 byte-rewrites/line x 256 lines = 8192 writes (boundary)            | sprites.vhd:368-380         | pass   | test/sprites/sprites_test.cpp:3644   |
| G17.PSL-PAT-01a | pre-frame pattern write survives rewind (no log entries)                       | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3702   |
| G17.PSL-PAT-01b | render sees pre-frame pattern byte 0x77 at sprite px 0                         | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3720   |
| G17.PSL-PAT-02a | Line 0: baseline pattern byte 0xAA at sprite px 0                              | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3784   |
| G17.PSL-PAT-02b | Line 99: still baseline pattern byte 0xAA at sprite px 0                       | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3793   |
| G17.PSL-PAT-02c | Line 100: post-write pattern byte 0xBB at sprite px 0                          | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3802   |
| G17.PSL-PAT-02d | Line 200: pattern byte still 0xBB (carried)                                    | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3811   |
| G17.PSL-PAT-03a | Line 0 sprite px = baseline 0x33 (pre-mid-frame pattern)                       | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3852   |
| G17.PSL-PAT-03b | Line 5: tall sprite px = baseline 0x33                                         | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3886   |
| G17.PSL-PAT-03c | Line 20: tall sprite px = post-rewrite 0x66                                    | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3895   |
| G17.PSL-PAT-04a | Pattern write logged (count > 0)                                               | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3909   |
| G17.PSL-PAT-04b | After reset: pattern_change_log_size == 0                                      | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3915   |
| G17.PSL-PAT-05  | log saturates at MAX_PATTERN_CHANGES_PER_FRAME                                 | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3933   |
| G17.PSL-PAT-06a | attr log has one mid-frame entry                                               | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3968   |
| G17.PSL-PAT-06b | pattern log has one mid-frame entry                                            | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3972   |
| G17.PSL-PAT-06c | Line 0: attribute X = baseline 0                                               | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3981   |
| G17.PSL-PAT-06d | Line 29: still baseline X=0                                                    | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3988   |
| G17.PSL-PAT-06e | Line 30: attribute X = 200 (mid-frame attr write)                              | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:3995   |
| G17.PSL-PAT-06f | Line 60: attribute X still 200 (pattern-only mid-frame)                        | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4013   |
| G17.PSL-PAT-06g | Line 60: rendered pixel uses post-rewrite pattern byte 0x99                    | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4052   |
| G17.PSL-PAT-07a | Both writes logged (visible + vblank)                                          | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4090   |
| G17.PSL-PAT-07b | Catch-up: pattern[0]=0xAA in next-frame baseline                               | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4128   |
| G17.PSL-PAT-07c | Catch-up: pattern[1]=0xBB in next-frame baseline (vblank entry flushed)        | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4132   |
| G17.PSL-PAT-08  | Full pattern-RAM re-stream (>16384 bytes/frame) overflows cap                  | sprites.vhd:561-572         | pass   | test/sprites/sprites_test.cpp:4167   |
| G06.NR70-01     | NR 0x70 b5:4 L2 resolution flip mid-frame must reroute width                   | zxnext.vhd:7400-7470        | pass   | test/sprites/sprites_test.cpp:4247   |

## Tilemap — `test/tilemap/tilemap_test.cpp`

Last-touch commit: `d599cd27615bf61efea60c49fdeb38dc7a6116b3` (`d599cd2761`)

| Test ID | Plan row title                  | VHDL file:line | Status  | Test file:line                     |
|---------|---------------------------------|----------------|---------|------------------------------------|
| TM-01   | Tilemap disabled by default     | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:198  |
| TM-02   | Enable tilemap                  | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:207  |
| TM-03   | Disable tilemap                 | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:217  |
| TM-04   | Reset defaults readback         | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:226  |
| TM-10   | 40-col basic display            | tilemap.vhd:382-383 | pass    | test/tilemap/tilemap_test.cpp:382  |
| TM-11   | 40-col tile index range         | tilemap.vhd:393  | pass    | test/tilemap/tilemap_test.cpp:400  |
| TM-12   | 40-col attribute palette offset | tilemap.vhd:382  | pass    | test/tilemap/tilemap_test.cpp:416  |
| TM-13   | 40-col X-mirror                 | tilemap.vhd:320-321 | pass    | test/tilemap/tilemap_test.cpp:440  |
| TM-14   | 40-col Y-mirror                 | tilemap.vhd:322  | pass    | test/tilemap/tilemap_test.cpp:464  |
| TM-15   | 40-col rotation                 | tilemap.vhd:320-324 | pass    | test/tilemap/tilemap_test.cpp:493  |
| TM-16   | 40-col rotation + X-mirror      | tilemap.vhd:320  | pass    | test/tilemap/tilemap_test.cpp:511  |
| TM-17   | 40-col ULA-over-tile flag       | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:527  |
| TM-20   | 80-col basic display            | tilemap.vhd:189  | pass    | test/tilemap/tilemap_test.cpp:550  |
| TM-21   | 80-col tile attributes          | tilemap.vhd:382  | pass    | test/tilemap/tilemap_test.cpp:565  |
| TM-22   | 80-col pixel selection          | tilemap.vhd:228  | pass    | test/tilemap/tilemap_test.cpp:589  |
| TM-30   | 512-tile mode enable            | tilemap.vhd:194  | pass    | test/tilemap/tilemap_test.cpp:621  |
| TM-31   | 512-tile index construction     | tilemap.vhd:393  | pass    | test/tilemap/tilemap_test.cpp:639  |
| TM-32   | 512-tile ULA-over interaction   | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:655  |
| TM-40   | Text mode enable                | tilemap.vhd:191  | pass    | test/tilemap/tilemap_test.cpp:681  |
| TM-41   | Text mode pixel extraction      | tilemap.vhd:385-386 | pass    | test/tilemap/tilemap_test.cpp:709  |
| TM-42   | Text mode palette construction  | tilemap.vhd:386  | pass    | test/tilemap/tilemap_test.cpp:732  |
| TM-43   | Text mode no transforms         | tilemap.vhd:386  | pass    | test/tilemap/tilemap_test.cpp:762  |
| TM-44   | Text mode transparency          | —              | missing | missing                            |
| TM-50   | Strip flags mode                | tilemap.vhd:190  | pass    | test/tilemap/tilemap_test.cpp:811  |
| TM-51   | Default attr applied            | tilemap.vhd:366  | pass    | test/tilemap/tilemap_test.cpp:829  |
| TM-52   | Strip flags + 40-col            | tilemap.vhd:395-398 | pass    | test/tilemap/tilemap_test.cpp:848  |
| TM-53   | Strip flags + 80-col            | tilemap.vhd:328,395-398 | pass    | test/tilemap/tilemap_test.cpp:869  |
| TM-60   | Map base address (bank 5)       | tilemap.vhd:403  | pass    | test/tilemap/tilemap_test.cpp:896  |
| TM-61   | Map base address (bank 7)       | tilemap.vhd:402  | pass    | test/tilemap/tilemap_test.cpp:911  |
| TM-62   | Tile def base (bank 5)          | tilemap.vhd:403  | pass    | test/tilemap/tilemap_test.cpp:926  |
| TM-63   | Tile def base (bank 7)          | tilemap.vhd:402  | pass    | test/tilemap/tilemap_test.cpp:940  |
| TM-64   | Address offset computation      | tilemap.vhd:403  | pass    | test/tilemap/tilemap_test.cpp:961  |
| TM-65   | Tile address with/without flags | tilemap.vhd:396  | pass    | test/tilemap/tilemap_test.cpp:988  |
| TM-70   | Standard pixel address          | tilemap.vhd:394  | pass    | test/tilemap/tilemap_test.cpp:1043 |
| TM-71   | Text mode pixel address         | tilemap.vhd:394  | pass    | test/tilemap/tilemap_test.cpp:1058 |
| TM-72   | Pixel nibble selection          | tilemap.vhd:383  | pass    | test/tilemap/tilemap_test.cpp:1077 |
| TM-80   | X scroll basic                  | tilemap.vhd:309-318 | pass    | test/tilemap/tilemap_test.cpp:1103 |
| TM-81   | X scroll wrap at 320 (40-col)   | tilemap.vhd:315  | pass    | test/tilemap/tilemap_test.cpp:1123 |
| TM-82   | X scroll wrap at 640 (80-col)   | tilemap.vhd:314  | pass    | test/tilemap/tilemap_test.cpp:1142 |
| TM-83   | Y scroll basic                  | tilemap.vhd:326  | pass    | test/tilemap/tilemap_test.cpp:1161 |
| TM-84   | Y scroll wrap at 256            | tilemap.vhd:326  | pass    | test/tilemap/tilemap_test.cpp:1179 |
| TM-85   | Per-line scroll update          | tilemap.vhd:345  | pass    | test/tilemap/tilemap_test.cpp:1206 |
| TM-90   | Standard transparency index     | tilemap.vhd:427  | pass    | test/tilemap/tilemap_test.cpp:1269 |
| TM-91   | Default transparency (0xF)      | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:1277 |
| TM-92   | Custom transparency index       | tilemap.vhd:427  | pass    | test/tilemap/tilemap_test.cpp:1291 |
| TM-93   | Text mode transparency (RGB)    | —              | missing | missing                            |
| TM-94   | Text mode vs standard path      | —              | missing | missing                            |
| TM-100  | Palette select 0                | zxnext.vhd       | pass    | test/tilemap/tilemap_test.cpp:1466 |
| TM-101  | Palette select 1                | —              | pass    | test/tilemap/tilemap_test.cpp:1474 |
| TM-102  | Palette routing                 | —              | pass    | test/tilemap/tilemap_test.cpp:1488 |
| TM-103  | Standard pixel composition      | tilemap.vhd:382-383 | pass    | test/tilemap/tilemap_test.cpp:1502 |
| TM-104  | Text mode pixel composition     | tilemap.vhd:386  | pass    | test/tilemap/tilemap_test.cpp:1516 |
| TM-110  | Default clip (full area)        | tilemap.vhd:424  | pass    | test/tilemap/tilemap_test.cpp:1606 |
| TM-111  | Custom clip window              | —              | pass    | test/tilemap/tilemap_test.cpp:1628 |
| TM-112  | Clip X coordinates              | tilemap.vhd:416-417,424 | pass    | test/tilemap/tilemap_test.cpp:1659 |
| TM-113  | Clip Y coordinates              | —              | pass    | test/tilemap/tilemap_test.cpp:1685 |
| TM-114  | Clip index cycling              | —              | missing | missing                            |
| TM-115  | Clip index reset                | —              | missing | missing                            |
| TM-116  | Clip readback                   | —              | pass    | test/tilemap/tilemap_test.cpp:1705 |
| TM-120  | Tilemap on top (default)        | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:1724 |
| TM-121  | Tilemap always on top           | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:1737 |
| TM-122  | Per-tile below flag             | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:1750 |
| TM-123  | Below flag in compositor        | —              | missing | missing                            |
| TM-124  | tm_on_top overrides per-tile    | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:1769 |
| TM-125  | 512-mode forces below           | tilemap.vhd:388  | pass    | test/tilemap/tilemap_test.cpp:1782 |
| TM-130  | Stencil mode (ULA AND TM)       | —              | missing | missing                            |
| TM-131  | Stencil transparency            | —              | missing | missing                            |
| TM-140  | TM disabled, tm_on_top=0        | —              | missing | missing                            |
| TM-141  | TM disabled, tm_on_top=1        | —              | missing | missing                            |

### Extra coverage (not in plan)

| Test ID | Assertion description                      | VHDL file:line | Test file:line                     |
|---------|--------------------------------------------|----------------|------------------------------------|
| TM-CB1  | Bit 6 = 80-column mode                     | —              | missing                            |
| TM-CB2  | Bit 7 = enable                             | —              | missing                            |
| TM-CB3  | Bit 1 = 512-tile mode (forces below)       | —              | missing                            |
| TM-CB4  | Bit 0 = tm_on_top overrides per-tile below | —              | missing                            |
| TM-CB5  | Bit 5 mapping (VHDL=strip, C++ may differ) | —              | missing                            |
| TM-RR1  | Control register roundtrip                 | —              | missing                            |
| TM-RR2  | Default attr roundtrip                     | —              | missing                            |
| TM-RR3  | Map base roundtrip                         | —              | missing                            |
| TM-RR4  | Def base roundtrip                         | —              | missing                            |
| TM-RR5  | Reset restores all defaults                | —              | missing                            |

### Companion regression suite — `test/tilemap/tilemap_fetch_split_test.cpp`

Raster-split fetch-state regression (TX-1696): NR `0x6E` / `0x6F` / `0x6C` are
changed mid-frame so one map/tile pair draws a fixed HUD and another the
scrolling playfield. The FPGA tilemap consumes these registers as live inputs,
so rendering a completed frame from only their final values paints one
configuration across every scanline.

**What these rows prove is the LATCHING, not a scanline period.** The VHDL
latch fires when the fetch state machine re-enters `S_IDLE`, and
`tilemap.vhd:264` forces that on a HORIZONTAL counter condition — once per tile
COLUMN, ~40/80 times a scanline — so on hardware a mid-scanline write takes
effect from the next tile column of the SAME line. jnext models the same latch
at per-scanline granularity (`Tilemap::snapshot_fetch_for_line`), which is the
project's declared accuracy model. An earlier wording of these rows claimed the
change "affects only subsequent scanlines", which is jnext's model described as
if it were the hardware; it is not, and the rows say so now.

| Test ID     | Assertion description                                                                                      | VHDL file:line                       | Status  | Test file:line                                |
|-------------|------------------------------------------------------------------------------------------------------------|--------------------------------------|---------|-----------------------------------------------|
| TM-SPLIT-01 | NR 0x6E map-base is latched at fetch time, so a change never repaints already-fetched cells                | tilemap.vhd:264,349, zxnext.vhd:4407 | pass    | test/tilemap/tilemap_fetch_split_test.cpp:141 |
| TM-SPLIT-02 | NR 0x6F tile-definition base is latched at fetch time, so a change never repaints already-fetched cells    | tilemap.vhd:264,350, zxnext.vhd:4408 | pass    | test/tilemap/tilemap_fetch_split_test.cpp:170 |
| TM-SPLIT-03 | NR 0x6C default attribute is consumed at fetch time, so a change never repaints already-fetched cells      | tilemap.vhd:264,366, zxnext.vhd:4394 | pass    | test/tilemap/tilemap_fetch_split_test.cpp:199 |
| TM-SPLIT-04 | the same fetch-time latch holds through the full Emulator + Copper path, not just the bare Tilemap fixture | tilemap.vhd:264,349                  | pass    | test/tilemap/tilemap_fetch_split_test.cpp:297 |

## Copper — `test/copper/copper_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID    | Plan row title                                        | VHDL file:line           | Status | Test file:line                   |
|------------|-------------------------------------------------------|--------------------------|--------|----------------------------------|
| RAM-8-01   | `NR 0x60` two-byte upload                             | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:191  |
| RAM-8-02   | `NR 0x60` upload starting odd                         | zxnext.vhd:3977,3998       | pass   | test/copper/copper_test.cpp:209  |
| RAM-16-01  | `NR 0x63` two-byte upload                             | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:230  |
| RAM-P-01   | `NR 0x61` sets low byte                               | zxnext.vhd:5427          | pass   | test/copper/copper_test.cpp:245  |
| RAM-P-02   | `NR 0x62` sets mode and addr hi                       | zxnext.vhd:5430-5431     | pass   | test/copper/copper_test.cpp:258  |
| RAM-P-03   | `NR 0x61` then `NR 0x62` addressing                   | zxnext.vhd:5427          | pass   | test/copper/copper_test.cpp:271  |
| RAM-AI-01  | Auto-increment over 4 bytes                           | zxnext.vhd:5419-5424     | pass   | test/copper/copper_test.cpp:286  |
| RAM-AI-02  | Byte pointer wraps at 0x7FF → 0x000                   | zxnext.vhd:5424          | pass   | test/copper/copper_test.cpp:305  |
| RAM-AI-03  | Full RAM fill                                         | zxnext.vhd:5424,3977/3998  | pass   | test/copper/copper_test.cpp:324  |
| RAM-MIX-01 | `nr_copper_write_8` latch across 0x60/0x63 mix        | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:349  |
| RAM-BK-01  | Read-back `NR 0x61`/`NR 0x62`/`NR 0x64`               | zxnext.vhd:6084          | pass   | test/copper/copper_test.cpp:364  |
| MOV-01     | MOVE NR 0x40 = 0x55                                   | copper.vhd:100-108,87-89   | pass   | test/copper/copper_test.cpp:454  |
| MOV-02     | MOVE to reg 0x7F                                      | copper.vhd:100-108       | pass   | test/copper/copper_test.cpp:472  |
| MOV-03     | MOVE NOP suppresses pulse                             | copper.vhd:104-108       | pass   | test/copper/copper_test.cpp:489  |
| MOV-04     | Two consecutive MOVEs                                 | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:509  |
| MOV-05     | MOVE pulse is exactly 1 clock                         | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:526  |
| MOV-06     | MOVE then WAIT pipeline                               | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:547  |
| MOV-07     | MOVE output width                                     | copper.vhd:42,102          | pass   | test/copper/copper_test.cpp:563  |
| WAI-01     | WAIT (0,0) matches at hcount=12                       | copper.vhd:92-96         | pass   | test/copper/copper_test.cpp:587  |
| WAI-02     | hpos threshold `hpos*8+12`                            | copper.vhd:94            | pass   | test/copper/copper_test.cpp:602  |
| WAI-03     | hpos=63 max                                           | copper.vhd:94            | pass   | test/copper/copper_test.cpp:630  |
| WAI-04     | vpos mismatch stalls                                  | copper.vhd:94            | pass   | test/copper/copper_test.cpp:711  |
| WAI-05     | vpos equality, not >=                                 | copper.vhd:94            | pass   | test/copper/copper_test.cpp:723  |
| WAI-06     | hcount >= once matched                                | copper.vhd:94            | pass   | test/copper/copper_test.cpp:743  |
| WAI-07     | WAIT then MOVE                                        | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:762  |
| WAI-08     | Multiple WAITs                                        | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:784  |
| WAI-09     | WAIT for line 0 edge case                             | copper.vhd:94            | pass   | test/copper/copper_test.cpp:798  |
| WAI-10     | Impossible WAIT, run-once                             | copper.vhd:85-96         | pass   | test/copper/copper_test.cpp:815  |
| WAI-11     | Missed-line WAIT in Run mode                          | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:834  |
| WAI-12     | Missed-line WAIT in Loop mode                         | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:857  |
| CTL-00     | Reset → mode `00` is idle                             | copper.vhd:60-65         | pass   | test/copper/copper_test.cpp:876  |
| CTL-01     | `00` freezes but does not reset                       | copper.vhd:70-78,112-114   | pass   | test/copper/copper_test.cpp:893  |
| CTL-02     | `01` resets addr on entry from `00`                   | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:909  |
| CTL-03     | `11` resets addr on entry from `00`                   | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:925  |
| CTL-04     | `01` does **not** loop                                | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:947  |
| CTL-05     | `11` loops at `cvc=0, hcount=0`                       | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:978  |
| CTL-06a    | **Mode `10` does NOT reset addr on entry**            | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1001 |
| CTL-06b    | **Mode `10` does NOT restart at vblank**              | copper.vhd:80-83         | pass   | test/copper/copper_test.cpp:1019 |
| CTL-06c    | Mode `10` resumes after pause                         | copper.vhd:70-85         | pass   | test/copper/copper_test.cpp:1044 |
| CTL-07     | Mode change clears pending MOVE pulse                 | copper.vhd:78            | pass   | test/copper/copper_test.cpp:1067 |
| CTL-08     | Same-mode rewrite does not reset addr                 | copper.vhd:70            | pass   | test/copper/copper_test.cpp:1083 |
| CTL-09     | Mode `01` → `11` mid-execution                        | copper.vhd:74-76         | pass   | test/copper/copper_test.cpp:1097 |
| CTL-10     | Mode `11` → `10` mid-execution                        | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1113 |
| TIM-01     | MOVE is 2 Copper clocks                               | copper.vhd:87-89         | pass   | test/copper/copper_test.cpp:1141 |
| TIM-02     | WAIT stall is 1 clock per no-match                    | copper.vhd:92-98         | pass   | test/copper/copper_test.cpp:1153 |
| TIM-03     | 10 consecutive MOVEs take 20 clocks                   | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:1169 |
| TIM-04     | WAIT then MOVE pipeline                               | copper.vhd:85-110        | pass   | test/copper/copper_test.cpp:1188 |
| TIM-05     | Dual-port instr fetch available                       | zxnext.vhd:3959-3998     | pass   | test/copper/copper_test.cpp:1205 |
| OFS-01     | Default offset = 0                                    | zxnext.vhd:5024          | pass   | test/copper/copper_test.cpp:1240 |
| OFS-02     | Non-zero offset loads `cvc`                           | zxula_timing.vhd:462     | pass   | test/copper/copper_test.cpp:1263 |
| OFS-03     | WAIT resolves on offset cvc                           | zxula_timing.vhd:462     | pass   | test/copper/copper_test.cpp:1285 |
| OFS-04     | Offset read-back                                      | zxnext.vhd:6090          | pass   | test/copper/copper_test.cpp:1296 |
| OFS-05     | Offset reset                                          | zxnext.vhd:5024          | pass   | test/copper/copper_test.cpp:1307 |
| OFS-06     | `cvc` wraps at `c_max_vc`                             | zxula_timing.vhd:463-464 | pass   | test/copper/copper_test.cpp:1336 |
| ARB-01     | Copper wins simultaneous write                        | zxnext.vhd:4775-4777     | pass   | test/copper/copper_test.cpp:1417 |
| ARB-02     | CPU write deferred until Copper clears                | zxnext.vhd:4769          | pass   | test/copper/copper_test.cpp:1443 |
| ARB-03     | Copper priority on different registers                | zxnext.vhd:4769-4777     | pass   | test/copper/copper_test.cpp:1472 |
| ARB-04     | Copper reg width masked to 7 bits                     | zxnext.vhd:4731          | pass   | test/copper/copper_test.cpp:1370 |
| ARB-05     | No Copper request when stopped                        | zxnext.vhd:4709          | pass   | test/copper/copper_test.cpp:1388 |
| ARB-06     | Copper write to `NR 0x02` triggers NMI signals        | zxnext.vhd:3830-3832,2090,2095-2128 | pass   | test/copper/copper_test.cpp:1523 |
| MUT-01     | Copper writes `NR 0x62` to stop itself                | zxnext.vhd:5430          | pass   | test/copper/copper_test.cpp:1585 |
| MUT-02     | Copper writes `NR 0x62` to switch itself to mode `10` | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1605 |
| MUT-03     | Copper writes its own addr-hi via `NR 0x62`           | zxnext.vhd:5430-5431     | pass   | test/copper/copper_test.cpp:1637 |
| MUT-04     | Copper writes RAM via `NR 0x60` inside a MOVE         | zxnext.vhd:3977          | pass   | test/copper/copper_test.cpp:1662 |
| EDG-01     | Instruction address wraps at 1024                     | copper.vhd:48,108          | pass   | test/copper/copper_test.cpp:1684 |
| EDG-02     | Empty program (first slot WAIT impossible)            | copper.vhd:92-96         | pass   | test/copper/copper_test.cpp:1700 |
| EDG-03     | Program at max size                                   | copper.vhd:108           | pass   | test/copper/copper_test.cpp:1718 |
| EDG-04     | Copper stopped mid-MOVE pulse                         | zxnext.vhd:4709          | pass   | test/copper/copper_test.cpp:1740 |
| EDG-05     | Mode `11` restart coincident with MOVE                | copper.vhd:82-83         | pass   | test/copper/copper_test.cpp:1761 |
| EDG-06     | WAIT hpos=0 matches at hcount=12                      | copper.vhd:94            | pass   | test/copper/copper_test.cpp:1776 |
| EDG-07     | All-WAIT program in Run mode                          | copper.vhd:92-98         | pass   | test/copper/copper_test.cpp:1791 |
| EDG-08     | All-NOP program                                       | copper.vhd:104-108       | pass   | test/copper/copper_test.cpp:1807 |
| EDG-09     | Rapid mode toggling                                   | copper.vhd:70-78         | pass   | test/copper/copper_test.cpp:1833 |
| COP-RST-01 | Copper hard reset                                     | copper.vhd:60-65         | pass   | test/copper/copper_test.cpp:1858 |
| COP-RST-02 | NR state reset                                        | zxnext.vhd:5020-5024     | pass   | test/copper/copper_test.cpp:1873 |
| COP-RST-03 | `last_state_s` reset                                  | copper.vhd:50            | pass   | test/copper/copper_test.cpp:1889 |
| RAM-BK-02       | Read-back NR 0x61 returns nr_copper_addr(7..0) post-autoincrement (G116)  | zxnext.vhd:6083-6084; copper.vhd:42         | pass   | test/copper/copper_test.cpp:386 |
| RAM-BK-03       | Read-back NR 0x62 returns mode & "000" & nr_copper_addr(10..8) (G116)     | zxnext.vhd:6086-6087; copper.vhd:42         | pass   | test/copper/copper_test.cpp:403 |
| TIM-CYC-01      | Copper MOVE burst rate is per 28 MHz cycle, not per Z80 instr (G117 closed 2026-04-30; RE-HOMED to `test/copper/copper_integration_test.cpp` `G117-MPC-01`, which exercises the burst behaviour at the real `Emulator::execute_single_instruction` cadence this bare-class harness cannot reach; see `copper_test.cpp:1210-1219`) | device/copper.vhd:54-119; zxnext.vhd:3950   | missing | missing                          |
| TIM-CYC-02      | Copper WAIT advances per 28 MHz cycle (boundary detection) (G117 closed 2026-04-30; RE-HOMED — same `G117-MPC-01` covers the per-cycle scheduler cadence, and the bare-class WAIT semantics it depended on are already proven live by WAI-01..12 above; `copper_integration_test.cpp:174-177` notes a separate integration row would be redundant) | device/copper.vhd:87-89,92-97               | missing | missing                          |
| COP-RST-04 | Soft reset preserves Copper instruction RAM (dpram2 has no reset) (G118)  | zxnext.vhd:3959-3996; copper.vhd:60-65      | pass   | test/copper/copper_test.cpp:1918 |
| ARB-G65-01      | True tied-edge CPU+Copper write: cpu_req held into next cycle (G65 closed 2026-04-30; RE-HOMED to `test/copper/copper_integration_test.cpp` `G65-PRI-01`, a genuinely equivalent live check at the full-`Emulator` tier — confirmed independently in the Contention GH #196 review, `test/contention/contention_test.cpp:1994-2018`) | zxnext.vhd:4769,4775-4777                   | missing | missing                          |

### Companion integration suite — `test/copper/copper_integration_test.cpp`

Copper rows that need the full machine: the post-G117 cycle-accurate MOVE
scheduler, the CPU-vs-Copper NextREG write arbitration, and the 50/60 Hz
`c_max_vc` wrap re-push.

| Test ID     | Assertion description                                                                                                                 | VHDL file:line                                         | Status  | Test file:line                              |
|-------------|---------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------|---------|---------------------------------------------|
| G117-MPC-01 | 16 Copper MOVEs to NR 0x14 all fire within 3 Z80 instructions (post-G117 cycle-accurate scheduler)                                    | —                                                    | pass    | test/copper/copper_integration_test.cpp:165 |
| G65-PRI-01  | Tied-edge CPU vs Copper NR write: CPU value wins as final                                                                             | zxnext.vhd:4769-4777                                   | pass    | test/copper/copper_integration_test.cpp:247 |
| T58-CVC-01  | runtime 50->60 Hz switch re-pushes the Copper c_max_vc wrap: WAIT vpos=60 with NR 0x64 offset=100 fires at vc=224 in a 264-line frame | zxula_timing.vhd:204/238/457-470, zxnext.vhd:6697-6700 | pass    | test/copper/copper_integration_test.cpp:322 |

## Compositor — `test/compositor/compositor_test.cpp`

Last-touch commit: `3fda139` (compositor 5-feature fix: sprite_en, L2 priority, border, stencil, BL-27 oracle)

| Test ID            | Plan row title                                               | VHDL file:line   | Status  | Test file:line                           |
|--------------------|--------------------------------------------------------------|------------------|---------|------------------------------------------|
| TR-10              | ULA pixel with palette output ≠ NR 0x14 is opaque            | zxnext.vhd:7100    | pass    | test/compositor/compositor_test.cpp:216  |
| TR-11              | ULA pixel with palette output = NR 0x14 is transparent; fal… | zxnext.vhd:7100,7214 | pass    | test/compositor/compositor_test.cpp:239  |
| TR-12              | ULA palette entry whose LSB differs from NR 0x14 LSB is sti… | zxnext.vhd:7100    | pass    | test/compositor/compositor_test.cpp:265  |
| TR-13              | `ula_clipped_2=1` forces ULA transparent regardless of RGB   | zxnext.vhd:7100    | pass    | test/compositor/compositor_test.cpp:282  |
| TR-14              | `ula_en_2=0` forces ULA transparent even if mix_transparent… | zxnext.vhd:7103    | pass    | test/compositor/compositor_test.cpp:296  |
| TR-15              | Compositor is resolution-agnostic at the ULA input boundary… | —                | pass    | test/compositor/compositor_test.cpp:310  |
| TR-16              | NR 0x14 = 0x00 with ULA palette output `RGB[8:1]=0x00` → UL… | zxnext.vhd:7100,7214 | pass    | test/compositor/compositor_test.cpp:329  |
| TR-17              | `ula_border_2` is ignored by stage-2 mix in modes 000/001/0… | —                | pass    | test/compositor/compositor_test.cpp:347  |
| TR-42              | NR 0x15[0] `nr_15_sprite_en = 0` forces every sprite-origin… | zxnext.vhd:6934,6819 | pass    | test/compositor/compositor_test.cpp:366  |
| TR-20              | Tilemap text-mode RGB compare                                | zxnext.vhd:7109    | pass    | test/compositor/compositor_test.cpp:379  |
| TR-21              | Tilemap non-text (attribute) ignores RGB compare             | —                | pass    | test/compositor/compositor_test.cpp:396  |
| TR-22              | Tilemap `pixel_en=0` transparent regardless of mode          | zxnext.vhd:7109    | pass    | test/compositor/compositor_test.cpp:409  |
| TR-23              | `tm_en_2=0` forces TM transparent                            | zxnext.vhd:7109    | pass    | test/compositor/compositor_test.cpp:422  |
| TR-30              | Layer 2 RGB compare                                          | zxnext.vhd:7121    | pass    | test/compositor/compositor_test.cpp:484  |
| TR-31              | Layer 2 `pixel_en=0` transparent                             | —                | pass    | test/compositor/compositor_test.cpp:496  |
| TR-32              | Layer 2 opaque pixel with non-zero `layer2_priority_2` prop… | zxnext.vhd:7123    | pass    | test/compositor/compositor_test.cpp:514  |
| TR-33              | Layer 2 priority forced to 0 when layer is transparent       | zxnext.vhd:7123    | pass    | test/compositor/compositor_test.cpp:529  |
| TR-40              | Sprite `pixel_en=0` transparent                              | —                | pass    | test/compositor/compositor_test.cpp:541  |
| TR-41              | Sprite `pixel_en=1` opaque regardless of NR 0x14             | —                | pass    | test/compositor/compositor_test.cpp:558  |
| TRI-10             | Sprite index matching NR 0x4B produces pixel_en=0 → composi… | sprites.vhd:1067 | pass    | test/compositor/compositor_test.cpp:886  |
| TRI-11             | Sprite index ≠ NR 0x4B and inside active area → pixel_en=1   | sprites.vhd:1067 | pass    | test/compositor/compositor_test.cpp:897  |
| TRI-20             | Tilemap nibble matching NR 0x4C → pixel_en=0                 | zxnext.vhd:4395  | pass    | test/compositor/compositor_test.cpp:910  |
| FB-10              | Fallback appears when all layers transparent (mode 000)      | —                | pass    | test/compositor/compositor_test.cpp:935  |
| FB-11              | Fallback=0x00                                                | —                | pass    | test/compositor/compositor_test.cpp:945  |
| FB-12              | Fallback=0x4A = `0100_1010`                                  | —                | pass    | test/compositor/compositor_test.cpp:955  |
| FB-13              | Fallback=0x01 = `0000_0001`                                  | —                | pass    | test/compositor/compositor_test.cpp:963  |
| FB-14              | Fallback=0x02 = `0000_0010`                                  | —                | pass    | test/compositor/compositor_test.cpp:971  |
| FB-15              | Fallback not used when any layer opaque                      | —                | pass    | test/compositor/compositor_test.cpp:983  |
| FB-16              | Reset default is 0xE3                                        | —                | pass    | test/compositor/compositor_test.cpp:992  |
| FB-17              | All 8 priority modes converge on fallback when every layer…  | —                | pass    | test/compositor/compositor_test.cpp:1012 |
| PRI-010-SLU-3      | Mode 000, all three opaque                                   | —                | pass    | test/compositor/compositor_test.cpp:1056 |
| PRI-010-SLU-LU     | Mode 000, only L+U                                           | —                | pass    | test/compositor/compositor_test.cpp:1057 |
| PRI-010-SLU-U      | Mode 000, only U                                             | —                | pass    | test/compositor/compositor_test.cpp:1058 |
| PRI-010-SLU-0      | Mode 000, none                                               | —                | pass    | test/compositor/compositor_test.cpp:1059 |
| PRI-011-LSU-3      | Mode 001, all three                                          | —                | pass    | test/compositor/compositor_test.cpp:1062 |
| PRI-011-LSU-SU     | Mode 001, S+U only                                           | —                | pass    | test/compositor/compositor_test.cpp:1063 |
| PRI-011-LSU-U      | Mode 001, U only                                             | —                | pass    | test/compositor/compositor_test.cpp:1064 |
| PRI-010-SUL-3      | Mode 010, all three                                          | —                | pass    | test/compositor/compositor_test.cpp:1067 |
| PRI-010-SUL-UL     | Mode 010, U+L                                                | —                | pass    | test/compositor/compositor_test.cpp:1068 |
| PRI-010-SUL-L      | Mode 010, L only                                             | —                | pass    | test/compositor/compositor_test.cpp:1069 |
| PRI-011-LUS-3      | Mode 011, all three                                          | —                | pass    | test/compositor/compositor_test.cpp:1072 |
| PRI-011-LUS-US     | Mode 011, U(non-border)+S                                    | —                | pass    | test/compositor/compositor_test.cpp:1073 |
| PRI-011-LUS-S      | Mode 011, S only                                             | —                | pass    | test/compositor/compositor_test.cpp:1074 |
| PRI-011-LUS-border | Mode 011, U(border) + S + TM transp                          | zxnext.vhd:7256    | pass    | test/compositor/compositor_test.cpp:1097 |
| PRI-100-USL-3      | Mode 100, all three                                          | —                | pass    | test/compositor/compositor_test.cpp:1077 |
| PRI-100-USL-border | Mode 100, U(border) + S, TM transp, L=✗                      | —                | pass    | test/compositor/compositor_test.cpp:1111 |
| PRI-100-USL-L      | Mode 100, L only                                             | —                | pass    | test/compositor/compositor_test.cpp:1078 |
| PRI-101-ULS-3      | Mode 101, all three                                          | —                | pass    | test/compositor/compositor_test.cpp:1081 |
| PRI-101-ULS-border | Mode 101, U(border)+L+S, TM transp                           | —                | pass    | test/compositor/compositor_test.cpp:1128 |
| PRI-101-ULS-S      | Mode 101, S only                                             | —                | pass    | test/compositor/compositor_test.cpp:1082 |
| PRI-B-0            | In every mode 000..101 with all three transparent, fallback… | —                | pass    | test/compositor/compositor_test.cpp:1153 |
| PRI-B-1            | Mode 000 with NR 0x14 = sprite_rgb[8:1] must not transparen… | —                | pass    | test/compositor/compositor_test.cpp:1164 |
| PRI-B-2            | Mode 001: even if sprite opaque, L2 opaque beats it          | —                | pass    | test/compositor/compositor_test.cpp:1176 |
| L2P-10             | Promotion in mode 000 over sprite                            | —                | pass    | test/compositor/compositor_test.cpp:1196 |
| L2P-11             | Promotion in mode 010 over sprite                            | —                | pass    | test/compositor/compositor_test.cpp:1197 |
| L2P-12             | Promotion in mode 100 over sprite (L2 above U)               | —                | pass    | test/compositor/compositor_test.cpp:1198 |
| L2P-13             | Promotion in mode 101 over sprite (L2 above U)               | —                | pass    | test/compositor/compositor_test.cpp:1199 |
| L2P-14             | No-op in mode 001 (L2 already top)                           | —                | pass    | test/compositor/compositor_test.cpp:1200 |
| L2P-15             | No-op in mode 011 (L2 already top)                           | —                | pass    | test/compositor/compositor_test.cpp:1201 |
| L2P-16             | `layer2_transparent=1` suppresses promotion                  | —                | pass    | test/compositor/compositor_test.cpp:1223 |
| L2P-17             | Promotion in mode 110 (blend): L2 promoted shows blend outp… | —                | pass    | test/compositor/compositor_test.cpp:1247 |
| L2P-18             | Promotion in mode 111 (blend): L2 promoted shows blend outp… | —                | pass    | test/compositor/compositor_test.cpp:1262 |
| BL-10              | Add no clamp                                                 | —                | pass    | test/compositor/compositor_test.cpp:1377 |
| BL-11              | Add clamp hi                                                 | —                | pass    | test/compositor/compositor_test.cpp:1390 |
| BL-12              | Add 0+0                                                      | —                | pass    | test/compositor/compositor_test.cpp:1403 |
| BL-13              | Add, `mix_top` opaque beats blend                            | —                | pass    | test/compositor/compositor_test.cpp:1419 |
| BL-14              | Add, sprite between mix_top and mix_bot                      | —                | pass    | test/compositor/compositor_test.cpp:1433 |
| BL-15              | Add, mix_bot wins after sprite transp                        | —                | pass    | test/compositor/compositor_test.cpp:1449 |
| BL-16              | Add, final fallback to blend                                 | —                | pass    | test/compositor/compositor_test.cpp:1463 |
| BL-20              | Sub, ≤4 clamps to 0                                          | —                | pass    | test/compositor/compositor_test.cpp:1477 |
| BL-21              | Sub, ≥12 clamps to 7                                         | —                | pass    | test/compositor/compositor_test.cpp:1491 |
| BL-22              | Sub, middle value                                            | —                | pass    | test/compositor/compositor_test.cpp:1505 |
| BL-23              | Sub gated by `mix_rgb_transparent`                           | —                | pass    | test/compositor/compositor_test.cpp:1519 |
| BL-24              | Sub, mix_top opaque wins over blend                          | —                | pass    | test/compositor/compositor_test.cpp:1532 |
| BL-25              | Sub, sprite between                                          | —                | pass    | test/compositor/compositor_test.cpp:1544 |
| BL-26              | Sub, mix_bot fallback                                        | —                | pass    | test/compositor/compositor_test.cpp:1556 |
| BL-27              | Sub, final L2-only fallback shows blended L2                 | —                | pass    | test/compositor/compositor_test.cpp:1573 |
| BL-28              | L2 priority bit overrides blend (mode 110)                   | —                | pass    | test/compositor/compositor_test.cpp:1589 |
| BL-29              | L2 priority bit overrides blend (mode 111)                   | —                | pass    | test/compositor/compositor_test.cpp:1604 |
| L2P-19             | Native 640: layer2_priority_[] not pixel-doubled (G93)       | zxnext.vhd:7039  | pass    | test/compositor/compositor_test.cpp:1319 |
| BL-30              | Mode 01 prio6: mix_bot=ULA wins (TM transp, tm_below=0)      | zxnext.vhd:7163  | pass    | test/compositor/compositor_test.cpp:1629 |
| BL-31              | Mode 01 prio6: mix_top=TM (ULA masked, tm_below=0)           | zxnext.vhd:7163  | pass    | test/compositor/compositor_test.cpp:1652 |
| BL-32              | Mode 01 prio6: tm_below=1 swap, mix_top=ULA wins             | zxnext.vhd:7163  | pass    | test/compositor/compositor_test.cpp:1675 |
| BL-40              | Mode 10 prio6: mix_rgb=ula_final, add(L2,ULA)                | zxnext.vhd:7149  | pass    | test/compositor/compositor_test.cpp:1696 |
| BL-41              | Mode 10 prio6: ulatm merge → TM (tm_below=1), add(L2,TM)     | zxnext.vhd:7115  | pass    | test/compositor/compositor_test.cpp:1718 |
| BL-42              | Mode 10 prio6: stencil ULA&TM routes via ula_final_rgb       | zxnext.vhd:7130  | pass    | test/compositor/compositor_test.cpp:1743 |
| BL-50              | Mode 11 prio6: mix_bot=ULA wins (tm_below=0)                 | zxnext.vhd:7156  | pass    | test/compositor/compositor_test.cpp:1769 |
| BL-51              | Mode 11 prio6: tm_below=1, mix_top=ULA wins                  | zxnext.vhd:7156  | pass    | test/compositor/compositor_test.cpp:1792 |
| BL-52              | Mode 11 prio6: TM as mix_rgb when ULA overlays transp        | zxnext.vhd:7156  | pass    | test/compositor/compositor_test.cpp:1816 |
| BL-60              | Mode 11 prio7 (sub): sub(L2,TM) = (4,2,0) cascade-to-mixer   | zxnext.vhd:7156  | pass    | test/compositor/compositor_test.cpp:1839 |
| UB-G26-01          | UTB-40/41 mix_top/mix_bot swap on tm_pixel_below_2 (G26)     | zxnext.vhd:7163  | missing | missing                                  |
| UB-G26-02          | L2 priority over opaque mix_top in modes 110/111 (G26)       | zxnext.vhd:7300  | pass    | test/compositor/compositor_test.cpp:1986 |
| PFF-G108-01        | NR 0x69 b5:0 → port_ff_reg(5:0) fan-out missing (G108)       | zxnext.vhd:3617  | pass    | test/compositor/compositor_integration_test.cpp:536 |
| PFF-G108-02        | NR 0x22 b2 → port_ff_reg(6) fan-out missing (G108)           | zxnext.vhd:3619  | pass    | test/compositor/compositor_integration_test.cpp:555 |
| PFF-G108-03        | NR 0xC4 b0 → port_ff_reg(6) inverted fan-out missing (G108)  | zxnext.vhd:3621  | pass    | test/compositor/compositor_integration_test.cpp:593 |
| UDIS-03            | NR 0x68 b6:5 decode → Renderer::blend_mode (UDIS-03 closed)  | zxnext.vhd:7141  | pass    | test/compositor/compositor_test.cpp:2399 |
| BLANK-G27-01       | rgb_blank_n_6 lockstep with rgb_out_6 pipeline (G27)         | zxnext.vhd:7395  | pass    | test/compositor/compositor_test.cpp:2598 |
| PSCAN-01           | write_8bit appends change-log entry tagged with current_li…  | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:2781 |
| PSCAN-02           | rewind_to_baseline restores live palette to frame start      | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:2802 |
| PSCAN-03           | apply_changes_for_line replays only matching lines, monoto…  | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:2859 |
| PSCAN-04           | change_log cap; overflow_warned_ latches once per frame      | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:2921 |
| PSCAN-05           | Renderer::render_frame replays per-line palette mid-frame    | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:3066 |
| PSCAN-G04-01       | NR 0x14 per-scanline replay not implemented (G04)            | zxnext.vhd:1137  | pass    | test/compositor/compositor_test.cpp:3304 |
| PSCAN-G04-02       | NR 0x4B per-scanline replay not implemented (G04)            | zxnext.vhd:5016  | missing | missing                                  |
| PSCAN-G04-03       | NR 0x4C per-scanline replay not implemented (G04)            | zxnext.vhd:5018  | missing | missing                                  |
| PSCAN-G11-01       | NR 0x68 b0 stencil per-scanline replay not implemented (G11) | zxnext.vhd:5450  | pass    | test/compositor/compositor_test.cpp:3367 |
| PSCAN-G11-02       | NR 0x68 b6:5 blend per-scanline replay not implemented (G11) | zxnext.vhd:5446  | pass    | test/compositor/compositor_test.cpp:3395 |
| PSCAN-G11-03       | NR 0x68 b3 ulap_en per-scanline replay not implemented (G11) | zxnext.vhd:5445  | pass    | test/compositor/compositor_test.cpp:3428 |
| UTB-10             | Mode 00, TM above                                            | —                | pass    | test/compositor/compositor_test.cpp:1869 |
| UTB-11             | Mode 00, TM below                                            | —                | pass    | test/compositor/compositor_test.cpp:1882 |
| UTB-20             | Mode 10, stencil-off combined                                | —                | pass    | test/compositor/compositor_test.cpp:1895 |
| UTB-30             | Mode 11, TM as U, ULA floats below                           | —                | pass    | test/compositor/compositor_test.cpp:1913 |
| UTB-31             | Mode 11, TM as U, ULA floats above                           | —                | pass    | test/compositor/compositor_test.cpp:1928 |
| UTB-40             | Mode 01 (`others`), below=0                                  | zxnext.vhd:7163-7177 | pass    | test/compositor/compositor_test.cpp:1944 |
| UTB-41             | Mode 01, below=1                                             | zxnext.vhd:7163-7177 | pass    | test/compositor/compositor_test.cpp:1957 |
| STEN-10            | Bitwise AND                                                  | —                | pass    | test/compositor/compositor_test.cpp:2113 |
| STEN-11            | AND with zero                                                | —                | pass    | test/compositor/compositor_test.cpp:2130 |
| STEN-12            | ULA transparent → stencil transparent                        | —                | pass    | test/compositor/compositor_test.cpp:2146 |
| STEN-13            | TM transparent → stencil transparent                         | —                | pass    | test/compositor/compositor_test.cpp:2162 |
| STEN-14            | Both transparent → transparent                               | —                | pass    | test/compositor/compositor_test.cpp:2175 |
| STEN-15            | Stencil inactive if `tm_en=0` (even with bit set)            | —                | pass    | test/compositor/compositor_test.cpp:2190 |
| STEN-16            | Stencil inactive if `ula_en=0`                               | —                | pass    | test/compositor/compositor_test.cpp:2203 |
| STEN-17            | Stencil off (bit=0), both enabled                            | —                | pass    | test/compositor/compositor_test.cpp:2216 |
| SOB-10             | Sprite rgb arrives at compositor with `sprite_pixel_en_2=1`… | sprites.vhd        | pass    | test/compositor/compositor_test.cpp:2425 |
| LINE-10            | Write NR 0x15[4:2] mid-line                                  | zxnext.vhd:6799    | pass    | test/compositor/compositor_test.cpp:2456 |
| LINE-11            | Write NR 0x14 mid-line                                       | —                | pass    | test/compositor/compositor_test.cpp:2473 |
| LINE-12            | Write NR 0x4A mid-line                                       | —                | pass    | test/compositor/compositor_test.cpp:2487 |
| LINE-13            | Copper write to NR 0x15 at end of line                       | —                | pass    | test/compositor/compositor_test.cpp:2500 |
| LINE-14            | Two writes in one line: only the last is visible next line   | —                | pass    | test/compositor/compositor_test.cpp:2510 |
| BLANK-10           | Active area passes through                                   | —                | pass    | test/compositor/compositor_test.cpp:2531 |
| BLANK-11           | Horizontal blanking forces 0                                 | —                | pass    | test/compositor/compositor_test.cpp:2545 |
| BLANK-12           | Vertical blanking forces 0                                   | —                | pass    | test/compositor/compositor_test.cpp:2548 |
| BLANK-13           | Fallback colour is NOT shown during blank                    | —                | pass    | test/compositor/compositor_test.cpp:2551 |
| PAL-10             | ULA pixel index → ULA/TM palette                             | —                | pass    | test/compositor/compositor_test.cpp:2626 |
| PAL-11             | ULA background substitution uses fallback                    | —                | pass    | test/compositor/compositor_test.cpp:2644 |
| PAL-12             | LoRes pixel overrides ULA background                         | —                | pass    | test/compositor/compositor_test.cpp:2657 |
| PAL-13             | L2 palette select 0 vs 1 (NR 0x43[2])                        | —                | pass    | test/compositor/compositor_test.cpp:2674 |
| PAL-14             | L2 palette bit 15 surfaces as `layer2_priority_2`            | —                | pass    | test/compositor/compositor_test.cpp:2690 |
| PAL-15             | Sprite palette is L2/Sprite RAM `sc(0)=1`                    | —                | pass    | test/compositor/compositor_test.cpp:2702 |
| RST-10             | After reset, all layers are transparent (TM disabled, S dis… | —                | pass    | test/compositor/compositor_test.cpp:2722 |
| RST-11             | After reset, mode is 000 (SLU)                               | —                | pass    | test/compositor/compositor_test.cpp:2735 |
| RST-12             | After reset, NR 0x4A = 0xE3                                  | —                | pass    | test/compositor/compositor_test.cpp:2744 |
| RST-13             | After reset, NR 0x14 = 0xE3                                  | —                | pass    | test/compositor/compositor_test.cpp:2754 |
| PRI-BOUND          | 3                                                            | —                | pass    | test/compositor/compositor_test.cpp:1138 |

### Companion integration suite — `test/compositor/compositor_integration_test.cpp`

Created 2026-04-24 (UDIS plan closure) to host end-to-end UDIS-class rows that require a full `Emulator` fixture (NR 0x68 bit 7 ULA-disable observed at the framebuffer level, including Copper mid-frame MOVE NR 0x68,0x80). Runtime: `Total:    8  Passed:    8  Failed:    0  Skipped:    0`. Each row is a live pass. Only the 2 UDIS rows are listed below. Of the other 6 live rows, `PFF-G108-01/02/03` are recorded in the parent `## Compositor` table — they are Compositor plan rows re-homed here 2026-04-28, not new rows; `PFF-G108-02b` is recorded only by sub-letter aliasing under `PFF-G108-02` (the script's `ALIASED` report); and `PFF-G108-04` + `PSCAN-VBLANK-COALESCE-01` are recorded nowhere (its `UNRECORDED` report). Both reports print on every run.

| Test ID            | Plan row title                                               | VHDL file:line   | Status  | Test file:line                                       |
|--------------------|--------------------------------------------------------------|------------------|---------|------------------------------------------------------|
| UDIS-01            | NR 0x68 b7 toggles whole-ULA transparency end-to-end         | zxnext.vhd:7103  | pass    | test/compositor/compositor_integration_test.cpp:274  |
| UDIS-02            | Copper mid-frame MOVE NR 0x68,0x80 flips ULA-enable line 100 | zxnext.vhd:7103  | pass    | test/compositor/compositor_integration_test.cpp:393  |


## Audio — `test/audio/audio_test.cpp` + `test/audio/audio_nextreg_test.cpp` + `test/audio/audio_port_dispatch_test.cpp`

Last-touch commit: `0020b7102565f8ca8555633aa662e4714db2f86a` (`0020b71025`)

| Test ID | Plan row title                                               | VHDL file:line | Status  | Test file:line                 |
|---------|--------------------------------------------------------------|----------------|---------|--------------------------------|
| AY-01   | Write register address via `busctrl_addr`                    | ym2149.vhd:172-173 | pass    | test/audio/audio_test.cpp:115  |
| AY-02   | Address only latches when `busctrl_addr=1`                   | ym2149.vhd:172   | pass    | test/audio/audio_test.cpp:126  |
| AY-03   | Reset clears address to 0                                    | ym2149.vhd:170-171 | pass    | test/audio/audio_test.cpp:136  |
| AY-04   | Write to all 16 registers (addr 0-15)                        | ym2149.vhd:189-207 | pass    | test/audio/audio_test.cpp:157  |
| AY-05   | Write with `addr[4]=1` is ignored                            | ym2149.vhd:188   | pass    | test/audio/audio_test.cpp:168  |
| AY-06   | Reset initialises all registers to 0x00                      | ym2149.vhd:184-186 | pass    | test/audio/audio_test.cpp:186  |
| AY-07   | Writing R13 triggers envelope reset                          | ym2149.vhd:209-211,392-401 | pass    | test/audio/audio_test.cpp:202  |
| AY-10   | Read R0 (Ch A fine tone) in AY mode                          | ym2149.vhd:226   | pass    | test/audio/audio_test.cpp:222  |
| AY-11   | Read R1 (Ch A coarse tone) in AY mode                        | ym2149.vhd:227   | pass    | test/audio/audio_test.cpp:233  |
| AY-12   | Read R1 in YM mode                                           | ym2149.vhd:227   | pass    | test/audio/audio_test.cpp:244  |
| AY-13   | Read R3, R5 (Ch B/C coarse tone) AY vs YM                    | ym2149.vhd:229,231 | pass    | test/audio/audio_test.cpp:262  |
| AY-14   | Read R6 (noise period) in AY mode                            | ym2149.vhd:232   | pass    | test/audio/audio_test.cpp:274  |
| AY-15   | Read R6 in YM mode                                           | ym2149.vhd:232   | pass    | test/audio/audio_test.cpp:285  |
| AY-16   | Read R7 (mixer enable)                                       | ym2149.vhd:233   | pass    | test/audio/audio_test.cpp:298  |
| AY-17   | Read R8/R9/R10 (volume) in AY mode                           | ym2149.vhd:234-236 | pass    | test/audio/audio_test.cpp:313  |
| AY-18   | Read R8/R9/R10 in YM mode                                    | ym2149.vhd:234-236 | pass    | test/audio/audio_test.cpp:329  |
| AY-19   | Read R13 (envelope shape) in AY mode                         | ym2149.vhd:239   | pass    | test/audio/audio_test.cpp:341  |
| AY-20   | Read R13 in YM mode                                          | ym2149.vhd:239   | pass    | test/audio/audio_test.cpp:352  |
| AY-21   | Read R11/R12 (envelope period)                               | ym2149.vhd:237-238 | pass    | test/audio/audio_test.cpp:368  |
| AY-22   | Read addr >= 16 in YM mode                                   | ym2149.vhd:222-223 | pass    | test/audio/audio_test.cpp:379  |
| AY-23   | Read addr >= 16 in AY mode                                   | ym2149.vhd:222   | pass    | test/audio/audio_test.cpp:391  |
| AY-24   | Read with `I_REG=1` (register query mode)                    | ym2149.vhd:220-221 | pass    | test/audio/audio_test.cpp:401  |
| AY-25   | AY_ID is "11" for PSG0, "10" for PSG1, "01" for PSG2         | turbosound.vhd:158,213,268 | pass    | test/audio/audio_test.cpp:413  |
| AY-30   | Read R14 with R7 bit 6 = 0 (Port A input mode)               | —              | missing | missing                        |
| AY-31   | Read R14 with R7 bit 6 = 1 (Port A output mode)              | —              | missing | missing                        |
| AY-32   | Read R15 with R7 bit 7 = 0 (Port B input mode)               | —              | missing | missing                        |
| AY-33   | Read R15 with R7 bit 7 = 1 (Port B output mode)              | —              | missing | missing                        |
| AY-34   | Port A/B inputs default to 0xFF (pullup)                     | turbosound.vhd:158 | missing | missing                        |
| AY-40   | Divider reloads with `I_SEL_L=1` (AY compat)                 | ym2149.vhd:260-279 | pass    | test/audio/audio_test.cpp:468  |
| AY-41   | Divider reloads with `I_SEL_L=0` (YM mode)                   | —              | missing | missing                        |
| AY-42   | `ena_div` pulses once per divider cycle                      | ym2149.vhd:264-268 | pass    | test/audio/audio_test.cpp:486  |
| AY-43   | `ena_div_noise` at half `ena_div` rate                       | —              | missing | missing                        |
| AY-44   | In turbosound wiring, `I_SEL_L='1'` always                   | turbosound.vhd:164 | pass    | test/audio/audio_test.cpp:512  |
| AY-50   | Tone period 0 or 1 produces constant high output             | ym2149.vhd:310   | pass    | test/audio/audio_test.cpp:530  |
| AY-51   | Tone period 2 toggles every 2 ena_div cycles                 | ym2149.vhd:310   | pass    | test/audio/audio_test.cpp:544  |
| AY-52   | Tone period 0xFFF (max) produces lowest freq                 | ym2149.vhd:310   | pass    | test/audio/audio_test.cpp:554  |
| AY-53   | Channel A uses R1[3:0] & R0                                  | ym2149.vhd:306   | pass    | test/audio/audio_test.cpp:564  |
| AY-54   | Channel B uses R3[3:0] & R2                                  | ym2149.vhd:307   | pass    | test/audio/audio_test.cpp:574  |
| AY-55   | Channel C uses R5[3:0] & R4                                  | ym2149.vhd:308   | pass    | test/audio/audio_test.cpp:584  |
| AY-56   | Tone output toggles (not pulse)                              | ym2149.vhd:321-322 | pass    | test/audio/audio_test.cpp:604  |
| AY-60   | Noise period from R6[4:0]                                    | ym2149.vhd:283   | pass    | test/audio/audio_test.cpp:621  |
| AY-61   | Noise period 0 or 1 => comparator 0                          | ym2149.vhd:283   | pass    | test/audio/audio_test.cpp:630  |
| AY-62   | Noise uses 17-bit LFSR (poly17)                              | ym2149.vhd:284,293 | pass    | test/audio/audio_test.cpp:653  |
| AY-63   | Noise output is poly17 bit 0                                 | —              | missing | missing                        |
| AY-64   | Noise clocked at `ena_div_noise` rate                        | —              | missing | missing                        |
| AY-70   | R7 bit 0 = 0: Channel A tone enabled                         | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:686  |
| AY-71   | R7 bit 0 = 1: Channel A tone disabled (forced 1)             | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:698  |
| AY-72   | R7 bit 3 = 0: Channel A noise enabled                        | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:715  |
| AY-73   | R7 bit 3 = 1: Channel A noise disabled (forced 1)            | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:727  |
| AY-74   | R7 bits 1,4: Channel B tone + noise control                  | ym2149.vhd:470   | pass    | test/audio/audio_test.cpp:740  |
| AY-75   | R7 bits 2,5: Channel C tone + noise control                  | ym2149.vhd:471   | pass    | test/audio/audio_test.cpp:743  |
| AY-76   | Both tone and noise disabled: constant high                  | ym2149.vhd:469-471 | pass    | test/audio/audio_test.cpp:757  |
| AY-77   | Both tone and noise enabled: AND of both                     | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:777  |
| AY-78   | Mixer output 0 => volume output 0                            | ym2149.vhd:469   | pass    | test/audio/audio_test.cpp:795  |
| AY-80   | R8 bit 4 = 0: Channel A uses fixed volume                    | ym2149.vhd:472-520 | pass    | test/audio/audio_test.cpp:815  |
| AY-81   | R8 bit 4 = 1: Channel A uses envelope volume                 | ym2149.vhd:472-520 | pass    | test/audio/audio_test.cpp:830  |
| AY-82   | Fixed volume 0 => output "00000"                             | ym2149.vhd:472-520 | pass    | test/audio/audio_test.cpp:842  |
| AY-83   | Fixed volume 1-15 => `{vol[3:0], "1"}`                       | ym2149.vhd:472-520 | pass    | test/audio/audio_test.cpp:861  |
| AY-84   | Same for R9 (Channel B) and R10 (Channel C)                  | ym2149.vhd:472-520 | pass    | test/audio/audio_test.cpp:874  |
| AY-90   | YM mode: 32-entry volume table                               | ym2149.vhd:157-162 | pass    | test/audio/audio_test.cpp:902  |
| AY-91   | AY mode: 16-entry volume table                               | ym2149.vhd:150-155 | pass    | test/audio/audio_test.cpp:914  |
| AY-92   | YM vol 0 = 0x00, vol 31 = 0xFF                               | ym2149.vhd:157-162 | pass    | test/audio/audio_test.cpp:934  |
| AY-93   | AY vol 0 = 0x00, vol 15 = 0xFF                               | ym2149.vhd:150-155 | pass    | test/audio/audio_test.cpp:955  |
| AY-94   | YM volume table exact values                                 | ym2149.vhd:157-162 | pass    | test/audio/audio_test.cpp:981  |
| AY-95   | AY volume table exact values                                 | ym2149.vhd:150-155 | pass    | test/audio/audio_test.cpp:1005 |
| AY-96   | Reset sets all audio outputs to 0x00                         | ym2149.vhd:184-186 | pass    | test/audio/audio_test.cpp:1017 |
| AY-100  | Envelope period from R12:R11 (16-bit)                        | ym2149.vhd:334   | pass    | test/audio/audio_test.cpp:1036 |
| AY-101  | Envelope period 0 or 1 => comparator 0                       | ym2149.vhd:335   | pass    | test/audio/audio_test.cpp:1046 |
| AY-102  | Writing R13 resets envelope counter to 0                     | ym2149.vhd:340-342 | pass    | test/audio/audio_test.cpp:1068 |
| AY-103  | Writing R13 resets envelope to initial state                 | —              | missing | missing                        |
| AY-110  | 0-3                                                          | ym2149.vhd:412-421 | pass    | test/audio/audio_test.cpp:1087 |
| AY-111  | 4-7                                                          | ym2149.vhd:412-421 | pass    | test/audio/audio_test.cpp:1102 |
| AY-112  | 8                                                            | ym2149.vhd:411   | pass    | test/audio/audio_test.cpp:1122 |
| AY-113  | 9                                                            | ym2149.vhd:428-431 | pass    | test/audio/audio_test.cpp:1141 |
| AY-114  | 10                                                           | ym2149.vhd:444-461 | pass    | test/audio/audio_test.cpp:1161 |
| AY-115  | 11                                                           | ym2149.vhd:424-427 | pass    | test/audio/audio_test.cpp:1176 |
| AY-116  | 12                                                           | ym2149.vhd:411   | pass    | test/audio/audio_test.cpp:1196 |
| AY-117  | 13                                                           | ym2149.vhd:438-441 | pass    | test/audio/audio_test.cpp:1212 |
| AY-118  | 14                                                           | ym2149.vhd:444-461 | pass    | test/audio/audio_test.cpp:1232 |
| AY-119  | 15                                                           | ym2149.vhd:434-437 | pass    | test/audio/audio_test.cpp:1247 |
| AY-120  | Attack=0 (At bit): initial vol=31, direction=down            | —              | missing | missing                        |
| AY-121  | Attack=1 (At bit): initial vol=0, direction=up               | —              | missing | missing                        |
| AY-122  | C=0: hold after first ramp regardless of Al/H                | ym2149.vhd:412-421 | pass    | test/audio/audio_test.cpp:1267 |
| AY-123  | C=1, H=1, Al=0: hold one step inside boundary                | ym2149.vhd:422-443 | pass    | test/audio/audio_test.cpp:1285 |
| AY-124  | C=1, H=1, Al=1: hold exactly at boundary                     | ym2149.vhd:422-443 | pass    | test/audio/audio_test.cpp:1304 |
| AY-125  | C=1, H=0, Al=1: triangle wave (continuous)                   | —              | missing | missing                        |
| AY-126  | C=1, H=0, Al=0: sawtooth (continuous)                        | —              | missing | missing                        |
| AY-127  | Envelope steps through 32 levels (0-31)                      | —              | missing | missing                        |
| AY-128  | Envelope period counter reset on R13 write                   | —              | missing | missing                        |
| TS-01   | Reset selects AY#0 (`ay_select = "11"`)                      | turbosound.vhd:123 | pass    | test/audio/audio_test.cpp:1330 |
| TS-02   | Select AY#0: write 0xFC+ to FFFD with bits[4:2]=111, bits[1… | turbosound.vhd:134 | pass    | test/audio/audio_test.cpp:1357 |
| TS-03   | Select AY#1: write with bits[1:0]=10                         | turbosound.vhd:132 | pass    | test/audio/audio_test.cpp:1343 |
| TS-04   | Select AY#2: write with bits[1:0]=01                         | turbosound.vhd:133 | pass    | test/audio/audio_test.cpp:1350 |
| TS-05   | Selection requires `turbosound_en_i = 1`                     | turbosound.vhd:129 | pass    | test/audio/audio_test.cpp:1369 |
| TS-06   | Selection requires `psg_reg_addr_i = 1`                      | turbosound.vhd:129 | pass    | test/audio/audio_test.cpp:1382 |
| TS-07   | Selection requires `psg_d_i[7] = 1`                          | turbosound.vhd:129 | pass    | test/audio/audio_test.cpp:1394 |
| TS-08   | Selection requires `psg_d_i[4:2] = "111"`                    | turbosound.vhd:129 | pass    | test/audio/audio_test.cpp:1406 |
| TS-09   | Panning set simultaneously: bits[6:5]                        | turbosound.vhd:132-134,323-327 | pass    | test/audio/audio_test.cpp:1428 |
| TS-10   | Reset sets all panning to "11" (both L+R)                    | turbosound.vhd:123-127,186-192 | pass    | test/audio/audio_test.cpp:1449 |
| TS-15   | Normal register address: bits[7:5] must be "000"             | turbosound.vhd:141 | pass    | test/audio/audio_test.cpp:1467 |
| TS-16   | Address routed to selected AY only                           | turbosound.vhd:143-150 | pass    | test/audio/audio_test.cpp:1482 |
| TS-17   | Write routed to selected AY only                             | —              | missing | missing                        |
| TS-18   | Readback from selected AY                                    | turbosound.vhd:321 | pass    | test/audio/audio_test.cpp:1504 |
| TS-20   | ABC stereo mode (`stereo_mode_i=0`): L=A+B, R=B+C            | turbosound.vhd:186-190 | pass    | test/audio/audio_test.cpp:1536 |
| TS-21   | ACB stereo mode (`stereo_mode_i=1`): L=A+C, R=C+B            | turbosound.vhd:186-190 | pass    | test/audio/audio_test.cpp:1564 |
| TS-22   | Mono mode for PSG0: L=R=A+B+C                                | turbosound.vhd:189-192 | pass    | test/audio/audio_test.cpp:1592 |
| TS-23   | Mono mode per-PSG: each bit controls one PSG                 | turbosound.vhd:189-192 | pass    | test/audio/audio_test.cpp:1622 |
| TS-24   | Stereo mode is global for all PSGs                           | turbosound.vhd:186,241,296 | pass    | test/audio/audio_test.cpp:1687 |
| TS-30   | Turbosound disabled: only selected PSG outputs               | turbosound.vhd:197-203 | pass    | test/audio/audio_test.cpp:1705 |
| TS-31   | Turbosound enabled: all three PSGs output                    | turbosound.vhd:197,252,307 | pass    | test/audio/audio_test.cpp:1726 |
| TS-32   | PSG0 active when `ay_select="11"` or ts enabled              | turbosound.vhd:197 | pass    | test/audio/audio_test.cpp:1781 |
| TS-33   | PSG1 active when `ay_select="10"` or ts enabled              | turbosound.vhd:252 | pass    | test/audio/audio_test.cpp:1794 |
| TS-34   | PSG2 active when `ay_select="01"` or ts enabled              | turbosound.vhd:307 | pass    | test/audio/audio_test.cpp:1807 |
| TS-40   | Pan "11": output to both L and R                             | —              | missing | missing                        |
| TS-41   | Pan "10": output to L only, R silenced                       | turbosound.vhd:323-327 | pass    | test/audio/audio_test.cpp:1899 |
| TS-42   | Pan "01": output to R only, L silenced                       | turbosound.vhd:186-192,323-329 | pass    | test/audio/audio_test.cpp:1922 |
| TS-43   | Pan "00": output silenced on both channels                   | turbosound.vhd:323-329 | pass    | test/audio/audio_test.cpp:1940 |
| TS-44   | Final L = sum of all three PSG L contributions               | turbosound.vhd:331-336 | pass    | test/audio/audio_test.cpp:1968 |
| TS-45   | Final R = sum of all three PSG R contributions               | turbosound.vhd:331-336 | pass    | test/audio/audio_test.cpp:1971 |
| TS-50   | PSG0 has AY_ID = "11"                                        | turbosound.vhd:158 | pass    | test/audio/audio_test.cpp:1985 |
| TS-51   | PSG1 has AY_ID = "10"                                        | turbosound.vhd:213 | pass    | test/audio/audio_test.cpp:1991 |
| TS-52   | PSG2 has AY_ID = "01"                                        | turbosound.vhd:268 | pass    | test/audio/audio_test.cpp:1997 |
| TS-60   | `TurboSound::reset()` clears only `ay_select` + `psg{0,1,2}_pan` (not enabled/stereo/mono)  | turbosound.vhd:118-138                  | pass    | test/audio/audio_test.cpp:1839                  |
| TS-61   | psg_mode=11 → audio_ay_reset must not clobber NR 0x08 b1/b5 or NR 0x09 mono bits            | zxnext.vhd (NR 0x06 psg_mode=11 strobe) | pass    | test/audio/audio_test.cpp:1868                  |
| SD-01   | Reset sets all channels to 0x80                              | soundrive.vhd:72-78 | pass    | test/audio/audio_test.cpp:2012 |
| AUD-SD-02 | Write channel A via port I/O (`chA_wr_i`)                    | soundrive.vhd:81-82 | pass    | test/audio/audio_test.cpp:2022 |
| SD-03   | Write channel B via port I/O (`chB_wr_i`)                    | soundrive.vhd:87-88 | pass    | test/audio/audio_test.cpp:2031 |
| SD-04   | Write channel C via port I/O (`chC_wr_i`)                    | soundrive.vhd:93-94 | pass    | test/audio/audio_test.cpp:2040 |
| SD-05   | Write channel D via port I/O (`chD_wr_i`)                    | soundrive.vhd:99-100 | pass    | test/audio/audio_test.cpp:2049 |
| SD-06   | NextREG 0x2D (mono) writes to chA AND chD                    | soundrive.vhd:83-85,101-103 | pass    | test/audio/audio_test.cpp:2058 |
| SD-07   | NextREG 0x2C (left) writes to chB only                       | soundrive.vhd:89-91 | pass    | test/audio/audio_test.cpp:2068 |
| SD-08   | NextREG 0x2E (right) writes to chC only                      | soundrive.vhd:95-97 | pass    | test/audio/audio_test.cpp:2078 |
| SD-09   | Port I/O takes priority over NextREG                         | —              | missing | missing                        |
| SD-10   | Soundrive mode 1 ports: 0x1F(A), 0x0F(B), 0x4F(C), 0x5F(D)   | zxnext.vhd:2429                         | pass    | test/audio/audio_port_dispatch_test.cpp:177    |
| SD-11   | Soundrive mode 2 ports: 0xF1(A), 0xF3(B), 0xF9(C), 0xFB(D)   | zxnext.vhd:2432                         | pass    | test/audio/audio_port_dispatch_test.cpp:198    |
| AUD-SD-12 | Profi Covox: 0x3F(A), 0x5F(D)                                | zxnext.vhd:2431/2661/2664               | pass    | test/audio/audio_port_dispatch_test.cpp:220    |
| AUD-SD-13 | Covox: 0x0F(B), 0x4F(C)                                      | zxnext.vhd:2659                         | pass    | test/audio/audio_port_dispatch_test.cpp:243    |
| AUD-SD-14 | Pentagon/ATM mono: 0xFB(A+D)                                 | zxnext.vhd:2433/2661/2664               | pass    | test/audio/audio_port_dispatch_test.cpp:273    |
| AUD-SD-15 | GS Covox: 0xB3(B+C)                                          | zxnext.vhd:2659/2662-2663               | pass    | test/audio/audio_port_dispatch_test.cpp:290    |
| AUD-SD-16 | SpecDrum: 0xDF(A+D)                                          | zxnext.vhd:2662                         | pass    | test/audio/audio_port_dispatch_test.cpp:308    |
| AUD-SD-17 | DAC requires `nr_08_dac_en=1`                                | zxnext.vhd:5179, :6436                  | pass    | test/audio/audio_nextreg_test.cpp:1022         |
| AUD-SD-18 | Mono ports (FB, DF, B3) write to both A+D or B+C             | zxnext.vhd port-decode fan              | pass    | test/audio/audio_port_dispatch_test.cpp:335    |
| AUD-SD-19 | DAC channels reset to 0x80 on `nr_08_dac_en` 1→0 transition                                 | soundrive.vhd:69-78; zxnext.vhd:6436    | pass    | test/audio/audio_nextreg_test.cpp:1078         |
| AUD-SD-20 | Left output = chA + chB (9-bit unsigned)                     | soundrive.vhd:112 | pass    | test/audio/audio_test.cpp:2121 |
| AUD-SD-21 | Right output = chC + chD (9-bit unsigned)                    | soundrive.vhd:113 | pass    | test/audio/audio_test.cpp:2131 |
| AUD-SD-22 | Max output: chA=0xFF, chB=0xFF => L=0x1FE                    | soundrive.vhd:112 | pass    | test/audio/audio_test.cpp:2141 |
| AUD-SD-23 | Reset output: L=0x100, R=0x100                               | soundrive.vhd:72-78,112-113 | pass    | test/audio/audio_test.cpp:2149 |
| BP-01   | Port 0xFE write stores bits [4:0]                            | zxnext.vhd:3593                         | pass    | test/audio/audio_port_dispatch_test.cpp:373    |
| BP-02   | Bit 4 is the EAR output (speaker)                            | zxnext.vhd:3598  | pass    | test/audio/audio_test.cpp:2180 |
| BP-03   | Bit 3 is the MIC output                                      | zxnext.vhd:3599  | pass    | test/audio/audio_test.cpp:2189 |
| BP-05   | Reset clears port_fe_reg to 0                                | zxnext.vhd:3591  | pass    | test/audio/audio_test.cpp:2201 |
| BP-06   | Port 0xFE decoded as A0=0                                    | zxnext.vhd:2582-2583,2711,2714          | pass    | test/audio/audio_port_dispatch_test.cpp:407    |
| BP-10   | `beep_mic_final` = `EAR_in XOR (mic AND issue2) XOR mic`     | zxnext.vhd:6503                         | pass    | test/audio/audio_nextreg_test.cpp:724          |
| BP-11   | Issue 2 mode: MIC is XOR'd twice (cancels)                   | zxnext.vhd:6503                         | pass    | test/audio/audio_nextreg_test.cpp:741          |
| BP-12   | Issue 3 mode: MIC contributes to beep                        | zxnext.vhd:6503                         | pass    | test/audio/audio_nextreg_test.cpp:760          |
| BP-13   | Internal speaker exclusive mode                              | zxnext.vhd:6504                         | pass    | test/audio/audio_nextreg_test.cpp:796          |
| MX-01   | EAR volume = 0x0200 (512) when active                        | audio_mixer.vhd:63,80 | pass    | test/audio/audio_test.cpp:2234 |
| MX-02   | MIC volume = 0x0080 (128) when active                        | audio_mixer.vhd:64,81 | pass    | test/audio/audio_test.cpp:2246 |
| AUD-MX-03 | EAR/MIC silenced when `exc_i=1`                              | zxnext.vhd:6504,6514                      | pass    | test/audio/audio_nextreg_test.cpp:831          |
| AUD-MX-04 | AY input: zero-extended 12-bit to 13-bit                     | audio_mixer.vhd:83-84 | pass    | test/audio/audio_test.cpp:2273 |
| MX-05   | DAC input: 9-bit left-shifted by 2 + zero-padded             | audio_mixer.vhd:86-87 | pass    | test/audio/audio_test.cpp:2287 |
| MX-06   | I2S input: zero-extended 10-bit to 13-bit                    | audio_mixer.vhd:89-90,99-100 | pass    | test/audio/audio_test.cpp:2322 |
| MX-07   | I2S min (0,0) is a full-NEGATIVE excursion, not silence      | i2s.vhd:179                               | pass    | test/audio/audio_test.cpp:2348 |
| MX-10   | Left output = ear + mic + ay_L + dac_L + i2s_L               | audio_mixer.vhd:99 | pass    | test/audio/audio_test.cpp:2371 |
| MX-11   | Right output = ear + mic + ay_R + dac_R + i2s_R              | audio_mixer.vhd:100 | pass    | test/audio/audio_test.cpp:2382 |
| MX-12   | Reset zeroes both output channels                            | audio_mixer.vhd:95-97 | pass    | test/audio/audio_test.cpp:2419 |
| MX-13   | EAR and MIC go to both L and R                               | audio_mixer.vhd:99-100 | pass    | test/audio/audio_test.cpp:2432 |
| MX-14   | Max theoretical output = 5998                                | audio_mixer.vhd:99 | pass    | test/audio/audio_test.cpp:2448 |
| MX-15   | No saturation/clipping in mixer                              | —              | missing | missing                        |
| MX-16   | Silence with the Pi I2S input wired and idle is digital 0    | zxnext.vhd:2358-2359                      | pass    | test/audio/audio_test.cpp:2406 |
| MX-17   | An assembled power-on machine emits digital 0 (16 samples)   | zxnext.vhd:2358-2359                      | pass    | test/audio/audio_nextreg_test.cpp:976          |
| MX-20   | `exc_i=1`: EAR and MIC contribute 0 to mix                   | audio_mixer.vhd:80                      | pass    | test/audio/audio_nextreg_test.cpp:853          |
| MX-21   | `exc_i=0`: EAR and MIC contribute normally                   | —              | missing | missing                        |
| MX-22   | `exc_i` derived from NextREGs 0x06 bit 6 AND 0x08 bit 4      | zxnext.vhd:6504                         | pass    | test/audio/audio_nextreg_test.cpp:874          |
| MX-23   | Mixer drops EAR/MIC contribution when `exc_i=1` (downstream gate)                           | audio_mixer.vhd:80-81                   | pass    | test/audio/audio_nextreg_test.cpp:928          |
| MX-30   | Pi I2S source delivers a continuous 10-bit sample stream (not single-latch)                 | audio_mixer.vhd (I2S input chain)       | missing | missing                                        |
| AUD-NR-01 | `nr_06_psg_mode[1:0]` from NextREG 0x06 bits [1:0]           | zxnext.vhd:5170,6389                      | pass    | test/audio/audio_nextreg_test.cpp:162          |
| AUD-NR-02 | Mode "00": YM2149 mode                                       | zxnext.vhd (NR 0x06 b1:0)               | pass    | test/audio/audio_nextreg_test.cpp:174          |
| AUD-NR-03 | Mode "01": AY-8910 mode                                      | zxnext.vhd (NR 0x06 b1:0)               | pass    | test/audio/audio_nextreg_test.cpp:185          |
| AUD-NR-04 | Mode "10": YM2149 mode (bit 0 = 0)                           | zxnext.vhd (NR 0x06 b1:0)               | pass    | test/audio/audio_nextreg_test.cpp:198          |
| AUD-NR-05 | Mode "11": AY reset (silent)                                 | zxnext.vhd:6379,6387                      | pass    | test/audio/audio_nextreg_test.cpp:224          |
| AUD-NR-06 | `nr_06_internal_speaker_beep` from bit 6                     | zxnext.vhd (NR 0x06 b6 latch)           | pass    | test/audio/audio_nextreg_test.cpp:239          |
| AUD-NR-10 | Bit 5: PSG stereo mode (0=ABC, 1=ACB)                        | zxnext.vhd (NR 0x08 b5 → stereo_mode)   | pass    | test/audio/audio_nextreg_test.cpp:271          |
| AUD-NR-11 | Bit 4: Internal speaker enable                               | zxnext.vhd:5178,1116,5906               | pass    | test/audio/audio_nextreg_test.cpp:289          |
| NR-12   | Bit 3: DAC enable                                            | zxnext.vhd:5179                         | pass    | test/audio/audio_nextreg_test.cpp:308          |
| AUD-NR-13 | Bit 1: Turbosound enable                                     | zxnext.vhd:5181,6390                      | pass    | test/audio/audio_nextreg_test.cpp:322          |
| AUD-NR-14 | Bit 0: Keyboard Issue 2 mode                                 | zxnext.vhd:5182,5906                      | pass    | test/audio/audio_nextreg_test.cpp:337          |
| NR-20   | Bits [7:5] of NextREG 0x09: per-PSG mono                     | zxnext.vhd (NR 0x09 b7:5 → mono_mode)   | pass    | test/audio/audio_nextreg_test.cpp:366          |
| NR-21   | Bit 7: PSG2 mono, Bit 6: PSG1 mono, Bit 5: PSG0 mono         | zxnext.vhd:5186,6398                      | pass    | test/audio/audio_nextreg_test.cpp:387          |
| NR-30   | NextREG 0x2C: write to Soundrive chB (left)                  | zxnext.vhd:4852,6453                      | pass    | test/audio/audio_nextreg_test.cpp:427          |
| NR-31   | NextREG 0x2D: write to Soundrive chA+chD (mono)              | zxnext.vhd:4853,6452                      | pass    | test/audio/audio_nextreg_test.cpp:443          |
| NR-32   | NextREG 0x2E: write to Soundrive chC (right)                 | zxnext.vhd:4854; soundrive.vhd:95-97    | pass    | test/audio/audio_nextreg_test.cpp:458          |
| NR-33   | NR 0x2C / NR 0x2E read returns `pi_audio_L/R(9 downto 2)` (not regs_[] shadow)              | zxnext.vhd:6006-6015,2358-2359            | pass    | test/audio/audio_nextreg_test.cpp:492          |
| NR-34   | NR 0x2D read returns `nr_2d_i2s_sample & "000000"` (low 2 bits from prior 0x2C/0x2E read)   | zxnext.vhd:6010-6011,6008,6015            | pass    | test/audio/audio_nextreg_test.cpp:529          |
| NR-43   | Mixer gates Pi I2S adder by `pi_i2s_en[L/R] AND NOT mute[L/R]`                              | audio_mixer.vhd:50-60                   | pass    | test/audio/audio_nextreg_test.cpp:668          |
| IO-01   | Port FFFD: `A[15:14]="11"`, A[2]=1, A[0]=1                   | zxnext.vhd:2647                         | pass    | test/audio/audio_port_dispatch_test.cpp:446    |
| IO-02   | Port BFFD: `A[15:14]="10"`, A[2]=1, A[0]=1                   | zxnext.vhd:2648                         | pass    | test/audio/audio_port_dispatch_test.cpp:462    |
| IO-03   | Port BFF5: BFFD with A[3]=0                                  | zxnext.vhd:2649/6395                    | pass    | test/audio/audio_port_dispatch_test.cpp:481    |
| IO-04   | FFFD read latched on falling CPU clock edge                  | —              | missing | missing                        |
| IO-05   | BFFD readable as FFFD on +3 timing                           | zxnext.vhd:2771                         | pass    | test/audio/audio_port_dispatch_test.cpp:542    |
| IO-10   | DAC writes require `dac_hw_en=1`                             | zxnext.vhd:2775-2778, :6436             | pass    | test/audio/audio_nextreg_test.cpp:1048         |
| IO-11   | Multiple port mappings can map to same channel               | zxnext.vhd port-decode alias            | pass    | test/audio/audio_port_dispatch_test.cpp:579    |
| IO-12   | Port FD conflict: F1 and F9 in mode 2                        | zxnext.vhd:2777                         | pass    | test/audio/audio_port_dispatch_test.cpp:617    |
| IO-13   | NR 0x84 b1 gates Soundrive Mode 2 ports (0xF1/F3/F9/FB)                                      | zxnext.vhd:2429-2435                    | pass    | test/audio/audio_port_dispatch_test.cpp:673    |
| IO-14   | NR 0x84 b3 gates Profi Covox ports (0x3F chA / 0x5F chD)                                     | zxnext.vhd:2429-2435                    | pass    | test/audio/audio_port_dispatch_test.cpp:693    |
| IO-15   | NR 0x84 b4 gates Covox ports (0x0F chB / 0x4F chC)                                           | zxnext.vhd:2429-2435                    | pass    | test/audio/audio_port_dispatch_test.cpp:719    |
| IO-16   | NR 0x84 b6 gates GS Covox port (0xB3 chB+chC)                                                | zxnext.vhd:2429-2435                    | pass    | test/audio/audio_port_dispatch_test.cpp:741    |
| IO-17   | NR 0x84 b7 gates SpecDrum port (0xDF chA+chD)                                                | zxnext.vhd:2429-2435                    | pass    | test/audio/audio_port_dispatch_test.cpp:763    |

## DMA — `test/dma/dma_test.cpp`

Last-touch commit: `651ea41d76a30d6745a4a83c7fa79d859d61ae77` (`651ea41d76`)

| Test ID | Plan row title                         | VHDL file:line | Status  | Test file:line             |
|---------|----------------------------------------|----------------|---------|----------------------------|
| 1.1     | Write to port 0x6B sets ZXN mode       | dma.vhd:664-665  | pass    | test/dma/dma_test.cpp:173  |
| 1.2     | Write to port 0x0B sets Z80-DMA mode   | dma.vhd:666-667  | pass    | test/dma/dma_test.cpp:183  |
| 1.3     | Read from port 0x6B sets ZXN mode      | dma.vhd:673-674  | pass    | test/dma/dma_test.cpp:197  |
| 1.4     | Read from port 0x0B sets Z80 mode      | dma.vhd:675-676  | pass    | test/dma/dma_test.cpp:208  |
| 1.5     | Mode defaults to ZXN (0) on reset      | —              | missing | missing                    |
| 1.6     | Mode switches on each access           | dma.vhd:664-668  | pass    | test/dma/dma_test.cpp:225  |
| 2.1     | R0 direction A->B                      | dma.vhd:656-658  | pass    | test/dma/dma_test.cpp:253  |
| 2.2     | R0 direction B->A                      | dma.vhd:659-662  | pass    | test/dma/dma_test.cpp:269  |
| 2.3     | R0 port A start address low byte       | dma.vhd:739      | pass    | test/dma/dma_test.cpp:282  |
| 2.4     | R0 port A start address high byte      | dma.vhd:752      | pass    | test/dma/dma_test.cpp:295  |
| 2.5     | R0 port A full 16-bit address          | dma.vhd:739,752  | pass    | test/dma/dma_test.cpp:309  |
| 2.6     | R0 block length low byte               | dma.vhd:763      | pass    | test/dma/dma_test.cpp:321  |
| 2.7     | R0 block length high byte              | dma.vhd:772      | pass    | test/dma/dma_test.cpp:334  |
| 2.8     | R0 selective byte programming          | dma.vhd:518-538  | pass    | test/dma/dma_test.cpp:350  |
| 3.1     | Port A is memory (default)             | dma.vhd:542      | pass    | test/dma/dma_test.cpp:379  |
| 3.2     | Port A is I/O                          | dma.vhd:542      | pass    | test/dma/dma_test.cpp:398  |
| 3.3     | Port A address increment               | dma.vhd:543      | pass    | test/dma/dma_test.cpp:408  |
| 3.4     | Port A address decrement               | dma.vhd:543      | pass    | test/dma/dma_test.cpp:417  |
| 3.5     | Port A address fixed                   | dma.vhd:543      | pass    | test/dma/dma_test.cpp:426  |
| 3.6     | Port A timing byte                     | dma.vhd:776      | pass    | test/dma/dma_test.cpp:439  |
| 4.1     | Port B is memory (default)             | dma.vhd:559      | pass    | test/dma/dma_test.cpp:464  |
| 4.2     | Port B is I/O                          | dma.vhd:559      | pass    | test/dma/dma_test.cpp:483  |
| 4.3     | Port B address increment               | dma.vhd:560      | pass    | test/dma/dma_test.cpp:493  |
| 4.4     | Port B address decrement               | dma.vhd:560      | pass    | test/dma/dma_test.cpp:502  |
| 4.5     | Port B address fixed                   | dma.vhd:560      | pass    | test/dma/dma_test.cpp:511  |
| 4.6     | Port B timing byte                     | dma.vhd:790      | pass    | test/dma/dma_test.cpp:524  |
| 4.7     | Port B prescaler byte                  | dma.vhd:799      | pass    | test/dma/dma_test.cpp:546  |
| 4.8     | Port B prescaler = 0 (no delay)        | dma.vhd:424      | pass    | test/dma/dma_test.cpp:560  |
| 5.1     | R3 with bit 6=1 triggers START_DMA     | dma.vhd:576-579  | pass    | test/dma/dma_test.cpp:582  |
| 5.2     | R3 with bit 6=0 does not start         | dma.vhd:576      | pass    | test/dma/dma_test.cpp:591  |
| 5.3     | R3 mask byte (bit 3)                   | dma.vhd:576-582  | pass    | test/dma/dma_test.cpp:605  |
| 5.4     | R3 match byte (bit 4)                  | dma.vhd:576-582  | pass    | test/dma/dma_test.cpp:620  |
| 6.1     | Byte mode (R4_mode = "00")             | dma.vhd:601      | pass    | test/dma/dma_test.cpp:643  |
| 6.2     | Continuous mode (R4_mode = "01")       | dma.vhd:601      | pass    | test/dma/dma_test.cpp:652  |
| 6.3     | Burst mode (R4_mode = "10")            | dma.vhd:601      | pass    | test/dma/dma_test.cpp:661  |
| 6.4     | Default mode is continuous ("01")      | dma.vhd:236      | pass    | test/dma/dma_test.cpp:670  |
| 6.5     | Port B start address low               | dma.vhd:816      | pass    | test/dma/dma_test.cpp:685  |
| 6.6     | Port B start address high              | dma.vhd:827      | pass    | test/dma/dma_test.cpp:701  |
| 6.7     | Port B full 16-bit address             | dma.vhd:816,827  | pass    | test/dma/dma_test.cpp:715  |
| 6.8     | Mode "11" treated as "00" (byte)       | dma.vhd:601      | pass    | test/dma/dma_test.cpp:731  |
| 7.1     | Auto-restart enabled                   | dma.vhd:473-491  | pass    | test/dma/dma_test.cpp:759  |
| 7.2     | Auto-restart disabled (default)        | dma.vhd:238,494  | pass    | test/dma/dma_test.cpp:773  |
| 7.3     | CE/WAIT mux bit                        | —              | missing | missing                    |
| 7.4     | R5 defaults on reset                   | —              | missing | missing                    |
| 8.1     | 0xC3 — Reset                           | dma.vhd:638      | pass    | test/dma/dma_test.cpp:812  |
| 8.2     | 0xC7 — Reset port A timing             | dma.vhd:648      | pass    | test/dma/dma_test.cpp:822  |
| 8.3     | 0xCB — Reset port B timing             | dma.vhd:651      | pass    | test/dma/dma_test.cpp:833  |
| 8.4     | 0xCF — Load                            | dma.vhd:654      | pass    | test/dma/dma_test.cpp:848  |
| 8.5     | 0xCF — Load A->B direction             | dma.vhd:656-658  | pass    | test/dma/dma_test.cpp:862  |
| 8.6     | 0xCF — Load B->A direction             | dma.vhd:660-662  | pass    | test/dma/dma_test.cpp:877  |
| 8.7     | 0xCF — Load counter ZXN mode           | dma.vhd:664-665  | pass    | test/dma/dma_test.cpp:888  |
| 8.8     | 0xCF — Load counter Z80 mode           | dma.vhd:666-667  | pass    | test/dma/dma_test.cpp:898  |
| 8.9     | 0xD3 — Continue                        | dma.vhd:670-676  | pass    | test/dma/dma_test.cpp:912  |
| 8.10    | 0xD3 — Continue ZXN mode               | dma.vhd:673-674  | pass    | test/dma/dma_test.cpp:924  |
| 8.11    | 0xD3 — Continue Z80 mode               | dma.vhd:675-676  | pass    | test/dma/dma_test.cpp:934  |
| 8.12    | 0x87 — Enable DMA                      | dma.vhd:725      | pass    | test/dma/dma_test.cpp:943  |
| 8.13    | 0x83 — Disable DMA                     | dma.vhd:728      | pass    | test/dma/dma_test.cpp:953  |
| 8.14    | 0x8B — Reinitialize status             | dma.vhd:691-692,902 | pass    | test/dma/dma_test.cpp:970  |
| 8.15    | 0xBB — Read mask follows               | dma.vhd:731,859-860 | pass    | test/dma/dma_test.cpp:986  |
| 8.16    | 0xBF — Read status byte                | dma.vhd:696-699  | pass    | test/dma/dma_test.cpp:1002 |
| 9.1     | Simple A->B, increment both            | dma.vhd:379-391  | pass    | test/dma/dma_test.cpp:1025 |
| 9.2     | Simple B->A, increment both            | dma.vhd:660-662,389-391 | pass    | test/dma/dma_test.cpp:1045 |
| 9.3     | A->B, decrement source                 | dma.vhd:384-387  | pass    | test/dma/dma_test.cpp:1066 |
| 9.4     | A->B, fixed source (fill)              | dma.vhd:379-396  | pass    | test/dma/dma_test.cpp:1087 |
| 9.5     | A->B, fixed dest (probe)               | dma.vhd:389-396  | pass    | test/dma/dma_test.cpp:1106 |
| 9.6     | Block length = 1                       | dma.vhd:426      | pass    | test/dma/dma_test.cpp:1120 |
| 9.7     | Block length = 256                     | dma.vhd:426      | pass    | test/dma/dma_test.cpp:1134 |
| 9.8     | Block length = 0 (edge case)           | dma.vhd:361,426  | pass    | test/dma/dma_test.cpp:1151 |
| 10.1    | Mem(A) -> IO(B), A inc, B fixed        | dma.vhd:559      | pass    | test/dma/dma_test.cpp:1182 |
| 10.2    | Mem(A) -> IO(B), A inc, B inc          | dma.vhd:559,389-391 | pass    | test/dma/dma_test.cpp:1201 |
| 10.3    | Verify MREQ on read, IORQ on write     | dma.vhd:186-190,290-296 | pass    | test/dma/dma_test.cpp:1232 |
| 10.4    | IO(A) -> Mem(B)                        | dma.vhd:542      | pass    | test/dma/dma_test.cpp:1255 |
| 10.5    | IO(A) -> IO(B)                         | dma.vhd:542,559  | pass    | test/dma/dma_test.cpp:1274 |
| 10.6    | Port B address as I/O port             | dma.vhd:36       | pass    | test/dma/dma_test.cpp:1294 |
| 11.1    | Both increment (A->B)                  | dma.vhd:379-391  | pass    | test/dma/dma_test.cpp:1316 |
| 11.2    | Both decrement (A->B)                  | dma.vhd:384-396  | pass    | test/dma/dma_test.cpp:1335 |
| 11.3    | Source inc, dest dec                   | dma.vhd:379-396  | pass    | test/dma/dma_test.cpp:1354 |
| 11.4    | Source dec, dest fixed                 | dma.vhd:384-396  | pass    | test/dma/dma_test.cpp:1374 |
| 11.5    | Both fixed (port-to-port)              | dma.vhd:379-396  | pass    | test/dma/dma_test.cpp:1394 |
| 11.6    | Address wrap at 0xFFFF                 | dma.vhd:36,381   | pass    | test/dma/dma_test.cpp:1415 |
| 12.1    | Continuous mode — full block           | dma.vhd:426-430,601 | pass    | test/dma/dma_test.cpp:1441 |
| 12.2    | Burst mode — no prescaler              | dma.vhd:424      | pass    | test/dma/dma_test.cpp:1464 |
| 12.3    | Burst mode — with prescaler            | dma.vhd:424-425  | pass    | test/dma/dma_test.cpp:1486 |
| 12.4    | Burst mode — bus release timing        | dma.vhd:445      | pass    | test/dma/dma_test.cpp:1509 |
| 12.5    | Burst mode — bus re-request            | dma.vhd:451-460  | pass    | test/dma/dma_test.cpp:1534 |
| 12.6    | Byte mode — single byte                | dma.vhd:426      | pass    | test/dma/dma_test.cpp:1561 |
| 12.7    | Continuous mode — no prescaler delay   | dma.vhd:424      | pass    | test/dma/dma_test.cpp:1593 |
| 12.8    | Burst mode — prescaler vs timer        | dma.vhd:424      | pass    | test/dma/dma_test.cpp:1612 |
| 13.1    | Prescaler = 0 (no wait)                | dma.vhd:424      | pass    | test/dma/dma_test.cpp:1644 |
| 13.2    | Prescaler > 0 at 3.5MHz                | dma.vhd:251      | pass    | test/dma/dma_test.cpp:1656 |
| 13.3    | Prescaler > 0 at 7MHz                  | dma.vhd:252      | pass    | test/dma/dma_test.cpp:1667 |
| 13.4    | Prescaler > 0 at 14MHz                 | dma.vhd:253      | pass    | test/dma/dma_test.cpp:1678 |
| 13.5    | Prescaler > 0 at 28MHz                 | dma.vhd:254      | pass    | test/dma/dma_test.cpp:1689 |
| 13.6    | Prescaler comparison                   | dma.vhd:424      | pass    | test/dma/dma_test.cpp:1703 |
| 13.7    | turbo=10 (14MHz): source byte latched on rising edge of dma_d_p_s (G122, WONT 2026-05-03) | dma.vhd:172-181         | missing | missing                    |
| 13.8    | turbo=10 (14MHz): rd_n / wr_n strobes extended across READ_4/WRITE_4 (G122, WONT 2026-05-03) | dma.vhd:158,160-161     | missing | missing                    |
| 14.1    | ZXN mode: counter starts at 0          | dma.vhd:664-665  | pass    | test/dma/dma_test.cpp:1761 |
| 14.2    | Z80 mode: counter starts at 0xFFFF     | dma.vhd:666-667  | pass    | test/dma/dma_test.cpp:1770 |
| 14.3    | Counter increments per byte            | dma.vhd:361      | pass    | test/dma/dma_test.cpp:1782 |
| 14.4    | ZXN: block_len=5 transfers 5 bytes     | dma.vhd:426      | pass    | test/dma/dma_test.cpp:1794 |
| 14.5    | Z80: block_len=5 transfers 6 bytes     | dma.vhd:426,666-667 | pass    | test/dma/dma_test.cpp:1809 |
| 14.6    | ZXN: block_len=0 transfers 0 bytes     | dma.vhd:361,426  | pass    | test/dma/dma_test.cpp:1824 |
| 14.7    | Z80: block_len=0 transfers 1 byte      | dma.vhd:361,426,667 | pass    | test/dma/dma_test.cpp:1836 |
| 14.8    | Counter readback accuracy              | dma.vhd:933-947  | pass    | test/dma/dma_test.cpp:1854 |
| 15.1    | DMA requests bus before transfer       | dma.vhd:278      | pass    | test/dma/dma_test.cpp:1877 |
| 15.2    | DMA waits for bus acknowledge          | dma.vhd:296      | pass    | test/dma/dma_test.cpp:1893 |
| 15.3    | DMA releases bus when idle             | dma.vhd:225,262  | pass    | test/dma/dma_test.cpp:1902 |
| 15.4    | DMA defers to external BUSREQ          | dma.vhd:269      | pass    | test/dma/dma_test.cpp:1916 |
| 15.5    | DMA defers to daisy chain              | dma.vhd:269      | pass    | test/dma/dma_test.cpp:1930 |
| 15.6    | DMA defers to IM2 delay                | dma.vhd:269      | pass    | test/dma/dma_test.cpp:1943 |
| 15.7    | Bus mux when DMA holds bus             | zxnext.vhd       | pass    | test/dma/dma_test.cpp:1956 |
| 15.8    | DMA cannot self-program                | —              | missing | missing                    |
| 16.1    | Auto-restart reloads addresses         | dma.vhd:473-481  | pass    | test/dma/dma_test.cpp:1985 |
| 16.2    | Auto-restart reloads counter           | dma.vhd:482-486  | pass    | test/dma/dma_test.cpp:1998 |
| 16.3    | Auto-restart direction A->B            | dma.vhd:474-476  | pass    | test/dma/dma_test.cpp:2011 |
| 16.4    | Auto-restart direction B->A            | dma.vhd:478-479  | pass    | test/dma/dma_test.cpp:2030 |
| 16.5    | Continue preserves addresses           | dma.vhd:670-676  | pass    | test/dma/dma_test.cpp:2044 |
| 16.6    | Continue vs Load                       | dma.vhd:656-662  | pass    | test/dma/dma_test.cpp:2064 |
| 17.1    | Status byte format                     | dma.vhd:902      | pass    | test/dma/dma_test.cpp:2088 |
| 17.2    | End-of-block flag clear initially      | dma.vhd:242      | pass    | test/dma/dma_test.cpp:2099 |
| 17.3    | End-of-block set after transfer        | dma.vhd:471      | pass    | test/dma/dma_test.cpp:2112 |
| 17.4    | At-least-one flag                      | dma.vhd:412      | pass    | test/dma/dma_test.cpp:2125 |
| 17.5    | Status cleared by 0x8B                 | dma.vhd:691-692  | pass    | test/dma/dma_test.cpp:2139 |
| 17.6    | Status cleared by 0xC3 (reset)         | dma.vhd:638-641  | pass    | test/dma/dma_test.cpp:2153 |
| 17.7    | Default read mask                      | dma.vhd:239      | pass    | test/dma/dma_test.cpp:2177 |
| 17.8    | Read sequence cycles through mask      | dma.vhd:902-922  | pass    | test/dma/dma_test.cpp:2196 |
| 17.9    | Custom read mask (status+counter only) | dma.vhd:696-717  | pass    | test/dma/dma_test.cpp:2214 |
| 17.10   | Read sequence wraps around             | dma.vhd:919-922  | pass    | test/dma/dma_test.cpp:2235 |
| 18.1    | Read status byte                       | dma.vhd:902      | pass    | test/dma/dma_test.cpp:2273 |
| 18.2    | Read counter LO                        | dma.vhd:933      | pass    | test/dma/dma_test.cpp:2283 |
| 18.3    | Read counter HI                        | dma.vhd:935      | pass    | test/dma/dma_test.cpp:2293 |
| 18.4    | Read port A addr LO (A->B)             | dma.vhd:910-912  | pass    | test/dma/dma_test.cpp:2304 |
| 18.5    | Read port A addr HI (A->B)             | dma.vhd:913-915  | pass    | test/dma/dma_test.cpp:2314 |
| 18.6    | Read port B addr LO (A->B)             | dma.vhd:916-918  | pass    | test/dma/dma_test.cpp:2324 |
| 18.7    | Read port B addr HI (A->B)             | dma.vhd:919-921  | pass    | test/dma/dma_test.cpp:2334 |
| 18.8    | Read port A/B in B->A mode             | dma.vhd:910-921  | pass    | test/dma/dma_test.cpp:2349 |
| 19.1    | Hardware reset defaults                | dma.vhd:213-242  | pass    | test/dma/dma_test.cpp:2377 |
| 19.2    | R6 0xC3 soft reset                     | dma.vhd:638-641  | pass    | test/dma/dma_test.cpp:2395 |
| 19.3    | 0xC3 does not reset R0/R4 addresses    | dma.vhd:638-645  | pass    | test/dma/dma_test.cpp:2413 |
| 19.4    | 0xC3 resets timing to "01"             | dma.vhd:641-642  | pass    | test/dma/dma_test.cpp:2425 |
| 19.5    | 0xC3 resets prescaler to 0x00          | dma.vhd:643      | pass    | test/dma/dma_test.cpp:2442 |
| 19.6    | 0xC3 resets auto-restart to 0          | dma.vhd:645      | pass    | test/dma/dma_test.cpp:2456 |
| 20.1    | DMA delay blocks START_DMA             | dma.vhd:269      | pass    | test/dma/dma_test.cpp:2482 |
| 20.2    | DMA delay mid-transfer                 | dma.vhd:427-428  | pass    | test/dma/dma_test.cpp:2498 |
| 20.3    | IM2 DMA interrupt enable regs          | —              | missing | missing                    |
| 20.4    | DMA delay signal composition           | —              | missing | missing                    |
| 21.1    | Timing "00" = 4-cycle read/write       | dma.vhd:313      | pass    | test/dma/dma_test.cpp:2533 |
| 21.2    | Timing "01" = 3-cycle (default)        | dma.vhd:314      | pass    | test/dma/dma_test.cpp:2541 |
| 21.3    | Timing "10" = 2-cycle                  | dma.vhd:315      | pass    | test/dma/dma_test.cpp:2549 |
| 21.4    | Timing "11" = 4-cycle                  | dma.vhd:316      | pass    | test/dma/dma_test.cpp:2557 |
| 21.5    | Read timing from source port           | dma.vhd:311      | pass    | test/dma/dma_test.cpp:2573 |
| 21.6    | Write timing from dest port            | dma.vhd:371      | pass    | test/dma/dma_test.cpp:2587 |
| 22.1    | Disable during active transfer         | dma.vhd:728      | pass    | test/dma/dma_test.cpp:2609 |
| 22.2    | Enable without Load                    | dma.vhd:725      | pass    | test/dma/dma_test.cpp:2620 |
| 22.3    | Multiple Loads before Enable           | dma.vhd:656-668  | pass    | test/dma/dma_test.cpp:2639 |
| 22.4    | Continue after auto-restart            | dma.vhd:670-676  | pass    | test/dma/dma_test.cpp:2657 |
| 22.5    | R0 register decoding ambiguity         | dma.vhd:542      | pass    | test/dma/dma_test.cpp:2675 |
| 22.6    | Simultaneous R0/R2 decode              | dma.vhd:518-520,559 | pass    | test/dma/dma_test.cpp:2688 |

## DivMMC+SPI — `test/divmmc/divmmc_test.cpp`

Last-touch commit: `d4ea4e1` (SPI pipeline delay + write MISO + SS-10 test fix)

| Test ID          | Plan row title                                               | VHDL file:line | Status  | Test file:line                   |
|------------------|--------------------------------------------------------------|----------------|---------|----------------------------------|
| E3-01            | Reset clears port 0xE3 to 0x00                               | zxnext.vhd:4173  | pass    | test/divmmc/divmmc_test.cpp:215  |
| E3-02            | Write 0x80: conmem=1, mapram=0, bank=0                       | zxnext.vhd:4180  | pass    | test/divmmc/divmmc_test.cpp:227  |
| E3-03            | Write 0x40: mapram latches ON permanently                    | zxnext.vhd:4183  | pass    | test/divmmc/divmmc_test.cpp:240  |
| E3-04            | Write 0x00 after mapram set: mapram stays 1                  | zxnext.vhd:4183  | pass    | test/divmmc/divmmc_test.cpp:252  |
| E3-05            | mapram cleared by NextREG 0x09 bit 3                         | zxnext.vhd:4184-4185 | pass    | test/divmmc/divmmc_test.cpp:264  |
| E3-06            | Write bank 0x0F: bits 3:0 select bank 0-15                   | zxnext.vhd:4188  | pass    | test/divmmc/divmmc_test.cpp:276  |
| E3-07            | Read port 0xE3 returns `{conmem, mapram, 00, bank[3:0]}`     | zxnext.vhd:4190  | pass    | test/divmmc/divmmc_test.cpp:291  |
| E3-08            | Bits 5:4 of write are ignored                                | zxnext.vhd:4190  | pass    | test/divmmc/divmmc_test.cpp:304  |
| CM-01            | conmem=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM               | divmmc.vhd:94    | pass    | test/divmmc/divmmc_test.cpp:369  |
| CM-02            | conmem=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N        | divmmc.vhd:95-96 | pass    | test/divmmc/divmmc_test.cpp:384  |
| CM-03            | conmem=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3        | divmmc.vhd:95-96 | pass    | test/divmmc/divmmc_test.cpp:402  |
| CM-04            | conmem=1, mapram=1: 0x2000-0x3FFF = DivMMC RAM bank N        | divmmc.vhd:95-96 | pass    | test/divmmc/divmmc_test.cpp:415  |
| CM-05            | conmem=1: 0x0000-0x1FFF is read-only                         | divmmc.vhd:100   | pass    | test/divmmc/divmmc_test.cpp:428  |
| CM-06            | conmem=1, mapram=1, bank=3: 0x2000-0x3FFF is read-only       | divmmc.vhd:100   | pass    | test/divmmc/divmmc_test.cpp:442  |
| CM-07            | conmem=1, mapram=1, bank!=3: 0x2000-0x3FFF is writable       | divmmc.vhd:100   | pass    | test/divmmc/divmmc_test.cpp:456  |
| CM-08            | conmem=0, automap=0: no DivMMC mapping                       | divmmc.vhd:94-95 | pass    | test/divmmc/divmmc_test.cpp:472  |
| CM-09            | DivMMC paging requires `port_divmmc_io_en=1`                 | divmmc.vhd:98    | pass    | test/divmmc/divmmc_test.cpp:491  |
| AM-01            | automap=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM              | divmmc.vhd:94    | pass    | test/divmmc/divmmc_test.cpp:519  |
| AM-02            | automap=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N       | divmmc.vhd:95-96 | pass    | test/divmmc/divmmc_test.cpp:534  |
| AM-03            | automap=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3       | divmmc.vhd:95-96 | pass    | test/divmmc/divmmc_test.cpp:548  |
| AM-04            | automap active, then deactivated: normal ROM restored        | divmmc.vhd:131   | pass    | test/divmmc/divmmc_test.cpp:569  |
| EP-01            | M1 fetch at 0x0000: automap_delayed_on activates             | zxnext.vhd:2850  | pass    | test/divmmc/divmmc_test.cpp:614  |
| EP-02            | M1 fetch at 0x0008: automap_rom3_delayed_on                  | zxnext.vhd:2856  | pass    | test/divmmc/divmmc_test.cpp:631  |
| EP-03            | M1 fetch at 0x0038: automap_rom3_delayed_on                  | zxnext.vhd:2890  | pass    | test/divmmc/divmmc_test.cpp:645  |
| EP-04            | M1 fetch at 0x0010: no automap (EP2 disabled)                | zxnext.vhd:2862  | pass    | test/divmmc/divmmc_test.cpp:659  |
| EP-05            | M1 fetch at 0x0018: no automap (EP3 disabled)                | zxnext.vhd:2868  | pass    | test/divmmc/divmmc_test.cpp:668  |
| EP-06            | M1 fetch at 0x0020: no automap (EP4 disabled)                | zxnext.vhd:2874  | pass    | test/divmmc/divmmc_test.cpp:677  |
| EP-07            | M1 fetch at 0x0028: no automap (EP5 disabled)                | zxnext.vhd:2880  | pass    | test/divmmc/divmmc_test.cpp:686  |
| EP-08            | M1 fetch at 0x0030: no automap (EP6 disabled)                | zxnext.vhd:2886  | pass    | test/divmmc/divmmc_test.cpp:695  |
| EP-09            | Set NR 0xBA[0]=1: 0x0000 becomes instant_on                  | divmmc.vhd:128-148 | pass    | test/divmmc/divmmc_test.cpp:725  |
| EP-10            | Set NR 0xB9[1]=1: 0x0008 becomes automap (not rom3)          | zxnext.vhd:2856  | pass    | test/divmmc/divmmc_test.cpp:754  |
| EP-11            | Set NR 0xB8=0xFF: all 8 RST addresses trigger                | zxnext.vhd:2892-2905 | pass    | test/divmmc/divmmc_test.cpp:778  |
| EP-12            | Automap only triggers on M1+MREQ (instruction fetch)         | divmmc.vhd:128   | pass    | test/divmmc/divmmc_test.cpp:790  |
| NR-01            | M1 at 0x04C6 with BB[2]=1: automap_rom3_delayed_on           | zxnext.vhd:2898  | pass    | test/divmmc/divmmc_test.cpp:812  |
| NR-02            | M1 at 0x0562 with BB[3]=1: automap_rom3_delayed_on           | zxnext.vhd:2900  | pass    | test/divmmc/divmmc_test.cpp:825  |
| NR-03            | M1 at 0x04D7 with BB[4]=0: no trigger (default)              | zxnext.vhd:2902  | pass    | test/divmmc/divmmc_test.cpp:839  |
| NR-04            | M1 at 0x056A with BB[5]=0: no trigger (default)              | zxnext.vhd:2904  | pass    | test/divmmc/divmmc_test.cpp:851  |
| NR-05            | Set BB[4]=1, M1 at 0x04D7: triggers rom3_delayed_on          | zxnext.vhd:2902  | pass    | test/divmmc/divmmc_test.cpp:864  |
| NR-06            | M1 at 0x3D00 with BB[7]=1: automap_rom3_instant_on           | zxnext.vhd:2898-2899,3138 | pass    | test/divmmc/divmmc_test.cpp:879  |
| NR-07            | M1 at 0x3DFF with BB[7]=1: automap_rom3_instant_on           | zxnext.vhd:2898-2899 | pass    | test/divmmc/divmmc_test.cpp:891  |
| NR-08            | Set BB[7]=0, M1 at 0x3D00: no trigger                        | zxnext.vhd:2898-2899 | pass    | test/divmmc/divmmc_test.cpp:904  |
| NR-12a           | 0x0066 + BB[0] delayed alone: no automap same-cycle          | divmmc.vhd:148       | pass    | test/divmmc/divmmc_test.cpp:964  |
| NR-12b           | 0x0066 + BB[0] delayed: hold promotes automap on next M1     | divmmc.vhd:128-141   | pass    | test/divmmc/divmmc_test.cpp:971  |
| DA-01            | M1 at 0x1FF8 with automap held: automap deactivates          | divmmc.vhd:131   | pass    | test/divmmc/divmmc_test.cpp:1049 |
| DA-02            | M1 at 0x1FFF with automap held: automap deactivates          | divmmc.vhd:131   | pass    | test/divmmc/divmmc_test.cpp:1062 |
| DA-03            | M1 at 0x1FF7: no deactivation                                | zxnext.vhd       | pass    | test/divmmc/divmmc_test.cpp:1075 |
| DA-04            | M1 at 0x2000: no deactivation                                | zxnext.vhd       | pass    | test/divmmc/divmmc_test.cpp:1087 |
| DA-05            | Set BB[6]=0: deactivation range disabled                     | zxnext.vhd       | pass    | test/divmmc/divmmc_test.cpp:1101 |
| DA-06            | RETN instruction seen: automap deactivates                   | divmmc.vhd:126,139 | pass    | test/divmmc/divmmc_test.cpp:1118 |
| DA-07            | Reset clears automap state                                   | divmmc.vhd:127   | pass    | test/divmmc/divmmc_test.cpp:1131 |
| DA-08            | `automap_reset` clears automap state                         | divmmc.vhd:126   | pass    | test/divmmc/divmmc_test.cpp:1147 |
| DMC-TM-01 | Instant on: DivMMC mapped during the triggering fetch        | divmmc.vhd:141   | pass    | test/divmmc/divmmc_test.cpp:1239 |
| DMC-TM-02 | Delayed on: DivMMC mapped on NEXT fetch after trigger        | divmmc.vhd:129,141,148 | pass    | test/divmmc/divmmc_test.cpp:1261 |
| DMC-TM-03 | automap_held latches on MREQ_n rising edge                   | divmmc.vhd:141-142,131 | pass    | test/divmmc/divmmc_test.cpp:1282 |
| DMC-TM-04 | automap_hold updates only during M1+MREQ                     | divmmc.vhd:128   | pass    | test/divmmc/divmmc_test.cpp:1305 |
| TM-05            | Held automap persists across non-deactivating fetches        | divmmc.vhd:131   | pass    | test/divmmc/divmmc_test.cpp:1322 |
| R3-01            | M1 at 0x0008 with ROM3 active: automap triggers              | zxnext.vhd:2856,3138 | pass    | test/divmmc/divmmc_test.cpp:1355 |
| R3-02            | M1 at 0x0008 with ROM0 active: no automap                    | zxnext.vhd:2856  | pass    | test/divmmc/divmmc_test.cpp:1373 |
| R3-03            | M1 at 0x0008 with Layer 2 mapped: no automap                 | zxnext.vhd:3138  | pass    | test/divmmc/divmmc_test.cpp:1407 |
| R3-04            | `automap_active` (non-ROM3 path) always enabled when DivMMC… | zxnext.vhd:3137  | pass    | test/divmmc/divmmc_test.cpp:1425 |
| NM-01            | DivMMC button press sets `button_nmi` (via NmiSource strobe) | divmmc.vhd:108-111, zxnext.vhd:2170 | pass  | test/divmmc/divmmc_test.cpp:1513 |
| NM-02            | M1 at 0x0066 with button_nmi: automap_nmi triggers           | divmmc.vhd:120-121 | pass    | test/divmmc/divmmc_test.cpp:1533 |
| NM-03            | M1 at 0x0066 without button_nmi: no NMI automap              | divmmc.vhd:120 | pass    | test/divmmc/divmmc_test.cpp:1553 |
| NM-04            | button_nmi cleared by reset                                  | divmmc.vhd:108 | pass    | test/divmmc/divmmc_test.cpp:1569 |
| NM-05            | button_nmi cleared by automap_reset (enabled→disabled edge)  | divmmc.vhd:108, zxnext.vhd:4112 | pass | test/divmmc/divmmc_test.cpp:1587 |
| NM-06            | button_nmi cleared by RETN (i_retn_seen)                     | divmmc.vhd:108 | pass    | test/divmmc/divmmc_test.cpp:1605 |
| NM-07            | button_nmi cleared when automap_held becomes 1               | divmmc.vhd:112-113 | pass    | test/divmmc/divmmc_test.cpp:1636 |
| NM-08            | `o_disable_nmi` = automap_held OR button_nmi                 | divmmc.vhd:150 | pass    | test/divmmc/divmmc_test.cpp:1725 |
| NA-01            | NR 0x0A[4]=0 (default): automap_reset asserted               | zxnext.vhd:4112  | pass    | test/divmmc/divmmc_test.cpp:1958 |
| NA-01b           | Cold boot: set_enabled(true) alone, NR 0x0A[4]=0 keeps reset | zxnext.vhd:1126,4112 | pass    | test/divmmc/divmmc_test.cpp:1980 |
| NA-01c           | CONMEM gated by port_divmmc_io_en only, not by NR 0x0A[4]    | divmmc.vhd:94    | pass    | test/divmmc/divmmc_test.cpp:2005 |
| NA-02            | NR 0x0A[4]=1: automap_reset deasserted                       | zxnext.vhd:4112  | pass    | test/divmmc/divmmc_test.cpp:2022 |
| NA-03            | port_divmmc_io_en=0: automap_reset asserted                  | zxnext.vhd:4112  | pass    | test/divmmc/divmmc_test.cpp:2046 |
| SM-01            | DivMMC ROM maps to SRAM address 0x010000-0x011FFF            | —              | missing | missing                          |
| SM-02            | DivMMC RAM bank 0 maps to SRAM 0x020000                      | —              | missing | missing                          |
| SM-03            | DivMMC RAM bank 3 maps to SRAM 0x026000                      | —              | missing | missing                          |
| SM-04            | DivMMC RAM bank 15 maps to SRAM 0x03E000                     | —              | missing | missing                          |
| SM-05            | DivMMC has priority over Layer 2 mapping                     | —              | missing | missing                          |
| SM-06            | DivMMC has priority over ROMCS                               | —              | missing | missing                          |
| SM-07            | ROMCS maps to DivMMC banks 14 and 15                         | —              | missing | missing                          |
| SS-01            | Reset: port_e7_reg = 0xFF (all deselected)                   | zxnext.vhd:3302  | pass    | test/divmmc/divmmc_test.cpp:2471 |
| SS-02            | Write 0x01 (sd_swap=0): selects SD1                          | zxnext.vhd:3311  | pass    | test/divmmc/divmmc_test.cpp:2502 |
| SS-03            | Write 0x02 (sd_swap=0): selects SD0                          | zxnext.vhd:3313  | pass    | test/divmmc/divmmc_test.cpp:2516 |
| SS-04            | Write 0x01 with sd_swap=1: selects SD0 (swapped)             | zxnext.vhd:3311  | pass    | test/divmmc/divmmc_test.cpp:2529 |
| SS-05            | Write 0x02 with sd_swap=1: selects SD1 (swapped)             | zxnext.vhd:3313  | pass    | test/divmmc/divmmc_test.cpp:2542 |
| SS-06            | Write 0xFB: selects RPI0 (bit 2 = 0)                         | zxnext.vhd:3318  | pass    | test/divmmc/divmmc_test.cpp:2556 |
| SS-07            | Write 0xF7: selects RPI1 (bit 3 = 0)                         | zxnext.vhd:3320  | pass    | test/divmmc/divmmc_test.cpp:2567 |
| SS-08            | Write 0x7F in config mode: selects Flash                     | —              | missing | missing                          |
| SS-09            | Write 0x7F outside config mode: all deselected (0xFF)        | zxnext.vhd:3326  | pass    | test/divmmc/divmmc_test.cpp:2597 |
| SS-10            | Write any other value: all deselected (0xFF)                 | zxnext.vhd:3322  | pass    | test/divmmc/divmmc_test.cpp:2613 |
| SS-11            | Only one device selected at a time                           | zxnext.vhd:3328  | pass    | test/divmmc/divmmc_test.cpp:2627 |
| SX-01            | Write to port 0xEB: sends byte via MOSI                      | spi_master.vhd:111-112 | pass    | test/divmmc/divmmc_test.cpp:2946 |
| SX-02            | Read from port 0xEB: sends 0xFF via MOSI, receives MISO      | spi_master.vhd:109-110 | pass    | test/divmmc/divmmc_test.cpp:2974 |
| SX-03            | Read returns PREVIOUS exchange result                        | spi_master.vhd:162-166 | pass    | test/divmmc/divmmc_test.cpp:3004 |
| SX-04            | First read after reset returns 0xFF                          | spi_master.vhd:74 | pass    | test/divmmc/divmmc_test.cpp:3030 |
| SX-05            | Write 0xAA then read: read returns MISO from write cycle     | spi_master.vhd:164-165 | pass    | test/divmmc/divmmc_test.cpp:3052 |
| SX-06            | SPI transfer is 16 clock cycles (8 bits x 2 edges)           | —              | missing | missing                          |
| SX-07            | SCK output matches state_r[0]                                | —              | missing | missing                          |
| SX-08            | MOSI outputs MSB first                                       | —              | missing | missing                          |
| SX-09            | MISO sampled on rising SCK edge (delayed by 1 cycle)         | —              | missing | missing                          |
| SX-10            | Back-to-back transfers: new transfer starts on last state    | —              | missing | missing                          |
| ST-01            | Reset: state = "10000" (idle)                                | —              | missing | missing                          |
| ST-02            | Transfer start: state goes to "00000"                        | —              | missing | missing                          |
| ST-03            | State increments each clock until 0x0F                       | —              | missing | missing                          |
| ST-04            | After state 0x0F, returns to idle ("10000")                  | —              | missing | missing                          |
| ST-05            | `spi_wait_n = 0` during active transfer                      | —              | missing | missing                          |
| ST-06            | `spi_wait_n = 1` when idle or on last cycle                  | —              | missing | missing                          |
| ST-07            | Transfer can begin from idle OR from last state              | —              | missing | missing                          |
| ST-08            | Read/write during mid-transfer: ignored                      | —              | missing | missing                          |
| ML-01            | MISO bits shifted in on delayed rising SCK                   | —              | missing | missing                          |
| ML-02            | Full byte latched into `miso_dat` on `state_last_d`          | —              | missing | missing                          |
| ML-03            | `miso_dat` holds value until next transfer completes         | spi_master.vhd:164-165 | pass    | test/divmmc/divmmc_test.cpp:3240 |
| ML-04            | Input and output shift registers are independent             | —              | missing | missing                          |
| ML-05            | Reset sets `ishift_r` to all 1s                              | spi_master.vhd:74 | pass    | test/divmmc/divmmc_test.cpp:3277 |
| ML-06            | 16 cycles minimum between read/write operations              | —              | missing | missing                          |
| MX-01            | Flash selected: MISO from flash                              | —              | missing | missing                          |
| MX-02            | RPI selected: MISO from RPI                                  | —              | missing | missing                          |
| MX-03            | SD selected: MISO from SD                                    | zxnext.vhd:3280  | pass    | test/divmmc/divmmc_test.cpp:3324 |
| MX-04            | No device selected: MISO reads as 1                          | zxnext.vhd:3280  | pass    | test/divmmc/divmmc_test.cpp:3352 |
| MX-05            | Priority: Flash > RPI > SD > default                         | —              | missing | missing                          |
| IN-01            | Boot sequence: automap at 0x0000, DivMMC ROM mapped          | divmmc.vhd:94    | pass    | test/divmmc/divmmc_test.cpp:3379 |
| IN-02            | SD card init: select SD0, exchange bytes, deselect           | zxnext.vhd:3302  | pass    | test/divmmc/divmmc_test.cpp:3399 |
| IN-03            | RETN after NMI handler: automap deactivated, normal ROM      | divmmc.vhd:126,139 | pass    | test/divmmc/divmmc_test.cpp:3417 |
| IN-04            | Automap at 0x0008 (RST 8): ROM3 conditional                  | zxnext.vhd:2856,3138 | pass    | test/divmmc/divmmc_test.cpp:3441 |
| IN-05            | Rapid SPI exchanges: back-to-back without idle gap           | spi_master.vhd:82 | pass    | test/divmmc/divmmc_test.cpp:3459 |
| IN-06            | conmem override during automap: conmem takes priority        | divmmc.vhd:94    | pass    | test/divmmc/divmmc_test.cpp:3473 |
| IN-07            | DivMMC disabled via NR 0x0A[4]=0: no automap, SPI still wor… | zxnext.vhd:4112  | pass    | test/divmmc/divmmc_test.cpp:3497 |

### Extra coverage (not in plan)

| Test ID | Assertion description                   | VHDL file:line | Test file:line                  |
|---------|-----------------------------------------|----------------|---------------------------------|
| MEM-01  | Write/read slot 1 RAM bank 2            | —              | missing                         |
| MEM-02  | Slot 0 writes discarded (ROM read-only) | —              | missing                         |
| MEM-03  | mapram=1, bank=3: slot 1 read-only      | —              | missing                         |
| MEM-04  | mapram=1, bank!=3: slot 1 writable      | —              | missing                         |
| MEM-05  | mapram=1: slot 0 reads RAM page 3       | —              | missing                         |
| MEM-06  | Bank switching: data preserved per bank | —              | missing                         |
| MEM-07  | Read outside range returns 0xFF         | —              | missing                         |
| NRD-01  | NR 0xB8 default = 0x83                  | —              | missing                         |
| NRD-02  | NR 0xB9 default = 0x01                  | —              | missing                         |
| NRD-03  | NR 0xBA default = 0x00                  | —              | missing                         |
| NRD-04  | NR 0xBB default = 0xCD                  | —              | missing                         |
| SD-01   | SD card: initial exchange returns 0xFF  | —              | missing                         |
| SD-02   | SD card: deselect after reset           | —              | missing                         |
| SD-03   | SD card: not mounted initially          | —              | missing                         |

## Multiface — `test/multiface/multiface_test.cpp`

The Multiface peripheral (`src/peripheral/multiface.{h,cpp}`), implemented in
the Task 8 waves of 2026-05 against `device/multiface.vhd` and the `zxnext.vhd`
glue that wires it to the port decoder, the NMI fabric and the memory map. The
suite has no separate plan doc: it was written row-by-row from the VHDL, and
each `check()` carries its own citation in the detail argument, which is what
the `VHDL file:line` column below is read from.

**Read from, not necessarily all of.** The extractor stops a citation's line
list at the first interrupting prose, so a detail spelled
`multiface.vhd:158 (clear), :165 (eff)` publishes `:158` alone — the `:165`
sits behind `(clear)`, and reaching across that means consuming English, whose
failure mode is a confidently WRONG citation. 11 of this section's cells are
short of their source that way (and 2 more elsewhere in this document); each
still names the row's primary evidence, and none names anything the source
does not. Closing them means re-spelling the detail as a plain list
(`:158,165`), which is an edit in the test source.

Four groups: `MF-CORE-*` the state machine (NMI arm, invisible latch, the
`0x0066` fetch that raises `mf_enable`, RETN teardown), `MF-PORT-*` the four
`mf_type` port-pair decodes and their negative cases, `MF-MUX-*` the read
multiplexer priority `zxnext.vhd:4310-4322`, `MF-OVL-*` the 0x0000-0x1FFF
overlay and its SRAM pages, and `MF-M1G-*` the M1 gating.

Several rows are asserted twice — once in the real `check()` and once in a
fixture-init guard reusing the same ID, which is textually FIRST. Both the
citation and the `Test file:line` are taken from the first call that carries a
citation, so these uncited guards shadow neither, and the two columns of a row
always name one and the same call.

The rule is "first CITED call", not "not a guard" — nothing row-local can tell
a guard from an assertion, and guessing from its wording is the kind of
inference this document refuses. A guard that carries a citation of its own
therefore does win, and 12 rows of `## Contention` are in exactly that shape:
their citation is right and their `Test file:line` names the guard. Untouched
here, and the fix is to drop the ID reuse in that suite.

| Test ID    | Assertion description                                                                          | VHDL file:line                         | Status  | Test file:line                         |
|------------|------------------------------------------------------------------------------------------------|----------------------------------------|---------|----------------------------------------|
| MF-CORE-01 | reset defaults: nmi=0 invisible=1 mf_enable=0 port_io_dly=0 mem=0 hold=0                       | multiface.vhd:126,141,156,175          | pass    | test/multiface/multiface_test.cpp:118  |
| MF-CORE-02 | button_press: arms nmi_active 0->1; second press no-op while nmi_active=1                      | multiface.vhd:135                      | pass    | test/multiface/multiface_test.cpp:149  |
| MF-CORE-03 | button_press clears invisible -> 0; invisible_eff = invisible AND NOT mode_48                  | multiface.vhd:158                      | pass    | test/multiface/multiface_test.cpp:171  |
| MF-CORE-04 | 0x0066 + m1 + mreq + nmi_active=1 -> mf_enable=1 (else stays 0)                                | multiface.vhd:169                      | pass    | test/multiface/multiface_test.cpp:204  |
| MF-CORE-05 | on_retn_seen clears both nmi_active and mf_enable                                              | multiface.vhd:144                      | pass    | test/multiface/multiface_test.cpp:221  |
| MF-CORE-06 | port_io_dly edge detector suppresses nmi_active clear when prior-cycle dly=1                   | multiface.vhd:128                      | pass    | test/multiface/multiface_test.cpp:272  |
| MF-CORE-07 | INVISIBLE: dis_wr+mode_128=set, en_wr+mode_p3=set, button=clear                                | multiface.vhd:158                      | pass    | test/multiface/multiface_test.cpp:305  |
| MF-CORE-08 | mf_enable_eff = mf_enable OR fetch_66 (FF carries forward post-fetch)                          | multiface.vhd:186                      | pass    | test/multiface/multiface_test.cpp:338  |
| MF-CORE-09 | mode dispatch: 00->p3, 11->48, 01/10->128 (combinational, ungated)                             | multiface.vhd:105-118                  | pass    | test/multiface/multiface_test.cpp:356  |
| MF-CORE-10 | is_active() = is_mem_active() OR is_nmi_hold()                                                 | zxnext.vhd:4305                        | pass    | test/multiface/multiface_test.cpp:380  |
| MF-CORE-11 | load_rom_bytes round-trips 8 KB buffer into rom_data()                                         | —                                    | pass    | test/multiface/multiface_test.cpp:398  |
| MF-CORE-12 | save_state / load_state round-trips FFs + mode + RAM                                           | —                                    | pass    | test/multiface/multiface_test.cpp:437  |
| MF-M1G-01  | quiescent M1 at 0x0066: no FF changes                                                          | multiface.vhd:169,176                  | pass    | test/multiface/multiface_test.cpp:1358 |
| MF-M1G-02  | M1 after a port strobe clocks port_io_dly 1->0                                                 | multiface.vhd:122-131                  | pass    | test/multiface/multiface_test.cpp:1376 |
| MF-M1G-03  | NMI armed: 0x0066 M1 latches mf_enable through the gate                                        | multiface.vhd:169,176                  | pass    | test/multiface/multiface_test.cpp:1390 |
| MF-M1G-04  | M1 after enable-rd strobe drops combinational mf_port_en and port_io_dly, preserves mf_enable  | multiface.vhd:128,195                  | pass    | test/multiface/multiface_test.cpp:1411 |
| MF-M1G-05  | mapped overlay survives quiescent M1s (incl. 0x0066 with nmi_active=0) — mf_enable untouched | multiface.vhd:169,171-184              | pass    | test/multiface/multiface_test.cpp:1432 |
| MF-M1G-06  | disabled: M1 preserves forced reset state                                                      | multiface.vhd:103,126,141,156,175      | pass    | test/multiface/multiface_test.cpp:1446 |
| MF-MUX-01  | MF+3 read 0x1xxx LSB 0x3F (FDC=0): bit 3 forced 0, bits 2:0 = port_1ffd_reg                    | zxnext.vhd:4312                        | pass    | test/multiface/multiface_test.cpp:729  |
| MF-MUX-01b | MF+3 read 0x1xxx LSB 0x3F (FDC=1): bit 3 = cpu_do(3), bits 2:0 = port_1ffd_reg                 | zxnext.vhd:4312                        | pass    | test/multiface/multiface_test.cpp:757  |
| MF-MUX-02  | MF+3 read 0x7xxx LSB 0x3F: returns full port_7ffd_reg                                          | zxnext.vhd:4313                        | pass    | test/multiface/multiface_test.cpp:777  |
| MF-MUX-03  | MF+3 read 0xDxxx LSB 0x3F: returns 0 / reg_6 / 0 / port_dffd_reg(4:0)                          | zxnext.vhd:4314                        | pass    | test/multiface/multiface_test.cpp:802  |
| MF-MUX-04  | MF+3 read 0xExxx LSB 0x3F: returns 0 / 0 / 0 / 0 / reg_3 / reg_2 / 0 / 0                       | zxnext.vhd:4315                        | pass    | test/multiface/multiface_test.cpp:823  |
| MF-MUX-05  | MF+3 read 0x0xxx LSB 0x3F: 'others' arm → port_fe_reg(2:0) (border bits, 0x02)               | zxnext.vhd:4316                        | pass    | test/multiface/multiface_test.cpp:849  |
| MF-MUX-06  | invisible_eff=1 closes mf_port_en gate (mux suppressed)                                        | multiface.vhd:195                      | pass    | test/multiface/multiface_test.cpp:887  |
| MF-MUX-07  | is_enabled=0 closes mf_port_en gate (mux suppressed)                                           | multiface.vhd:103,195, zxnext.vhd:2816 | pass    | test/multiface/multiface_test.cpp:906  |
| MF-MUX-08  | MF128 var A read LSB 0xBF: returns port_7ffd_reg(3) & 0x7F (shadow on→0xFF, off→0x7F)      | zxnext.vhd:4319                        | pass    | test/multiface/multiface_test.cpp:937  |
| MF-MUX-09  | cpu_a(15:12) decode: 0x1xxx→port_1ffd (0x05), 0x7xxx→port_7ffd (0x42)                      | zxnext.vhd:4312-4313                   | pass    | test/multiface/multiface_test.cpp:959  |
| MF-MUX-10  | MF128 read at 0x1FBF: case-mux bypassed (else-branch = 0x7F, NOT port_1ffd & 0x0F)             | zxnext.vhd:4318-4320                   | pass    | test/multiface/multiface_test.cpp:986  |
| MF-OVL-01  | MF inactive (is_mem_active=0): read at 0x0000 does NOT match MF ROM                            | multiface.vhd:186                      | pass    | test/multiface/multiface_test.cpp:1080 |
| MF-OVL-02  | MF active: reads at 0x0000 and 0x1FFF return MF ROM bytes                                      | zxnext.vhd:3028-3035                   | pass    | test/multiface/multiface_test.cpp:1104 |
| MF-OVL-03  | MF active: reads at 0x2000 and 0x3FFF return MF RAM bytes                                      | zxnext.vhd:3028-3035                   | pass    | test/multiface/multiface_test.cpp:1127 |
| MF-OVL-04  | write 0x42 to 0x2123 lands in MF RAM[0x0123]; read-back matches                                | zxnext.vhd:3035                        | pass    | test/multiface/multiface_test.cpp:1156 |
| MF-OVL-05  | write 0xAA to 0x0123 (ROM half) is ignored; MF ROM unchanged                                   | zxnext.vhd:3035                        | pass    | test/multiface/multiface_test.cpp:1178 |
| MF-OVL-06  | addr 0x4000 (outside slot 0): MF overlay does NOT fire                                         | zxnext.vhd:3029                        | pass    | test/multiface/multiface_test.cpp:1205 |
| MF-OVL-07  | fetch_66 bypass + FF latch: overlay activates on the 0x0066 fetch                              | multiface.vhd:186                      | pass    | test/multiface/multiface_test.cpp:1240 |
| MF-OVL-08  | on_retn_seen deactivates overlay; reads fall through                                           | multiface.vhd:144-145,178-179          | pass    | test/multiface/multiface_test.cpp:1265 |
| MF-OVL-09  | both MF and DivMMC active: read at 0x0000 returns MF ROM (0xAA)                                | zxnext.vhd:3030,3036,3084              | pass    | test/multiface/multiface_test.cpp:1303 |
| MF-OVL-10  | boot ROM enabled + MF active: read at 0x0000 returns boot ROM (0xBB)                           | zxnext.vhd:1856-1857                   | pass    | test/multiface/multiface_test.cpp:1331 |
| MF-PORT-01 | MF+3 (mf_type=00): OUT 0x3F → enable_wr strobe (port_io_dly=1)                               | zxnext.vhd:2612,2615,2730-2733         | pass    | test/multiface/multiface_test.cpp:537  |
| MF-PORT-02 | MF+3 (mf_type=00): IN 0x3F → enable_rd strobe (port_io_dly=1)                                | zxnext.vhd:2612,2615,2730-2733         | pass    | test/multiface/multiface_test.cpp:540  |
| MF-PORT-03 | MF+3 (mf_type=00): OUT 0xBF → disable_wr strobe (port_io_dly=1)                              | zxnext.vhd:2613,2616,2730-2733         | pass    | test/multiface/multiface_test.cpp:543  |
| MF-PORT-04 | MF+3 (mf_type=00): IN 0xBF → disable_rd strobe (port_io_dly=1)                               | zxnext.vhd:2613,2616,2730-2733         | pass    | test/multiface/multiface_test.cpp:546  |
| MF-PORT-05 | MF+3 (mf_type=00): OUT 0x9F → no MF strobe (LSB not active)                                  | —                                    | pass    | test/multiface/multiface_test.cpp:549  |
| MF-PORT-06 | MF+3 (mf_type=00): OUT 0x1F → no MF strobe (LSB not active)                                  | —                                    | pass    | test/multiface/multiface_test.cpp:552  |
| MF-PORT-07 | MF128 var A (mf_type=01): OUT 0xBF → enable_wr strobe                                        | zxnext.vhd:2612                        | pass    | test/multiface/multiface_test.cpp:565  |
| MF-PORT-08 | MF128 var A (mf_type=01): OUT 0x3F → disable_wr strobe                                       | zxnext.vhd:2613                        | pass    | test/multiface/multiface_test.cpp:568  |
| MF-PORT-09 | MF128 var B (mf_type=10): OUT 0x9F → enable_wr strobe                                        | zxnext.vhd:2612                        | pass    | test/multiface/multiface_test.cpp:586  |
| MF-PORT-10 | MF128 var B (mf_type=10): IN 0x1F → disable_rd strobe                                        | zxnext.vhd:2613                        | pass    | test/multiface/multiface_test.cpp:589  |
| MF-PORT-11 | MF128 var B (mf_type=10): OUT 0xBF → no MF strobe (var-A LSB)                                | —                                    | pass    | test/multiface/multiface_test.cpp:592  |
| MF-PORT-12 | MF1 (mf_type=11): IN 0x9F → enable_rd strobe                                                 | zxnext.vhd:2612                        | pass    | test/multiface/multiface_test.cpp:610  |
| MF-PORT-13 | MF1 (mf_type=11): OUT 0x1F → disable_wr strobe                                               | zxnext.vhd:2613                        | pass    | test/multiface/multiface_test.cpp:613  |
| MF-PORT-14 | MF1 (mf_type=11): IN 0x3F → no MF strobe (MF+3 LSB only)                                     | —                                    | pass    | test/multiface/multiface_test.cpp:616  |
| MF-PORT-15 | OUT 0x3F with NR 0x83 b1 = 0 → no MF strobe (gate held off)                                  | zxnext.vhd:2615                        | pass    | test/multiface/multiface_test.cpp:633  |
| MF-PORT-16 | OUT 0x3F: fires when mf_type b1=0, suppressed when mf_type b1=1                                | zxnext.vhd:2612-2613                   | pass    | test/multiface/multiface_test.cpp:647  |

## CTC+Interrupts — `test/ctc/ctc_test.cpp` + `test/ctc_interrupts/ctc_interrupts_test.cpp`

Last-touch commit: `0336c20` (Phase 3 dashboard refresh; Phase 3 merge at `a397422`)

Task 3 SKIP-reduction plan (`doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`) landed 2026-04-21 Phase 0 → 5. `ctc_test.cpp` moved from `150/44/0/106` to `133/128/0/5` **as of that merge**; it runs at `132 / 132 pass / 0 fail / 0 skip` today. 17 rows migrated from `check()`/`skip()` to source-level re-home or category-merge comments. NR-C0-02 was subsequently closed by GH #84 and now passes in `atic_atac_nmi_test` ATIC-NMI-02. See `doc/testing/audits/task3-ctc-phase5.md` for the historical row-by-row rationale.

| Test ID    | Plan row title                                               | VHDL file:line | Status  | Test file:line            |
|------------|--------------------------------------------------------------|----------------|---------|---------------------------|
| CTC-SM-01  | Hard reset: channel starts in S_RESET                        | ctc_chan.vhd:189 | pass    | test/ctc/ctc_test.cpp:156 |
| CTC-SM-02  | Write control word without D2=1 while in S_RESET             | ctc_chan.vhd:212 | pass    | test/ctc/ctc_test.cpp:169 |
| CTC-SM-03  | Write control word with D2=1 (TC follows)                    | ctc_chan.vhd:210 | pass    | test/ctc/ctc_test.cpp:182 |
| CTC-SM-04  | Write time constant after D2=1 control word                  | ctc_chan.vhd:216,223-226 | pass    | test/ctc/ctc_test.cpp:197 |
| CTC-SM-05  | Timer mode (D6=0) without trigger (D3=1): wait in S_TRIGGER  | ctc_chan.vhd:216,224 | pass    | test/ctc/ctc_test.cpp:211 |
| CTC-SM-06  | Timer mode (D6=0) without trigger (D3=0): immediate S_RUN    | ctc_chan.vhd:216,223-226 | pass    | test/ctc/ctc_test.cpp:224 |
| CTC-SM-07  | Counter mode (D6=1): immediate S_RUN from S_TRIGGER          | ctc_chan.vhd:226 | pass    | test/ctc/ctc_test.cpp:238 |
| CTC-SM-08  | Write control word with D2=1 while in S_RUN                  | ctc_chan.vhd:230 | pass    | test/ctc/ctc_test.cpp:253 |
| CTC-SM-09  | Write time constant while in S_RUN_TC                        | ctc_chan.vhd:236 | pass    | test/ctc/ctc_test.cpp:268 |
| CTC-SM-10  | Soft reset (D1=1, D2=0): return to S_RESET                   | ctc_chan.vhd:202 | pass    | test/ctc/ctc_test.cpp:285 |
| CTC-SM-11  | Soft reset (D1=1, D2=1): go to S_RESET_TC                    | ctc_chan.vhd:204 | pass    | test/ctc/ctc_test.cpp:299 |
| CTC-SM-12  | Double soft reset required when in S_RESET_TC                | ctc_chan.vhd:257 | pass    | test/ctc/ctc_test.cpp:313 |
| CTC-SM-13  | Control word write while running (D1=0, D2=0)                | ctc_chan.vhd:232 | pass    | test/ctc/ctc_test.cpp:331 |
| CTC-TM-01  | Prescaler = 16 (D5=0): counter decrements every 16 clocks    | ctc_chan.vhd:146 | pass    | test/ctc/ctc_test.cpp:353 |
| CTC-TM-02  | Prescaler = 256 (D5=1): counter decrements every 256 clocks  | ctc_chan.vhd:146 | pass    | test/ctc/ctc_test.cpp:366 |
| CTC-TM-03  | Time constant = 1: ZC/TO after 1 prescaler cycle             | ctc_chan.vhd:170 | pass    | test/ctc/ctc_test.cpp:380 |
| CTC-TM-04  | Time constant = 0 means 256 (8-bit wrap)                     | ctc_chan.vhd:158 | pass    | test/ctc/ctc_test.cpp:398 |
| CTC-TM-05  | Prescaler resets on soft reset                               | ctc_chan.vhd:136 | pass    | test/ctc/ctc_test.cpp:416 |
| CTC-TM-06  | ZC/TO reloads time constant automatically                    | ctc_chan.vhd:170 | pass    | test/ctc/ctc_test.cpp:433 |
| CTC-TM-07  | ZC/TO pulse duration is exactly 1 clock cycle                | ctc_chan.vhd:170 | pass    | test/ctc/ctc_test.cpp:448 |
| CTC-TM-08  | Read port returns current down-counter value                 | ctc_chan.vhd:168 | pass    | test/ctc/ctc_test.cpp:462 |
| CTC-CM-01  | Counter mode: decrement on falling external edge (D4=0)      | ctc_chan.vhd:128 | pass    | test/ctc/ctc_test.cpp:520 |
| CTC-CM-02  | Counter mode: decrement on rising external edge (D4=1)       | ctc_chan.vhd:128 | pass    | test/ctc/ctc_test.cpp:534 |
| CTC-CM-03  | Counter mode: ZC/TO when count reaches 0                     | ctc_chan.vhd:170 | pass    | test/ctc/ctc_test.cpp:550 |
| CTC-CM-04  | Counter mode: automatic reload after ZC/TO                   | ctc_chan.vhd:158 | pass    | test/ctc/ctc_test.cpp:568 |
| CTC-CM-05  | Changing edge polarity (D4) counts as clock edge             | ctc_chan.vhd:289 | pass    | test/ctc/ctc_test.cpp:584 |
| CTC-CH-01  | Channel 0 trigger = ZC/TO of channel 3                       | zxnext.vhd:4084  | pass    | test/ctc/ctc_test.cpp:617 |
| CTC-CH-02  | Channel 1 trigger = ZC/TO of channel 0                       | zxnext.vhd:4084  | pass    | test/ctc/ctc_test.cpp:632 |
| CTC-CH-03  | Channel 2 trigger = ZC/TO of channel 1                       | zxnext.vhd:4084  | pass    | test/ctc/ctc_test.cpp:649 |
| CTC-CH-04  | Channel 3 trigger = ZC/TO of channel 2                       | zxnext.vhd:4084  | pass    | test/ctc/ctc_test.cpp:668 |
| CTC-CH-05  | Cascaded chain: ch0 timer -> ch1 counter -> ch2 counter      | zxnext.vhd:4084  | pass    | test/ctc/ctc_test.cpp:686 |
| CTC-CH-06  | Circular chain avoided: only one channel in timer mode       | ctc_chan.vhd:150 | pass    | test/ctc/ctc_test.cpp:704 |
| CTC-CW-01  | Control word (D0=1): bits [7:3] stored in control_reg        | ctc_chan.vhd:269 | pass    | test/ctc/ctc_test.cpp:725 |
| CTC-CW-02  | Vector word (D0=0): only accepted by channel 0               | ctc.vhd:150      | pass    | test/ctc/ctc_test.cpp:742 |
| CTC-CW-03  | Vector word to channels 1-3: treated as vector but o_vector… | ctc.vhd:150      | pass    | test/ctc/ctc_test.cpp:759 |
| CTC-CW-04  | Time constant follows control word with D2=1                 | ctc_chan.vhd:257 | pass    | test/ctc/ctc_test.cpp:771 |
| CTC-CW-05  | Write during S_RESET_TC: any byte is the time constant       | ctc_chan.vhd:257 | pass    | test/ctc/ctc_test.cpp:784 |
| CTC-CW-06  | Control word with D7=1: enable interrupt for channel         | ctc_chan.vhd:276 | pass    | test/ctc/ctc_test.cpp:794 |
| CTC-CW-07  | Control word with D7=0: disable interrupt for channel        | ctc_chan.vhd:269 | pass    | test/ctc/ctc_test.cpp:805 |
| CTC-CW-08  | External int_en_wr overrides D7 bit                          | ctc_chan.vhd:271 | pass    | test/ctc/ctc_test.cpp:817 |
| CTC-CW-09  | Hard reset clears control_reg to all zeros                   | ctc_chan.vhd:267 | pass    | test/ctc/ctc_test.cpp:828 |
| CTC-CW-10  | Hard reset clears time_constant_reg to 0x00                  | ctc_chan.vhd:281 | pass    | test/ctc/ctc_test.cpp:841 |
| CTC-CW-11  | Write edge: iowr is rising-edge detected (i_iowr AND NOT io… | —              | missing | missing                                            |
| CTC-NR-01  | NextREG 0xC5 write: sets CTC interrupt enable bits [3:0]     | ctc_chan.vhd:271 | pass    | test/ctc/ctc_test.cpp:875                          |
| CTC-NR-02  | NextREG 0xC5 read: returns ctc_int_en[7:0]                   | zxnext.vhd:4078  | pass    | test/ctc/ctc_test.cpp:890                          |
| CTC-NR-03  | CTC control word D7 also sets int_en independently           | ctc_chan.vhd:269-271 | pass    | test/ctc/ctc_test.cpp:902                          |
| CTC-NR-04  | NextREG 0xC5 write does not overlap with port CTC write      | —              | missing | missing                                            |
| IM2C-01    | ED prefix detected: enter S_ED_T4                            | im2_control.vhd:163 | pass    | test/ctc/ctc_test.cpp:945                          |
| IM2C-02    | ED 4D sequence: o_reti_seen pulsed                           | im2_control.vhd:234 | pass    | test/ctc/ctc_test.cpp:959                          |
| IM2C-03    | ED 45 sequence: o_retn_seen pulsed                           | im2_control.vhd:236 | pass    | test/ctc/ctc_test.cpp:972                          |
| IM2C-04    | ED followed by non-4D/45: return to S_0                      | im2_control.vhd:171-180 | pass    | test/ctc/ctc_test.cpp:985                          |
| IM2C-05    | o_reti_decode asserted during S_ED_T4                        | im2_control.vhd:233 | pass    | test/ctc/ctc_test.cpp:1004                         |
| IM2C-06    | CB prefix: enter S_CB_T4, wait for next fetch                | im2_control.vhd:193 | pass    | test/ctc/ctc_test.cpp:1015                         |
| IM2C-07    | DD/FD prefix chain: stay in S_DDFD_T4                        | im2_control.vhd:199-206 | pass    | test/ctc/ctc_test.cpp:1032                         |
| IM2C-08    | DMA delay: asserted during ED, ED4D, ED45, SRL states        | im2_control.vhd:238 | pass    | test/ctc/ctc_test.cpp:1056                         |
| IM2C-09    | SRL delay states: 2 extra cycles after RETI/RETN             | im2_control.vhd:186-192 | pass    | test/ctc/ctc_test.cpp:1075                         |
| IM2C-10    | IM mode detection: ED 46 = IM 0                              | im2_control.vhd:224 | pass    | test/ctc/ctc_test.cpp:1085                         |
| IM2C-11    | IM mode detection: ED 56 = IM 1                              | im2_control.vhd:224 | pass    | test/ctc/ctc_test.cpp:1096                         |
| IM2C-12    | IM mode detection: ED 5E = IM 2                              | im2_control.vhd:224 | pass    | test/ctc/ctc_test.cpp:1107                         |
| IM2C-13    | IM mode updates on falling edge of CLK_CPU                   | —              | missing | missing                                                 |
| IM2C-14    | IM mode default after reset: IM 0                            | im2_control.vhd:222 | pass    | test/ctc/ctc_test.cpp:1126                         |
| IM2D-01    | Interrupt request: S_0 -> S_REQ when i_int_req=1 and M1=high | im2_device.vhd:106 | pass    | test/ctc/ctc_test.cpp:1144                         |
| IM2D-02    | INT_n asserted in S_REQ when IEI=1 and IM2 mode              | im2_device.vhd:150 | pass    | test/ctc/ctc_test.cpp:1158                         |
| IM2D-03    | INT_n not asserted when IEI=0                                | im2_device.vhd:150 | pass    | test/ctc/ctc_test.cpp:1180                         |
| IM2D-04    | INT_n not asserted when not in IM2 mode                      | im2_device.vhd:150 | pass    | test/ctc/ctc_test.cpp:1195                         |
| IM2D-05    | Acknowledge: S_REQ -> S_ACK on M1=0, IORQ=0, IEI=1           | im2_device.vhd:112 | pass    | test/ctc/ctc_test.cpp:1212                         |
| IM2D-06    | S_ACK -> S_ISR when M1 returns high                          | im2_device.vhd:119 | pass    | test/ctc/ctc_test.cpp:1227                         |
| IM2D-07    | S_ISR -> S_0 on RETI seen with IEI=1                         | im2_device.vhd:125 | pass    | test/ctc/ctc_test.cpp:1246                         |
| IM2D-08    | S_ISR stays in S_ISR without RETI                            | im2_device.vhd:123-128 | pass    | test/ctc/ctc_test.cpp:1261                         |
| IM2D-09    | Vector output during S_ACK (or S_ACK transition)             | im2_device.vhd:155 | pass    | test/ctc/ctc_test.cpp:1278                         |
| IM2D-10    | Vector output = 0 when not in ACK                            | im2_device.vhd:155 | pass    | test/ctc/ctc_test.cpp:1291                         |
| IM2D-11    | o_isr_serviced pulsed on S_ISR -> S_0 transition             | im2_device.vhd:159 | pass    | test/ctc/ctc_test.cpp:1319                         |
| IM2D-12    | DMA interrupt: o_dma_int=1 whenever state != S_0 and dma_in… | im2_device.vhd:151 | pass    | test/ctc/ctc_test.cpp:1338                         |
| IM2P-01    | IEO = IEI in S_0 state (idle)                                | im2_device.vhd:139-140 | pass    | test/ctc/ctc_test.cpp:1354                         |
| IM2P-02    | IEO = IEI AND reti_decode in S_REQ state                     | im2_device.vhd:141-142 | pass    | test/ctc/ctc_test.cpp:1372                         |
| IM2P-03    | IEO = 0 in S_ACK and S_ISR states                            | im2_device.vhd:143-144 | pass    | test/ctc/ctc_test.cpp:1389                         |
| IM2P-04    | Highest-priority device (index 0) has IEI=1 always           | peripherals.vhd:82 | pass    | test/ctc/ctc_test.cpp:1401                         |
| IM2P-05    | Two simultaneous requests: lower index wins                  | peripherals.vhd:86-128 | pass    | test/ctc/ctc_test.cpp:1422                         |
| IM2P-06    | Lower-priority device queued while higher is serviced        | im2_device.vhd:143-144 | pass    | test/ctc/ctc_test.cpp:1450                         |
| IM2P-07    | After RETI of higher-priority ISR: lower device proceeds     | im2_device.vhd:123-128 | pass    | test/ctc/ctc_test.cpp:1476                         |
| IM2P-08    | Chain of 3: device 0 in ISR, device 1 requesting, device 2…  | peripherals.vhd  | pass    | test/ctc/ctc_test.cpp:1495                         |
| IM2P-09    | INT_n is AND of all device int_n signals                     | peripherals.vhd:146-156 | pass    | test/ctc/ctc_test.cpp:1512                         |
| IM2P-10    | Vector OR: only acknowledged device outputs non-zero vector  | peripherals.vhd:134-144 | pass    | test/ctc/ctc_test.cpp:1528                         |
| PULSE-01   | Pulse mode (nr_c0[0]=0): pulse_en from qualified int_req     | im2_peripheral.vhd:186 | pass    | test/ctc/ctc_test.cpp:1550                         |
| PULSE-02   | IM2 mode (nr_c0[0]=1): pulse_en suppressed                   | im2_peripheral.vhd:186 | pass    | test/ctc/ctc_test.cpp:1565                         |
| PULSE-03   | ULA exception (EXCEPTION='1'): pulse even in IM2 when CPU n… | im2_peripheral.vhd:192 | pass    | test/ctc/ctc_test.cpp:1590                         |
| PULSE-04   | pulse_int_n goes low on pulse_en, stays low for count durat… | zxnext.vhd:2017-2031 | pass    | test/ctc/ctc_test.cpp:1610                         |
| PULSE-05   | 48K/+3 timing: pulse duration = 32 CPU cycles                | zxnext.vhd:2033  | pass    | test/ctc/ctc_test.cpp:1636                         |
| PULSE-06   | 128K/Pentagon timing: pulse duration = 36 CPU cycles         | zxnext.vhd:2033  | pass    | test/ctc/ctc_test.cpp:1658                         |
| PULSE-07   | Pulse counter resets when pulse_int_n=1                      | zxnext.vhd:2036-2044 | pass    | test/ctc/ctc_test.cpp:1694                         |
| PULSE-08   | INT_n to Z80 = pulse_int_n AND im2_int_n                     | zxnext.vhd:1840  | pass    | test/ctc/ctc_test.cpp:1715                         |
| PULSE-09   | External bus INT: o_BUS_INT_n = pulse_int_n AND im2_int_n    | —              | missing | missing                                            |
| IM2W-01    | Edge detection: int_req = i_int_req AND NOT int_req_d        | im2_peripheral.vhd:90-101 | pass    | test/ctc/ctc_test.cpp:1803                         |
| IM2W-02    | im2_int_req latched: stays high until ISR serviced           | im2_peripheral.vhd:167-178 | pass    | test/ctc/ctc_test.cpp:1818                         |
| IM2W-03    | im2_int_req cleared by im2_isr_serviced                      | im2_peripheral.vhd:148,175 | pass    | test/ctc/ctc_test.cpp:1842                         |
| IM2W-04    | int_status set by int_req or int_unq                         | im2_peripheral.vhd:160 | pass    | test/ctc/ctc_test.cpp:1855                         |
| IM2W-05    | int_status cleared by i_int_status_clear                     | im2_peripheral.vhd:160 | pass    | test/ctc/ctc_test.cpp:1911                         |
| IM2W-06    | o_int_status = int_status OR im2_int_req                     | im2_peripheral.vhd:180 | pass    | test/ctc/ctc_test.cpp:1930                         |
| IM2W-07    | im2_reset_n = mode_pulse AND NOT reset                       | im2_peripheral.vhd:105 | pass    | test/ctc/ctc_test.cpp:1966                         |
| IM2W-08    | Unqualified interrupt (int_unq): bypasses int_en             | im2_peripheral.vhd:172 | pass    | test/ctc/ctc_test.cpp:1982                         |
| IM2W-09    | isr_serviced edge detection across clock domains             | —              | missing | missing                                            |
| ULA-INT-01 | ULA interrupt generated at specific HC/VC position           | zxnext.vhd:1937,1941 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:144      |
| ULA-INT-02 | ULA interrupt disabled by port 0xFF bit (port_ff_interrupt_… | zxnext.vhd:3619-3620,3635,6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:174      |
| ULA-INT-03 | ULA interrupt enable: ula_int_en[0] = NOT port_ff_interrupt… | zxnext.vhd:6239,6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:198      |
| ULA-INT-04 | Line interrupt at configurable scanline                      | zxula_timing.vhd:577-582 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:254    |
| ULA-INT-05 | Line interrupt enable: nr_22_line_interrupt_en               | zxnext.vhd:5297,6239      | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:222      |
| ULA-INT-06 | Line interrupt scanline 0 maps to c_max_vc                   | zxula_timing.vhd:566-570 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:281    |
| ULA-INT-07 | ULA interrupt is priority index 11                           | zxnext.vhd:1941  | pass    | test/ctc/ctc_test.cpp:2078                         |
| ULA-INT-08 | Line interrupt is priority index 0 (highest)                 | zxnext.vhd:1941  | pass    | test/ctc/ctc_test.cpp:2098                         |
| ULA-INT-09 | ULA has EXCEPTION='1' in peripherals instantiation           | zxnext.vhd:1964  | pass    | test/ctc/ctc_test.cpp:2133                         |
| NR-C0-01   | Write NextREG 0xC0: bits [7:5] = IM2 vector MSBs             | zxnext.vhd:5597/1999 | pass    | test/ctc/ctc_test.cpp:2150                         |
| NR-C0-02   | Write NextREG 0xC0: bit [3] = stackless NMI                  | zxnext.vhd:2050-2085 | pass    | test/nmi/atic_atac_nmi_test.cpp (ATIC-NMI-02) | <!-- protected: cross-file, hand-maintained (GH #105) -->
| NR-C0-03   | Write NextREG 0xC0: bit [0] = pulse(0)/IM2(1) mode           | zxnext.vhd:5599/1975 | pass    | test/ctc/ctc_test.cpp:2178                         |
| NR-C0-04   | Read NextREG 0xC0: returns vector, stackless, im_mode, int_… | zxnext.vhd:6229-6230 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:837      |
| NR-C4-01   | Write NextREG 0xC4: bit [7] = expansion bus int enable       | zxnext.vhd       | pass    | test/ctc/ctc_test.cpp:2196                         |
| NR-C4-02   | Write NextREG 0xC4: bit [1] = line interrupt enable          | zxnext.vhd:5607-5610,6239 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:858      |
| NR-C4-03   | Read NextREG 0xC4: returns expbus & ula_int_en               | zxnext.vhd:6239/3621-3622/3635/6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:890      |
| NR-C5-01   | Write NextREG 0xC5: CTC interrupt enable bits [3:0]          | zxnext.vhd:4078/1949 | pass    | test/ctc/ctc_test.cpp:2216                         |
| NR-C5-02   | Read NextREG 0xC5: returns ctc_int_en[7:0]                   | —              | missing | missing                                            |
| NR-C6-01   | Write NextREG 0xC6: UART interrupt enable                    | zxnext.vhd       | pass    | test/ctc/ctc_test.cpp:2235                         |
| NR-C6-02   | Read NextREG 0xC6: returns 0_654_0_210                       | zxnext.vhd:6244-6245 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:906      |
| NR-C8-01   | Read NextREG 0xC8: line and ULA interrupt status             | zxnext.vhd:6247  | pass    | test/ctc/ctc_test.cpp:2255                         |
| NR-C9-01   | Read NextREG 0xC9: CTC interrupt status [10:3]               | zxnext.vhd:6250  | pass    | test/ctc/ctc_test.cpp:2271                         |
| NR-CA-01   | Read NextREG 0xCA: UART interrupt status                     | zxnext.vhd:6253  | pass    | test/ctc/ctc_test.cpp:2289                         |
| NR-CC-01   | Write NextREG 0xCC: DMA interrupt enable group 0             | zxnext.vhd:5629-5630/1957-1958 | pass    | test/ctc/ctc_test.cpp:2306                         |
| NR-CD-01   | Write NextREG 0xCD: DMA interrupt enable group 1             | zxnext.vhd:5633/1957 | pass    | test/ctc/ctc_test.cpp:2324                         |
| NR-CE-01   | Write NextREG 0xCE: DMA interrupt enable group 2             | zxnext.vhd:5636-5637/1957-1958 | pass    | test/ctc/ctc_test.cpp:2339                         |
| ISC-01     | Write NextREG 0xC8 bit 1: clear line interrupt status        | zxnext.vhd:1955  | pass    | test/ctc/ctc_test.cpp:2397                         |
| ISC-02     | Write NextREG 0xC8 bit 0: clear ULA interrupt status         | zxnext.vhd:1952  | pass    | test/ctc/ctc_test.cpp:2418                         |
| ISC-03     | Write NextREG 0xC9: clear individual CTC status bits         | zxnext.vhd:1953  | pass    | test/ctc/ctc_test.cpp:2440                         |
| ISC-04     | Write NextREG 0xCA bit 6: clear UART1 TX status              | zxnext.vhd:1952  | pass    | test/ctc/ctc_test.cpp:2461                         |
| ISC-05     | Write NextREG 0xCA bit 2: clear UART0 TX status              | zxnext.vhd:1952  | pass    | test/ctc/ctc_test.cpp:2482                         |
| ISC-06     | Write NextREG 0xCA bits 5                                    | zxnext.vhd:1954  | pass    | test/ctc/ctc_test.cpp:2503                         |
| ISC-07     | Write NextREG 0xCA bits 1                                    | zxnext.vhd:1954  | pass    | test/ctc/ctc_test.cpp:2524                         |
| ISC-08     | Status bit re-set by new interrupt while clear pending       | im2_peripheral.vhd:160 | pass    | test/ctc/ctc_test.cpp:2562                         |
| ISC-09     | Legacy NextREG 0x20 read: returns mixed status               | zxnext.vhd:5988-5989 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:946      |
| ISC-10     | Legacy NextREG 0x22 read: includes pulse_int_n state         | zxnext.vhd:5991-5992 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:976      |
| DMA-01     | im2_dma_int set when any peripheral has dma_int=1            | peripherals.vhd:174-184 | pass    | test/ctc/ctc_test.cpp:2589                         |
| DMA-02     | im2_dma_delay latched on im2_dma_int                         | zxnext.vhd:2001-2010 | pass    | test/ctc/ctc_test.cpp:2604                         |
| DMA-03     | im2_dma_delay held by dma_delay signal                       | zxnext.vhd:2007  | pass    | test/ctc/ctc_test.cpp:2632                         |
| DMA-04     | NMI also triggers DMA delay when nr_cc_dma_int_en_0_7=1      | zxnext.vhd:2007 | pass    | test/ctc/ctc_test.cpp:2667                                                     |
| DMA-05     | DMA delay cleared on reset                                   | zxnext.vhd:2004-2005 | pass    | test/ctc/ctc_test.cpp:2687                         |
| DMA-06     | Per-peripheral DMA int enable via NextREGs 0xCC-0xCE         | zxnext.vhd:1957-1958 | pass    | test/ctc/ctc_test.cpp:2709                         |
| UNQ-01     | NextREG 0x20 write bit 7: unqualified line interrupt         | zxnext.vhd:1946-1947 | pass    | test/ctc/ctc_test.cpp:2727                         |
| UNQ-02     | NextREG 0x20 write bits [3:0]: unqualified CTC 0-3           | zxnext.vhd:1946-1947 | pass    | test/ctc/ctc_test.cpp:2743                         |
| UNQ-03     | NextREG 0x20 write bit 6: unqualified ULA interrupt          | zxnext.vhd:1946-1947 | pass    | test/ctc/ctc_test.cpp:2759                         |
| UNQ-04     | Unqualified interrupt bypasses int_en check                  | im2_peripheral.vhd:172 | pass    | test/ctc/ctc_test.cpp:2789                         |
| UNQ-05     | Unqualified interrupt sets int_status                        | im2_peripheral.vhd:160 | pass    | test/ctc/ctc_test.cpp:2804                         |
| JOY-01     | Joystick IO mode 01: CTC channel 3 ZC/TO toggles pin7        | —              | missing | missing                                            |
| JOY-02     | Toggle conditioned on nr_0b_joy_iomode_0 or pin7=0           | —              | missing | missing                                            |
| IM2C-G87-01     | RETI (ED 4D) seen by Im2Controller via real Z80Cpu M1 stream                         | im2_control.vhd:158-209,234       | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:1031 |
| IM2C-G87-02     | RETN (ED 45) seen by Im2Controller via real Z80Cpu M1 stream                         | im2_control.vhd:233-238           | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:1055 |
| IM2-G89-01      | LDIRX samples INT/NMI between iterations                                             | —                               | missing | missing         |
| IM2-G89-02      | LDDRX samples INT/NMI between iterations                                             | —                               | missing | missing         |
| IM2-G89-03      | LDPIRX samples INT/NMI between iterations                                            | —                               | missing | missing         |
| IM2-G89-04      | LDIRSCALE samples INT/NMI between iterations                                         | —                               | missing | missing         |
| IM2-G90-01      | 28 MHz turbo SRAM-read wait state asserts sram_wait_n                                | —                               | missing | missing         |
| CTC-TM-G120-01  | Mid-stream TC reload preserves prescaler (S_RUN_TC -> S_RUN)                         | ctc_chan.vhd:131-141              | pass   | test/ctc/ctc_test.cpp:493 |
| IM2W-G119-01    | ZC/TO raised unconditionally; int_en AND at fabric edge (mid-pulse en flip)          | zxnext.vhd:1941                   | pass   | test/ctc/ctc_test.cpp:2036 |
| PULSE-G121-01   | NR 0x03 machine-timing post-boot updates pulse_count_end                             | zxnext.vhd:2033                   | pass   | test/ctc/ctc_test.cpp:1766 |
| NR-C2-01        | NMI captures PC into NR 0xC2 (RETN address LSB)                                      | zxnext.vhd:2050-2085,6232         | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:1089 |
| NR-C3-01        | NMI captures PC into NR 0xC3 (RETN address MSB)                                      | zxnext.vhd:2050-2085,6236         | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:1094 |
| Z80-04          | Z80Cpu M1 hook fires per fetched byte (cross-link to G87 ED-prefix observability)    | —                               | missing | missing         |
| CFG-08          | NR 0x02 reset_type[2:0] FSM advance on soft_reset rising edge (G153)                 | zxnext.vhd:1306,1732-1739       | missing | missing         |
| NR02-07         | NR 0x02 read returns reset_type[2:0] in bits [1:0] + bit 2 (G153)                    | zxnext.vhd:5891                 | missing | missing         |
| NR02-08         | NR 0x02 reset_type power-on default = "100"; SPI-Flash-CS arms on first power-on (G153) | zxnext.vhd:1306              | missing | missing         |
| RST-04          | Hard-reset via host F1 hotkey advances NR 0x02 reset_type FSM (G152 + G153 cross)    | —                               | missing | missing         |
| HK-06           | Host F1 SDL scancode -> emulator hard reset injector (G152)                          | —                               | missing | missing         |
| HK-07           | Host F4 SDL scancode -> emulator soft reset injector (G152)                          | —                               | missing | missing         |
| HK-08           | Host F9 SDL scancode -> Multiface NMI assert (hotkey_m1) (G152)                      | —                               | missing | missing         |
| HK-09           | Host F10 SDL scancode -> DivMMC button NMI (hotkey_drive) (G152)                     | —                               | missing | missing         |
| MF-G162-01      | Port 0x2FFD/0x3FFD iotrap decode raises nmi_gen_iotrap                               | zxnext.vhd:3835-3837            | missing | missing         |
| MF-G162-02      | NmiSource::strobe_iotrap propagates to MF assert (OR with nmi_gen_nr_mf)             | nmi_source.cpp:124-127,384      | missing | missing         |
| MF-G48-01       | NR 0x0A bits [7:6] -> nr_0a_mf_type forwarded to Multiface entity                    | zxnext.vhd:5193                 | missing | missing         |
| MF-G48-02       | MF +3 mode (type=00) decodes ports 0x3F / 0xBF                                       | multiface.vhd:122-131           | missing | missing         |
| MF-G48-03       | MF 128 mode (type=01/10) decodes ports 0xBF / 0x3F                                   | multiface.vhd:122-131           | missing | missing         |
| MF-G48-04       | MF 48 mode (type=11) decodes ports 0x9F / 0x1F                                       | multiface.vhd:122-131           | missing | missing         |
| MF-G48-05       | MF a_0066 / mf_is_active / mf_mem_en / mf_port_en signals tracked                    | multiface.vhd:152-163           | missing | missing         |
| MF-G48-06       | MF +3 readback mux on cpu_a(15:12) returns port_dffd_reg_6 + port_1ffd/7ffd shadow   | zxnext.vhd:4310-4322            | missing | missing         |
| MF-G48-07       | DivMMC RETN-seen suppressed when mf_is_active=1 (band-aid removal invariant)         | zxnext.vhd:4111                 | missing | missing         |

### Extra coverage (not in plan)

| Test ID | Assertion description                | VHDL file:line | Test file:line            |
|---------|--------------------------------------|----------------|---------------------------|
| MC-01   | 4 channels loaded with different TCs | —              | missing                   |
| MC-02   | Channels decrement independently     | —              | missing                   |
| MC-03   | Read invalid channel returns 0xFF    | —              | missing                   |

### Companion integration suite — `test/ctc_interrupts/ctc_interrupts_test.cpp`

Created 2026-04-21 (commit `87fb998`) to host the 10 integration-tier re-home targets from `ctc_test.cpp` that require a full `Emulator` fixture (port 0xFF / NR 0x22 / NR 0xC0-0xCA read-path composition). Runtime: `Total:   48  Passed:   48  Failed:    0  Skipped:    0`. The suite has grown well past those original 10: the 10 rows listed below are only the ones recorded here, 16 more that it asserts are recorded in the parent `## CTC+Interrupts` table above (`ULA-INT-01..06`, `NR-C0-04`, `NR-C2-01`, `NR-C3-01`, `NR-C4-02/03`, `NR-C6-02`, `ISC-09/10`, `IM2C-G87-01/02`), and the rest are reported `unrecorded` on every run. Each entry below cross-references the CTC+Interrupts plan row.

| Test ID    | Plan row title                                                | VHDL file:line | Status | Test file                                      |
|------------|---------------------------------------------------------------|----------------|--------|------------------------------------------------|
| ULA-INT-01 | ULA interrupt generated at specific HC/VC position            | zxnext.vhd:1937,1941 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:144  |
| ULA-INT-02 | ULA interrupt disabled by port 0xFF bit (workaround NR 0x22)  | zxnext.vhd:3619-3620,3635,6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:174  |
| ULA-INT-03 | ULA interrupt enable: ula_int_en[0] mirror                    | zxnext.vhd:6239,6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:198  |
| ULA-INT-05 | Line interrupt enable: nr_22_line_interrupt_en                | zxnext.vhd:5297,6239      | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:222  |
| NR-C0-04   | Read NextREG 0xC0: vector, stackless, im_mode, int_mode       | zxnext.vhd:6229-6230 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:837  |
| NR-C4-02   | Write NextREG 0xC4: bit [1] = line interrupt enable           | zxnext.vhd:5607-5610,6239 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:858  |
| NR-C4-03   | Read NextREG 0xC4: returns expbus & ula_int_en                | zxnext.vhd:6239/3621-3622/3635/6711 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:890  |
| NR-C6-02   | Read NextREG 0xC6: returns 0_654_0_210                        | zxnext.vhd:6244-6245 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:906  |
| ISC-09     | Legacy NextREG 0x20 read: returns mixed status                | zxnext.vhd:5988-5989 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:946  |
| ISC-10     | Legacy NextREG 0x22 read: includes pulse_int_n state (reset-state invariant) | zxnext.vhd:5991-5992 | pass    | test/ctc_interrupts/ctc_interrupts_test.cpp:976 |

## UART+I2C/RTC — `test/uart/uart_test.cpp`

Last-touch commit: `7cf61e20fa0eb7a804920eda36b9a4532823bc89` (`7cf61e20fa`)

| Test ID | Plan row title                                               | VHDL file:line | Status  | Test file:line              |
|---------|--------------------------------------------------------------|----------------|---------|-----------------------------|
| UART-SEL-01 | Reset state: read select register                            | uart.vhd:273-278,355 | pass    | test/uart/uart_test.cpp:224 |
| UART-SEL-02 | Write 0x40 to select, read back                              | uart.vhd:371     | pass    | test/uart/uart_test.cpp:237 |
| UART-SEL-03 | Write 0x00 to select, read back                              | uart.vhd:280,355 | pass    | test/uart/uart_test.cpp:250 |
| UART-SEL-04 | Write 0x15 (bit4=1, bits2:0=101), read back with UART 0      | uart.vhd:281-283,355 | pass    | test/uart/uart_test.cpp:262 |
| SEL-05  | Write 0x55 (bit6=1, bit4=1, bits2:0=101), read back with UA… | uart.vhd:284-286,371 | pass    | test/uart/uart_test.cpp:275 |
| SEL-06  | Hard reset clears prescaler MSB to 0                         | uart.vhd:273-278 | pass    | test/uart/uart_test.cpp:292 |
| SEL-07  | Soft reset clears uart_select_r to 0 but preserves prescale… | uart.vhd:273-278 | pass    | test/uart/uart_test.cpp:307 |
| FRM-01  | Hard reset state: read frame                                 | uart.vhd:297-299 | pass    | test/uart/uart_test.cpp:327 |
| FRM-02  | Write 0x1B (8 bits, parity odd, 2 stop), read back           | uart.vhd:302     | pass    | test/uart/uart_test.cpp:338 |
| FRM-03  | Frame applies to selected UART only                          | uart.vhd:301-305 | pass    | test/uart/uart_test.cpp:352 |
| FRM-04  | Bit 7 write resets FIFO                                      | uart.vhd:360     | pass    | test/uart/uart_test.cpp:367 |
| FRM-05  | Bit 6 sets break on TX                                       | uart_tx.vhd:234  | pass    | test/uart/uart_test.cpp:391 |
| FRM-06  | Frame bits 4:0 sampled at transmission start                 | uart_tx.vhd:107-114 | pass    | test/uart/uart_test.cpp:421 |
| BAUD-01 | Default prescaler = 243 (115200 @ 28MHz)                     | uart.vhd:276-277 | pass    | test/uart/uart_test.cpp:444 |
| BAUD-02 | Write 0x33 to port 0x143B (bit7=0): sets LSB bits 6:0 = 0x33 | —              | missing | missing                     |
| BAUD-03 | Write 0x85 to port 0x143B (bit7=1): sets LSB bits 13:7 = 0x… | —              | missing | missing                     |
| BAUD-04 | Write prescaler MSB via select register                      | uart.vhd:281-286,355 | pass    | test/uart/uart_test.cpp:468 |
| BAUD-05 | Prescaler applies to selected UART independently             | uart.vhd:282-286 | pass    | test/uart/uart_test.cpp:486 |
| BAUD-06 | Hard reset restores default prescaler for both UARTs         | uart.vhd:276-277 | pass    | test/uart/uart_test.cpp:504 |
| BAUD-07 | Prescaler sampled at start of TX/RX (not mid-byte)           | uart_tx.vhd:86,111 | pass    | test/uart/uart_test.cpp:535 |
| TX-01   | Write byte to port 0x133B when TX FIFO empty                 | uart.vhd:360     | pass    | test/uart/uart_test.cpp:559 |
| TX-02   | Write 64 bytes: FIFO full                                    | uart.vhd:360     | pass    | test/uart/uart_test.cpp:572 |
| TX-03   | Write 65th byte when full                                    | uart.vhd         | pass    | test/uart/uart_test.cpp:586 |
| TX-04   | TX empty flag: requires FIFO empty AND transmitter not busy  | uart.vhd:360     | pass    | test/uart/uart_test.cpp:599 |
| TX-05   | TX FIFO write is edge-triggered                              | uart.vhd:529-4   | pass    | test/uart/uart_test.cpp:630 |
| TX-06   | Frame bit 7 resets TX FIFO and transmitter                   | uart.vhd:302,536 | pass    | test/uart/uart_test.cpp:644 |
| TX-07   | Frame bit 6 (break): TX line held low, busy = 1, cannot send | uart_tx.vhd:239  | pass    | test/uart/uart_test.cpp:661 |
| TX-08   | 8N1 frame: start(0) + 8 data bits (LSB first) + stop(1)      | uart_tx.vhd:236-245 | pass    | test/uart/uart_test.cpp:695 |
| TX-09   | 7E2 frame: start + 7 bits + even parity + 2 stops            | uart_tx.vhd:152,216-225 | pass    | test/uart/uart_test.cpp:724 |
| TX-10   | 5O1 frame: start + 5 bits + odd parity + 1 stop              | uart_tx.vhd      | pass    | test/uart/uart_test.cpp:751 |
| TX-11   | Flow control: bit 5 enabled, CTS_n=1 blocks TX start         | uart_tx.vhd:180-192 | pass    | test/uart/uart_test.cpp:772 |
| TX-12   | Flow control disabled: CTS_n ignored                         | uart_tx.vhd:187-192 | pass    | test/uart/uart_test.cpp:794 |
| TX-13   | Parity calculation: even parity (frame bit 1 = 0)            | uart_tx.vhd:153,156 | pass    | test/uart/uart_test.cpp:816 |
| TX-14   | Parity calculation: odd parity (frame bit 1 = 1)             | uart_tx.vhd:153,156 | pass    | test/uart/uart_test.cpp:837 |
| RX-01   | Inject byte into RX: read port 0x143B                        | uart.vhd:347-353 | pass    | test/uart/uart_test.cpp:860 |
| RX-02   | Read empty RX FIFO                                           | uart.vhd:351-352 | pass    | test/uart/uart_test.cpp:870 |
| RX-03   | Fill RX FIFO with 512 bytes                                  | uart.vhd:360     | pass    | test/uart/uart_test.cpp:882 |
| RX-04   | RX FIFO overflow: 513th byte                                 | uart.vhd:540-513 | pass    | test/uart/uart_test.cpp:896 |
| RX-05   | Read advances RX FIFO pointer (edge-triggered)               | fifop.vhd        | pass    | test/uart/uart_test.cpp:912 |
| RX-06   | RX near-full flag at 3/4 capacity (384 bytes)                | fifop.vhd        | pass    | test/uart/uart_test.cpp:925 |
| RX-07   | Frame bit 7 resets RX FIFO                                   | uart.vhd:302,536 | pass    | test/uart/uart_test.cpp:940 |
| RX-08   | Framing error: missing stop bit                              | uart.vhd:541     | pass    | test/uart/uart_test.cpp:1016 |
| RX-09   | Parity error                                                 | uart.vhd:541     | pass    | test/uart/uart_test.cpp:1036 |
| RX-10   | Break condition: all-zero shift register in error state      | uart_rx.vhd:314  | pass    | test/uart/uart_test.cpp:1056 |
| RX-11   | Error byte stored with 9th bit in FIFO                       | uart.vhd:359     | pass    | test/uart/uart_test.cpp:1080 |
| RX-12   | Noise rejection: pulse < 2^NOISE_REJECTION_BITS / CLK is fi… | uart_rx.vhd:119-131 | pass    | test/uart/uart_test.cpp:1100 |
| RX-13   | RX state machine: pause mode (frame bit 6)                   | uart_rx.vhd:231-232 | pass    | test/uart/uart_test.cpp:1118 |
| RX-14   | RX variables sampled at start bit detection                  | uart_rx.vhd:144-154 | pass    | test/uart/uart_test.cpp:1153 |
| RX-15   | Hardware flow control: RTR_n asserted when FIFO almost full  | uart.vhd:442-446 | pass    | test/uart/uart_test.cpp:1175 |
| STAT-01 | Sticky errors (overflow, framing) persist across reads of RX | uart.vhd:536-540 | pass    | test/uart/uart_test.cpp:1199 |
| STAT-02 | Reading TX/status port (0x133B read) clears sticky errors    | uart.vhd:265,536 | pass    | test/uart/uart_test.cpp:1213 |
| STAT-03 | FIFO reset (frame bit 7) clears sticky errors                | uart.vhd:536     | pass    | test/uart/uart_test.cpp:1226 |
| STAT-04 | Status bits reflect correct UART (per select register)       | uart.vhd:346-378 | pass    | test/uart/uart_test.cpp:1240 |
| STAT-05 | tx_empty = tx_fifo_empty AND NOT tx_busy                     | uart.vhd:360     | pass    | test/uart/uart_test.cpp:1252 |
| STAT-06 | rx_avail = NOT rx_fifo_empty                                 | uart.vhd:360     | pass    | test/uart/uart_test.cpp:1265 |
| DUAL-01 | UART 0 and UART 1 have independent FIFOs                     | uart.vhd:387-388,572-573 | pass    | test/uart/uart_test.cpp:1291 |
| DUAL-02 | Independent prescalers                                       | uart.vhd:282-286,355,371 | pass    | test/uart/uart_test.cpp:1309 |
| DUAL-03 | Independent frame registers                                  | uart.vhd:300-305 | pass    | test/uart/uart_test.cpp:1324 |
| DUAL-04 | Independent status registers                                 | uart.vhd:346-378 | pass    | test/uart/uart_test.cpp:1337 |
| DUAL-05 | UART 0 = ESP, UART 1 = Pi channel assignment                 | zxnext.vhd       | pass    | test/uart/uart_integration_test.cpp:629 |
| DUAL-06 | Joystick UART mode multiplexing                              | zxnext.vhd:3340-3341 | pass    | test/uart/uart_integration_test.cpp:678 |
| I2C-01  | Reset state: SCL = 1, SDA = 1 (both released)                | zxnext.vhd:3235-3247 | pass    | test/uart/uart_test.cpp:1372 |
| I2C-02  | Write 0x00 to port 0x103B                                    | zxnext.vhd:3237-3238 | pass    | test/uart/uart_test.cpp:1384 |
| I2C-03  | Write 0x01 to port 0x103B                                    | zxnext.vhd:3237-3238 | pass    | test/uart/uart_test.cpp:1396 |
| I2C-04  | Write 0x00 to port 0x113B                                    | zxnext.vhd:3248-3249 | pass    | test/uart/uart_test.cpp:1407 |
| I2C-05  | Write 0x01 to port 0x113B                                    | zxnext.vhd:3248-3249 | pass    | test/uart/uart_test.cpp:1419 |
| I2C-06  | Read port 0x103B                                             | zxnext.vhd:3259  | pass    | test/uart/uart_test.cpp:1431 |
| I2C-07  | Read port 0x113B                                             | zxnext.vhd:3266  | pass    | test/uart/uart_test.cpp:1441 |
| I2C-08  | Only bit 0 is significant for write                          | zxnext.vhd:3238  | pass    | test/uart/uart_test.cpp:1453 |
| I2C-09  | Bits 7:1 always read as 1                                    | zxnext.vhd:3259,3266 | pass    | test/uart/uart_test.cpp:1467 |
| I2C-10  | I2C port enable gated by internal_port_enable(10)            | zxnext.vhd:2418,2392 | pass    | test/uart/uart_integration_test.cpp:570 |
| I2C-11  | Pi I2C1 AND-gating: if pi_i2c1_scl = 0, SCL reads 0          | zxnext.vhd:3259  | pass    | test/uart/uart_test.cpp:1496 |
| I2C-12  | Reset releases both lines                                    | zxnext.vhd:3235-3247 | pass    | test/uart/uart_test.cpp:1511 |
| I2C-13  | NR 0xA0 bit 3 (pi_i2c1_en) gates GPIO 2/3 -> I2C1 wired-AND mux (G138)                                  | zxnext.vhd:2280,2309-2318               | pass    | test/uart/uart_test.cpp:1542 |
| I2C-14  | EEPROM 24LC256 at write addr 0xA0: device ACKs (alongside DS1307) (G139)                                | i2c.cpp                                 | missing | missing                      |
| I2C-P01 | START condition: SDA high->low while SCL high                | zxnext.vhd:3237-3249 | pass    | test/uart/uart_test.cpp:1586 |
| I2C-P02 | STOP condition: SDA low->high while SCL high                 | zxnext.vhd:3237-3249 | pass    | test/uart/uart_test.cpp:1599 |
| I2C-P03 | Send byte (8 clocks): MSB first, clock each bit              | zxnext.vhd       | pass    | test/uart/uart_test.cpp:1614 |
| I2C-P04 | Read ACK: release SDA, clock SCL, read SDA bit 0             | —              | pass    | test/uart/uart_test.cpp:1628 |
| I2C-P05 | Read byte (8 clocks): release SDA, read 8 bits               | —              | pass    | test/uart/uart_test.cpp:1648 |
| I2C-P06 | Send ACK/NACK after read                                     | —              | pass    | test/uart/uart_test.cpp:1678 |
| RTC-01  | Address 0xD0 write: device ACKs                              | —              | pass    | test/uart/uart_test.cpp:1717 |
| RTC-02  | Address 0xD1 read: device ACKs                               | —              | pass    | test/uart/uart_test.cpp:1729 |
| RTC-03  | Wrong address: device NACKs                                  | —              | pass    | test/uart/uart_test.cpp:1741 |
| RTC-04  | Write register pointer (0x00), read seconds                  | —              | pass    | test/uart/uart_test.cpp:1752 |
| RTC-05  | Read minutes (register 0x01)                                 | —              | pass    | test/uart/uart_test.cpp:1762 |
| RTC-06  | Read hours (register 0x02)                                   | —              | pass    | test/uart/uart_test.cpp:1781 |
| RTC-07  | Read day-of-week (register 0x03)                             | —              | pass    | test/uart/uart_test.cpp:1790 |
| RTC-08  | Read date (register 0x04)                                    | —              | pass    | test/uart/uart_test.cpp:1805 |
| RTC-09  | Read month (register 0x05)                                   | —              | pass    | test/uart/uart_test.cpp:1814 |
| RTC-10  | Read year (register 0x06)                                    | —              | pass    | test/uart/uart_test.cpp:1823 |
| RTC-11  | Read control register (0x07)                                 | —              | pass    | test/uart/uart_test.cpp:1875 |
| RTC-12  | Write seconds register                                       | —              | pass    | test/uart/uart_test.cpp:1891 |
| RTC-13  | Write hours in 12h mode (bit 6 = 1)                          | —              | pass    | test/uart/uart_test.cpp:1908 |
| RTC-14  | Sequential read: auto-increment register pointer             | —              | pass    | test/uart/uart_test.cpp:1930 |
| RTC-15  | Sequential write: auto-increment register pointer            | —              | pass    | test/uart/uart_test.cpp:1948 |
| RTC-16  | Clock halt bit (seconds register bit 7)                      | —              | pass    | test/uart/uart_test.cpp:1976 |
| RTC-17  | NVRAM registers 0x08-0x3F (56 bytes)                         | —              | pass    | test/uart/uart_test.cpp:2008 |
| RTC-18  | Snapshot in 12h mode preserves bit 6 + AM/PM bit 5 (G161)                                               | i2c.cpp:111                             | pass    | test/uart/uart_test.cpp:2059 |
| INT-01  | UART 0 RX interrupt: rx_avail when int_en bit set            | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:175 |
| INT-02  | UART 0 RX near-full always triggers                          | zxnext.vhd:1941-1944, zxnext.vhd:1943,1950 | pass    | test/uart/uart_integration_test.cpp:204 |
| INT-03  | UART 1 RX interrupt: same logic as UART 0                    | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:222 |
| INT-04  | UART 0 TX empty interrupt                                    | zxnext.vhd:1942,1950 | pass    | test/uart/uart_integration_test.cpp:238 |
| INT-05  | UART 1 TX empty interrupt                                    | zxnext.vhd:1941,1949 | pass    | test/uart/uart_integration_test.cpp:264 |
| INT-06  | Interrupt enable controlled by NextREG 0xC6                  | zxnext.vhd:1941,1949 | pass    | test/uart/uart_integration_test.cpp:280 |
| INT-07  | Asymmetric vector-1 enable: NR 0xC6 b1=1, b0=0 -> UART0 RX vector fires on near-full only (G134)        | zxnext.vhd:1941-1944; uart.cpp:626-630  | pass    | test/uart/uart_integration_test.cpp:321 |
| GATE-01 | UART port enable (internal_port_enable bit 12)               | zxnext.vhd:2420,2392 | pass    | test/uart/uart_integration_test.cpp:399 |
| GATE-02 | I2C port enable (internal_port_enable bit 10)                | zxnext.vhd:2418,2392 | pass    | test/uart/uart_integration_test.cpp:448 |
| GATE-03 | Enable controlled by NextREG 0x82-0x85                       | zxnext.vhd:2412,2418,2420,2392 | pass    | test/uart/uart_integration_test.cpp:541 |
| NR_A0-01 | Reset default NR 0xA0 reads 0x00 (all routes off) (G135)                                                | zxnext.vhd:5080,6188-6189                 | pass    | test/uart/uart_integration_test.cpp:1224 |
| NR_A0-02 | Write NR 0xA0=0x10 (b4): pi_uart_en asserted; UART1 RX/TX exposed to Pi GPIO (G135)                     | zxnext.vhd                              | pass    | test/uart/uart_integration_test.cpp:1253 |
| NR_A0-03 | Pi UART OFF: UART1 writes do NOT egress to GPIO 14/15 (G135)                                            | zxnext.vhd:2278-2281                    | pass    | test/uart/uart_integration_test.cpp:1284 |

### Companion integration suite — `test/uart/uart_integration_test.cpp`

Hosts integration-tier rows for the UART/I2C subsystem (full Emulator wiring, dual-port + I2C bit-bang). Runs at `25 / 25 pass / 0 fail / 0 skip`. The suite reports no skips: the G135 NR 0xA0 Pi-UART-routing row this paragraph used to call a skip is `NR_A0-03` below, which reads `missing` — it is asserted nowhere at all, which is a larger gap than a skip, not a smaller one. The first 13 IDs listed below are additionally listed in the parent `## UART+I2C/RTC` table above; the five `DEV-*` rows are not — they are the `UartDevice` attach/detach seam GH #25 branch 1 added here, whose backend on UART 0 is the emulated ESP-01 traced in the three `## ESP-01 …` sections below. The four `ESP-*` rows are the G39 placeholders GH #25 branch 5 closed: they re-home here from `uart_test.cpp`'s WONT comments, and unlike `DEV-*` they drive the real `ThreadedEsp` that `--esp` builds rather than a stub. `ESP-03` is deliberately RESTATED — the NR 0x02 bit-7 reset line is latched and read back but drives no device reset in v1.0 (design doc §4.2, §10).

| Test ID    | Plan row title                                                          | VHDL file:line             | Status | Test file:line                              |
|------------|-------------------------------------------------------------------------|----------------------------|--------|---------------------------------------------|
| INT-01     | Bare UART RX → IM2 vector chain                                         | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:175     |
| INT-02     | Bare UART TX → IM2 vector chain                                         | zxnext.vhd:1941-1944, zxnext.vhd:1943,1950 | pass    | test/uart/uart_integration_test.cpp:204     |
| INT-03     | UART0 + UART1 share IM2 vector multiplex                                | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:222     |
| INT-04     | UART RX overflow does not crash IM2 chain                               | zxnext.vhd:1942,1950         | pass    | test/uart/uart_integration_test.cpp:238     |
| INT-05     | UART RX FIFO half-full triggers IM2                                     | zxnext.vhd:1941,1949         | pass    | test/uart/uart_integration_test.cpp:264     |
| INT-06     | UART TX FIFO empty triggers IM2                                         | zxnext.vhd:1941,1949         | pass    | test/uart/uart_integration_test.cpp:280     |
| GATE-01    | NR 0x82 b1 (port_7ffd_io_en) gates port-7FFD writes                     | zxnext.vhd:2420,2392         | pass    | test/uart/uart_integration_test.cpp:399     |
| GATE-02    | NR 0x82 b0 gates port-FE attribute writes                               | zxnext.vhd:2418,2392         | pass    | test/uart/uart_integration_test.cpp:448     |
| GATE-03    | NR 0x84 DAC-port-pair enables                                           | zxnext.vhd:2412,2418,2420,2392 | pass    | test/uart/uart_integration_test.cpp:541     |
| I2C-10     | DS1307 RTC read at 0x68 returns BCD-encoded snapshot                    | zxnext.vhd:2418,2392         | pass    | test/uart/uart_integration_test.cpp:570     |
| DUAL-05    | Dual-UART pin-routing assertion (tautological — pins not visible)       | zxnext.vhd                 | pass    | test/uart/uart_integration_test.cpp:629     |
| DUAL-06    | Pin-7 multiplexed across UART/Joystick/CTC                              | zxnext.vhd                 | pass    | test/uart/uart_integration_test.cpp:678     |
| NR_A0-03   | Pi UART OFF: UART1 writes do NOT egress to GPIO 14/15 (G135)            | zxnext.vhd:2278-2281       | pass    | test/uart/uart_integration_test.cpp:1284    |
| DEV-01     | UartDevice attach diverts channel TX to the device and suppresses loopback | zxnext.vhd:1611,3381       | pass    | test/uart/uart_integration_test.cpp:751     |
| DEV-02     | UartDevice guest-bound sink injects via inject_rx; IM2 follows the NR 0xC6 mask | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:803     |
| DEV-03     | detach_device restores loopback and clears the device's RxSink          | —                        | pass    | test/uart/uart_integration_test.cpp:840     |
| DEV-04     | Device attachment is per-channel: UART 0 (ESP) and UART 1 (Pi) stay separate | zxnext.vhd:3343-3344       | pass    | test/uart/uart_integration_test.cpp:911     |
| DEV-05     | An attached UartDevice takes precedence over the on_tx_byte observer hook | —                        | pass    | test/uart/uart_integration_test.cpp:876     |
| ESP-01     | UART 0 TX egresses to the REAL emulated ESP-01, which answers, not to the channel loopback (G39) | zxnext.vhd:1611-1612,3381 | pass    | test/uart/uart_integration_test.cpp:1081 |
| ESP-02     | The real ESP's reply lands in the UART 0 RX FIFO and raises UART0_RX under the NR 0xC6 mask (G39) | zxnext.vhd:1941-1944,1949-1950 | pass    | test/uart/uart_integration_test.cpp:1113 |
| ESP-03     | NR 0x02 b7 (o_RESET_PERIPHERAL) latches and reads back; v1.0 drives NO device reset (design doc §4.2) | zxnext.vhd:5119,1579 | pass    | test/uart/uart_integration_test.cpp:1163 |
| ESP-04     | With no ESP backend attached, UART 0 keeps its loopback unchanged (G39)  | —                        | pass    | test/uart/uart_integration_test.cpp:1188 |

## ESP-01 socket transport — `src/esp01/test/esp_socket_test.cpp`

The outbound TCP transport of the emulated ESP-01 (GH #25) and the address
policy that gates it. **The sources of this suite live inside the module, at
`src/esp01/`, not under `test/`** — the ESP-01 emulation is a self-contained
component meant to be reusable by other projects, so its tests ship with its
code. It is still a first-class declared suite: `test/unit-tests.conf` pins its
row count and `src/esp01/CMakeLists.txt` registers it with `add_test()`, with
the binary emitted into `build/test/` like every other one.

**The `VHDL file:line` column is a tombstone, `(host sockets)`, not a gap.** The
FPGA core has no ESP8266 in it — only the pins that reach one (`zxnext.vhd:1611-1612`
drives `o_UART0_TX` from `uart0_tx_esp`; `:3381` labels the channel `uart 0 (esp)`).
Everything this suite tests is on the far side of those pins, so there is no line
of the core to cite. Its authority is the host OS socket API, the RFC address
ranges the policy encodes, and the owner's security decisions recorded on GH #25
(default off, RFC1918 explicitly ALLOWED, loopback / link-local / cloud-metadata
denied, no server mode).

Runs at `130 / 130 pass / 0 fail / 0 skip` on a host that can bind a loopback
listener and `fork()`. The `NET-*`, `NET-ERR-*`, `SEC-*`, `TRACE-01..04` and
`SIG-*` rows self-skip when those are unavailable, so a constrained host reports
skips rather than failures. `SIG-01/02` read `skip` in the Status column below
even though they pass here: this document derives status from the SOURCE, and
those two are the only rows whose unavailable-path uses the `skip()` helper —
the rest print their own `SKIP` line. The column is honest about what the source
says, not about this machine.

| Test ID     | Assertion description                                                                                      | VHDL file:line         | Status  | Test file:line                           |
|-------------|------------------------------------------------------------------------------------------------------------|------------------------|---------|------------------------------------------|
| POL-LB-01   | 127.0.0.1 denied as Loopback under the default policy                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:391   |
| POL-LB-02   | 127.0.0.0 (bottom of 127/8) denied as Loopback                                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:392   |
| POL-LB-03   | 127.255.255.255 (top of 127/8) denied as Loopback                                                          | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:393   |
| POL-LB-04   | 126.255.255.255 (just below 127/8) allowed                                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:394   |
| POL-LB-05   | 128.0.0.0 (just above 127/8) allowed                                                                       | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:395   |
| POL-LB-06   | IPv6 ::1 denied as Loopback                                                                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:396   |
| POL-LB-07   | IPv6 ::2 is not loopback and is allowed                                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:398   |
| POL-LB-08   | IPv4-mapped ::ffff:127.0.0.1 denied as Loopback — the mapped bypass is closed                            | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:400   |
| POL-LB-09   | `deny_loopback = false` really allows 127.0.0.1 (the override the NET/TRACE rows run under)                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:402   |
| POL-LB-10   | `deny_loopback = false` really allows ::1                                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:403   |
| POL-LL-01   | 169.254.0.0 (bottom of 169.254/16) denied as LinkLocal                                                     | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:406   |
| POL-LL-02   | 169.254.255.255 (top of 169.254/16) denied as LinkLocal                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:407   |
| POL-LL-03   | 169.253.255.255 (just below the range) allowed                                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:408   |
| POL-LL-04   | 169.255.0.0 (just above the range) allowed                                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:409   |
| POL-LL-05   | IPv6 fe80::1 denied as LinkLocal                                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:410   |
| POL-LL-06   | IPv6 febf:ffff:… (top of fe80::/10) denied as LinkLocal                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:411   |
| POL-LL-07   | IPv6 fe7f::1 (just below fe80::/10) allowed                                                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:414   |
| POL-LL-08   | IPv6 fec0::1 (just above fe80::/10) allowed                                                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:415   |
| POL-MD-01   | 169.254.169.254 reported as CloudMetadata, not the vaguer LinkLocal it also matches                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:419   |
| POL-MD-02   | Alibaba 100.100.100.200 denied as CloudMetadata although CGNAT itself is allowed                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:422   |
| POL-MD-03   | 100.100.100.199 (adjacent to the Alibaba endpoint) allowed                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:423   |
| POL-MD-04   | AWS IMDS over IPv6 fd00:ec2::254 denied as CloudMetadata although ULA itself is allowed                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:425   |
| POL-MD-05   | fd00:ec2::253 (adjacent to the IMDS endpoint) allowed                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:427   |
| POL-MD-06   | IPv4-mapped ::ffff:169.254.169.254 denied as CloudMetadata                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:429   |
| POL-MD-07   | `deny_cloud_metadata = false` FALLS THROUGH to LinkLocal rather than allowing                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:435   |
| POL-MD-08   | with metadata AND link-local both off, 169.254.169.254 really is allowed                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:438   |
| POL-PRIV-01 | 10.0.0.1 allowed by default (owner decision: RFC1918 is reachable)                                         | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:442   |
| POL-PRIV-02 | 10.255.255.255 (top of 10/8) allowed by default                                                            | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:443   |
| POL-PRIV-03 | 172.16.0.1 allowed by default                                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:444   |
| POL-PRIV-04 | 172.31.255.255 (top of 172.16/12) allowed by default                                                       | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:445   |
| POL-PRIV-05 | 192.168.1.1 allowed by default                                                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:446   |
| POL-PRIV-06 | CGNAT 100.64.0.1 allowed by default                                                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:447   |
| POL-PRIV-07 | IPv6 ULA fd12:3456::1 allowed by default                                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:448   |
| POL-PRIV-08 | `deny_private = true` denies 10.0.0.1 as Private                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:453   |
| POL-PRIV-09 | `deny_private = true` denies 192.168.1.1 as Private                                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:454   |
| POL-PRIV-10 | `deny_private = true` denies 172.16.0.0 (bottom boundary of 172.16/12)                                     | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:455   |
| POL-PRIV-11 | 172.15.255.255 (just below 172.16/12) stays allowed with `deny_private`                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:456   |
| POL-PRIV-12 | 172.32.0.0 (just above 172.16/12) stays allowed with `deny_private`                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:457   |
| POL-PRIV-13 | `deny_private = true` denies CGNAT 100.64.0.1 as Private                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:458   |
| POL-PRIV-14 | 100.63.255.255 (just below 100.64/10) stays allowed with `deny_private`                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:459   |
| POL-PRIV-15 | `deny_private = true` denies IPv6 ULA fd12::1 as Private                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:460   |
| POL-PRIV-16 | `deny_private = true` denies fc00::1 (bottom of fc00::/7)                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:462   |
| POL-PRIV-17 | fe00::1 (just above fc00::/7) stays allowed with `deny_private`                                            | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:464   |
| POL-RSV-01  | 0.0.0.0 denied as Unspecified                                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:469   |
| POL-RSV-02  | 0.255.255.255 (top of 0/8) denied as Unspecified                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:470   |
| POL-RSV-03  | 1.0.0.0 (just above 0/8) allowed                                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:471   |
| POL-RSV-04  | IPv6 :: denied as Unspecified                                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:472   |
| POL-RSV-05  | 224.0.0.1 denied as MulticastOrReserved                                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:473   |
| POL-RSV-06  | 223.255.255.255 (just below the multicast range) allowed                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:474   |
| POL-RSV-07  | 240.0.0.0 denied as MulticastOrReserved                                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:475   |
| POL-RSV-08  | 255.255.255.255 denied as MulticastOrReserved                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:476   |
| POL-RSV-09  | IPv6 ff02::1 denied as MulticastOrReserved                                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:477   |
| NORM-01     | ::ffff:1.2.3.4 unwraps to 1.2.3.4                                                                          | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:481   |
| NORM-02     | NAT64 64:ff9b::1.2.3.4 unwraps to 1.2.3.4                                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:487   |
| NORM-03     | the NAT64 route to loopback is closed: 64:ff9b::127.0.0.1 denied as Loopback                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:491   |
| NORM-04     | IPv4-compatible ::1.2.3.4 unwraps to 1.2.3.4                                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:496   |
| NORM-05     | :: keeps its IPv6 identity and is not folded to 0.0.0.0                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:499   |
| NORM-06     | ::1 keeps its IPv6 identity and is not folded to 0.0.0.1                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:501   |
| NORM-07     | ::0.0.0.5 is not unwrapped either                                                                          | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:503   |
| NORM-08     | an ordinary global IPv6 address is returned unchanged                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:505   |
| NORM-09     | an IPv4 address is returned unchanged                                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:508   |
| POL-TUN-01  | 6to4 2002:7f00:1:: judged by its embedded endpoint: denied as Loopback                                     | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:515   |
| POL-TUN-02  | 6to4 wrapping 169.254.169.254 denied as CloudMetadata                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:516   |
| POL-TUN-03  | 6to4 wrapping 169.254.0.1 denied as LinkLocal                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:517   |
| POL-TUN-04  | 6to4 wrapping a public address (93.184.216.34) allowed                                                     | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:518   |
| POL-TUN-05  | 6to4 wrapping RFC1918 10.0.0.1 allowed by default, like the bare address                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:522   |
| POL-TUN-06  | …and denied as Private when `deny_private` is on — the endpoint takes the FULL policy                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:526   |
| POL-TUN-07  | Teredo is deliberately NOT unwrapped: a 2001:0::/32 address spelling 127.0.0.1 stays allowed               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:533   |
| POL-TUN-08  | ISATAP is an interface-identifier pattern, not a prefix: ::5efe:7f00:1 under a global prefix stays allowed | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:539   |
| POL-TUN-09  | …while fe80::5efe:7f00:1, where ISATAP actually lives, is already denied as LinkLocal                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:543   |
| POL-TUN-10  | `normalize()` leaves a 6to4 address as IPv6 rather than folding it to IPv4                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:546   |
| POL-TUN-11  | a 6to4 address listed FIRST does not win the IPv4 preference pass in `select_candidate`                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:556   |
| POL-TUN-12  | `tunnel_endpoint()` extracts the 6to4 gateway address                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:562   |
| POL-TUN-13  | `tunnel_endpoint()` declines an ordinary IPv6 address                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:565   |
| POL-TUN-14  | `tunnel_endpoint()` declines an IPv4 address whose first octets are 0x20,0x02                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:570   |
| ESP-SEL-01 | an empty candidate list selects nothing and reports DenyReason::None                                       | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:578   |
| ESP-SEL-02 | IPv4 is preferred even when an IPv6 candidate comes first                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:586   |
| ESP-SEL-03 | a denied candidate is skipped in favour of an allowed one                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:594   |
| ESP-SEL-04 | IPv6 is used when there is no IPv4 candidate                                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:602   |
| ESP-SEL-05 | an all-denied list reports the FIRST candidate's deny reason                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:611   |
| ESP-SEL-06 | with loopback allowed, IPv4 loopback still wins the preference pass over IPv6                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:620   |
| ESP-SEL-07 | a mapped IPv4 candidate counts as IPv4 and is returned verbatim, not unwrapped                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:629   |
| FMT-01      | `to_string()` renders IPv4 as a dotted quad                                                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:635   |
| FMT-02      | `to_string()` renders IPv6 in full uncompressed 8-group form                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:637   |
| FMT-03      | IPv6 groups drop leading zeros but keep their positions                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:639   |
| FMT-04      | every DenyReason has distinct, non-"unknown" text                                                          | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:642   |
| FMT-05      | every TransportState has distinct, non-"unknown" text                                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:646   |
| SEAM-01     | the logging seam's default threshold is info — the module's own quiet default                            | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:659   |
| SEAM-02     | every LogLevel has distinct, non-"unknown" text                                                            | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:661   |
| SEAM-03     | `log_hex_byte()` renders a byte as two upper-case hex digits, not as a character                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:665   |
| SEAM-04     | an installed sink receives the module's output                                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:676   |
| SEAM-04b    | clearing the sink restores silence at any level (an unbound seam is silent)                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:681   |
| SEAM-05     | the threshold drops everything below it and keeps the rest                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:690   |
| SEAM-06     | lowering the threshold lets trace through                                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:695   |
| SEAM-07     | `{}` substitutes positionally, in order, for mixed argument types                                          | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:705   |
| SEAM-08     | `{{` and `}}` render as literal braces                                                                     | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:707   |
| SEAM-09     | a format spec inside the braces is ignored, not printed                                                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:708   |
| SEAM-10     | surplus arguments and surplus placeholders are both harmless                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:710   |
| SOCK-TRACE-01 | an IP literal is resolved without a DNS lookup (AI_NUMERICHOST path, traced at debug)                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:745   |
| SOCK-TRACE-02 | at the default level a full connect/send/recv/close session logs open + close and nothing else             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:763   |
| SOCK-TRACE-03 | a policy refusal is logged at the default level — never silent                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:777   |
| SOCK-TRACE-04 | a host NAME takes the resolve path, not the numeric fast path                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:799   |
| TR-01       | a fresh transport is Idle with no error string                                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:815   |
| TR-02       | an empty host is rejected outright, leaving the state Idle                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:817   |
| TR-03       | port 0 is rejected outright, leaving the state Idle                                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:819   |
| TR-04       | `close()` on an idle transport stays Idle                                                                  | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:821   |
| TR-05       | `send()` before Connected moves no bytes                                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:824   |
| TR-06       | `recv()` before Connected moves no bytes                                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:826   |
| TR-07       | `poll()` in Idle is a harmless no-op                                                                       | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:828   |
| TR-08       | an accepted request parks in Resolving without resolving                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:831   |
| TR-09       | a second `begin_connect()` while busy is refused                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:834   |
| SEC-01      | the default policy refuses a loopback connect through the real transport (state Failed)                    | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:851   |
| SEC-02      | the refusal error string says WHY, naming the policy and the loopback rule                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:853   |
| SEC-03      | a refused connect never reached the listener — the socket was never opened                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:856   |
| NET-01      | `begin_connect()` to an in-process loopback listener is accepted                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:921   |
| NET-02      | the connect completes through `poll()` alone, with no blocking call                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:923   |
| NET-03      | the listener sees the connection                                                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:926   |
| NET-04      | `peer_address()` is the loopback address actually connected to                                             | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:927   |
| NET-05      | `send()` accepts the bytes and the transport stays Connected                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:934   |
| NET-06      | the server receives exactly what was sent, byte for byte                                                   | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:936   |
| NET-07      | `recv()` with nothing pending returns 0 and stays Connected                                                | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:940   |
| NET-08      | `recv()` returns exactly what the server sent                                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:956   |
| NET-09      | a peer close moves the transport to Closed                                                                 | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:960   |
| NET-10      | `send()`/`recv()` after Closed move no bytes                                                               | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:963   |
| NET-11      | a closed transport is reusable: a second `begin_connect()` connects                                        | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:967   |
| NET-12      | `close()` on a live connection ends in Closed and the server sees EOF                                      | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:972   |
| NET-ERR-01  | a connect to a closed port ends in Failed rather than hanging                                              | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:1004  |
| NET-ERR-02  | the failure carries an explanatory error string naming `connect`                                           | (host sockets)         | pass    | src/esp01/test/esp_socket_test.cpp:1005  |
| SIG-01      | a blind send to a closed peer does not signal-kill the process (MSG_NOSIGNAL / SO_NOSIGPIPE)               | (host sockets)         | skip    | src/esp01/test/esp_socket_test.cpp:1124  |
| SIG-02      | …and surfaces as Failed with an error string instead                                                     | (host sockets)         | skip    | src/esp01/test/esp_socket_test.cpp:1125  |

## ESP-01 AT engine — `src/esp01/test/esp_at_test.cpp`

The AT command model of the emulated ESP-01 (GH #25), driven against an
in-memory fake transport — no socket, no DNS, no listener — plus the optional
threaded wrapper (`MODE-*`). Like the transport suite above, **its sources live
inside the module at `src/esp01/`**, registered from `src/esp01/CMakeLists.txt`
and pinned in `test/unit-tests.conf`.

**The `VHDL file:line` column is a tombstone, `(ESP-AT firmware)`, not a gap.**
The AT surface is not FPGA behaviour at all: it was derived from the Espressif
AT firmware's response strings as evidenced by the software that actually parses
them — the NextZXOS `ESPAT.DRV` and dot-command sources shipped on the official
SD card, plus the NXtel and nextsync clients. That evidence, not `zxnext.vhd`, is
what each row's exact byte framing answers to, and the full derivation is
recorded in `doc/design/ESP01-EMULATOR-DESIGN.md`.

Runs at `137 / 137 pass / 0 fail / 0 skip`.

| Test ID     | Assertion description                                                                                          | VHDL file:line         | Status  | Test file:line                           |
|-------------|----------------------------------------------------------------------------------------------------------------|------------------------|---------|------------------------------------------|
| AT-01       | a bare CRLF is an empty command, answered ERROR (nextsync's first probe)                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:443       |
| AT-02       | `AT` answers exactly `\r\nOK\r\n`                                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:447       |
| AT-03       | `ATE0` answers OK                                                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:449       |
| AT-03b      | …and leaves echo off                                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:450       |
| AT-04       | `ATE1` is not itself echoed — echo was still off while its own bytes arrived                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:453       |
| AT-04b      | …but echo is really on afterwards                                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:456       |
| AT-04c      | the NEXT line is echoed, terminator and all, before its reply                                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:458       |
| AT-04d      | `ATE0` still echoes itself before switching echo off                                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:461       |
| AT-05       | `AT+CIPMUX=0` answers OK — the only supported mode                                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:464       |
| AT-06       | `AT+CIPMUX=1` is REFUSED: accepting it would promise a `+IPD` form nextsync cannot read                        | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:467       |
| AT-07       | `AT+CIPCLOSE` with nothing open answers ERROR (nextsync loops until it sees it)                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:472       |
| AT-08       | `AT+RST` answers OK then the two WIFI URCs, never `ready`                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:476       |
| AT-09       | an unsupported command answers ERROR                                                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:479       |
| AT-10       | a bare LF produces nothing — it is only ever the CR's partner                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:482       |
| AT-10b      | …and is not echoed either, even with echo on                                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:486       |
| AT-11       | command names match case-insensitively                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:488       |
| AT-12       | an overlong line answers exactly one ERROR                                                                     | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:500       |
| AT-12b      | …and is refused WHOLE: its truncated prefix, a valid CIPSTART, is never run                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:502       |
| AT-13       | nextsync's `AT+UART_CUR=1152000,8,1,0,0` baud switch is acknowledged                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:506       |
| AT-13b      | …and the requested baud is recorded for tracing                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:507       |
| AT-14       | the `AT+UART_DEF` form is accepted too                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:511       |
| AT-14b      | …as is the plain `AT+UART` form                                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:513       |
| AT-14c      | syncfast's 2 Mbaud is recorded                                                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:514       |
| AT-15       | a non-numeric baud answers ERROR, not a clamped number                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:516       |
| CON-01      | `AT+CIPSTART` answers NOTHING until the transport settles — there is no synchronous connect                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:524       |
| CON-01b     | …and the engine reports that it is waiting                                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:528       |
| CON-02      | a settled connection answers OK                                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:530       |
| CON-02b     | …the engine reports connected                                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:531       |
| CON-02c     | …and the transport received the parsed host and port                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:532       |
| CON-03      | a failed connect answers ERROR only — never FAIL, never CLOSED for a connection that never existed           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:538       |
| CON-03b     | …and the engine is not connected                                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:542       |
| CON-04      | NXtel's 4-argument `AT+CIPSTART` (with keepalive) connects                                                     | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:546       |
| CON-04b     | …with host and port parsed past the keepalive argument                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:548       |
| CON-04c     | trailing garbage after a VALID keepalive still answers ERROR                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:590       |
| CON-04d     | …and no connect was attempted                                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:592       |
| CON-05      | UDP is refused — v1.0 is TCP only                                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:553       |
| CON-05b     | …and no connect was ever started                                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:554       |
| CON-06      | a second `AT+CIPSTART` while connected answers ERROR, not `ALREADY CONNECTED`                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:559       |
| CON-06b     | …and is rejected by the ENGINE: the transport is never asked a second time                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:562       |
| CON-07      | guest input arriving during a connect is deferred, not answered early                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:569       |
| CON-07b     | …then replayed in order once the connect settles                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:572       |
| CON-08      | closing a live connection reports CLOSED then OK                                                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:578       |
| CON-08b     | …and the transport was really closed                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:580       |
| CON-09      | port 0 answers ERROR                                                                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:584       |
| CON-10      | a transport that refuses the request answers ERROR immediately                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:629       |
| CON-11      | a connect that never completes is abandoned with ERROR once the connect deadline expires                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:606       |
| CON-11b     | …and the engine stops waiting                                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:608       |
| CON-11c     | …having released the socket                                                                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:609       |
| CON-12      | the connection slot is reusable after a timed-out connect                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:615       |
| CON-12b     | …and the retry really connects                                                                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:616       |
| CON-13      | a connect still inside its deadline is awaited, not refused                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:622       |
| CON-13b     | …and remains pending                                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:624       |
| SEND-01     | `AT+CIPSEND` answers `\r\nOK\r\n> ` — TRAILING SPACE INCLUDED; three parsers busy-wait on it with no timeout | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:638       |
| SEND-01b    | …and 3 payload bytes are outstanding                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:642       |
| SEND-02     | the completed payload answers `\r\nSEND OK\r\n`                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:650       |
| SEND-02b    | …and exactly the payload reached the peer                                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:652       |
| SEND-03     | NXtel's 5 bytes after `CIPSEND=3`: 3 are payload, the trailing CRLF becomes an empty command line              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:663       |
| SEND-03b    | …and the peer got exactly the 3 IAC bytes, not 5                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:667       |
| SEND-04     | `AT+CIPSENDEX` is a distinct command with the same prompt                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:673       |
| SEND-04b    | …and the same completion                                                                                     | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:676       |
| SEND-04c    | …delivering the payload                                                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:677       |
| SEND-05     | `AT+CIPSEND` with no connection answers ERROR and no prompt, which would otherwise hang the guest              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:680       |
| SEND-06     | a zero length answers ERROR                                                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:686       |
| SEND-06b    | …as does one over the 2048-byte ceiling                                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:688       |
| SEND-06c    | …but exactly 2048 IS accepted, prompt and all                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:693       |
| SEND-06d    | …with the full payload outstanding                                                                           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:695       |
| SEND-07     | the send path is 8-bit clean, NUL and ESC included                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:703       |
| SEND-08     | a partial socket accept still answers SEND OK exactly once                                                     | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:710       |
| SEND-08b    | …with only what the kernel took so far delivered                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:712       |
| SEND-08c    | …and the remainder flushed by later polls                                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:715       |
| SEND-09     | payload bytes are never echoed, even with echo on                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:721       |
| IPD-01      | inbound data is framed as the unmultiplexed `+IPD,<len>:` form                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:730       |
| IPD-02      | `+IPD` is 8-bit clean and `<len>` counts raw bytes                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:736       |
| IPD-03      | bytes trickling in while a chunk drains coalesce into ONE following chunk                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:753       |
| IPD-04      | SEND OK then `+IPD`, with no stray `+` between them                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:767       |
| IPD-04b     | …the first `+` in the stream is the `+IPD`'s own                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:769       |
| IPD-05      | a 3000-byte burst is split at the 2048-byte chunk ceiling                                                      | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:776       |
| IPD-05b     | …and the remainder is a second chunk, not a dribble                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:778       |
| IPD-05c     | …totalling exactly the payload plus two headers                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:780       |
| IPD-07      | once the header starts, every byte-slot delivers a byte — no gap can open inside `+IPD,<len>:`               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:799       |
| IPD-07b     | …and the header arrived intact                                                                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:803       |
| IPD-08      | a peer close is reported only AFTER its last bytes have been framed                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:811       |
| IPD-09      | no `+IPD` is cut while a command line is half-received                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:820       |
| IPD-09b     | …it follows the completed command's reply                                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:823       |
| IPD-10      | no `+IPD` is cut between the `>` prompt and the payload's SEND OK                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:832       |
| IPD-10b     | …it follows SEND OK                                                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:835       |
| PACE-01     | a burst is drip-fed one byte per byte-time, never dumped into the 512-byte RX FIFO                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:845       |
| PACE-02     | a 10-byte-time span releases exactly 10 bytes                                                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:853       |
| PACE-03     | sub-byte spans accumulate rather than rounding up to a byte                                                    | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:862       |
| PACE-04     | idle time banks no credit — a quiet link must not burst at unbounded speed when data arrives                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:871       |
| PACE-05     | a faster byte-time delivers proportionally more bytes (the live prescaler is followed)                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:882       |
| PACE-06     | a zero byte-time neither divides by zero nor hangs                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:905       |
| PACE-07     | a long span drains the whole reply                                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:891       |
| PACE-07b    | …and the leftover sub-byte credit does NOT survive into the next burst                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:894       |
| PACE-07c    | …the next byte arrives a full byte-time after the refill                                                     | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:898       |
| HOOK-01     | an idle engine lowers the tick gate                                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:915       |
| HOOK-02     | queued output raises the tick gate                                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:917       |
| HOOK-02b    | …and draining lowers it again                                                                                | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:919       |
| DIAG-01     | `AT+CWJAP?` carries NXtel's `CWJAP:"` SSID anchor                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:925       |
| DIAG-01b    | …and the `","` anchor that precedes the AP MAC                                                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:927       |
| DIAG-01c    | …ending in an OK                                                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:929       |
| DIAG-02     | `AT+CIFSR` carries the `TAIP,"` and `TAMAC,"` anchors                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:933       |
| DIAG-03     | `AT+CIPSTA?` carries the `gateway:"` and `netmask:"` anchors                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:938       |
| DIAG-04     | `AT+GMR` carries both version anchors                                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:945       |
| DIAG-04b    | …each terminated by a `(` on its OWN line, so neither field renders as garbage                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:956       |
| DIAG-05     | `AT+CIPDNS_CUR?` carries the `+CIPDNS_CUR:` anchor twice                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:962       |
| DIAG-06     | every diagnostic reply terminates with the exact OK framing `.ESPBAUD` compares against                        | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:974       |
| DIAG-07     | the advertised SSID is the fixed synthetic literal `JNextWifiHost`, never a host network                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:977       |
| NEVER-01    | a full session emits none of the never-emit URCs (`busy p…`, `ALREADY CONNECTED`, `SEND FAIL`, …)          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1010      |
| NEVER-02    | …and `AT+RST` drops the connection without an unsolicited CLOSED                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1011      |
| TRACE-01    | at the default level a connection open is reported                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1046      |
| TRACE-02    | …and so is the close                                                                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1048      |
| TRACE-03    | …and NOTHING else — no AT chatter, no prompt, no `+IPD`, no pacing                                         | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1050      |
| TRACE-04    | at debug every AT command received is traced                                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1064      |
| TRACE-05    | …every response emitted is traced, escaped so framing is visible                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1066      |
| TRACE-06    | …the payload byte count is traced                                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1068      |
| TRACE-07    | …and the `+IPD` framing decision is traced                                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1070      |
| TRACE-08    | …but per-byte pacing is not — that is trace level                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1072      |
| TRACE-09    | at trace the RX pacing and queue state are visible                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1081      |
| MODE-01     | an unstarted `ThreadedEsp` wrapper is not running                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1108      |
| MODE-02     | driven INLINE, `receive()` alone answers as the bare core does                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1114      |
| MODE-02b    | …and a connect's reply is still deferred, not invented                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1124      |
| MODE-02c    | …until an inline `poll()` settles the transport                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1128      |
| MODE-03     | `start()` brings the worker thread up                                                                          | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1137      |
| MODE-04     | the worker drains guest input without being polled                                                             | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1141      |
| MODE-05     | driven THREADED the wrapper answers with the identical bytes                                                   | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1142      |
| MODE-06     | `stop()` joins the worker and reports it                                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1145      |
| MODE-07     | …and `stop()` is idempotent                                                                                  | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1147      |
| MODE-08     | a connect completes on the worker thread                                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1168      |
| MODE-09     | …the CIPSEND prompt still comes back byte-exact                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1172      |
| MODE-10     | …the payload is acknowledged                                                                                 | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1177      |
| MODE-11     | …and really reached the transport                                                                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1178      |
| MODE-12     | unsolicited peer data is framed and paced out unprompted                                                       | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1188      |
| MODE-13     | the worker really ran while the wrapper was alive                                                              | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1214      |
| MODE-14     | destroying a running wrapper JOINS: the destructor cannot return while the worker is inside `poll()`           | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1219      |
| MODE-15     | the worker is provably inside a long `poll()` when the deadlock hazard is probed                               | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1245      |
| MODE-16     | …and `tick()` returns immediately rather than waiting for that `poll()` to finish                            | (ESP-AT firmware)      | pass    | src/esp01/test/esp_at_test.cpp:1252      |

## ESP-01 jnext UART adapter — `test/esp/esp_uart_adapter_test.cpp`

jnext's own side of the ESP-01 seam: the `UartDevice` implementation
(`src/peripheral/esp_uart_adapter.*`), the `Uart::tick` call site that consumes
its hot-path gate, and the binding of the module's logging seam to jnext's
`esp01` spdlog logger. This suite stays under `test/` precisely because it is
jnext-specific — putting it in the module would drag jnext back into a component
that is meant to have no jnext types.

**No tombstone here, deliberately.** This is the one ESP suite whose authority is
mixed: `HOOK-03/03b/03c` exercise the device gate in `Uart::tick` and
`HOOK-06/06b` the framing-bit-7 UART reset, both of which the FPGA core does
specify (`uart.vhd`, `uart_tx.vhd`, `uart_rx.vhd`), while the `ADP-*` and `LOG-*`
rows are jnext-internal seam contracts (`ADP-08..17` are the replay gate, whose
oracle is jnext's own rewind/RZX model and not the core at all). A tombstone is
stamped on every uncited
row of its suite, so applying one here would claim "there is nothing to cite"
about rows that have something to cite. Those cells read `—` instead — an honest
"citation missing", closable by citing the VHDL in the test source.

Runs at `30 / 30 pass / 0 fail / 0 skip`.

| Test ID     | Assertion description                                                                            | VHDL file:line         | Status  | Test file:line                           |
|-------------|--------------------------------------------------------------------------------------------------|------------------------|---------|------------------------------------------|
| HOOK-03     | `Uart::tick` does not tick a device that lowered its `UartDevice` gate                           | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:142   |
| HOOK-03b    | …and ticks it once per call when the gate is raised                                            | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:145   |
| HOOK-03c    | …and stops entirely once the device is detached                                                | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:149   |
| HOOK-04     | a real `Uart` round-trips `AT\r\n` through the adapter and back into the RX FIFO                 | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:165   |
| HOOK-05     | delivery follows the live channel prescaler, not a hardcoded 115200                              | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:187   |
| HOOK-06     | a UART held in reset (framing bit 7) stops device ticking — its RX FIFO is about to be cleared | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:197   |
| HOOK-06b    | …and ticking resumes when the guest releases the reset                                         | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:202   |
| ADP-01      | a fresh adapter mirrors the engine's lowered tick gate                                           | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:213   |
| ADP-02      | `receive()` forwards to the engine AND raises the mirrored gate                                  | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:216   |
| ADP-03      | `tick()` forwards, and the engine's output reaches the adapter's RX sink                         | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:220   |
| ADP-04      | …and the mirrored gate falls again once the engine is idle                                     | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:222   |
| ADP-05      | the adapter delivered all 6 reply bytes while it was alive                                       | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:240   |
| ADP-06      | destroying the adapter clears the engine's sink rather than leaving it dangling                  | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:244   |
| ADP-07      | the same adapter drives the THREADED `EspDevice` wrapper unchanged                               | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:265   |
| ADP-08      | a fresh adapter is live, not inert                                                               | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:283   |
| ADP-09      | an inert adapter does not forward guest TX bytes to the engine                                   | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:287   |
| ADP-10      | …and holds the hot-path tick gate DOWN                                                         | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:289   |
| ADP-11      | …and delivers nothing to the guest                                                             | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:291   |
| ADP-12      | the engine has a reply queued and the gate is up (the discriminator for ADP-13)                  | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:303   |
| ADP-13      | `set_inert(true)` lowers the gate immediately, not after one more call                           | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:306   |
| ADP-14      | `set_inert(false)` re-raises it from the ESP's own state, without waiting for `poll()`           | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:308   |
| ADP-15      | …and the reply held back during the inert window still arrives intact                          | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:312   |
| ADP-16      | repeating `set_inert(false)` does not disturb a gate the engine legitimately raised              | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:323   |
| ADP-17      | repeating `set_inert(true)` keeps the gate down                                                  | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:327   |
| LOG-01      | `Log::init()` registers jnext's `esp01` spdlog logger                                            | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:336   |
| LOG-02      | `--log-level esp01=trace` reaches that logger                                                    | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:340   |
| LOG-03      | a line the module emits through its own seam comes out of jnext's `esp01` logger                 | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:366   |
| LOG-04      | …carrying the level the module chose, not a flattened one                                      | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:368   |
| LOG-05      | an `esp01` logger at `off` raises the module's seam threshold to error                           | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:379   |
| LOG-06      | …and turning the logger up lowers the threshold within one `poll()`                            | —                    | pass    | test/esp/esp_uart_adapter_test.cpp:383   |

## NextREG — `test/nextreg/nextreg_test.cpp`

Last-touch commit: `044f9c57877c114c6c32221b1f9b6016e24e5958` (`044f9c5787`)

| Test ID | Plan row title                                         | VHDL file:line | Status  | Test file:line                    |
|---------|--------------------------------------------------------|----------------|---------|-----------------------------------|
| SEL-01  | Write 0x243B = 0x15, read 0x243B                       | zxnext.vhd:4597-4599 | pass    | test/nextreg/nextreg_test.cpp:123 |
| SEL-02  | Reset, read 0x243B                                     | zxnext.vhd:4594-4596 | pass    | test/nextreg/nextreg_test.cpp:147 |
| SEL-03  | Write 0x243B = 0x00, write 0x253B = 0x42, read NR 0x00 | zxnext.vhd:5884-5885 | pass    | test/nextreg/nextreg_integration_test.cpp:1416               |
| SEL-04  | Write 0x243B = 0x7F, write 0x253B = 0xAB, read NR 0x7F | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:166 |
| SEL-05a    | Z80N NEXTREG instruction preserves NR 0x07 selection (Z80N injects rr,vv directly) | zxnext.vhd:4739-4745 | missing | missing                           |
| SEL-05b    | Discriminative pair: post-Z80N data write lands at preserved selection  | zxnext.vhd:4739-4745          | missing | missing                           |
| RO-01   | Read NR 0x00                                           | —              | pass    | test/nextreg/nextreg_integration_test.cpp:1311               |
| RO-02   | Write NR 0x00, read back                               | zxnext.vhd:5884-5885 | pass    | test/nextreg/nextreg_integration_test.cpp:1327               |
| RO-03   | Read NR 0x01                                           | —              | pass    | test/nextreg/nextreg_integration_test.cpp:1338               |
| RO-04   | Read NR 0x0E                                           | zxnext_top_issue2.vhd:38 | pass    | test/nextreg/nextreg_integration_test.cpp:1350               |
| RO-05   | Read NR 0x0F                                           | —              | pass    | test/nextreg/nextreg_integration_test.cpp:1362               |
| RO-06   | Read NR 0x1E/0x1F                                      | zxnext.vhd:5982-5986 | pass    | test/nextreg/nextreg_integration_test.cpp:1388               |
| NREG-RST-01 | After reset, read NR 0x14                              | zxnext.vhd:4947  | pass    | test/nextreg/nextreg_integration_test.cpp:172                |
| NREG-RST-02 | After reset, read NR 0x15                              | zxnext.vhd:4948  | pass    | test/nextreg/nextreg_integration_test.cpp:183                |
| NREG-RST-03 | After reset, read NR 0x4A                              | zxnext.vhd:5002  | pass    | test/nextreg/nextreg_integration_test.cpp:193                |
| NREG-RST-04 | After reset, read NR 0x42                              | zxnext.vhd:4993  | pass    | test/nextreg/nextreg_integration_test.cpp:203                |
| NREG-RST-05 | After reset, read NR 0x50-0x57                         | zxnext.vhd:4610-4618 | pass    | test/nextreg/nextreg_integration_test.cpp:228                |
| NREG-RST-06 | After reset, read NR 0x68                              | zxnext.vhd:5029  | pass    | test/nextreg/nextreg_integration_test.cpp:238                |
| NREG-RST-07 | After reset, read NR 0x0B                              | zxnext.vhd:4939-4941 | pass    | test/nextreg/nextreg_integration_test.cpp:249                |
| NREG-RST-08 | After reset, read NR 0x82-0x85                         | zxnext.vhd:5052-5068 | pass    | test/nextreg/nextreg_integration_test.cpp:271                |
| RST-09  | After reset, read NR 0x1B clip                         | zxnext.vhd:5971-5977 | pass    | test/nextreg/nextreg_integration_test.cpp:316                |
| RW-01   | 0x07                                                   | zxnext.vhd:5902-5903 | pass    | test/nextreg/nextreg_integration_test.cpp:2172               |
| RW-02   | 0x08                                                   | zxnext.vhd:5906 | pass    | test/nextreg/nextreg_integration_test.cpp:2224               |
| RW-03   | 0x12                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:277 |
| RW-04   | 0x14                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:289 |
| RW-05   | 0x15                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:302 |
| RW-06   | 0x16                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:313 |
| RW-07   | 0x42                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:324 |
| RW-08   | 0x43                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:338 |
| RW-09   | 0x4A                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:349 |
| RW-10   | 0x50-57                                                | zxnext.vhd:4607-4700 | pass    | test/nextreg/nextreg_test.cpp:373 |
| RW-11   | 0x7F                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:384 |
| RW-12   | 0x6B                                                   | zxnext.vhd       | pass    | test/nextreg/nextreg_test.cpp:396 |
| CLIP-01 | Write NR 0x18 four times: 10,20,30,40                  | zxnext.vhd:5242-5249 | pass    | test/nextreg/nextreg_integration_test.cpp:1449               |
| CLIP-02 | Write NR 0x18 five times                               | zxnext.vhd:5242-5249 | pass    | test/nextreg/nextreg_integration_test.cpp:1469               |
| CLIP-03 | Write NR 0x1C bit 0 = 1                                | zxnext.vhd:5278-5281 | pass    | test/nextreg/nextreg_integration_test.cpp:1488               |
| CLIP-04 | Write NR 0x1C bit 1 = 1                                | zxnext.vhd:5242-5290 | pass    | test/nextreg/nextreg_integration_test.cpp:1509               |
| CLIP-05 | Write NR 0x1C bit 2 = 1                                | zxnext.vhd:5242-5290 | pass    | test/nextreg/nextreg_integration_test.cpp:1531               |
| CLIP-06 | Write NR 0x1C bit 3 = 1                                | zxnext.vhd:5242-5290 | pass    | test/nextreg/nextreg_integration_test.cpp:1549               |
| CLIP-07 | Read NR 0x1C                                           | zxnext.vhd:5979-5980 | pass    | test/nextreg/nextreg_integration_test.cpp:1568               |
| CLIP-08 | Read NR 0x18 cycles through clip values                | zxnext.vhd:5947-5953 | pass    | test/nextreg/nextreg_integration_test.cpp:1613               |
| MMU-01  | Reset defaults                                         | —              | missing | missing                                                      |
| NR-MMU-02 | Write NR 0x52 = 0x20, read back                        | zxnext.vhd:4613  | pass    | test/nextreg/nextreg_test.cpp:455 |
| MMU-03  | Write port 0x7FFD, check MMU6/7                        | —              | missing | missing                                                      |
| MMU-04  | NextREG write overrides port write                     | —              | missing | missing                                                      |
| CFG-01  | Write NR 0x03 bits 6:4 for timing                      | zxnext.vhd:1099,5893-5894 | pass    | test/nextreg/nextreg_integration_test.cpp:2504               |
| CFG-02  | Write NR 0x03 bit 3 toggles dt_lock                    | zxnext.vhd:5121-5151,5894 | pass    | test/nextreg/nextreg_integration_test.cpp:2540               |
| CFG-03  | Write NR 0x03 bits 2:0 = 111                           | zxnext.vhd:5147-5148 | pass    | test/nextreg/nextreg_test.cpp:515 |
| CFG-04  | Write NR 0x03 bits 2:0 = 001-100                       | zxnext.vhd:5149-5150 | pass    | test/nextreg/nextreg_test.cpp:538 |
| CFG-05  | Machine type only writable in config mode              | zxnext.vhd:5147-5151 | pass    | test/nextreg/nextreg_integration_test.cpp:2571               |
| CFG-06     | Write NR 0x03 bits 2:0 = 000 — no-op on config_mode                     | zxnext.vhd:5147-5151          | pass    | test/nextreg/nextreg_test.cpp:562 |
| CFG-07     | Power-on / reset default `nr_03_config_mode = 1`                        | zxnext.vhd:1102,5147-5151       | pass    | test/nextreg/nextreg_test.cpp:592 |
| CFG-08     | NR 0x03 config_mode preservation across soft-reset                      | zxnext.vhd:1102, 5147-5151    | missing | missing                           |
| PAL-01  | Write NR 0x40 = 0x10 (palette index)                   | zxnext.vhd:4918-4920 | pass    | test/nextreg/nextreg_integration_test.cpp:1846               |
| PAL-02  | Write NR 0x41 (8-bit colour)                           | zxnext.vhd:4918-4920 | pass    | test/nextreg/nextreg_integration_test.cpp:1860               |
| PAL-03  | Write NR 0x44 twice (9-bit colour)                     | zxnext.vhd:4918-4920 | pass    | test/nextreg/nextreg_integration_test.cpp:1880               |
| PAL-04  | Read NR 0x41                                           | zxnext.vhd       | pass    | test/nextreg/nextreg_integration_test.cpp:1895               |
| PAL-05  | Read NR 0x44                                           | zxnext.vhd       | pass    | test/nextreg/nextreg_integration_test.cpp:1908               |
| PAL-06  | Auto-increment disabled (NR 0x43 bit 7)                | zxnext.vhd:4918-4920 | pass    | test/nextreg/nextreg_integration_test.cpp:1932               |
| PE-01   | Write NR 0x82 = 0x00                                   | zxnext.vhd:2392-2442,5052-5068 | pass    | test/nextreg/nextreg_test.cpp:681 |
| PE-02   | Read NR 0x82 after write                               | zxnext.vhd:2392-2442,5052-5068 | pass    | test/nextreg/nextreg_test.cpp:693 |
| PE-03   | Disable joystick port (bit 6)                          | zxnext.vhd:2392-2442 | pass    | test/nextreg/nextreg_integration_test.cpp:1990               |
| PE-04   | Reset with reset_type=1                                | —              | missing | missing                                                      |
| PE-05   | Reset with bus reset_type=0                            | zxnext.vhd:1234-1235,6147-6150 | pass    | test/nextreg/nextreg_integration_test.cpp:2009               |
| COP-01  | CPU write NR 0x15                                      | zxnext.vhd:4706-4777 | pass    | test/nextreg/nextreg_test.cpp:757 |
| COP-02  | Copper write NR 0x15 simultaneously                    | —              | missing | missing                                                        |
| COP-03  | CPU write while copper active                          | —              | missing | missing                                                        |
| COP-04  | Copper register limited to 0x7F                        | —              | missing | missing                                                      |
| FT-D8-01   | NR 0xD8 nr_d8_io_trap_fdc_en write/read-back                            | zxnext.vhd:3866               | missing | missing                           |
| FT-D8-02   | NR 0xD8 enable=1 must allow strobe_iotrap to assert MF                  | zxnext.vhd:3866               | missing | missing                           |
| FT-D9-01   | NR 0xD9 nr_d9_iotrap_write captures CPU write byte                      | zxnext.vhd:3870               | missing | missing                           |
| FT-DA-01   | NR 0xDA nr_da_iotrap_cause encoding 01/10/11                            | zxnext.vhd:3878-3880          | missing | missing                           |
| FT-DA-02   | NR 0xDA cause clears via NR 0x02 b4 write=0                             | zxnext.vhd:3878-3880          | missing | missing                           |
| BYPASS-Q-01 | NR 0x28-0x2B keymap.bin read-back unspecified                          | —                             | missing | missing                           |
| BYPASS-Q-02 | altROM 0x06/0x07 layout in enNextZX.rom unspecified                    | —                             | missing | missing                           |
| PE-06      | NR 0x82 read returns raw shadow byte; VHDL packs more                   | zxnext.vhd:5508-5522          | missing | missing                           |
| PE-07      | NR 0x86 has no read_handler; raw regs_[] leak                           | zxnext.vhd:5061-5067          | missing | missing                           |
| PE-08      | NR 0x89 bit 7 inversion on reset_type=0 missing                         | zxnext.vhd:6138, 6150         | missing | missing                           |
| PE-09      | NR 0x80 / 0x88 not initialised at reset                                 | zxnext.vhd:6147-6150          | missing | missing                           |
| WO-01      | NR 0x04 leaks last-written byte on read (write-only NR)                 | zxnext.vhd:5878-6289          | missing | missing                           |
| WO-02      | NR 0x29 leaks last-written byte on read (write-only NR)                 | zxnext.vhd:5878-6289          | missing | missing                           |
| WO-03      | NR 0x60 leaks last-written byte on read (write-only NR)                 | zxnext.vhd:5878-6289          | missing | missing                           |
| WO-04      | NR 0x35 leaks last-written byte on read (write-only NR)                 | zxnext.vhd:5878-6289          | missing | missing                           |
| G56-CR-05  | NR 0x05 composed-read divergence                                        | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-06  | NR 0x06 psg_mode source-of-truth                                        | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-09  | NR 0x09 sprite_tie composed-read                                        | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-0A  | NR 0x0A divmmc_automap_en mirror                                        | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-0B  | NR 0x0B joystick composed-read                                          | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-10  | NR 0x10 video-timing cvc composed                                       | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-15  | NR 0x15 layer composed-read                                             | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-22  | NR 0x22 bit 7 dynamic pulse_int_n                                       | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-23  | NR 0x23 line-int compare ladder                                         | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-34  | NR 0x34 sprite-attr index live counter                                  | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-40  | NR 0x40 palette idx autoinc state                                       | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-43  | NR 0x43 palette ctrl composed-read                                      | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-4C  | NR 0x4C bits 7:4 mask not propagated                                    | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-68  | NR 0x68 b4 from port_ff3b_ulap_en                                       | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-69  | NR 0x69 bits composed from port_ff                                      | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-6A  | NR 0x6A radastan/lores composed                                         | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-6B  | NR 0x6B b7 from nr_6b_tm_en                                             | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-6C  | NR 0x6C tilemap composed-read                                           | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-6E  | NR 0x6E bit 6 always 0                                                  | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-6F  | NR 0x6F bit 6 always 0                                                  | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-70  | NR 0x70 bits 7:6 always 0                                               | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-71  | NR 0x71 bits 7:1 always 0                                               | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-80  | NR 0x80 expansion-bus dynamic state                                     | zxnext.vhd:5897-6125          | missing | missing                           |
| G56-CR-81  | NR 0x81 b7 from i_BUS_ROMCS_n                                           | zxnext.vhd:5897-6125          | missing | missing                           |

### Extra coverage (not in plan)

| Test ID | Assertion description                         | VHDL file:line | Test file:line                    |
|---------|-----------------------------------------------|----------------|-----------------------------------|
| NREG-RST-10 | NR 0x12 L2 active bank (VHDL: 0x08)           | zxnext.vhd:4945  | test/nextreg/nextreg_integration_test.cpp:281 |
| NREG-RST-11 | NR 0x68 ULA control (VHDL: bit7=NOT ula_en=0) | zxnext.vhd:5003  | test/nextreg/nextreg_integration_test.cpp:291 |
| NREG-RST-12 | NR 0x6B tilemap = 0x00                        | zxnext.vhd:5004  | test/nextreg/nextreg_integration_test.cpp:301 |
| NREG-RST-13 | NR 0x82-0x85 internal port enables = 0xFF     | zxnext.vhd:5087-5090 | test/nextreg/nextreg_integration_test.cpp:343 |
| RST-14  | NR 0x86-0x89 bus port enables = 0xFF          | —              | missing                           |
| RST-15  | NR 0x4B sprite transparent (VHDL: 0xE3)       | —              | missing                           |
| RST-16a | NR 0x16 L2 scroll X = 0x00                    | —              | missing                           |
| RST-16b | NR 0x17 L2 scroll Y = 0x00                    | —              | missing                           |
| WH-01   | Write handler called with correct value       | —              | missing                           |
| WH-02   | Write handler via write_selected              | —              | missing                           |
| WH-03   | Read handler returns 0xDD despite cached 0x11 | —              | missing                           |
| WH-04   | No handler — direct storage round-trip        | —              | missing                           |
| EDGE-01 | All 256 registers store and retrieve          | —              | missing                           |
| EDGE-02 | Reset clears NR 0x7F to 0                     | —              | missing                           |
| EDGE-03 | Reset restores NR 0x00=0x0A, NR 0x01=0x32     | —              | missing                           |
| EDGE-04 | Write handler survives reset                  | —              | missing                           |
| EDGE-05 | Multiple selects, last wins                   | —              | missing                           |

### Companion integration suite — `test/nextreg/nextreg_integration_test.cpp`

Created 2026-04-15 onwards (Phase 2 Wave 1 commit `0dc128e` and beyond) to host integration-tier rows from the NextREG plan that require the full `Emulator` fixture (subsystem wiring for reset defaults, MMU/Layer2/Sprite/Tilemap clip-window cycling, palette pipeline, NR 0x82-bit-6 port-1F gate, NR 0x07/0x08 read composition, NR 0x03 machine-config state, DMA IM2-delay composition, soft-reset semantics, NR 0x8E RAM-rebuild gate, Layer 2 bank routing). Runtime: `Total:  301  Passed:  301  Failed:    0  Skipped:    0`. The 74 rows listed below are only the ones recorded here; 37 more that the suite asserts are recorded in the parent `## NextREG` table above, and the rest are reported `unrecorded` on every run. Each row cross-references the bare-suite plan row when a re-home applies.

| Test ID            | Plan row title                                                                                   | VHDL file:line                  | Status | Test file:line                                |
|--------------------|--------------------------------------------------------------------------------------------------|---------------------------------|--------|-----------------------------------------------|
| MID-01             | NR 0x00 machine ID reset = 0x0A (VHDL g_machine_id; NextZXOS-boot fix 2026-07-09)                | zxnext_top_issue2.vhd:35        | pass    | test/nextreg/nextreg_integration_test.cpp:163  |
| NREG-RST-01 | NR 0x14 global transparent reset = 0xE3                                                          | zxnext.vhd:4947                 | pass    | test/nextreg/nextreg_integration_test.cpp:172  |
| NREG-RST-02 | NR 0x15 sprite/layer control reset = 0x00                                                        | zxnext.vhd:4948                 | pass    | test/nextreg/nextreg_integration_test.cpp:183  |
| NREG-RST-03 | NR 0x4A fallback RGB reset = 0xE3                                                                | zxnext.vhd:5002                 | pass    | test/nextreg/nextreg_integration_test.cpp:193  |
| NREG-RST-04 | NR 0x42 ULANext format reset = 0x07                                                              | zxnext.vhd:4993                 | pass    | test/nextreg/nextreg_integration_test.cpp:203  |
| NREG-RST-05 | NR 0x50-0x57 MMU defaults                                                                        | zxnext.vhd:4610-4618            | pass    | test/nextreg/nextreg_integration_test.cpp:228  |
| NREG-RST-06 | NR 0x68 ULA control reset = 0x00 (ula_en=1 → bit7=0)                                             | zxnext.vhd:5029                 | pass    | test/nextreg/nextreg_integration_test.cpp:238  |
| NREG-RST-07 | NR 0x0B I/O mode reset = 0x01                                                                    | zxnext.vhd:4939-4941            | pass    | test/nextreg/nextreg_integration_test.cpp:249  |
| NREG-RST-08 | NR 0x82-0x85 internal port enables reset = 0xFF (NR 0x85 reads as 0x8F)                          | zxnext.vhd:5052-5068            | pass    | test/nextreg/nextreg_integration_test.cpp:271  |
| NREG-RST-10 | NR 0x12 Layer 2 active bank reset = 0x08                                                         | zxnext.vhd:4945                 | pass    | test/nextreg/nextreg_integration_test.cpp:281  |
| NREG-RST-11 | NR 0x4B sprite transparent index reset = 0xE3                                                    | zxnext.vhd:5003                 | pass    | test/nextreg/nextreg_integration_test.cpp:291  |
| NREG-RST-12 | NR 0x4C tilemap transparent index reset = 0x0F                                                   | zxnext.vhd:5004                 | pass    | test/nextreg/nextreg_integration_test.cpp:301  |
| RST-09             | NR 0x1B post-reset read returns tilemap clip_x1 = 0x00                                           | zxnext.vhd:4977-4981, 5971-5977 | pass    | test/nextreg/nextreg_integration_test.cpp:316  |
| RO-01              | NR 0x00 machine ID reset = 0x0A via port path (NextZXOS-boot fix 2026-07-09)                     | src/port/nextreg.cpp:27         | pass    | test/nextreg/nextreg_integration_test.cpp:1311 |
| RO-02              | NR 0x00 read-only enforcement (write 0x42; read still 0x0A)                                      | zxnext.vhd:5884-5885            | pass    | test/nextreg/nextreg_integration_test.cpp:1327 |
| RO-03              | NR 0x01 core version reset = 0x32 (core 3.02)                                                    | src/port/nextreg.cpp:28         | pass    | test/nextreg/nextreg_integration_test.cpp:1338 |
| RO-04              | NR 0x0E sub-version reset = 0x03                                                                 | zxnext_top_issue2.vhd:38        | pass    | test/nextreg/nextreg_integration_test.cpp:1350 |
| RO-05              | NR 0x0F board issue reset = 0x00                                                                 | g_board_issue (generic)         | pass    | test/nextreg/nextreg_integration_test.cpp:1362 |
| RO-06              | NR 0x1E/0x1F active video line readable via port path                                            | emulator.cpp:405-414            | pass    | test/nextreg/nextreg_integration_test.cpp:1388 |
| SEL-03             | NR 0x00 via select+write+read path returns 0x08 (read-only)                                      | zxnext.vhd:5884-5885            | pass    | test/nextreg/nextreg_integration_test.cpp:1416 |
| CLIP-01            | NR 0x18 4-write cycle → x1=0x11 x2=0x22 y1=0x33 y2=0x44                                          | zxnext.vhd:5242-5249            | pass    | test/nextreg/nextreg_integration_test.cpp:1449 |
| CLIP-02            | NR 0x18 fifth write wraps back to x1 (mod-4 idx)                                                 | zxnext.vhd:5242-5249            | pass    | test/nextreg/nextreg_integration_test.cpp:1469 |
| CLIP-03            | NR 0x1C bit 0 resets L2 clip idx (next 0x18 write → x1)                                          | zxnext.vhd:5278-5281            | pass    | test/nextreg/nextreg_integration_test.cpp:1488 |
| CLIP-04            | NR 0x1C bit 1 resets sprite clip idx (next 0x19 write → x1)                                      | zxnext.vhd:5242-5290            | pass    | test/nextreg/nextreg_integration_test.cpp:1509 |
| CLIP-05            | NR 0x1C bit 2 resets ULA clip idx (next 0x1A write → x1)                                         | zxnext.vhd:5242-5290            | pass    | test/nextreg/nextreg_integration_test.cpp:1531 |
| CLIP-06            | NR 0x1C bit 3 resets tilemap clip idx (next 0x1B write → x1)                                     | zxnext.vhd:5242-5290            | pass    | test/nextreg/nextreg_integration_test.cpp:1549 |
| CLIP-07a           | NR 0x1C read post-all-reset packs idx=0000 → 0x00                                                | zxnext.vhd:5979-5980            | pass    | test/nextreg/nextreg_integration_test.cpp:1568 |
| CLIP-07b           | NR 0x1C after one NR 0x1B write: bits 7:6 = 01 → 0x40                                            | zxnext.vhd:5276, 5979-5980      | pass    | test/nextreg/nextreg_integration_test.cpp:1578 |
| CLIP-08            | NR 0x18 read mux cycles through x1, x2, y1, y2 as idx advances                                   | zxnext.vhd:5947-5953            | pass    | test/nextreg/nextreg_integration_test.cpp:1613 |
| CLIP-09a           | NR 0x1B read does NOT advance idx (two consecutive reads equal)                                  | zxnext.vhd:5971-5977            | pass    | test/nextreg/nextreg_integration_test.cpp:1637 |
| CLIP-09b           | NR 0x1B reads at idx=3 both return y2=0xFF; read does not wrap                                   | zxnext.vhd:5971-5977            | pass    | test/nextreg/nextreg_integration_test.cpp:1653 |
| CLIP-10            | NR 0x1B write lands x1 AND advances tm idx (NR 0x1C bits 7:6 = 01)                               | zxnext.vhd:5276, 5980           | pass    | test/nextreg/nextreg_integration_test.cpp:1668 |
| PAL-01             | NR 0x41 auto-increments palette index: pal[0]=0xFC pal[1]=0x03                                   | zxnext.vhd:4918-4920            | pass    | test/nextreg/nextreg_integration_test.cpp:1846 |
| PAL-02             | NR 0x41 8-bit palette value round-trips at selected index                                         | zxnext.vhd:4918-4920            | pass    | test/nextreg/nextreg_integration_test.cpp:1860 |
| PAL-03             | NR 0x44 9-bit write: upper 8 bits land at selected idx (sub_idx latch)                            | zxnext.vhd:4918-4920            | pass    | test/nextreg/nextreg_integration_test.cpp:1880 |
| PAL-04             | NR 0x41 read returns palette byte at selected index                                               | zxnext.vhd:5867-6292            | pass    | test/nextreg/nextreg_integration_test.cpp:1895 |
| PAL-05             | NR 0x44 read returns priority+LSB for selected index                                              | zxnext.vhd:5867-6292            | pass    | test/nextreg/nextreg_integration_test.cpp:1908 |
| PAL-06             | NR 0x43 bit 7 disables auto-inc: 2× NR 0x41 keeps pointer on same idx                            | zxnext.vhd:4918-4920            | pass    | test/nextreg/nextreg_integration_test.cpp:1932 |
| PE-03              | NR 0x82 bit 6 gates port 0x1F (Kempston 1): bit6=1→handler, bit6=0→0xFF                          | zxnext.vhd:2392-2442            | pass    | test/nextreg/nextreg_integration_test.cpp:1990 |
| PE-05              | NR 0x89 bus port enable reset = 0x8F (bit 7 reset_type=1, bits 3:0 = 1111)                       | zxnext.vhd:1234-1235, 6147-6150 | pass    | test/nextreg/nextreg_integration_test.cpp:2009 |
| RW-01              | NR 0x07 read = (actual<<4) \| requested, pads 0 in bits[7:6] and bits[3:2]                       | zxnext.vhd:5902-5903            | pass    | test/nextreg/nextreg_integration_test.cpp:2172 |
| RW-02              | NR 0x08 bit 7 read = NOT port_7ffd_locked, bit 6 = nr_08_contention_disable                      | zxnext.vhd:5906                 | pass    | test/nextreg/nextreg_integration_test.cpp:2224 |
| CFG-01             | NR 0x03 bits[6:4] compose from nr_03_machine_timing (reset default "011")                        | zxnext.vhd:1099, 5893-5894      | pass    | test/nextreg/nextreg_integration_test.cpp:2504 |
| CFG-02             | NR 0x03 bit 3 XOR-toggles nr_03_user_dt_lock; read composes bit 3 from state                     | zxnext.vhd:5121-5151, 5894      | pass    | test/nextreg/nextreg_integration_test.cpp:2540 |
| CFG-05             | NR 0x03 bits 2:0 = 001 clears config_mode at write time                                          | zxnext.vhd:5147-5151            | pass    | test/nextreg/nextreg_integration_test.cpp:2571 |
| CFG-09-INT         | NR 0x03 machine-type latch read-back lost on reset (G63)                                         | zxnext.vhd:1103,5137-5145,5894    | pass    | test/nextreg/nextreg_integration_test.cpp:2619 |
| 20.3a              | NR 0xCC readback masks to bits 7 and 1:0                                                         | zxnext.vhd:6257                 | pass    | test/nextreg/nextreg_integration_test.cpp:367  |
| 20.3b              | NR 0xCC ignores bits 6:2 on readback                                                             | zxnext.vhd:5629-5630            | pass    | test/nextreg/nextreg_integration_test.cpp:375  |
| 20.3c              | NR 0xCD readback preserves all 8 bits (CTC 7..0)                                                 | zxnext.vhd:5633, 6260           | pass    | test/nextreg/nextreg_integration_test.cpp:383  |
| 20.3d              | NR 0xCE readback masks to bits 6:4 + 2:0 (bits 7,3 zero)                                         | zxnext.vhd:5636-5637, 6263      | pass    | test/nextreg/nextreg_integration_test.cpp:391  |
| 20.3e              | im2_dma_int_en = 0x3FFF when all NR enable bits are set                                          | zxnext.vhd:1957-1958            | pass    | test/nextreg/nextreg_integration_test.cpp:406  |
| 20.3f              | NR CC[0] alone → im2_dma_int_en[11]                                                              | zxnext.vhd:1957                 | pass    | test/nextreg/nextreg_integration_test.cpp:417  |
| 20.3g              | NR CE[0] or CE[1] → im2_dma_int_en[1] (UART0 Rx OR)                                              | zxnext.vhd:1958                 | pass    | test/nextreg/nextreg_integration_test.cpp:432  |
| 20.4a              | All inputs deasserted → im2_dma_delay = 0                                                        | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:450  |
| 20.4b              | im2_dma_int = 1 → im2_dma_delay = 1                                                              | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:456  |
| 20.4c              | nmi=1 & nr_cc_7=0 → im2_dma_delay = 0                                                            | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:466  |
| 20.4d              | nmi=1 & nr_cc_7=1 → im2_dma_delay = 1                                                            | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:474  |
| 20.4e              | latched_prev=1 & dma_delay=1 → im2_dma_delay stays 1                                              | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:484  |
| 20.4f              | latched_prev=1 & dma_delay=0 → im2_dma_delay drops to 0                                          | zxnext.vhd:2007                 | pass    | test/nextreg/nextreg_integration_test.cpp:493  |
| TM-114             | NR 0x1B 4-write cycle programs x1/x2/y1/y2 in order                                              | zxnext.vhd:5242-5290            | pass    | test/nextreg/nextreg_integration_test.cpp:523  |
| TM-115             | NR 0x1C bit 3 resets tilemap clip idx so next 0x1B write → x1                                    | emulator.cpp:304-308            | pass    | test/nextreg/nextreg_integration_test.cpp:546  |
| SR-01              | NR 0x02=0x01 (RESET_SOFT) preserves SRAM contents                                                 | VHDL: SRAM not in reset domain  | pass    | test/nextreg/nextreg_integration_test.cpp:590  |
| SR-02              | NR 0x02=0x02 (RESET_HARD) zeroes SRAM                                                             | full power-on reinit            | pass    | test/nextreg/nextreg_integration_test.cpp:612  |
| SR-03              | boot_rom_en_ cleared state survives RESET_SOFT with boot ROM loaded                               | zxnext.vhd:1101, 5109-5111, 5122 | pass    | test/nextreg/nextreg_integration_test.cpp:638  |
| SR-04              | NR 0x02=0x80 (RESET_ESPBUS alone) is a no-op — SRAM untouched                                    | —                               | pass    | test/nextreg/nextreg_integration_test.cpp:653  |
| SR-05              | NR 0x02=0x03 (RESET_HARD\|RESET_SOFT): hard wins, SRAM zeroed                                    | —                               | pass    | test/nextreg/nextreg_integration_test.cpp:668  |
| SR-06              | ROM-in-SRAM window (ram_ pages 0..7) survives RESET_SOFT                                          | —                               | pass    | test/nextreg/nextreg_integration_test.cpp:685  |
| SR-07              | ROM-in-SRAM window re-seeded from rom_ on RESET_HARD                                              | —                               | pass    | test/nextreg/nextreg_integration_test.cpp:703  |
| RSTD-E3-01         | RESET_SOFT clears port 0xE3 conmem (bit 7) and the whole register                                 | zxnext.vhd:4176-4177, zxnext_top_issue2.vhd:840 | pass    | test/nextreg/nextreg_integration_test.cpp:771  |
| RSTD-E3-02         | RESET_HARD clears port 0xE3 conmem via the host cold boot; NR 0x02 b1 only defers                 | zxnext.vhd:4176-4177,1730, zxnext_top_issue2.vhd:840 | pass    | test/nextreg/nextreg_integration_test.cpp:804  |
| RSTD-8C-01         | reset RELOADS nr_8c_altrom_lock_rom1 from bit 1 (does not clear)                                  | zxnext.vhd:2254-2255            | pass    | test/nextreg/nextreg_integration_test.cpp:830  |
| RSTD-8C-02         | reset RELOADS nr_8c_altrom_lock_rom0 from bit 0 (does not clear)                                  | zxnext.vhd:2254-2255            | pass    | test/nextreg/nextreg_integration_test.cpp:855  |
| RSTD-04-01         | RESET_SOFT PRESERVES nr_04_romram_bank (no reset clause in the VHDL)                              | zxnext.vhd:4930-5111, zxnext.vhd:1104, zxnext.vhd:3045, zxnext.vhd:5717, zxnext.vhd:5732 | pass | test/nextreg/nextreg_integration_test.cpp:900  |
| RSTD-04-02         | RESET_HARD clears nr_04_romram_bank via the host cold boot; NR 0x02 b1 only defers                 | zxnext.vhd:1104, zxnext_top_issue2.vhd:1195 | pass | test/nextreg/nextreg_integration_test.cpp:928  |
| N8E-RAM-PRESERVE-0 | NR 0x56 override survives NR 0x8E write with bit 3 = 0                                            | zxnext.vhd:3814, 4677           | pass    | test/nextreg/nextreg_integration_test.cpp:2709 |
| N8E-RAM-REBUILD-1  | NR 0x8E bit 3 = 1 rebuilds MMU6/7 from port_7ffd_bank                                            | zxnext.vhd:3814, 4677           | pass    | test/nextreg/nextreg_integration_test.cpp:2734 |
| L2M-05             | NR 0x12 write sets Layer 2 active bank (7-bit)                                                    | zxnext.vhd:4945                 | pass    | test/nextreg/nextreg_integration_test.cpp:2816 |
| L2M-05b            | NR 0x12 top bit masked on write (Layer2 state) AND on readback                                    | zxnext.vhd:4945, 5930           | pass    | test/nextreg/nextreg_integration_test.cpp:2836 |
| L2M-06             | NR 0x13 write sets Layer 2 shadow bank (7-bit)                                                    | zxnext.vhd:4946                 | pass    | test/nextreg/nextreg_integration_test.cpp:2852 |
| L2M-06b            | NR 0x13 top bit masked on write (Layer2 state) AND on readback                                    | zxnext.vhd:4946, 5931           | pass    | test/nextreg/nextreg_integration_test.cpp:2869 |


## IO Port Dispatch — `test/port/port_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID       | Plan row title                                               | VHDL file:line       | Status  | Test file:line               |
|---------------|--------------------------------------------------------------|----------------------|---------|------------------------------|
| LIBZ80-01     | `OUT (C),r` to 0x7FFD vs 0xBFFD                              | zxnext.vhd:2593      | pass    | test/port/port_test.cpp:177  |
| LIBZ80-02     | `IN A,(nn)` upper byte honoured                              | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:203  |
| LIBZ80-03     | `OUT (nn),A` upper byte honoured                             | zxnext.vhd:2626      | pass    | test/port/port_test.cpp:216  |
| LIBZ80-04     | INIR block transfer uses full BC                             | zxnext.vhd:2635      | pass    | test/port/port_test.cpp:230  |
| LIBZ80-05     | MSB-only discrimination                                      | zxnext.vhd:2648      | pass    | test/port/port_test.cpp:246  |
| REG-01        | ULA 0xFE matches any even address                            | zxnext.vhd:2582      | pass    | test/port/port_test.cpp:271  |
| REG-01b       | 0xFE decodes ANY even port (0xFC/0xF8/0x4242), not LSB 0xFE  | zxnext.vhd:2582      | pass    | test/port/port_test.cpp:293  |
| REG-02        | 0xFE does not match on odd address                           | zxnext.vhd:2582–2583 | pass    | test/port/port_test.cpp:305  |
| REG-02b       | Timex 0xFF decodes any port with LSB 0xFF (e.g. 0x12FF)      | zxnext.vhd:2540-2571   | pass    | test/port/port_test.cpp:324  |
| REG-03        | NextReg select 0x243B                                        | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:336  |
| REG-03a       | IN 0x243B before any select returns the reset value 0x24     | zxnext.vhd:4594-4596 | pass    | test/port/port_test.cpp:369  |
| REG-03b       | IN 0x243B returns the selected NextREG number (GH #52)       | zxnext.vhd:4603      | pass    | test/port/port_test.cpp:378  |
| REG-03c       | NextZXOS ISR save/restore of the 0x243B selection (GH #52)   | zxnext.vhd:4603      | pass    | test/port/port_test.cpp:401  |
| REG-04        | NextReg data 0x253B                                          | zxnext.vhd:2626      | pass    | test/port/port_test.cpp:342  |
| REG-05        | 0x243C/0x253C not decoded                                    | zxnext.vhd:2625      | pass    | test/port/port_test.cpp:427  |
| REG-06+07     | AY select 0xFFFD real                                        | zxnext.vhd:2647      | pass    | test/port/port_test.cpp:441  |
| REG-06+07     | AY data 0xBFFD real                                          | zxnext.vhd:2648      | pass    | test/port/port_test.cpp:441  |
| REG-08        | 0x7FFD MMU bank select                                       | zxnext.vhd:2593      | pass    | test/port/port_test.cpp:454  |
| REG-09        | 0x1FFD +3 extended                                           | zxnext.vhd:2599      | pass    | test/port/port_test.cpp:473  |
| REG-10        | 0xDFFD Pentagon ext                                          | zxnext.vhd:2596      | pass    | test/port/port_test.cpp:499  |
| REG-11        | DivMMC 0xE3 real                                             | zxnext.vhd:2608      | pass    | test/port/port_test.cpp:509  |
| REG-12        | SPI CS 0xE7, data 0xEB                                       | zxnext.vhd:2620–2621 | pass    | test/port/port_test.cpp:527  |
| REG-13        | Sprite 0x303B write-then-read                                | zxnext.vhd:2681      | pass    | test/port/port_test.cpp:575  |
| REG-14        | Layer 2 0x123B                                               | zxnext.vhd:2635      | pass    | test/port/port_test.cpp:584  |
| REG-15        | I²C 0x103B / 0x113B                                          | zxnext.vhd:2630–2631 | pass    | test/port/port_test.cpp:598  |
| REG-16        | UART 0x143B / 0x153B                                         | zxnext.vhd:2639      | pass    | test/port/port_test.cpp:610  |
| REG-17        | UART 0x133B rejected                                         | zxnext.vhd:2639      | pass    | test/port/port_test.cpp:624  |
| REG-18        | Kempston 1 0x001F                                            | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:634  |
| REG-19        | Kempston 2 0x0037                                            | zxnext.vhd:2675      | pass    | test/port/port_test.cpp:658  |
| REG-20        | Mouse 0xFADF/0xFBDF/0xFFDF                                   | zxnext.vhd:2668–2670 | pass    | test/port/port_test.cpp:670  |
| REG-21        | ULA+ 0xBF3B / 0xFF3B                                         | zxnext.vhd:2685–2686 | pass    | test/port/port_test.cpp:681  |
| REG-22        | DMA 0x6B vs 0x0B                                             | zxnext.vhd:2643      | pass    | test/port/port_test.cpp:694  |
| REG-23        | CTC 0x183B range                                             | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:745  |
| REG-24        | Unmapped port read                                           | zxnext.vhd:2589      | pass    | test/port/port_test.cpp:757  |
| REG-25        | Unmapped port write                                          | zxnext.vhd:2697      | pass    | test/port/port_test.cpp:775  |
| REG-26        | 0xDF routes to Specdrum/port_1f sink (positive combo)        | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:795  |
| REG-27        | 0xDF re-routed away from port_1f when mouse enabled (negati… | zxnext.vhd:2670      | pass    | test/port/port_test.cpp:806  |
| NR82-00       | 0x82 b0                                                      | zxnext.vhd:2397      | pass    | test/port/port_test.cpp:1211 |
| NR82-01       | 0x82 b1                                                      | zxnext.vhd:2399      | pass    | test/port/port_test.cpp:1224 |
| NR82-02       | 0x82 b2                                                      | zxnext.vhd:2400      | pass    | test/port/port_test.cpp:1244 |
| NR82-03       | 0x82 b3                                                      | zxnext.vhd:2401      | pass    | test/port/port_test.cpp:1261 |
| NR82-04       | 0x82 b4                                                      | zxnext.vhd:2403      | pass    | test/port/port_test.cpp:1270 |
| NR82-05       | 0x82 b5                                                      | zxnext.vhd:2405      | pass    | test/port/port_test.cpp:1282 |
| NR82-06       | 0x82 b6                                                      | zxnext.vhd:2407      | pass    | test/port/port_test.cpp:1290 |
| NR82-07       | 0x82 b7                                                      | zxnext.vhd:2408      | pass    | test/port/port_test.cpp:1298 |
| NR83-00       | 0x83 b0                                                      | zxnext.vhd:2412      | pass    | test/port/port_test.cpp:1308 |
| NR83-01       | 0x83 b1                                                      | zxnext.vhd:2415      | pass    | test/port/port_test.cpp:1309 |
| NR83-02       | 0x83 b2                                                      | zxnext.vhd:2418      | pass    | test/port/port_test.cpp:1310 |
| NR83-03       | 0x83 b3                                                      | zxnext.vhd:2419      | pass    | test/port/port_test.cpp:1311 |
| NR83-04       | 0x83 b4                                                      | zxnext.vhd:2420      | pass    | test/port/port_test.cpp:1312 |
| NR83-05       | 0x83 b5                                                      | zxnext.vhd:2422      | pass    | test/port/port_test.cpp:1313 |
| NR83-06       | 0x83 b6                                                      | zxnext.vhd:2423      | pass    | test/port/port_test.cpp:1314 |
| NR83-07       | 0x83 b7                                                      | zxnext.vhd:2424      | pass    | test/port/port_test.cpp:1315 |
| NR84-00       | 0x84 b0                                                      | zxnext.vhd:2428      | pass    | test/port/port_test.cpp:1334 |
| NR84-01       | 0x84 b1                                                      | zxnext.vhd:2429      | pass    | test/port/port_test.cpp:1335 |
| NR84-02       | 0x84 b2                                                      | zxnext.vhd:2430      | pass    | test/port/port_test.cpp:1336 |
| NR84-03       | 0x84 b3                                                      | zxnext.vhd:2431      | pass    | test/port/port_test.cpp:1337 |
| NR84-04       | 0x84 b4                                                      | zxnext.vhd:2432      | pass    | test/port/port_test.cpp:1338 |
| NR84-05       | 0x84 b5                                                      | zxnext.vhd:2433      | pass    | test/port/port_test.cpp:1339 |
| NR84-06       | 0x84 b6                                                      | zxnext.vhd:2434      | pass    | test/port/port_test.cpp:1340 |
| NR84-07       | 0x84 b7                                                      | zxnext.vhd:2435      | pass    | test/port/port_test.cpp:1341 |
| NR84-07-combo | 0x84 b7 AND 0x83 b5 AND 0x82 b6 (combinatorial)              | zxnext.vhd:2674      | pass    | test/port/port_test.cpp:1375 |
| NR85-00       | 0x85 b0                                                      | zxnext.vhd:2439      | pass    | test/port/port_test.cpp:1384 |
| NR85-01       | 0x85 b1                                                      | zxnext.vhd:2440      | pass    | test/port/port_test.cpp:1385 |
| NR85-02       | 0x85 b2                                                      | zxnext.vhd:2441      | pass    | test/port/port_test.cpp:1386 |
| NR85-03       | 0x85 b3                                                      | zxnext.vhd:2442      | pass    | test/port/port_test.cpp:1387 |
| NR85-03b      | 0x85 b3                                                      | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:1423 |
| NR85-03c      | 0x85 b3                                                      | zxnext.vhd:2690      | pass    | test/port/port_test.cpp:1441 |
| NR-DEF-01     | Power-on defaults all-enabled                                | zxnext.vhd:1226–1230 | pass    | test/port/port_test.cpp:1454 |
| NR-RST-01     | Soft reset reloads when reset_type=1                         | zxnext.vhd:5052–5057 | pass    | test/port/port_test.cpp:1482 |
| NR-RST-02     | Soft reset does NOT reload when reset_type=0                 | zxnext.vhd:5052–5057 | pass    | test/port/port_test.cpp:1496 |
| NR-85-PK      | NR 0x85 packing: bits 4–6 read back zero                     | zxnext.vhd:5508–5509 | pass    | test/port/port_test.cpp:1470 |
| BUS-86-01     | NR 0x86 inert when expbus_eff_en=0                           | zxnext.vhd:2392      | pass    | test/port/port_test.cpp:1523 |
| BUS-86..89-W  | NR 0x86 gates when expbus_eff_en=1                           | zxnext.vhd:2393      | pass    | test/port/port_test.cpp:1543 |
| BUS-86..89-W  | NR 0x86 AND with NR 0x82                                     | zxnext.vhd:2393      | pass    | test/port/port_test.cpp:1543 |
| BUS-86..89-W  | DivMMC enable-diff detection                                 | zxnext.vhd:2413      | pass    | test/port/port_test.cpp:1543 |
| BUS-86..89-W  | NR 0x88 AND with NR 0x84 (AY)                                | zxnext.vhd:2393      | pass    | test/port/port_test.cpp:1543 |
| BUS-86..89-W  | NR 0x89 AND with NR 0x85 (ULA+)                              | zxnext.vhd:2393      | pass    | test/port/port_test.cpp:1543 |
| PR-01         | Registering an overlapping handler must fail (target contra… | zxnext.vhd:2696–2699 | pass    | test/port/port_test.cpp:1623 |
| PR-02         | One-hot invariant over all real peripherals after `Emulator… | zxnext.vhd:2696–2699 | pass    | test/port/port_test.cpp:1650 |
| PR-01-CUR     | **Document current-code asymmetry (guard test until PR-01 c… | zxnext.vhd:2696-2699   | pass    | test/port/port_test.cpp:1598 |
| PR-03         | `clear_handlers()` then re-register on reset                 | —                    | pass    | test/port/port_test.cpp:1667 |
| PR-04         | Default-read used when no handler matches                    | —                    | pass    | test/port/port_test.cpp:1679 |
| PR-05         | Default-read NOT used when any handler matches (even with 0… | —                    | pass    | test/port/port_test.cpp:1695 |
| IORQ-01       | Interrupt ack not routed to `in` (COVERED — resolves via the dedicated `on_int_ack()` callback, structurally separate from `PortDispatch::in`; a prior citation of "FUSE Z80 opcode suite" coverage was wrong, corrected GH #196 phase 1.1 — real coverage is `cpu_z80n_im2_regressions_test.cpp` + `ctc_interrupts_test.cpp`) | zxnext.vhd:2705; z80_cpu.cpp:716 | missing | missing                      |
| IORQ-02       | Normal IN is routed                                          | zxnext.vhd:2705      | pass    | test/port/port_test.cpp:1786 |
| IORQ-02b      | Port 0xFE bit 6 follows the OUT-0xFE bit-4 EAR latch         | zxnext.vhd:3459        | pass    | test/port/port_test.cpp:1803 |
| IORQ-02c      | Pressed key reads the exact byte 0xBD ('O') / 0xBE (SPACE)   | zxnext.vhd:3459        | pass    | test/port/port_test.cpp:1826 |
| RMW-01        | 0xFE border + beeper latch                                   | zxnext.vhd:2582      | pass    | test/port/port_test.cpp:1864 |
| CTN-01        | Contended-port timing on 0x4000-range port (real, untested gap — contention is nulled on the FUSE Z80 harness path; a prior claim of FUSE-suite coverage was wrong, corrected GH #196 phase 1.1; identical to Contention's CT-FUSE-03) | zxula.vhd:595, zxnext.vhd:4496 | missing | missing                      |
| CTN-02        | Uncontended `IN A,(nn)` outside 0x4000 range (same corrected reasoning as CTN-01; identical to Contention's CT-FUSE-04) | zxula.vhd:595, zxnext.vhd:4496 | missing | missing                      |
| AMAP-01       | DivMMC enable diff freezes expansion bus                     | zxnext.vhd:2180      | missing | missing                      |
| AMAP-02       | 0xE3 writes honoured even when automap held                  | zxnext.vhd:2608      | pass    | test/port/port_test.cpp:1903 |
| AMAP-03       | NR 0x83 b0 = 0 disables 0xE3 regardless of automap           | zxnext.vhd:2412      | pass    | test/port/port_test.cpp:1917 |
| BUS-01        | Single-owner invariant over all registered                   | —                    | pass    | test/port/port_test.cpp:1948 |
| BUS-02        | Disabled port yields default-read byte                       | zxnext.vhd:2428      | pass    | test/port/port_test.cpp:1963 |
| BUS-03        | SCLD read gated by `nr_08_port_ff_rd_en`, not just `port_ff… | zxnext.vhd:2813      | pass    | test/port/port_test.cpp:2097 |

### Extra coverage (not in plan)

| Test ID      | Assertion description | VHDL file:line | Test file:line              |
|--------------|-----------------------|----------------|-----------------------------|
| REG-06+07    |                       | —              | test/port/port_test.cpp:441 |
| BUS-86..89-W |                       | —              | test/port/port_test.cpp:1543 |

## Input — `test/input/input_test.cpp`

Last-touch commit: `fcbd9aed6138dc8836623e5f558b5c744968b725` (`fcbd9aed61`)

| Test ID   | Plan row title                                               | VHDL file:line                        | Status | Test file:line                |
|-----------|--------------------------------------------------------------|---------------------------------------|--------|-------------------------------|
| MD-01     | Mode 101; `i_JOY_LEFT` = U+D+L+R+A+B (bits 6,4,3,2,1,0)      | zxnext.vhd:3441-3442                    | pass   | test/input/input_test.cpp:1401 |
| KBD-01    | none                                                         | membrane.vhd:251                        | pass   | test/input/input_test.cpp:149 |
| KBD-02    | none                                                         | membrane.vhd:236,242                    | pass   | test/input/input_test.cpp:156 |
| KBD-03    | none                                                         | membrane.vhd:242                        | pass   | test/input/input_test.cpp:163 |
| KBD-04    | none                                                         | membrane.vhd:242                        | pass   | test/input/input_test.cpp:170 |
| KBD-05    | none                                                         | membrane.vhd:242                        | pass   | test/input/input_test.cpp:177 |
| KBD-06    | none                                                         | membrane.vhd:242                        | pass   | test/input/input_test.cpp:184 |
| KBD-07    | none                                                         | membrane.vhd:243                        | pass   | test/input/input_test.cpp:197 |
| KBD-08    | none                                                         | membrane.vhd:244                        | pass   | test/input/input_test.cpp:209 |
| KBD-09    | none                                                         | membrane.vhd:245                        | pass   | test/input/input_test.cpp:220 |
| KBD-10    | none                                                         | membrane.vhd:246                        | pass   | test/input/input_test.cpp:231 |
| KBD-11    | none                                                         | membrane.vhd:247                        | pass   | test/input/input_test.cpp:242 |
| KBD-12    | none                                                         | membrane.vhd:248                        | pass   | test/input/input_test.cpp:253 |
| KBD-13    | none                                                         | membrane.vhd:249                        | pass   | test/input/input_test.cpp:260 |
| KBD-14    | none                                                         | membrane.vhd:249                        | pass   | test/input/input_test.cpp:267 |
| KBD-15    | none                                                         | membrane.vhd:249                        | pass   | test/input/input_test.cpp:274 |
| KBD-16    | none                                                         | membrane.vhd:249                        | pass   | test/input/input_test.cpp:281 |
| KBD-17    | none                                                         | membrane.vhd:249                        | pass   | test/input/input_test.cpp:288 |
| KBD-18    | none                                                         | membrane.vhd:251                        | pass   | test/input/input_test.cpp:297 |
| KBD-19    | none                                                         | membrane.vhd:251                        | pass   | test/input/input_test.cpp:308 |
| KBD-20    | none                                                         | membrane.vhd:242-251                    | pass   | test/input/input_test.cpp:317 |
| KBD-21    | none                                                         | membrane.vhd:251                        | pass   | test/input/input_test.cpp:327 |
| KBD-22    | none                                                         | zxnext.vhd:3459                         | pass    | test/input/input_integration_test.cpp:157 |
| KBD-23    | none                                                         | zxnext.vhd:3459                         | pass    | test/input/input_integration_test.cpp:181 |
| KBDHYS-01 | Pulse CS for one scan, then release; read the next scan      | membrane.vhd:178,188-191,232            | pass   | test/input/input_test.cpp:371 |
| KBDHYS-02 | Hold CS continuously across 3 scans                          | membrane.vhd:190                        | pass   | test/input/input_test.cpp:392 |
| KBDHYS-03 | `i_cancel_extended_entries = 1` mid-scan                     | membrane.vhd:253                        | pass   | test/input/input_test.cpp:425 |
| EXT-01    | Press UP, read NR 0xB0                                       | —                                     | pass   | test/input/input_test.cpp:720 |
| EXT-02    | Press DOWN, read NR 0xB0                                     | —                                     | pass   | test/input/input_test.cpp:725 |
| EXT-03    | Press LEFT, read NR 0xB0                                     | —                                     | pass   | test/input/input_test.cpp:730 |
| EXT-04    | Press RIGHT, read NR 0xB0                                    | —                                     | pass   | test/input/input_test.cpp:735 |
| EXT-05    | Press ';'                                                    | —                                     | pass   | test/input/input_test.cpp:740 |
| EXT-06    | Press '"'                                                    | —                                     | pass   | test/input/input_test.cpp:745 |
| EXT-07    | Press ','                                                    | —                                     | pass   | test/input/input_test.cpp:750 |
| EXT-08    | Press '.'                                                    | —                                     | pass   | test/input/input_test.cpp:755 |
| EXT-09    | Press DELETE                                                 | —                                     | pass   | test/input/input_test.cpp:766 |
| EXT-10    | Press EDIT                                                   | —                                     | pass   | test/input/input_test.cpp:771 |
| EXT-11    | Press BREAK                                                  | —                                     | pass   | test/input/input_test.cpp:776 |
| EXT-12    | Press INV VIDEO                                              | —                                     | pass   | test/input/input_test.cpp:781 |
| EXT-13    | Press TRUE VIDEO                                             | —                                     | pass   | test/input/input_test.cpp:786 |
| EXT-14    | Press GRAPH                                                  | —                                     | pass   | test/input/input_test.cpp:791 |
| EXT-15    | Press CAPS LOCK                                              | —                                     | pass   | test/input/input_test.cpp:796 |
| EXT-16    | Press EXTEND                                                 | —                                     | pass   | test/input/input_test.cpp:801 |
| EXT-17    | Press EDIT; read 0xF7FE (row 3)                              | membrane.vhd:236-240                    | pass   | test/input/input_test.cpp:820 |
| EXT-18    | Press ','; read 0xDFFE (row 5)                               | membrane.vhd:217                        | pass   | test/input/input_test.cpp:836 |
| EXT-19    | Press LEFT; read 0x7FFE (row 7)                              | membrane.vhd:225                        | pass   | test/input/input_test.cpp:849 |
| EXT-20    | UP + DOWN + LEFT + RIGHT                                     | zxnext.vhd:6208                         | pass   | test/input/input_test.cpp:863 |
| JMODE-01  | NR 0x05 = 0x00 = 0b0000_0000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:899 |
| JMODE-02  | NR 0x05 = 0x68 = 0b0110_1000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:914 |
| JMODE-02r | NR 0x05 = 0xC9 = 0b1100_1001                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:929 |
| JMODE-03  | NR 0x05 = 0x40 = 0b0100_0000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:944 |
| JMODE-04  | NR 0x05 = 0x08 = 0b0000_1000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:959 |
| JMODE-05  | NR 0x05 = 0x88 = 0b1000_1000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:974 |
| JMODE-06  | NR 0x05 = 0x22 = 0b0010_0010                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:989 |
| JMODE-07  | NR 0x05 = 0x30 = 0b0011_0000                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:1004 |
| JMODE-08  | Power-on, read joystick mode                                 | zxnext.vhd:5157-5158                    | pass   | test/input/input_test.cpp:1028 |
| KEMP-01   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1134 |
| KEMP-02   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1142 |
| KEMP-03   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1150 |
| KEMP-04   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1158 |
| KEMP-05   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1166 |
| KEMP-06   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1174 |
| KEMP-07   | joy0=001                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1183 |
| KEMP-08   | joy0=001                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1191 |
| KEMP-09   | joy0=001                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1200 |
| KEMP-10   | joy0=100                                                     | zxnext.vhd:3482                         | pass   | test/input/input_test.cpp:1209 |
| KEMP-11   | joy0=100                                                     | —                                     | pass   | test/input/input_test.cpp:1217 |
| KEMP-12   | joy0=000                                                     | zxnext.vhd:3475                         | pass   | test/input/input_test.cpp:1230 |
| KEMP-13   | joy0=001, joy1=001, L.U + R.R                                | zxnext.vhd:3499                         | pass   | test/input/input_test.cpp:1241 |
| KEMP-14   | joy0=001, joy1=100, L.U, R.D                                 | zxnext.vhd:3475-3488                    | pass   | test/input/input_test.cpp:1252 |
| KEMP-15   | joy0=101, L.A pressed                                        | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1263 |
| MD-02     | joy0=101                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1409 |
| MD-03     | joy0=101                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1417 |
| MD-04     | joy0=101                                                     | zxnext.vhd:3479                         | pass   | test/input/input_test.cpp:1425 |
| MD-05     | joy0=101                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1433 |
| MD-06     | joy0=001                                                     | zxnext.vhd:3478                         | pass   | test/input/input_test.cpp:1442 |
| MD-07     | joy0=110                                                     | zxnext.vhd:3482                         | pass   | test/input/input_test.cpp:1452 |
| MD-08     | joy1=110                                                     | zxnext.vhd:3494                         | pass   | test/input/input_test.cpp:1462 |
| MD-09     | joy0=101, joy1=101 (both MD1 — illegal?)                     | zxnext.vhd:3499                         | pass   | test/input/input_test.cpp:1476 |
| MD6-01    | joy0=101                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1506 |
| MD6-02    | joy0=101                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1514 |
| MD6-03    | joy0=101                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1522 |
| MD6-04    | joy0=101                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1530 |
| MD6-05    | joy1=110                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1538 |
| MD6-06    | joy1=110                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1546 |
| MD6-07    | joy1=110                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1554 |
| MD6-08    | joy1=110                                                     | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1562 |
| MD6-09    | both MD                                                      | zxnext.vhd:6215                         | pass   | test/input/input_test.cpp:1571 |
| MD6-10    | joy0=001 (Kempston, not MD), `i_JOY_LEFT(10)=1`              | zxnext.vhd:6215,3441-3442               | pass   | test/input/input_test.cpp:1589 |
| MD6-11a   | 0000 (left, sub=000)                                         | md6_joystick_connector_x2.vhd:135-139 | pass   | test/input/input_test.cpp:1616 |
| MD6-11b   | 0100 (left, sub=010)                                         | md6_joystick_connector_x2.vhd:141-144   | pass   | test/input/input_test.cpp:1635 |
| MD6-11c   | 0110 (left, sub=011)                                         | md6_joystick_connector_x2.vhd:151-152   | pass   | test/input/input_test.cpp:1649 |
| MD6-11d   | 1000 (left, sub=100)                                         | md6_joystick_connector_x2.vhd:157-158   | pass   | test/input/input_test.cpp:1665 |
| MD6-11e   | 1010 (left, sub=101)                                         | md6_joystick_connector_x2.vhd:163-166   | pass   | test/input/input_test.cpp:1681 |
| MD6-11f   | 0101 (right, sub=010)                                        | md6_joystick_connector_x2.vhd:146-149   | pass   | test/input/input_test.cpp:1695 |
| MD6-11g   | 0111 (right, sub=011)                                        | md6_joystick_connector_x2.vhd:154-155   | pass   | test/input/input_test.cpp:1709 |
| MD6-11h   | 1011 (right, sub=101)                                        | md6_joystick_connector_x2.vhd:168-171   | pass   | test/input/input_test.cpp:1724 |
| MD6-11i   | 1000 (left, sub=100), 3-button pad                           | md6_joystick_connector_x2.vhd:163-166   | pass   | test/input/input_test.cpp:1740 |
| SINC1-01  | joy0=011                                                     | —                                     | pass   | test/input/input_test.cpp:1798 |
| SINC1-02  | joy0=011                                                     | —                                     | pass   | test/input/input_test.cpp:1803 |
| SINC1-03  | joy0=011                                                     | —                                     | pass   | test/input/input_test.cpp:1808 |
| SINC1-04  | joy0=011                                                     | —                                     | pass   | test/input/input_test.cpp:1813 |
| SINC1-05  | joy0=011                                                     | —                                     | pass   | test/input/input_test.cpp:1818 |
| SINC2-01  | joy1=000                                                     | —                                     | pass   | test/input/input_test.cpp:1827 |
| SINC2-02  | joy1=000                                                     | —                                     | pass   | test/input/input_test.cpp:1832 |
| SINC2-03  | joy1=000                                                     | —                                     | pass   | test/input/input_test.cpp:1837 |
| SINC2-04  | joy1=000                                                     | —                                     | pass   | test/input/input_test.cpp:1842 |
| SINC2-05  | joy1=000                                                     | —                                     | pass   | test/input/input_test.cpp:1847 |
| SINC-06   | joy0=011, joy1=000, both LEFT                                | membrane_stick.vhd:192                  | pass   | test/input/input_test.cpp:1871 |
| CURS-01   | joy0=010                                                     | —                                     | pass   | test/input/input_test.cpp:1907 |
| CURS-02   | joy0=010                                                     | —                                     | pass   | test/input/input_test.cpp:1913 |
| CURS-03   | joy0=010                                                     | —                                     | pass   | test/input/input_test.cpp:1919 |
| CURS-04   | joy0=010                                                     | —                                     | pass   | test/input/input_test.cpp:1925 |
| CURS-05   | joy0=010                                                     | —                                     | pass   | test/input/input_test.cpp:1931 |
| CURS-06   | joy0=010, LEFT + RIGHT                                       | —                                     | pass   | test/input/input_test.cpp:1944 |
| IOMODE-01 | Reset                                                        | zxnext.vhd:3510-3539,5200-5203          | pass   | test/input/input_test.cpp:1971 |
| IOMODE-02 | Write NR 0x0B = 0x80 (en=1, mode=00, iomode_0=0)             | zxnext.vhd:3520                         | pass   | test/input/input_test.cpp:1981 |
| IOMODE-03 | Write NR 0x0B = 0x81 (en=1, mode=00, iomode_0=1)             | zxnext.vhd:3520                         | pass   | test/input/input_test.cpp:1991 |
| IOMODE-04 | Write NR 0x0B = 0x91 (en=1, mode=01) + pulse `ctc_zc_to(3)`  | zxnext.vhd:3521-3524                    | pass   | test/input/input_test.cpp:2011 |
| IOMODE-05 | Write NR 0x0B = 0xA0 (en=1, mode=10, iomode_0=0)             | zxnext.vhd:3526-3531                    | pass   | test/input/input_test.cpp:2028 |
| IOMODE-06 | Write NR 0x0B = 0xA1 (en=1, mode=10, iomode_0=1)             | zxnext.vhd:3526-3531                    | pass   | test/input/input_test.cpp:2045 |
| IOMODE-07 | NR 0x0B = 0xA0/0xA1 (mode "10" → LEFT), press `JOY_LEFT(5)`  | zxnext.vhd:3538,90                        | pass   | test/input/input_test.cpp:2073 |
| IOMODE-08 | NR 0x0B = 0xB0/0xB1 (mode "11" → RIGHT), press `JOY_RIGHT(5)`| zxnext.vhd:3538,90-91                     | pass   | test/input/input_test.cpp:2098 |
| IOMODE-09 | Write NR 0x0B = 0xA0, assert `joy_uart_en`                   | zxnext.vhd:3537                         | pass   | test/input/input_test.cpp:2117 |
| IOMODE-10 | Write NR 0x0B = 0x80                                         | zxnext.vhd:3537                         | pass   | test/input/input_test.cpp:2136 |
| IOMODE-11 | NR 0x05 joy0/joy1 = 111 (user I/O) + NR 0x0B configured      | zxnext.vhd:5157-5158,5200-5203          | pass   | test/input/input_test.cpp:2166 |
| MOUSE-01  | `port_mouse_io_en=1`                                         | zxnext.vhd:3546                         | pass   | test/input/input_test.cpp:2340 |
| MOUSE-02  | `port_mouse_io_en=1`                                         | zxnext.vhd:3553                         | pass   | test/input/input_test.cpp:2352 |
| MOUSE-03  | `port_mouse_io_en=1`                                         | zxnext.vhd:3560                         | pass   | test/input/input_test.cpp:2366 |
| MOUSE-04  | `port_mouse_io_en=1`                                         | —                                     | pass   | test/input/input_test.cpp:2379 |
| MOUSE-05  | `port_mouse_io_en=1`                                         | —                                     | pass   | test/input/input_test.cpp:2391 |
| MOUSE-06  | `port_mouse_io_en=1`                                         | —                                     | pass   | test/input/input_test.cpp:2403 |
| MOUSE-07  | `port_mouse_io_en=1`                                         | zxnext.vhd:3560                         | pass   | test/input/input_test.cpp:2416 |
| MOUSE-08  | `port_mouse_io_en=0`                                         | zxnext.vhd:2668-2670                    | pass   | test/input/input_test.cpp:2447 |
| MOUSE-09  | NR 0x0A bit 3 = 1 (reverse)                                  | —                                     | missing | missing                       |
| MOUSE-10  | `port_mouse_io_en=1`                                         | —                                     | missing | missing                       |
| MOUSE-11  | `port_mouse_io_en=1`, `nr_0a_mouse_dpi = "00"` vs `"11"`     | zxnext.vhd                              | missing | missing                       |
| NMI-01    | NR 0x06 bit 3 = 1 (M1 en)                                    | —                                     | pass   | test/input/input_test.cpp:2797 |
| NMI-02    | NR 0x06 bit 3 = 0                                            | —                                     | pass   | test/input/input_test.cpp:2814 |
| NMI-03    | NR 0x06 bit 4 = 1                                            | —                                     | pass   | test/input/input_test.cpp:2831 |
| NMI-04    | NR 0x06 bit 4 = 0                                            | —                                     | pass   | test/input/input_test.cpp:2848 |
| NMI-05    | NR 0x06 bit 3 = 1                                            | zxnext.vhd:2090                         | pass   | test/input/input_test.cpp:2867 |
| NMI-06    | NR 0x06 bit 4 = 1                                            | —                                     | pass   | test/input/input_test.cpp:2885 |
| NMI-07    | both NR 0x06 bits 3,4 = 1, both hotkeys asserted             | —                                     | pass   | test/input/input_test.cpp:2903 |
| FE-01     | No keys, EAR=0, no `port_fe_ear`                             | zxnext.vhd:3459                         | pass    | test/input/input_integration_test.cpp:206 |
| FE-02     | EAR input high                                               | zxnext.vhd:3459,1636                      | pass    | test/input/input_integration_test.cpp:227 |
| FE-03     | Write 0xFE bit 4 high (`port_fe_ear`=1), then read           | zxnext.vhd:3459                         | pass    | test/input/input_integration_test.cpp:246 |
| FE-04     | NR 0x08 bit 0 = 1 (issue 2), MIC=1, EAR=0                    | zxnext.vhd:5182,1636,3459                 | pass    | test/input/input_integration_test.cpp:310 |
| FE-05     | `expbus_eff_en=1`, `port_propagate_fe=1`, expansion bus dri… | —                                     | missing | missing                       |
| JMODE-09      | NR 0x05=0x40 (joy0=Kempston1) -> MembraneStick::set_mode(joy0) called from Joystick::set_nr_05       | membrane_stick.vhd:117-149              | pass   | test/input/input_test.cpp:1094 |
| KEMP-16       | NR 0x82 b7=0; joy0=100 (Kempston2); read port 0x37 -> floating-bus, NOT joystick byte                | zxnext.vhd:2408,2675                    | pass   | test/input/input_test.cpp:1302 |
| KEMP-17       | joy0=000 (Sinclair2), joy1=010 (Cursor); read port 0x1F -> floating-bus 0xFF (port_1f_hw_en=0)       | zxnext.vhd:2454-2455,2674-2675          | pass   | test/input/input_test.cpp:1377 |
| MOUSE-12      | port_dac_mono_AD_df_io_en=1 AND port_mouse_io_en=0 AND joy0=001 -> read 0xDF returns Kempston byte   | zxnext.vhd:2674                         | pass   | test/input/input_test.cpp:2526 |
| MOUSE-13      | Host SDL mouse motion -> KempstonMouse X/Y deltas at port 0xFADF / 0xFBDF (G43)                      | zxnext.vhd:3546,3553                    | pass   | test/input/input_test.cpp:2574 |
| MOUSE-14      | Host SDL mouse buttons -> KempstonMouse button bits at port 0xFBDF (G43)                             | zxnext.vhd:3560                         | pass   | test/input/input_test.cpp:2613 |
| MOUSE-15      | Host wheel scroll -> KempstonMouse wheel byte (NR 0x0A bit 3 reverse honoured) (G43)                 | zxnext.vhd:3560                         | pass   | test/input/input_test.cpp:2645 |
| KBDHYS-04     | `Emulator` main loop calls `Keyboard::tick_scan()` each membrane scan-cycle (production wire)        | membrane.vhd:178-191                    | pass   | test/input/input_test.cpp:682 |
| KBDHYS-05     | `Keyboard::tick_scan()` cancels extended entries when `i_cancel_extended_entries` asserted (prod)    | membrane.vhd:178-191                    | missing | missing                       |
| JCAL-01       | NR 0x28 keymap_sel write (2-bit) persists to NextReg readback                                        | zxnext.vhd:6294-6300                    | pass   | test/input/input_test.cpp:2977 |
| JCAL-02       | NR 0x29 addr-low + NR 0x2B data-write/inc -> SDP-RAM-analogue keyjoy_64_6 entry                      | zxnext.vhd:6301-6324                    | pass   | test/input/input_test.cpp:3016 |
| JCAL-03       | NR 0x05 joy0/1=111 + JCAL-programmed entry -> membrane fold redirects through user-defined keymap   | zxnext.vhd:5157-5158,3429-3438          | pass   | test/input/input_test.cpp:3060 |
| FNK-01        | F-key 7-state FSM (`emu_fnkeys.vhd`) consuming i_button_m1_n + i_button_reset_n + membrane           | input/membrane/emu_fnkeys.vhd:53-202    | pass   | test/input/input_test.cpp:3100 |
| HOTKEY-01     | Host SDL_SCANCODE_F8 -> NR 0x07 cpu_speed (mod 4); F3 toggles 50/60 Hz; gated by nr_06_hotkey_*_en  | zxnext.vhd:5790-5791,6342-6347          | pass    | test/input/input_integration_test.cpp:963 |
| JOY-WIRE-01   | OUT (0x253B), 0x40 with reg=0x05 -> MembraneStick fold redirects through full NR-write path          | emulator.cpp:456-458                    | pass    | test/input/input_integration_test.cpp:618 |
| JOY-WIRE-02   | Host SDL gamepad buttons -> Kempston bits at port 0x1F (G42)                                         | zxnext.vhd:3441-3442,3470-3479            | pass    | test/input/input_integration_test.cpp:673 |
| JOY-WIRE-03   | Host SDL gamepad axis -> Kempston bits at port 0x1F (G42)                                            | —                                       | pass    | test/input/input_integration_test.cpp:731 |
| JOY-WIRE-04   | Host SDL controller index 0/1 -> joy_left / joy_right lanes; index >= 2 ignored (G42)                | —                                       | pass    | test/input/input_integration_test.cpp:787 |
| HK-WIRE-01    | Host F1 SDL key dispatched into `Emulator::trigger_hard_reset()` injector (G152)                     | emulator.h:328-329                      | missing | missing                       |
| HK-WIRE-02    | Host F4 SDL key dispatched into `Emulator::trigger_soft_reset()` injector (G152)                     | emulator.h:328-329                      | missing | missing                       |
| HK-WIRE-03    | Host F9 SDL key dispatched into NMI source `assert_mf` injector (G152)                               | nmi_source.cpp                          | missing | missing                       |
| HK-WIRE-04    | Host F10 SDL key dispatched into DivMMC button NMI injector (G152)                                   | divmmc.cpp                              | missing | missing                       |
| FE-04A        | NR 0x08 b0=1 (issue-2), keyboard EAR/MIC composition with port_fe_ear (G44)                          | keyboard.cpp                            | missing | missing                       |
| IOMODE-11A    | NR 0x05 joy0/joy1=111 (user I/O) + NR 0x0B with iomode=01 -> CTC ZC/TO routes to UART pin-7 (G72)    | zxnext.vhd                              | pass   | test/input/input_test.cpp:2243 |
| IOMODE-11B    | Emulator per-tick feed: Joystick line-5 (button C) -> IoMode joy_uart_rx via run_frame() (GH #90)    | zxnext.vhd:90-91,3441-3442,3538         | pass   | test/input/input_test.cpp:2311 |
| GH115-01      | LShift pulls ONLY row 0 col 0 (CAPS SHIFT) low; release restores row 0 to 0x1F (GH #115)             | keymaps.vhd:83,113; ps2_keyb.vhd:198        | pass    | test/input/input_test.cpp:4880     |
| GH115-02      | RShift pulls ONLY row 0 col 0 (CAPS SHIFT) low; release restores row 0 to 0x1F (GH #115)             | keymaps.vhd:83,131; ps2_keyb.vhd:198        | pass    | test/input/input_test.cpp:4885     |
| GH115-03      | LCtrl pulls ONLY row 7 col 1 (SYMBOL SHIFT) low; release restores row 7 to 0x1F (GH #115)            | keymaps.vhd:84,113; ps2_keyb.vhd:197        | pass    | test/input/input_test.cpp:4890     |
| GH115-04      | RCtrl (PS/2 extended 0x14) pulls ONLY row 7 col 1 (SYMBOL SHIFT) low; clean release (GH #115)        | keymaps.vhd:84,165; ps2_keyb.vhd:197        | pass    | test/input/input_test.cpp:4895     |
| GH115-05      | Converse of the inversion: LShift leaves row 7 at 0x1F and LCtrl leaves row 0 at 0x1F                | keymaps.vhd:83-84; ps2_keyb.vhd:197-198     | pass    | test/input/input_test.cpp:4912     |
| GH115-06      | CapsLock pulls row 0 col 0 AND row 3 col 1 (CS + 2 = CAPS LOCK); both clear on release               | keymaps.vhd:43,89,131; membrane.vhd:236-237 | pass    | test/input/input_test.cpp:4925     |
| GH115-07      | backslash pulls row 0 col 0 AND row 3 col 3 (CS + 4 = INV VIDEO); both clear on release              | keymaps.vhd:44,94,131; membrane.vhd:236-237 | pass    | test/input/input_test.cpp:4930     |
| GH115-08      | slash pulls row 7 col 1 AND row 0 col 4 (SS + V = '/'); both clear on release                        | keymaps.vhd:42,127; ps2_keyb.vhd:197        | pass    | test/input/input_test.cpp:4935     |
| GH115-09      | minus pulls row 7 col 1 AND row 6 col 3 (SS + J = '-'); both clear on release                        | keymaps.vhd:48,127; ps2_keyb.vhd:197        | pass    | test/input/input_test.cpp:4940     |
| GH115-10      | equals pulls row 7 col 1 AND row 6 col 1 (SS + L = '='); both clear on release                       | keymaps.vhd:48,129; ps2_keyb.vhd:197        | pass    | test/input/input_test.cpp:4945     |
| GH115-11      | CapsLock / backslash also read back on NR 0xB1 as 0x02 / 0x10; 0x00 with no key down                 | keymaps.vhd:43-44; membrane.vhd:253         | pass    | test/input/input_test.cpp:4962     |

### Companion integration suite — `test/input/input_integration_test.cpp`

Hosts production-wire integration scenarios for the membrane keyboard, joystick host wiring, and Beeper composition. Runs at `17 / 17 pass / 0 fail / 0 skip`. The 5 skips this paragraph used to describe are gone: G42 host-joystick wiring (JOY-WIRE-01..04) and G147 host-hotkey dispatch (HOTKEY-01) all closed and are live passes now. The 17th live row, `JOY-WIRE-04-SDL`, is a production-wiring sanity check that is deliberately not a plan row and is not listed below; the script reports it as `unrecorded` on every run. G44 (FE-04A issue-2 analogue relaxation) was retired as WONT 2026-04-28 — bit-exact in the no-tape regime; the tape-edge transient path is bypassed by jnext's tape stack and would require a Tape-subsystem refactor for a niche issue-2 edge case (FUSE/ZEsarUX both ship without it) — which is why the `FE-04A` row below reads `missing`.

| Test ID     | Plan row title                                                                  | VHDL file:line                          | Status | Test file:line                              |
|-------------|---------------------------------------------------------------------------------|-----------------------------------------|--------|---------------------------------------------|
| KBD-22      | Compound key arrow handling end-to-end                                          | membrane.vhd:178-191                    | pass    | test/input/input_integration_test.cpp:157   |
| KBD-23      | Caps+Sym compound mapping under issue-2/issue-3                                 | membrane.vhd                            | pass    | test/input/input_integration_test.cpp:181   |
| FE-01       | Port 0xFE keyboard-row read steady-state                                        | zxnext.vhd                              | pass    | test/input/input_integration_test.cpp:206   |
| FE-02       | Port 0xFE EAR-bit composition                                                   | zxnext.vhd:3459,1636                      | pass    | test/input/input_integration_test.cpp:227   |
| FE-03       | Port 0xFE MIC-bit composition                                                   | zxnext.vhd                              | pass    | test/input/input_integration_test.cpp:246   |
| FE-04       | Issue-3 EAR/MIC analogue composition                                            | zxnext.vhd:5182,1636,3459                 | pass    | test/input/input_integration_test.cpp:310   |
| FE-04A      | Issue-2 EAR/MIC analogue relaxation (G44, retired WONT 2026-04-28)              | symmetric_relaxation.vhd                | missing | missing                                     |
| BP-04       | Beeper EAR-only composition into Mixer                                          | audio_mixer.vhd                         | pass    | test/input/input_integration_test.cpp:434   |
| BP-20       | Beeper MIC-only composition into Mixer                                          | zxnext.vhd:3459,3598                      | pass    | test/input/input_integration_test.cpp:453   |
| BP-21       | Beeper composite EAR+MIC into Mixer                                             | audio_mixer.vhd                         | pass    | test/input/input_integration_test.cpp:486   |
| BP-22       | Beeper exclusive-mode AND-gate (NR 0x06+0x08)                                   | audio_mixer.vhd:80-81                   | pass    | test/input/input_integration_test.cpp:526   |
| BP-23       | Beeper port_fe write triggers EAR/MIC composition                               | zxnext.vhd                              | pass    | test/input/input_integration_test.cpp:558   |
| JOY-WIRE-01 | Production NR 0x05 mode change propagates to MembraneStick (G126)               | membrane_stick.vhd:117-149              | pass    | test/input/input_integration_test.cpp:618   |
| JOY-WIRE-02 | SDL gamepad button events route to Joystick::set_buttons (G42)                  | zxnext.vhd:3441-3442,3470-3479              | pass    | test/input/input_integration_test.cpp:673   |
| JOY-WIRE-03 | SDL gamepad axis events route to Joystick::set_axes (G42)                       | —                                       | pass    | test/input/input_integration_test.cpp:731   |
| JOY-WIRE-04 | SDL idx 0/1 -> joy_left / joy_right lanes; idx >= 2 ignored (G42)               | —                                       | pass    | test/input/input_integration_test.cpp:787   |
| HOTKEY-01   | Host F2/F3/F7/F8 hotkeys cycle CPU-speed/50-60/scandouble/scanline (G132/G147)  | input/membrane/emu_fnkeys.vhd:53-202    | pass    | test/input/input_integration_test.cpp:963   |


## Rewind — `test/rewind/rewind_test.cpp`

Last-touch commit: HEAD

Suite covers the rewind / backwards-execution pipeline: `RewindBuffer` ring-wrap
semantics, `step_back()` PC restoration via the trace log, `rewind_to_frame()`
register-state restoration, save-state round-trip determinism, and the
disabled-rewind escape path. `RB-FRAME-01..03` are live, passing rows (G67
closed 2026-07-15, Task 60b); the matrix scanner reports them `missing` only
because `rewind_test.cpp` uses its own uppercase `CHECK()` macro, not the
lowercase `check()`/`skip()` pattern `refresh-traceability-matrix.pl`
recognizes — a scanner blind spot, not a coverage gap. The former
`SS-VER-01..07` placeholder rows (G66) were removed 2026-07-15 as
misclassified coverage debt (see `KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md` G66)
and dropped from this table by this GH #196 pass. Rewind has no direct VHDL
anchor (it is a host-side save-state framing concern); all rows show `—`.

| Test ID     | Plan row title                                       | VHDL file:line | Status | Test file:line                  |
|-------------|------------------------------------------------------|----------------|--------|---------------------------------|
| RING-01     | Buffer starts empty                                  | (jnext-internal) | missing | missing                         |
| RING-02     | Depth capped at 4 after 6 frames                     | (jnext-internal) | missing | missing                         |
| RING-03     | Newest frame_num after wrap                          | (jnext-internal) | missing | missing                         |
| RING-04     | Oldest frame_num after wrap                          | (jnext-internal) | missing | missing                         |
| SB-01       | step_back(5) returns true                            | (jnext-internal) | missing | missing                         |
| SB-02       | step_back(5) lands on correct PC                     | (jnext-internal) | missing | missing                         |
| SB-03       | step_back(10) returns true                           | (jnext-internal) | missing | missing                         |
| SB-04       | step_back(10) lands on correct PC                    | (jnext-internal) | missing | missing                         |
| RTF-01      | Five snapshots after five frames                     | (jnext-internal) | missing | missing                         |
| RTF-02      | rewind_to_frame() returns true                       | (jnext-internal) | missing | missing                         |
| RTF-03      | frame_num matches target+1 after rewind              | (jnext-internal) | missing | missing                         |
| RT-01       | Snapshot size greater than zero                      | (jnext-internal) | missing | missing                         |
| RT-02       | Snapshot size under 3 MB sanity bound                | (jnext-internal) | missing | missing                         |
| RT-03       | save_state writes exact snap_size (pass 1)           | (jnext-internal) | missing | missing                         |
| RT-04       | save_state writes exact snap_size (pass 2)           | (jnext-internal) | missing | missing                         |
| RT-05       | save→load→save byte-identical (determinism)          | (jnext-internal) | missing | missing                         |
| SBD-01      | Rewind buffer null when disabled                     | (jnext-internal) | missing | missing                         |
| SBD-02      | step_back returns false when disabled                | (jnext-internal) | missing | missing                         |
| RB-FRAME-01 | take_snapshot bound assertion absent (G67)           | (jnext-internal) | missing | missing                         |
| RB-FRAME-02 | Post-widening clean error path absent (G67)          | (jnext-internal) | missing | missing                         |
| RB-FRAME-03 | Construction-vs-measured size match check (G67)      | (jnext-internal) | missing | missing                         |


## Floating Bus — `test/floating_bus/floating_bus_test.cpp`

Last-touch commit: HEAD

Suite covers the two floating-bus surfaces the Next FPGA exposes: port 0xFF (48K/128K timing, ULA capture) and port 0x0FFD (+3 timing, contended-write latch with bit-0 force / `port_7ffd_locked` gate / NR 0x82 b4 `port_p3_floating_bus_io_en` decode). Plan §1-§6 = 26 plan rows (5 re-homed from ULA §10 + 21 VHDL-justified neighbours), plus 1 port-conflict neighbour FB-3X (Branch B reviewer note 2) and 5 FB-HARNESS-NN fixture-helper smoke rows. Closed 2026-04-25 at 32/32 pass / 0 fail / 0 skip via Branches A/B/C/D (commits `0ee05c5`, `8bcae9b`, `c43b201`, `42b52f0`).

| Test ID       | Plan row title                                                                  | VHDL file:line                              | Status | Test file:line                                  |
|---------------|---------------------------------------------------------------------------------|---------------------------------------------|--------|-------------------------------------------------|
| FB-01         | 48K V-border port 0xFF read returns 0xFF                                        | zxula.vhd:312-316,414,573                   | pass   | test/floating_bus/floating_bus_test.cpp:275     |
| FB-02         | 48K H-blank inside V-active returns 0xFF                                        | zxula.vhd:316,416,573                       | pass   | test/floating_bus/floating_bus_test.cpp:290     |
| FB-2A         | 48K active display, T%8=2 → pixel byte from VRAM (hc 0x9)                       | zxula.vhd:325-327                           | pass   | test/floating_bus/floating_bus_test.cpp:326     |
| FB-2B         | 48K active display, T%8=3 → attribute byte from VRAM (hc 0xB)                   | zxula.vhd:329-330                           | pass   | test/floating_bus/floating_bus_test.cpp:343     |
| FB-2C         | 48K active display, T%8=4 → pixel+1 byte from VRAM (hc 0xD)                     | zxula.vhd:332-333                           | pass   | test/floating_bus/floating_bus_test.cpp:361     |
| FB-2D         | 48K active display, T%8=5 → attr+1 byte from VRAM (hc 0xF)                      | zxula.vhd:335-336                           | pass   | test/floating_bus/floating_bus_test.cpp:378     |
| FB-2E         | 48K active display, idle phase (T%8=0) returns 0xFF (hc 0x1 reset)              | zxula.vhd:321-323,573                       | pass   | test/floating_bus/floating_bus_test.cpp:400     |
| FB-2F         | 48K above-active V-border (line < 64) returns 0xFF                              | zxula.vhd:414-416,573                       | pass   | test/floating_bus/floating_bus_test.cpp:415     |
| FB-03         | +3 port 0xFF in active capture phase hard-forced to 0xFF                        | zxnext.vhd:4513                             | pass   | test/floating_bus/floating_bus_test.cpp:458     |
| FB-03a        | +3 port 0x0FFD bit-0 force (latch=0xA4 → 0xA5)                                  | zxula.vhd:573 + zxnext.vhd:4517             | pass   | test/floating_bus/floating_bus_test.cpp:487     |
| FB-04         | +3 port 0xFF at border ignores p3_floating_bus_dat shadow → 0xFF                | zxnext.vhd:4513                             | pass   | test/floating_bus/floating_bus_test.cpp:503     |
| FB-04a        | +3 port 0x0FFD border fallback via p3_floating_bus_dat → 0xA5                   | zxula.vhd:573 + zxnext.vhd:4498-4509,4517   | pass   | test/floating_bus/floating_bus_test.cpp:519     |
| FB-04b        | +3 port 0x0FFD bit-0 force scoped to active display, not border                 | zxula.vhd:573                                 | pass    | test/floating_bus/floating_bus_test.cpp:568     |
| FB-3A         | +3 port 0x0FFD + port_7ffd_locked=1 → 0xFF                                      | zxnext.vhd:4517                             | pass   | test/floating_bus/floating_bus_test.cpp:590     |
| FB-3B         | +3 port 0x0FFD + NR 0x82 b4=0 → decode blocked → 0xFF                           | zxnext.vhd:2403,2589,2814                   | pass   | test/floating_bus/floating_bus_test.cpp:617     |
| FB-3C         | 48K port 0x0FFD → 0xFF (p3_timing_hw_en gate)                                   | zxnext.vhd:2589,2814                        | pass   | test/floating_bus/floating_bus_test.cpp:631     |
| FB-3D         | 128K port 0x0FFD → 0xFF (p3_timing_hw_en gate)                                  | zxnext.vhd:2589,2814                        | pass   | test/floating_bus/floating_bus_test.cpp:643     |
| FB-3E         | Pentagon port 0x0FFD decode-gate (**RETIRED 2026-05-04** — the standalone Pentagon machine type was dropped, Wave 0.3 follow-up, so this row has no machine to run on; FB-3D (128K) and FB-3F (Next) still cover the decode-gate path; no `check()` row exists) | —                                            | missing | missing                                         |
| FB-3F         | Next port 0x0FFD decoded → latch 0x42, not blocked 0xFF                         | zxnext.vhd:2589,1099                          | pass   | test/floating_bus/floating_bus_test.cpp:680     |
| FB-4A         | 128K active capture → ULA floating bus reaches port 0xFF (0x5A)                 | zxnext.vhd:4513                             | pass   | test/floating_bus/floating_bus_test.cpp:814     |
| FB-4B         | Pentagon active capture → port 0xFF hard-forced 0xFF (**RETIRED 2026-05-04** — the standalone Pentagon machine type was dropped, Wave 0.3 follow-up, so this row has no machine to run on; FB-4C (Next) still covers the same gate path; no `check()` row exists) | —                                            | missing | missing                                         |
| FB-4C         | Next-base active capture → port 0xFF hard-forced 0xFF                           | zxnext.vhd:4513                             | pass   | test/floating_bus/floating_bus_test.cpp:834     |
| FB-06         | 48K CPU IN A,(0xFF) at border via port-dispatch default returns 0xFF            | zxnext.vhd:2713,2813                        | pass   | test/floating_bus/floating_bus_test.cpp:864     |
| FB-5A         | 48K CPU IN A,(0xFF) in active line sees VRAM marker (saturated row)             | zxnext.vhd:2713,2813                        | pass   | test/floating_bus/floating_bus_test.cpp:913     |
| FB-07         | 48K NR 0x08 b2=1 + port 0xFF write 0x05 → read returns 0x05 (Timex arm)         | zxnext.vhd:2813,5180,3630                   | pass   | test/floating_bus/floating_bus_test.cpp:953     |
| FB-6A         | 48K reset NR 0x08 b2=0 → border read returns 0xFF (floating-bus arm wins)       | zxnext.vhd:1118,2813,5180                   | pass   | test/floating_bus/floating_bus_test.cpp:969     |
| FB-6B         | 48K NR 0x08 b2=1 + NR 0x82 b0=0 → Timex arm collapses → 0xFF                    | zxnext.vhd:2397,2813                        | pass   | test/floating_bus/floating_bus_test.cpp:988     |
| FB-3X         | +3 port 0x0FFD dispatches to dedicated 0x0FFD handler not 0x7FFD (mask 0xF003 > 0x8003) | zxula.vhd:573, zxnext.vhd:4478,4499-4508    | pass   | test/floating_bus/floating_bus_test.cpp:784     |
| FB-HARNESS-01 | set_raster_position(line, tstate) lands clock at expected master cycle         | —                                            | pass   | test/floating_bus/floating_bus_test.cpp:1287    |
| FB-HARNESS-02 | set_raster_position_hc(line, hc) lands clock at expected master cycle (7 MHz domain) | —                                       | pass   | test/floating_bus/floating_bus_test.cpp:1313    |
| FB-HARNESS-03 | cpu_in_a_FF executes IN A,(0xFF), PC advances 2, returns border 0xFF           | —                                            | pass   | test/floating_bus/floating_bus_test.cpp:1335    |
| FB-HARNESS-04 | cpu_in_a_0FFD executes IN A,(C) with BC=0x0FFD; PC advances 2, BC preserved    | —                                            | pass   | test/floating_bus/floating_bus_test.cpp:1353    |
| FB-HARNESS-05 | read_port_default(0x00FF) on fresh 48K returns 0xFF via port_dispatch default  | —                                            | pass   | test/floating_bus/floating_bus_test.cpp:1368    |


## VideoTiming — `test/videotiming/videotiming_test.cpp`

Plan: `doc/testing/VIDEOTIMING-TEST-PLAN-DESIGN.md`. The VideoTiming expansion plan was closed 2026-04-26 once the per-machine accessors landed (V1 rebase, `display_origin()`, `ula_prefetch_origin_hc()`, `int_position()`, 60 Hz toggle, `int_line_num()` promotion). All 22 originally-scoped rows (Sections 1-6, VT-01..VT-21) flipped from skip() to live `check()` and pass. Task 7 round 1 added Section 7 ("Production scheduler wiring") with VT-22..VT-25 as F-skips for the production-wiring gaps tracked under G106 / G107 / G109 in `doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md`. Task 7 round 2 added VT-26, a class-G walkback row pinning the test-only pulse-counter accessors at `src/video/timing.h:97-134` as dead-code-pending-removal — the cleanup lands as a consequence of the G106/G107 fixes (see G71). Final per-row tally: 22 pass, 4 skip (F-blocked on the scheduler refactor), 1 skip (G-walkback).

| Test ID | Plan row title                                                                                | VHDL file:line                  | Status | Test file:line                                       |
|---------|------------------------------------------------------------------------------------------------|---------------------------------|--------|------------------------------------------------------|
| VT-01   | 48K `hc_max()`/`vc_max()` after `init(ZX48K)` = 447, 311                                       | zxula_timing.vhd:262,270        | pass   | test/videotiming/videotiming_test.cpp:109            |
| VT-02   | 128K `hc_max()`/`vc_max()` after `init(ZX128K)` = 455, 310                                     | zxula_timing.vhd:196,204        | pass   | test/videotiming/videotiming_test.cpp:117            |
| VT-03   | Pentagon `hc_max()`/`vc_max()` after `init(PENTAGON)` = 447, 319                               | zxula_timing.vhd:160,168        | missing | missing                                              |
| VT-04   | 128K `display_origin()` = {136, 64}                                                            | zxula_timing.vhd:195,203        | pass   | test/videotiming/videotiming_test.cpp:139            |
| VT-05   | Pentagon `display_origin()` = {128, 80}                                                        | zxula_timing.vhd:159,167        | missing | missing                                              |
| VT-06   | 48K `display_origin()` = {128, 64} — symmetry baseline                                         | zxula_timing.vhd:261,269        | pass   | test/videotiming/videotiming_test.cpp:148            |
| VT-07   | 48K `ula_prefetch_origin_hc()` = 128 - 12 = 116                                                | zxula_timing.vhd:423            | pass   | test/videotiming/videotiming_test.cpp:167            |
| VT-08   | 128K `ula_prefetch_origin_hc()` = 136 - 12 = 124                                               | zxula_timing.vhd:423            | pass   | test/videotiming/videotiming_test.cpp:174            |
| VT-10   | 48K `int_position()` = {116, 0}                                                                | zxula_timing.vhd:257,265        | pass   | test/videotiming/videotiming_test.cpp:283            |
| VT-11   | 128K `int_position()` = {128, 1}                                                               | zxula_timing.vhd:187,199        | pass   | test/videotiming/videotiming_test.cpp:290            |
| VT-12   | Pentagon `int_position()` = {439, 319}                                                         | zxula_timing.vhd:155,163        | missing | missing                                              |
| VT-13   | +3 `int_position()` = {126, 1} — VHDL `i_timing(0)='1'` selects 126                            | zxula_timing.vhd:189,199        | pass   | test/videotiming/videotiming_test.cpp:298            |
| VT-14   | 48K 60 Hz frame length = 448 * 264 / 2 = 59 136 T-states                                       | zxula_timing.vhd:290,298        | pass   | test/videotiming/videotiming_test.cpp:323            |
| VT-15   | 128K 60 Hz frame length = 456 * 264 / 2 = 60 192 T-states                                      | zxula_timing.vhd:230,238        | pass   | test/videotiming/videotiming_test.cpp:333            |
| VT-16   | 60 Hz `display_origin().vc` = 40 for both 48K and 128K                                         | zxula_timing.vhd:297,237        | pass   | test/videotiming/videotiming_test.cpp:344            |
| VT-17   | 60 Hz `int_position().vc` = 0 for both 48K and 128K                                            | zxula_timing.vhd:293,233        | pass   | test/videotiming/videotiming_test.cpp:354            |
| VT-17b  | +3 60 Hz `int_position().hc` = 126 vs 128K 60 Hz = 128 — `i_timing(0)='1'` split               | zxula_timing.vhd:221,223        | pass   | test/videotiming/videotiming_test.cpp:361            |
| VT-18   | 48K target=0 → `int_line_num() == c_max_vc == 311`                                             | zxula_timing.vhd:566-570        | pass   | test/videotiming/videotiming_test.cpp:385            |
| VT-19   | 128K target=0 → `int_line_num() == 310`                                                        | zxula_timing.vhd:566-570        | pass   | test/videotiming/videotiming_test.cpp:394            |
| VT-21   | Any machine: target=10 → `int_line_num() == 9`                                                 | zxula_timing.vhd:568            | pass   | test/videotiming/videotiming_test.cpp:404            |
| VT-22   | 48K target=10: line-int fires at `cvc=9, hc_ula=255`                                           | zxula_timing.vhd:563-583        | pass   | test/videotiming/videotiming_test.cpp:453            |
| VT-23   | 48K target=0: line-int fires at `cvc=c_max_vc=311, hc_ula=255` (target=0 wrap)                 | zxula_timing.vhd:566-570        | pass   | test/videotiming/videotiming_test.cpp:473            |
| VT-24   | 128K frame-INT fires at `(hc=128, vc=1)` per `c_int_h`/`c_int_v`                               | zxula_timing.vhd:187,199        | pass   | test/videotiming/videotiming_test.cpp:488            |
| VT-25   | NR 0x64 = 5 → line-int compare uses `cvc` offset-adjusted, not raw `vc`                        | zxula_timing.vhd:577            | pass   | test/videotiming/videotiming_test.cpp:516            |
| VT-26   | Walkback: pulse-counter accessors must be sole source — `Emulator::line_int_*` removed         | zxula_timing.vhd:563-583        | pass   | test/videotiming/videotiming_test.cpp:539            |


## Contention — `test/contention/contention_test.cpp`

Last-touch commit: `886b4c233a7e00885e469ae783073f4f16b04c24` (`886b4c2`)

The base 28→0 skip closure for this suite landed on 2026-04-26 via a
three-phase wave (Phase A=28 bare-class rows, Phase B=36 full-Emulator
rows, Phase C=4 integration-smoke rows — `doc/testing/CONTENTION-TEST-PLAN-DESIGN.md` §15
"closed 2026-04-26"). Subsequent additions for
G51 (`CT-TURBO-08`), G53 (`CT-FUSE-05`), G141 (`CT-FUSE-01..04`) and
G142 (`CT-TURBO-07`) are tracked here as `F-skip` rows pending the
respective emulator work (FUSE-table retirement, NR 0x07 bus-idle
commit edge, combined hc(8)+bus-idle ordering). G50
(`CT-DELAY-01`, full-frame integration drift bound) closed 2026-04-28
via §14b — drift envelope is `(0, 6·N]` for 48K/128K/+3 and `0` for
Pentagon, derived from the VHDL `wait_s × pattern` LUT.
Plan numbering: §4–§14 land green; §16 + post-§14 G-rows are the open
backlog. The CON-* rows from `test/mmu/mmu_test.cpp` were C2-moved
into this suite as part of the 2026-04-26 closure.

| Test ID      | Plan row title                                                      | VHDL file:line                      | Status | Test file:line                              |
|--------------|---------------------------------------------------------------------|-------------------------------------|--------|---------------------------------------------|
| CT-GATE-01   | ZX48K, page=0x0A — enable=1, mem=1                                  | zxnext.vhd:4481,4490                | pass   | test/contention/contention_test.cpp:118     |
| CT-GATE-02   | NR 0x08 bit 6 (`contention_disable`) gates enable off               | zxnext.vhd:4481                     | pass   | test/contention/contention_test.cpp:131     |
| CT-GATE-03   | `cpu_speed=1` (7 MHz) gates enable off                              | zxnext.vhd:4481,5817                | pass   | test/contention/contention_test.cpp:144     |
| CT-GATE-04   | `cpu_speed=2` (14 MHz) gates enable off                             | zxnext.vhd:4481,5817                | pass   | test/contention/contention_test.cpp:156     |
| CT-GATE-05   | `cpu_speed=3` (28 MHz) gates enable off                             | zxnext.vhd:4481,5817                | pass   | test/contention/contention_test.cpp:168     |
| CT-GATE-06   | `pentagon_timing=1` gates enable off (**RETIRED 2026-05-04** — standalone Pentagon machine type dropped, Wave 0.3; `ContentionModel` no longer exposes `pentagon_timing`; CT-GATE-01/02/03/04/05/07/08 cover the surviving enable-gate terms) | —                                    | missing | missing                                     |
| CT-GATE-07   | All gate inputs at VHDL power-on defaults — enable=1                | zxnext.vhd:4481,4490                | pass   | test/contention/contention_test.cpp:189     |
| CT-GATE-08   | Default-constructed `ContentionModel` (no `build()`) — enable=0     | src/memory/contention.cpp:87-90     | pass   | test/contention/contention_test.cpp:215     |
| CT-M48-01    | 48K page 0x0A (bank 5, bits(3:1)=101) — contended                   | zxnext.vhd:4490                     | pass   | test/contention/contention_test.cpp:240     |
| CT-M48-03    | 48K page 0x00 (bits(3:1)=000) — not contended                       | zxnext.vhd:4490                     | pass   | test/contention/contention_test.cpp:251     |
| CT-M48-05    | 48K page 0x0E (bits(3:1)=111) — not contended                       | zxnext.vhd:4490                     | pass   | test/contention/contention_test.cpp:262     |
| CT-M48-06    | 48K page 0x10 — high-nibble guard blocks                            | zxnext.vhd:4489                     | pass   | test/contention/contention_test.cpp:274     |
| CT-M48-08    | 48K page 0xFF — floating-bus sentinel; high-nibble guard            | zxnext.vhd:4489                     | pass   | test/contention/contention_test.cpp:286     |
| CT-M128-01   | 128K page 0x02 (bank 1, bit(1)=1) — contended                       | zxnext.vhd:4491                     | pass   | test/contention/contention_test.cpp:306     |
| CT-M128-03   | 128K page 0x04 (bank 2, bit(1)=0) — not contended                   | zxnext.vhd:4491                     | pass   | test/contention/contention_test.cpp:317     |
| CT-M128-08   | 128K page 0x10 — high-nibble guard                                  | zxnext.vhd:4489                     | pass   | test/contention/contention_test.cpp:328     |
| CT-MP3-01    | +3 page 0x08 (bank 4, bit(3)=1) — contended                         | zxnext.vhd:4492                     | pass   | test/contention/contention_test.cpp:348     |
| CT-MP3-05    | +3 page 0x00 (bank 0, bit(3)=0) — not contended                     | zxnext.vhd:4492                     | pass   | test/contention/contention_test.cpp:359     |
| CT-MP3-08    | +3 ROM-style high page (≥0xF0) — high-nibble guard                  | zxnext.vhd:4489                     | pass   | test/contention/contention_test.cpp:371     |
| CT-IO-01     | 48K, even port 0xFE — `port_contend=1`                              | zxnext.vhd:4496                     | pass   | test/contention/contention_test.cpp:397     |
| CT-IO-02     | 48K, odd port 0xFF — `port_contend=0`                               | zxnext.vhd:4496                     | pass   | test/contention/contention_test.cpp:405     |
| CT-IO-03     | 48K, even port 0x00 — contended                                     | zxnext.vhd:4496                     | pass   | test/contention/contention_test.cpp:413     |
| CT-IO-04     | 48K, odd port 0x01 — not contended                                  | zxnext.vhd:4496                     | pass   | test/contention/contention_test.cpp:421     |
| CT-IO-05     | 128K, port 0x7FFD — odd, `port_7ffd_active=1` OR-term               | zxnext.vhd:4496,2594                | pass   | test/contention/contention_test.cpp:447     |
| CT-IO-06     | 48K, port 0x7FFD — `port_7ffd_active=0` on 48K                      | zxnext.vhd:4496,2594                | pass   | test/contention/contention_test.cpp:459     |
| CT-IO-07     | port 0xBF3B (ULA+ index, odd) — contended via `port_bf3b` term      | zxnext.vhd:4496,2685                | pass   | test/contention/contention_test.cpp:470     |
| CT-IO-08     | port 0xFF3B (ULA+ data, odd) — contended via `port_ff3b` term       | zxnext.vhd:4496,2686                | pass   | test/contention/contention_test.cpp:478     |
| CT-IO-09     | port 0xBF3B with `port_ulap_io_en=0` — masked                       | zxnext.vhd:4496,2685                | pass   | test/contention/contention_test.cpp:486     |
| CT-WIN-01    | 48K, hc=0, vc=0 — `+1` offset prelude (wait_s=0)                    | zxula.vhd:582-583                   | pass   | test/contention/contention_test.cpp:514     |
| CT-WIN-02    | 48K, hc=3, vc=100 — phase boundary, wait_s=1                        | zxula.vhd:582-583                   | pass   | test/contention/contention_test.cpp:530     |
| CT-WIN-03    | 48K, hc=15, vc=100 — 4-bit wrap, wait_s=0                           | zxula.vhd:178,582-583               | pass   | test/contention/contention_test.cpp:542     |
| CT-WIN-04    | 48K, hc=255, vc=100 — same wrap at high end                         | zxula.vhd:178,582-583               | pass   | test/contention/contention_test.cpp:554     |
| CT-WIN-05    | 48K, hc=256, vc=100 — `hc(8)=1`, window OFF                         | zxula.vhd:583                       | pass   | test/contention/contention_test.cpp:565     |
| CT-WIN-06    | 48K, hc=100, vc=192 — `border_active_v=1`, window OFF               | zxula.vhd:583                       | pass   | test/contention/contention_test.cpp:580     |
| CT-WIN-07    | 48K, vc 0..191 sweep — per-phase pattern matches LUT                | zxula.vhd:579-583,587-595           | pass   | test/contention/contention_test.cpp:617     |
| CT-WIN-08    | +3, `hc_adj(3:1)=000` extra-phase clause (pattern row 0)            | zxula.vhd:582-583                   | pass   | test/contention/contention_test.cpp:642     |
| CT-WIN-09    | 48K, hc=16, vc=100 — pairs with WIN-04 to pin 4-bit-only wrap       | zxula.vhd:178,582-583               | pass   | test/contention/contention_test.cpp:658     |
| CT-WIN-10    | 48K, hc=7, vc=100 — pattern[7]=0, pattern bit-3 input branch        | zxula.vhd:582-583,587-595           | pass   | test/contention/contention_test.cpp:674     |
| CT-S48-01    | 48K, bank 5 mem read inside window, stretched phase — LUT delay    | zxula.vhd:587-595                   | pass   | test/contention/contention_test.cpp:709     |
| CT-S48-02    | 48K, bank 5 mem read, non-stretched phase — zero added              | zxula.vhd:582-595                   | pass   | test/contention/contention_test.cpp:725     |
| CT-S48-03    | 48K, bank 0 mem read (never contended page) — zero added            | zxula.vhd:595, zxnext.vhd:4490      | pass   | test/contention/contention_test.cpp:739     |
| CT-S48-04    | 128K, bank 1 mem read, display+stretched — LUT delay                | zxula.vhd:587-595, zxnext.vhd:4491  | pass   | test/contention/contention_test.cpp:752     |
| CT-S48-05    | 128K, bank 4 mem read (even bank, not contended) — zero added       | zxnext.vhd:4491                     | pass   | test/contention/contention_test.cpp:767     |
| CT-S48-06    | 48K, port 0xFE (even, port_contend=1) display+stretched             | zxula.vhd:587-595, zxnext.vhd:4496  | pass   | test/contention/contention_test.cpp:782     |
| CT-S48-07    | 48K, port 0xFF (odd, port_contend=0) — zero added                   | zxnext.vhd:4496                     | pass   | test/contention/contention_test.cpp:797     |
| CT-S48-08    | 48K, mem read OUTSIDE display window — zero added                   | zxula.vhd:583                       | pass   | test/contention/contention_test.cpp:811     |
| CT-SP3-01    | +3, bank 4 mem read inside window, stretched — WAIT_n LUT           | zxula.vhd:600                       | pass   | test/contention/contention_test.cpp:841     |
| CT-SP3-02    | +3, bank 7 mem read inside window, stretched — LUT delay            | zxula.vhd:600, zxnext.vhd:4492      | pass   | test/contention/contention_test.cpp:859     |
| CT-SP3-03    | +3, bank 0 mem read (bit(3)=0) — not contended                      | zxnext.vhd:4492                     | pass   | test/contention/contention_test.cpp:873     |
| CT-SP3-04    | +3, bank 4 mem read OUTSIDE window — wait_s=0                       | zxula.vhd:583,600                   | pass   | test/contention/contention_test.cpp:887     |
| CT-SP3-05    | +3, bank 4 with `contention_disable=1` — enable gate off            | zxnext.vhd:4481                     | pass   | test/contention/contention_test.cpp:901     |
| CT-SP3-06    | +3, port 0xFE display window — WAIT path memory-only (zero added)   | zxula.vhd:599-600                   | pass   | test/contention/contention_test.cpp:921     |
| CT-SP3-07    | +3, port 0xFE display window, contended bank in slot — same         | zxula.vhd:599-600                   | pass   | test/contention/contention_test.cpp:938     |
| CT-SP3-08    | +3, `hc_adj(3:1)=000` extra phase, bank 4 read — stall asserts      | zxula.vhd:582-583,600               | pass   | test/contention/contention_test.cpp:961     |
| CT-PENT-01   | Pentagon, page 0x0A — never contended (**RETIRED 2026-05-04** — standalone Pentagon machine type dropped, Wave 0.3; standalone Pentagon `build()` path no longer exists; CT-GATE-01/07/08 + CT-M48-\*/CT-M128-\*/CT-MP3-\* cover the surviving 48K/128K/+3/Next path) | —                                    | missing | missing                                     |
| CT-PENT-04   | Pentagon, full Emulator, port 0xFE — gate blocks before decode (**RETIRED 2026-05-04** — same removal, Wave 0.3; CT-IO-01..04/07..09 + CT-INT-01 cover the surviving I/O-contention path) | —                                    | missing | missing                                     |
| CT-PENT-05   | Pentagon, full-frame contended program — 71680 T-state budget (**RETIRED 2026-05-04** — same removal, Wave 0.3; CT-INT-01..03 cover the surviving 48K full-frame integration path) | —                                    | missing | missing                                     |
| CT-TURBO-01  | 48K, `cpu_speed=1` (7 MHz) bare-class — enable gate off             | zxnext.vhd:4481,5817                | pass   | test/contention/contention_test.cpp:1000    |
| CT-TURBO-04  | 48K, full Emulator, NR 0x07=0x01 → bank-5 read — zero added         | zxnext.vhd:5787-5790,5817           | pass   | test/contention/contention_test.cpp:1024    |
| CT-TURBO-05  | 48K, full Emulator, NR 0x08 bit 6=1 → bank-5 read — zero added      | zxnext.vhd:4481,5823                | pass   | test/contention/contention_test.cpp:1074    |
| CT-TURBO-06  | NR 0x08 bit 6 mid-line write — `hc(8)` rising-edge commit gate      | zxnext.vhd:5822-5823                | pass   | test/contention/contention_test.cpp:1139    |
| CT-TURBO-07  | NR 0x07 bus-idle commit edge (G142)                                 | zxnext.vhd:5796-5828                | pass   | test/contention/contention_test.cpp:1206    |
| CT-TURBO-08  | NR 0x08+0x07 combined commit ordering (G51)                         | zxnext.vhd:5796-5828, 5822-5823     | pass   | test/contention/contention_test.cpp:2181    |
| CT-FB-01     | +3, mem read bank 4 — `p3_floating_bus_dat` equals byte read        | zxnext.vhd:4498-4505                | pass   | test/contention/contention_test.cpp:1250    |
| CT-FB-02     | +3, mem write bank 4 — latch equals byte written                    | zxnext.vhd:4498-4508                | pass   | test/contention/contention_test.cpp:1280    |
| CT-FB-03     | +3, pre-seed via contended write, then bank-0 read — latch held     | zxnext.vhd:4498-4501                | pass   | test/contention/contention_test.cpp:1306    |
| CT-FB-04     | +3, I/O read (no MREQ) — latch unchanged (capture gated on MREQ)    | zxnext.vhd:4501                     | pass   | test/contention/contention_test.cpp:1335    |
| CT-INT-01    | 48K, HALT-loop 1-frame, contention ON — frame T-states match LUT    | zxula.vhd:582-595, zxula_timing.vhd | pass   | test/contention/contention_test.cpp:1381    |
| CT-INT-02    | 48K, same program, contention OFF via NR 0x08 bit 6 — 69888 baseline| zxnext.vhd:4481,5823                | pass   | test/contention/contention_test.cpp:1415    |
| CT-INT-03    | Regression screenshot — 48K contention-sensitive demo               | —                                   | pass   | test/contention/contention_test.cpp:1494    |
| CT-FUSE-01   | 48K, `LD A,(0x4000)` from page 0x0A — M1 fetch contention (G141)    | zxula.vhd:583,595; z80_macros.h:109 | pass   | test/contention/contention_test.cpp:1874    |
| CT-FUSE-02   | 48K, `LDIR` over page 0x0A — no-MREQ tail contention (G141)         | zxula.vhd:583,595; z80_macros.h:118 | pass   | test/contention/contention_test.cpp:1952    |
| CT-FUSE-03   | 48K, `OUT (0xFE),A` in display window — port-write contention (real, implementable gap — CT-IO-\*/CT-INT-01 do NOT cover it, see plan doc §16; on=2995/off=2806/delta=189 T-states confirmed constructible) | zxula.vhd:595, zxnext.vhd:4496      | missing | missing                                     |
| CT-FUSE-04   | 48K, `IN A,(0xFE)` in display window — port-read contention (same reasoning as CT-FUSE-03 for the read side, see plan doc §16) | zxula.vhd:595, zxnext.vhd:4496      | missing | missing                                     |
| CT-FUSE-05   | FUSE-table retirement bypass-toggle (G53)                           | zxnext.vhd:4481                       | pass   | test/contention/contention_test.cpp:2057    |
| CT-DELAY-01  | Full-frame integration drift bound — 48K/128K/+3 ∈ (0, 6·N]; Pent=0 | zxula.vhd:582-595, zxnext.vhd:4481-4492 | pass   | test/contention/contention_test.cpp:1599    |


## LoRes — `test/lores/lores_test.cpp`

The LoRes layer (`src/video/lores.{h,cpp}`), 128x96 8-bit and Radastan 4-bit.
Plan: [LORES-TEST-PLAN-DESIGN.md](LORES-TEST-PLAN-DESIGN.md), whose row titles
are the Description column below. LoRes is **not** a layer of its own — it
substitutes the ULA-slot pixel (`zxnext.vhd:6980`), which is why its rows are
about address generation and the NR `$15` b7 / `$32` / `$33` / `$6A` inputs
rather than about compositing.

The plan has 91 rows; this suite implements the 48 that are reachable from the
`LoRes` class alone. Two more need a running CPU and the real memory-timing
model and live in the companion suite below; the remainder are compositor- or
demo-level and are recorded in the sections that own them.

| Test ID | Plan row title                                                                                   | VHDL file:line                        | Status  | Test file:line                |
|---------|--------------------------------------------------------------------------------------------------|---------------------------------------|---------|-------------------------------|
| LR-100  | X scroll advances the source column                                                              | lores.vhd:82,91                       | pass    | test/lores/lores_test.cpp:472 |
| LR-101  | The X-scroll LSB is discarded in 8-bit mode                                                      | lores.vhd:82,91                       | pass    | test/lores/lores_test.cpp:482 |
| LR-102  | X scroll wraps mod 256 (= mod 128 LoRes pixels)                                                  | lores.vhd:82                          | pass    | test/lores/lores_test.cpp:490 |
| LR-103  | X scroll wraps within the row, never into the next row                                           | lores.vhd:82,91                       | pass    | test/lores/lores_test.cpp:500 |
| LR-104  | Radastan X scroll granularity: the low two bits select nibble then byte                          | lores.vhd:82,96,106                   | pass    | test/lores/lores_test.cpp:516 |
| LR-105  | Y scroll advances the source row                                                                 | lores.vhd:84-87,91                    | pass    | test/lores/lores_test.cpp:527 |
| LR-106  | The Y-scroll LSB is discarded away from the wrap boundary                                        | lores.vhd:86-87,91                    | pass    | test/lores/lores_test.cpp:534 |
| LR-107  | Y scroll wraps mod 192                                                                           | lores.vhd:84-87                       | pass    | test/lores/lores_test.cpp:542 |
| LR-108  | Y wrap is exact across the whole legal range                                                     | lores.vhd:84-87                       | pass    | test/lores/lores_test.cpp:560 |
| LR-109  | **Hardware quirk**: for `vc + scroll_y >= 384` the wrap is wrong                                 | lores.vhd:84-87                       | pass    | test/lores/lores_test.cpp:570 |
| LR-110  | The quirk's address consequence: the 3-bit `+1` field wraps to 0                                 | lores.vhd:93-94                       | pass    | test/lores/lores_test.cpp:582 |
| LR-111  | Scroll registers are independent                                                                 | lores.vhd:82,84,91                    | pass    | test/lores/lores_test.cpp:590 |
| LR-120  | LoRes IS clipped by NR `$1A` — it is not exempt                                                | lores.vhd:115, zxnext.vhd:4258-4261   | pass    | test/lores/lores_test.cpp:618 |
| LR-121  | Clip X bounds are inclusive at both ends                                                         | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:630 |
| LR-122  | Clip Y bounds are inclusive at both ends                                                         | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:640 |
| LR-123  | Clip X is in 256-pixel display units (it indexes `phc`), so a clip edge can fall mid-LoRes-pixel | lores.vhd:115, zxnext.vhd:4250        | pass    | test/lores/lores_test.cpp:656 |
| LR-125  | An inverted X window (`x1 > x2`) draws nothing                                                   | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:671 |
| LR-126  | An inverted Y window (`y1 > y2`) draws nothing                                                   | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:679 |
| LR-160  | NR `$26` / `$27` (ULA scroll) do not move the LoRes image                                        | lores.vhd:82,84, zxnext.vhd:4241-4271 | pass    | test/lores/lores_test.cpp:730 |
| LR-23   | `pixel_en` is 0 for `phc >= 256`                                                                 | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:150 |
| LR-24   | `pixel_en` is 0 for `vc >= 192` at default clip                                                  | lores.vhd:115, zxnext.vhd:4974        | pass    | test/lores/lores_test.cpp:163 |
| LR-25   | `pixel_en` is 1 at the display corners                                                           | lores.vhd:115                         | pass    | test/lores/lores_test.cpp:177 |
| LR-40   | Top-left LoRes pixel reads bank-5 offset 0                                                       | lores.vhd:91                          | pass    | test/lores/lores_test.cpp:195 |
| LR-41   | Row stride is 128 bytes                                                                          | lores.vhd:91                          | pass    | test/lores/lores_test.cpp:201 |
| LR-42   | Column stride is 1 byte per 2 display pixels                                                     | lores.vhd:91                          | pass    | test/lores/lores_test.cpp:207 |
| LR-43   | A LoRes pixel is a 2×2 block of display pixels                                                  | lores.vhd:91                          | pass    | test/lores/lores_test.cpp:214 |
| LR-44   | Last byte of the top half                                                                        | lores.vhd:91,93                       | pass    | test/lores/lores_test.cpp:222 |
| LR-45   | First byte of the bottom half skips the attribute area                                           | lores.vhd:93-94                       | pass    | test/lores/lores_test.cpp:228 |
| LR-46   | Last byte of the bottom half                                                                     | lores.vhd:91,93                       | pass    | test/lores/lores_test.cpp:234 |
| LR-47   | No address in `0x1800-0x1FFF` is ever generated in 8-bit mode                                    | lores.vhd:93-94                       | pass    | test/lores/lores_test.cpp:252 |
| LR-48   | The half-select uses the **scrolled** y, not `vc`                                                | lores.vhd:86-87,93                    | pass    | test/lores/lores_test.cpp:259 |
| LR-51   | In 8-bit mode the Timex display-file bit and NR `$6A` bit 4 have no effect                       | lores.vhd:96,98                       | pass    | test/lores/lores_test.cpp:281 |
| LR-60   | Radastan row stride is 64 bytes                                                                  | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:297 |
| LR-61   | Two LoRes pixels per byte                                                                        | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:303 |
| LR-62   | `x(1) = 0` selects the HIGH nibble (left pixel of the pair)                                      | lores.vhd:106                         | pass    | test/lores/lores_test.cpp:314 |
| LR-63   | `dfile = 0` bases the image at offset 0                                                          | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:323 |
| LR-64   | `dfile = 1` bases the image at offset `0x2000`                                                   | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:329 |
| LR-65   | Radastan applies **no** `+0x800` correction at row 48                                            | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:335 |
| LR-67   | The Radastan image is 6144 bytes, contiguous within its half                                     | lores.vhd:96                          | pass    | test/lores/lores_test.cpp:352 |
| LR-68   | Switching NR `$6A` bit 5 switches the address generator, nothing else latched                    | lores.vhd:98                          | pass    | test/lores/lores_test.cpp:370 |
| LR-80   | 8-bit: offset adds to the HIGH nibble only                                                       | lores.vhd:102,111                     | pass    | test/lores/lores_test.cpp:403 |
| LR-81   | 8-bit: the high-nibble add wraps at 4 bits with no carry out                                     | lores.vhd:102                         | pass    | test/lores/lores_test.cpp:409 |
| LR-82   | 8-bit: the low nibble is passed through untouched by any offset                                  | lores.vhd:111                         | pass    | test/lores/lores_test.cpp:415 |
| LR-83   | 8-bit: offset 0 is the identity                                                                  | lores.vhd:102                         | pass    | test/lores/lores_test.cpp:421 |
| LR-84   | Radastan: high nibble **is** the offset (not an add)                                             | lores.vhd:107,111                     | pass    | test/lores/lores_test.cpp:426 |
| LR-85   | Radastan: with ULA+ enabled the high nibble becomes `"11" & offset(1:0)`                         | lores.vhd:107                         | pass    | test/lores/lores_test.cpp:432 |
| LR-86   | Radastan + ULA+: offset bits 3:2 are ignored                                                     | lores.vhd:107                         | pass    | test/lores/lores_test.cpp:439 |
| LR-88   | The offset never affects `pixel_en`                                                              | lores.vhd:111,115                     | pass    | test/lores/lores_test.cpp:457 |

### Companion integration suite — `test/lores/lores_integration_test.cpp`

The two LoRes plan rows that assert the ABSENCE of an effect on the CPU-visible
side of the machine, and therefore need a full 128K emulator with contention
and a floating bus rather than the bare `LoRes` class. Both follow from one
hardware fact: the LoRes fetch uses bank 5's dual-port BRAM **port A**
(`zxnext.vhd:6603-6631`) while ULA contention and the floating bus are
functions of the ULA's own **port B** fetch.

| Test ID | Plan row title                                        | VHDL file:line                      | Status  | Test file:line                            |
|---------|-------------------------------------------------------|-------------------------------------|---------|-------------------------------------------|
| LR-163  | Enabling LoRes does not change ULA memory contention  | zxula.vhd:583, zxnext.vhd:6603-6631 | pass    | test/lores/lores_integration_test.cpp:152 |
| LR-164  | Enabling LoRes does not change the floating-bus value | zxula.vhd:573                       | pass    | test/lores/lores_integration_test.cpp:200 |

## SD Card — `test/sdcard/sdcard_test.cpp`

The `test/sdcard/sdcard_test.cpp` suite was added when Task 7 introduced
the `skip()` helper here, exercising the `SdCardDevice` SPI-mode state
machine directly against a tiny temporary raw-image file so the
byte-accurate SPI pipeline delivered to the host can be asserted
deterministically. The fixture builds a 16-sector image with
distinctive per-sector identity bytes, then drives CMD0/CMD8/CMD55/
ACMD41/CMD58 init, CMD17 single-block reads, CMD18 multi-block streams
(including end-of-image, CMD12 abort, and CS-deassert abort) and
records skips for SD-spec corners that the device does not yet model
(see G40, G41, G158, G159, G160). No dedicated test plan exists; the
rows below are extracted directly from the test source. SD behaviour
is governed by the SD Physical Layer 2.00 spec (external), so VHDL
anchors are `—` for most rows; the boot/hot-plug rows touch the
emulator-side SD plumbing only.

Dashboard (Task 7 r2): 21 total / 8 pass / 0 fail / 13 skip.

| Test ID    | Plan row title                                                              | VHDL file:line                      | Status | Test file:line                  |
|------------|-----------------------------------------------------------------------------|-------------------------------------|--------|---------------------------------|
| INIT-01    | CMD0 returns R1=0x01 (in-idle) before ACMD41                                | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:171 |
| INIT-02    | After CMD0/8/55/41/58 init, CMD17 R1=0x00 (ready)                           | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:179 |
| CMD17-01   | CMD17 sector=1 returns correct first 4 sector-identity bytes                | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:205 |
| CMD18-01   | CMD18 first block sector=3 has identity bytes (b[0]=0x03)                   | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:249 |
| CMD18-02   | CMD18 streamed blocks cover sector+1 and +2                                 | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:257 |
| CMD18-03   | CMD12 aborts stream cleanly; subsequent CMD17 works                         | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:265 |
| CMD18-04   | CS deassert during stream aborts cleanly; CMD17 afterwards works            | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:344 |
| CMD18-05   | End-of-image (sectors 14..15) terminates cleanly; follow-up CMD17 works     | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:305 |
| SD-01      | CMD0 CRC validation + CMD59 toggle absent (see G159)                        | (SD SPI spec)                         | missing | missing                         |
| SD-02      | CMD13 returns R1 fall-through, not R2 (see G160)                            | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:512 |
| SD-10      | CMD9 SEND_CSD (**RE-HOMED** — CMD9 IS implemented, `cmd9_send_csd()`; covered by SD-NAC-04; see G40) | (SD SPI spec)                         | missing | missing                         |
| SD-11      | CMD10 SEND_CID (**RE-HOMED** — CMD10 IS implemented, `cmd10_send_cid()`; covered by SD-NAC-05 + V24-DIVMMC-01 (CID MDT); see G40) | (SD SPI spec)                         | missing | missing                         |
| SD-12      | CMD16 SET_BLOCKLEN ack absent (see G40)                                     | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:535 |
| SD-13      | CMD23 SET_BLOCK_COUNT not handled (see G40)                                 | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:558 |
| SD-14      | CMD24 WRITE_BLOCK absent — read-only fixture (see G40)                      | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:891 |
| SD-15      | CMD25 multi-block write not modelled (see G40)                              | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:1113 |
| MMC-01     | MMC CMD1 init path absent — SDHC only (see G41)                             | (SD SPI spec)                         | pass   | test/sdcard/sdcard_test.cpp:941 |
| MMC-02     | CMD8 illegal-cmd response on MMC not modelled (see G41)                     | (SD SPI spec)                         | missing | missing                         |
| MMC-03     | MMC byte-vs-block addressing duality unsupported (see G41)                  | (SD SPI spec)                         | missing | missing                         |
| BOOT-SD-01 | SD hot-plug round-trip not exposed at runtime (see G158)                    | zxnext.vhd:sd pin routing           | pass   | test/sdcard/sdcard_test.cpp:1006 |
| BOOT-SD-02 | SD unmount mid-transfer untested — no GUI/CLI eject affordance (see G158)   | zxnext.vhd:sd pin routing           | pass   | test/sdcard/sdcard_test.cpp:2191 |


## NMI Source Pipeline — `test/nmi/nmi_test.cpp` + `test/nmi/nmi_integration_test.cpp`

Last-touch commit: `886b4c233a7e00885e469ae783073f4f16b04c24` (`886b4c233a`)

NMI Source Pipeline plan (`doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md`) closed end-to-end 2026-04-24d (per `project_session_handover_20260424d_eod.md` — Phase 1 scaffold + Wave A NR 0x02 + Wave B HK/DIS/CLR + Wave C gate registers + Wave E NMI-activated DMA delay all landed; 5 real emulator bugs fixed). The plan was reopened by Task 7 r1 + r2 (2026-04-24/25) which added skip rows for the residual gaps it surfaced — specifically G87/G88 (NMIACK PC-capture cross-link to NR 0xC2/0xC3 and CTC owner), G152/G153 (host F-key dispatch + NR 0x02 reset_type FSM bits 1:0), and G162 (iotrap strobe + Multiface port 0x2FFD/0x3FFD trap, parked until `MULTIFACE-TEST-PLAN-DESIGN.md` is authored). Companion integration suite `test/nmi/nmi_integration_test.cpp` runs the full button/software-NMI chain through a real `Emulator` fixture (NmiSource → arbiter strobe → DivMmc::set_button_nmi → cpu request_nmi → PC=0x0066 → automap → RETN clear); its 4 skips track the same G152 host-hotkey wiring debt at the GUI level. `nmi_test.cpp` runs at `54 / 32 pass / 0 fail / 22 skip` (Z80-04 re-homed to CTC plan 2026-04-28 — duplicate cross-link with NR-C2-01/NR-C3-01) and `nmi_integration_test.cpp` at `9 / 5 / 0 / 4`.

| Test ID         | Plan row title                                                                                | VHDL file:line                                  | Status | Test file:line                              |
|-----------------|-----------------------------------------------------------------------------------------------|-------------------------------------------------|--------|---------------------------------------------|
| NMI-RST-01 | Reset defaults: FSM idle + latches clear + gates off + nmi_generate_n high + not activated    | zxnext.vhd:2120,2149 / 2095-2105 / 1109-1110,1222 / 2164-2170 / 2107 | pass   | test/nmi/nmi_test.cpp:131                   |
| NMI-RST-04 | NR 0x02 reset_type[2:0] power-on default = "100"                                              | zxnext.vhd:1306, 5891                           | pass   | test/nmi/nmi_test.cpp:147                   |
| NR02-01         | NR 0x02 bit 3 write sets nmi_mf latch                                                         | zxnext.vhd:3832,3837,2097                       | pass   | test/nmi/nmi_test.cpp:189                   |
| NR02-02         | NR 0x02 bit 2 write sets nmi_divmmc latch                                                     | zxnext.vhd:3833,3838,2099                       | pass   | test/nmi/nmi_test.cpp:206                   |
| NR02-03         | Simultaneous bit 3+2 write: MF wins, DivMMC blocked                                           | zxnext.vhd:2097-2105                            | pass   | test/nmi/nmi_test.cpp:224                   |
| NR02-04         | NR 0x02 readback bit 3 = 1 after bit-3 write                                                  | zxnext.vhd:5891                                 | pass   | test/nmi/nmi_test.cpp:244                   |
| NR02-05         | NR 0x02 readback bits 3/2 auto-clear at FSM S_NMI_END                                         | zxnext.vhd:5891, 2149-2162                      | pass   | test/nmi/nmi_test.cpp:282                   |
| NR02-06         | MF-enable gate OFF blocks nmi_mf latch on NR 0x02 bit 3 write                                 | zxnext.vhd:2090                                 | pass   | test/nmi/nmi_test.cpp:308                   |
| NR02-07         | reset_type FSM advance on soft-reset rising edge                                              | zxnext.vhd:1732-1739                            | pass   | test/nmi/nmi_test.cpp:335                   |
| NR02-08         | NR 0x02 readback bits 1:0 reflect reset_type[1:0]                                             | zxnext.vhd:5891                                 | pass   | test/nmi/nmi_test.cpp:367                   |
| NR02-INT-01     | NR 0x02 write via OUT 0x253B routes to NmiSource (Wave A handler)                             | zxnext.vhd:3833,3838,2099                       | pass   | test/nmi/nmi_test.cpp:421                   |
| HK-01           | set_mf_button edge + NR 0x06 bit 3 = 1 → FSM IDLE→FETCH (MF latched)                          | zxnext.vhd:2090 / 2095-2116 / 2124-2128         | pass   | test/nmi/nmi_test.cpp:475                   |
| HK-02           | set_divmmc_button edge + NR 0x06 bit 4 = 1 → FSM IDLE→FETCH (DivMMC latched)                  | zxnext.vhd:2091 / 2095-2116 / 2124-2128         | pass   | test/nmi/nmi_test.cpp:490                   |
| HK-03           | NR 0x06 bit 3 = 0 blocks MF producer (no FSM advance, no latch)                               | zxnext.vhd:1110 / 2090                          | pass   | test/nmi/nmi_test.cpp:505                   |
| HK-04           | NR 0x06 bit 4 = 0 blocks DivMMC producer (no FSM advance, no latch)                           | zxnext.vhd:1109 / 2091                          | pass   | test/nmi/nmi_test.cpp:519                   |
| HK-05           | Simultaneous MF + DivMMC press, both gates enabled: MF wins priority                          | zxnext.vhd:2107-2113                            | pass   | test/nmi/nmi_test.cpp:538                   |
| HK-06           | Host F9 → NmiSource MF producer dispatch                                                      | zxnext.vhd:6340-6349, 2089-2091                 | pass   | test/nmi/nmi_test.cpp:559                   |
| HK-07           | Host F10 → NmiSource DivMMC producer dispatch                                                 | zxnext.vhd:6340-6349, 2089-2091                 | pass   | test/nmi/nmi_test.cpp:576                   |
| HK-07b          | F10 DivMMC NMI honours the port_divmmc_io_en gate (NR 0x83 b0)                                | zxnext.vhd:6349                                 | pass   | test/nmi/nmi_test.cpp:597                   |
| HK-08           | Host F4 → soft-reset / reset_type FSM dispatch                                                | zxnext.vhd:6340-6349, 1732-1739                 | pass   | test/nmi/nmi_test.cpp:631                   |
| HK-09           | Host F1 → hard-reset Emulator dispatch                                                        | zxnext.vhd:6340-6349                            | pass   | test/nmi/nmi_test.cpp:650                   |
| Z80-04          | NR 0xC2/0xC3 NMIACK PC capture (LSB/MSB latches) — RE-HOMED 2026-04-28 to CTC plan (NR-C2-01/NR-C3-01) | zxnext.vhd:2050-2085, 6232-6236                 | missing | missing                                         |
| MF-G162-01      | iotrap strobe OR'd into MF assert (nmi_sw_gen_mf includes nmi_gen_iotrap)                     | zxnext.vhd:3835-3837                            | pass   | test/nmi/nmi_test.cpp:708                   |
| MF-G162-01b     | iotrap MF assert honours the NR 0x06 b3 gate (no latch when off)                              | zxnext.vhd:2090                                 | pass   | test/nmi/nmi_test.cpp:719                   |
| MF-G162-02      | Port 0x2FFD/0x3FFD trap-decode handler                                                        | zxnext.vhd:3835-3837                            | pass   | test/nmi/nmi_test.cpp:765                   |
| MF-G48-01       | Mode-decoded MF port table per nr_0a_mf_type                                                  | multiface.vhd                                   | pass   | test/nmi/nmi_test.cpp:822                   |
| MF-G48-02       | NR 0x0A b7:6 nr_0a_mf_type forward to MF type                                                 | multiface.vhd                                   | pass   | test/nmi/nmi_test.cpp:849                   |
| MF-G48-03       | port_io_dly edge detector                                                                     | multiface.vhd:122-131                           | pass   | test/nmi/nmi_test.cpp:891                   |
| MF-G48-04       | INVISIBLE FF (multiface invisible flip-flop)                                                  | multiface.vhd:152-163                           | pass   | test/nmi/nmi_test.cpp:929                   |
| MF-G48-05       | MF +3 port 0x1FFD/0x7FFD readback mux on cpu_a(15:12)                                         | multiface.vhd                                   | pass   | test/nmi/nmi_test.cpp:976                   |
| MF-G48-06       | DivMMC retn_seen AND-NOT mf_is_active gate                                                    | multiface.vhd                                   | pass   | test/nmi/nmi_test.cpp:1039                  |
| MF-G48-07       | Port 0xDFFD bit 6 storage for MF readback                                                     | multiface.vhd                                   | pass   | test/nmi/nmi_test.cpp:1082                  |
| BOOT-LOOP-01    | NextZXOS RAM-test outer loop (208 passes × 112 banks over ~15 s) (**COVERED AT regression tier** — `boot-nextzxos-welcome`, test/00regression/regression_tests.conf; the RAM-test loop must complete and fall through to BASIC before that screenshot renders, Task 8a 2026-07-13 — see doc/testing/TEST-TAXONOMY.md Layer 1; no `check()`/`skip()` row exists) | n/a (end-to-end behavioural)                    | missing | missing                                     |
| BOOT-LOGO-01    | NextZXOS loader logo + 4-entry log render (**COVERED AT regression tier** — `boot-nextzxos-splash`, test/00regression/regression_tests.conf; pins the clean loading log at frame 252, Task 8a 2026-07-13 — see doc/testing/TEST-TAXONOMY.md Layer 1; no `check()`/`skip()` row exists) | n/a (rendering)                                 | missing | missing                                     |
| BOOT-DOT-01     | NextZXOS BASIC + dot-command surface (**COVERED AT regression tier** — `boot-nextzxos-dotls`, test/00regression/regression_tests.conf; types `.ls` in the NextZXOS Command Line and pins the SD-root listing, Task 57 2026-07-14, closes G47 — see doc/testing/TEST-TAXONOMY.md Layer 1; no `check()`/`skip()` row exists) | n/a (end-to-end)                                | missing | missing                                     |
| BYPASS-FAT-01   | Host-side FAT32 reader for direct enNextZX.rom load (**WONT** — `--bypass-tbblue-fw` (G59), the only consumer needing a direct host-side load of enNextZX.rom, was removed from src/ 2026-07-11 per explicit user decision, EMULATOR-DESIGN-PLAN.md Phase 10; native firmware-faithful boot loads that ROM via the emulated SD/SPI path instead; no `check()`/`skip()` row exists) | n/a (host-side)                                 | missing | missing                                     |
| BYPASS-INI-01   | config.ini / menu.ini / menu.def parser (**WONT** — scoped exclusively as a dependency of G59/BYPASS-FAT-01, removed 2026-07-11; no feature left to implement; no `check()`/`skip()` row exists) | n/a (host-side)                                 | missing | missing                                     |
| DIS-01          | FSM IDLE→FETCH for DivMMC path pulses nmi_divmmc_button → DivMmc::set_button_nmi(true)        | zxnext.vhd:2170 / divmmc.vhd:108-111            | pass   | test/nmi/nmi_test.cpp:1268                  |
| DIS-02          | DivMmc automap_held=1 → is_nmi_hold()=1 → NmiSource divmmc_nmi_hold=1                         | divmmc.vhd:150 / zxnext.vhd:2107,2118           | pass   | test/nmi/nmi_test.cpp:1297                  |
| DIS-03          | is_nmi_hold() = automap_held OR button_nmi across {00,10,01,11}                               | divmmc.vhd:150                                  | pass   | test/nmi/nmi_test.cpp:1338                  |
| DIS-04          | FSM HOLD → END when divmmc_nmi_hold transitions to 0                                          | zxnext.vhd:2118 / 2135-2148 / divmmc.vhd:150    | pass   | test/nmi/nmi_test.cpp:1374                  |
| CLR-01          | reset() clears button_nmi_                                                                    | divmmc.vhd:108 (i_reset)                        | pass   | test/nmi/nmi_test.cpp:1397                  |
| CLR-02          | set_enabled(true→false) edge (i_automap_reset) clears button_nmi_                             | divmmc.vhd:108 / zxnext.vhd:4112                | pass   | test/nmi/nmi_test.cpp:1418                  |
| CLR-03          | on_retn_seen() clears button_nmi_                                                             | divmmc.vhd:108 (i_retn_seen)                    | pass   | test/nmi/nmi_test.cpp:1436                  |
| CLR-04          | automap_held rising edge clears button_nmi_                                                   | divmmc.vhd:112-113                              | pass   | test/nmi/nmi_test.cpp:1469                  |
| NMI-GATE-01 | NR 0x06 bit 3 decode sets NmiSource::mf_enable()                                              | zxnext.vhd:1110                                 | pass   | test/nmi/nmi_test.cpp:1515                  |
| NMI-GATE-02 | NR 0x06 bit 4 decode sets NmiSource::divmmc_enable()                                          | zxnext.vhd:1109                                 | pass   | test/nmi/nmi_test.cpp:1530                  |
| NMI-GATE-03 | NR 0x81 bit 5 decode sets NmiSource::expbus_debounce_disable()                                | zxnext.vhd:1222                                 | pass   | test/nmi/nmi_test.cpp:1545                  |
| GATE-04         | CONMEM=1 blocks MF latch even with enable+button set                                          | zxnext.vhd:2107 (port_e3_reg(7) gate)           | pass   | test/nmi/nmi_test.cpp:1569                  |
| GATE-05         | mf_is_active=1 blocks DivMMC latch even with enable+button set                                | zxnext.vhd:2099 (mf_is_active gate)             | pass   | test/nmi/nmi_test.cpp:1590                  |
| GATE-06         | config_mode=1 force-clears all three priority latches                                         | zxnext.vhd:2102-2105                            | pass   | test/nmi/nmi_test.cpp:1616                  |
| GATE-07         | config_mode=1 force-clears FSM to Idle from any state                                         | zxnext.vhd:2102-2105                            | pass   | test/nmi/nmi_test.cpp:1636                  |
| GATE-08         | Power-on gate flags (mf_en, divmmc_en, expbus_debounce_dis, config_mode) all false            | zxnext.vhd:1109-1110 / 1222 / NR 0x03           | pass   | test/nmi/nmi_test.cpp:1657                  |
| NMI-DMA-01 | is_activated() true while any NMI latch is set                                                | zxnext.vhd:2107                                 | pass   | test/nmi/nmi_test.cpp:1765                  |
| NMI-DMA-02 | im2_dma_delay latches when is_activated() AND nr_cc_dma_int_en_0_7                            | zxnext.vhd:2007                                 | pass   | test/nmi/nmi_test.cpp:1789                  |
| NMI-DMA-03 | NR 0xCC bit 7 = 0 (or nmi_activated=0) blocks NMI-driven DMA delay                            | zxnext.vhd:2007                                 | pass   | test/nmi/nmi_test.cpp:1818                  |

### Companion integration suite — `test/nmi/nmi_integration_test.cpp`

End-to-end NMI chain on a real `Emulator` fixture (NmiSource FSM → arbitration strobe → `DivMmc::set_button_nmi` → `cpu_.request_nmi()` → Z80 jumps to 0x0066 → automap activates → RETN clears latches). The 4 host-hotkey skips mirror the `nmi_test.cpp` HK-06..09 rows at the integration tier (G152 — GUI F-key dispatch is not wired). Runtime: `Total:    9  Passed:    5  Failed:    0  Skipped:    4`.

| Test ID    | Plan row title                                                                                            | VHDL file:line                                          | Status | Test file:line                                  |
|------------|-----------------------------------------------------------------------------------------------------------|---------------------------------------------------------|--------|-------------------------------------------------|
| NMI-INT-01 | DivMMC button → NmiSource FSM → /NMI → Z80 PC=0x0066                                                      | zxnext.vhd:2091, 2095-2170, 1841                        | pass    | test/nmi/nmi_integration_test.cpp:186           |
| NMI-INT-02 | At PC=0x0066 after NMI + one M1 fetch, DivMmc automap instant-on fires via button_nmi latch               | divmmc.vhd:120 / zxnext.vhd:2170                        | pass    | test/nmi/nmi_integration_test.cpp:225           |
| NMI-INT-03 | RETN (ED 45) clears DivMmc button_nmi_                                                                    | divmmc.vhd:108 (i_retn_seen)                            | pass    | test/nmi/nmi_integration_test.cpp:262           |
| NMI-INT-04 | NR 0x02 bit 2 write via OUT 0x253B → NMI → PC=0x0066                                                      | zxnext.vhd:3833, 3838, 2091, 2095-2170, 1841            | pass    | test/nmi/nmi_integration_test.cpp:286           |
| NMI-INT-05 | NR 0x02 bit 3 (MF sw-NMI) with NR 0x06 bit 3 set → MF latches, /NMI falls, Z80 PC=0x0066                  | zxnext.vhd:2090, 2095-2170, 1841 (MF feedback → Task 8) | pass    | test/nmi/nmi_integration_test.cpp:313           |
| HK-06-INT  | GUI F9 → NmiSource MF producer end-to-end                                                                 | zxnext.vhd:6348,2090,2095-2170,1841                       | pass    | test/nmi/nmi_integration_test.cpp:362           |
| HK-07-INT  | GUI F10 → NmiSource DivMMC producer end-to-end                                                            | zxnext.vhd:6349,2091,2095-2170,1841                       | pass    | test/nmi/nmi_integration_test.cpp:389           |
| HK-08-INT  | GUI F4 → soft-reset / reset_type FSM end-to-end                                                           | zxnext.vhd:6370,1732-1739,1102                            | pass    | test/nmi/nmi_integration_test.cpp:440           |
| HK-09-INT  | GUI F1 → hard-reset Emulator path end-to-end                                                              | zxnext.vhd:6371,1109-1110,2154-2155                       | pass    | test/nmi/nmi_integration_test.cpp:475           |

### Extended/self-streaming NEX — `test/core/extended_nex_test.cpp`

Additive GH #29/#84 coverage uses runtime-generated files only. Runtime:
`Total: 27 Passed: 27 Failed: 0 Skipped: 0`. The functional witness
`extended-nex-stream-func` additionally executes synthetic Z80 code through
the file API, NextZXOS streaming API, port `$EB`, and raw CMD18 paths.

**This table is NOT REFRESHED** by `test/refresh-traceability-matrix.pl`, and
every cell in it is hand-written. It has no `Test file:line` column and its
reference column is prose, not a VHDL citation; its `Test ID` column names
RANGES (`XNEX-01..04`), which no ID lookup can resolve. Nothing here is
computed — read it as an editorial summary, and re-check it by hand (GH #192).

| Test IDs | Behavior | Contract/source reference | Status |
|---|---|---|---|
| XNEX-01..04 | Generated NEX parsing, exact payload boundary, and closed-file form | NEX header offset 140; GH #29 | pass |
| XNEX-05..08 | Register handle, seek/read, position, and file metadata | NextZXOS `F_SEEK`, `F_READ`, `F_FGETPOS`, `F_FSTAT` | pass |
| XNEX-09..13 | Read-only same-directory companion sandbox | Host bridge security contract; GH #84 `ATICATAC.CFG` | pass |
| XNEX-14..16 | File map and port `$EB` stream start/end | NextZXOS `DISK_FILEMAP`, `DISK_STRMSTART`, `DISK_STRMEND` | pass |
| XNEX-17 | Memory-address file-handle delivery | NEX header offset 140 values `$4000..$FFFF` | pass |
| XNEX-18..20 | Sector framing, interleaved-handle isolation, final-sector padding, and file-map refill | NextZXOS streaming contract | pass |
| XNEX-21..23 | Initialized direct-load SDHC state and synthetic raw CMD18 overlay | SD SPI CMD17/CMD18; GH #84 | pass |
| XNEX-24 | Post-init GUI NEX load activates the otherwise-dormant host bridge | JNext File → Open lifecycle | pass |
| XNEX-25..27 | GUI/CLI soft and hard resets disarm host calls, streaming state, and synthetic SD overlays | Direct-NEX lifecycle isolation; GH #29/#84 | pass |

### Atic Atac Next NMI regressions — `test/nmi/atic_atac_nmi_test.cpp`

Additive GH #84 coverage using only runtime-generated fixtures. Runtime:
`Total: 4 Passed: 4 Failed: 0 Skipped: 0`.

**This table is NOT REFRESHED** by `test/refresh-traceability-matrix.pl`, and
every cell in it is hand-written. It has no `Test file:line` column and its
reference column is prose, not a VHDL citation, so nothing here is computed —
read it as an editorial summary, and re-check it by hand (GH #192).

| Test ID | Behavior | RTL/source reference | Status |
|---|---|---|---|
| ATIC-NMI-01 | Config-mode writes to physical SRAM page `$08` supply the DivMMC `$0066` handler | zxnext.vhd DivMMC ROM SRAM mapping; divmmc.vhd:120 | pass |
| ATIC-NMI-02 | NR `$C0` stackless NMI suppresses stack RAM cycles, captures C3:C2, and RETN uses the live pair | zxnext.vhd:2050-2085, 6229-6236 | pass |
| ATIC-NMI-03 | DivMMC clears after RETN executes and before the returned opcode is predecoded | im2_control.vhd:236; divmmc.vhd:108,126,139 | pass |
| ATIC-NMI-04 | Multiface clears at the same completed-instruction boundary | multiface.vhd:144,178 | pass |

## Discrepancies noted

- **Z80N**: the Z80N suite is a FUSE-style data-driven runner (`test/z80n_test.cpp` parses `tests.in`/`tests.expected`) and has no in-source `check()` calls. The plan row identifiers used here are the Z80N opcodes from the coverage table (lines 70–100 of the plan doc); they are the natural grouping, not literal test IDs. Every row shows as `missing` in the "Test file:line" column because the opcode tokens do not appear as `check()` IDs. Coverage is verified by the runner's overall pass/fail count.

- **Memory/MMU, ULA Video, Tilemap, Audio, DMA, DivMMC+SPI, CTC+Interrupts, UART+I2C/RTC, NextREG**: these older rewrites do not use the Phase 2 `skip()` helper, so the Status column is reported as `—` where a `check()` exists. Running the binaries is required to populate pass/fail.

- **Per-row pass/fail is not computed anywhere** because this pass is read-only (no build, no test run). Even the 6 Phase 2 subsystems report `pass` where a `check()` exists and `skip` where a `skip()` exists; actual runtime fails would only show as `fail` if the test was executed.

- **Total extra-coverage rows** across all subsystems: 79.
