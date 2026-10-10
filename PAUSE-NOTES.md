# GH #319 pause notes (2026-10-10)

Done and committed (0e5ccfe6f) + later uncommitted: wine-run.sh --serve fix (wineserver -k/-w before -p).
- regression.sh --platform (tags os=, per-OS pins 135/127, accounting), test-functions.inc (JNEXT_SDL, TEST_BIN_DIR, Darwin load, wineserver kill), wine-run.sh --jnext/--serve, Makefile regression-win/regression-macos, conf tags (20 rows) + pins, row fixes (benchmark, audio-underrun, joystick-picker, rzx-record-status, rzx-reset via full-disk.sh, dzrp-peer.py jnext_process_under), product fix "wb" in dac_trace_recorder.cpp + trace.cpp, +3 rows in audio_capture_test (count 20 in unit-tests.conf).
- `make regression-win` GREEN: 194/194 (1+66+127), 0 SKIP, log ~/tmp/gh319-logs/win2.log.

Nothing running. Next steps, in order:
1. Commit the wine-run.sh fix.
2. Add harness-selftest rows HS-89..95 (see plan sec.7) to test/harness-selftest.sh, update EXPECTED_TOTAL (160 + new).
3. Mutations (plan sec.8), traceability regen, shellcheck, docs (REGRESSION-TEST-SUITE.md, devguide + make docs-devguide, CLAUDE.md per D11, ChangeLog Unreleased lines).
4. CI: add macos-regression job and test (regression-win) leg to .github/workflows/ci.yml (plain make targets; brew incl. ffmpeg, libspectrum, imagemagick, mtools, dosfstools, qt@6).
5. Full gate (make clean && make gui-release, unit-test, unit-test-sdl, unit-test-win, FUSE, make regression, make regression-win); push branch (JNEXT_ALLOW_PUSH=1, explicit refspec), gh workflow run ci.yml, iterate on macOS failures; write report gh319-report.md.
