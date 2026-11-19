# GH #371 pause notes (2026-11-19 ~09:15)

Branch gh371-gfx-viewer, base main 2c2012341. Stage 2 APPROVED by the owner (2026-11-19 ~07:50) and implemented.

## Done and verified
- Stage 1 (decoders, gfx_decode_test GFXD-01..19, GraphicsPanel, window wiring, QGFX-01..14, GR-05 8 -> 9, docs, docshot, images, matrix): `make unit-test` ran 16494 rows, 223/225 suites pass; the 2 failures (rewind_test, tap_loader_test) were 300 s load timeouts (host load 25-31) and pass solo: 370/370 and 80/80.
- Stage 2 (PaletteManager::entry_priority, Debugger::palette_priority, banner 53 -> 55 and video/audio 16 -> 18, INS-15-22..26, Pri column, QGFX-15, docs, dev guide cites re-anchored +8): debugger_backend_test 1597/1597, debugger_panels_test 206/206 (built and run, before the QGFX-07 extension).
- `make docs-check` green after stage 2 (cites 0 failed).
- Mutations seen red for the right rows: 1,2,3,4,5,6,10,11,13,14,15,16a,16b,17 (see report).

## NOT yet done (exact next steps)
1. Uncommitted at pause: test/debug/gfx_decode_test.cpp GFXD-14 now also checks byte 0x80 (row 1) because 0x81 is its own mirror image and mutation 8 (text-mode bit order reversed) stayed green. Build and run gfx_decode_test (expect 19/19), commit.
2. The QGFX-07 extension (Active tile palette follows NR 0x6B bit 4) was committed but debugger_panels_test was not re-run after it: build and run (expect 206/206).
3. Mutations still to run, script ~/tmp/gh371-mut2.sh (never started; sources verified equal to the cp backups in ~/tmp/gh371-mutbak): 7 (bank 7 mask removed, use `(void)bank7;` to build), 8 (needs the new GFXD-14), 9 (text transparency by NR 0x4C, use `(void)nr14;`), 12a/12b (Active ignoring NR 0x43 b3 / NR 0x6B b4; the sed needs `(choice == 0 ? false : choice == 2)` parenthesised if `(void)d,` does not compile as an initializer, otherwise use a `(void)d;` statement).
4. Gates after those: `make unit-test` (full, via slot.sh), `make unit-test-sdl` (decoder, palette_priority and debugger_backend_test run there; not run for either stage yet), then regenerate nothing else (images unchanged by stage 2: docshot shows Patterns only).
5. Final report to ~/tmp/autorun/jnext/2026-11-17/gh371-report.md; nothing pushed.
