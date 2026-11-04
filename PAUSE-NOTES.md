# GH #437 PAUSE NOTES (2026-11-04)

Branch gh437-c-variables-edit at d1a22f776 (before this notes commit), base epic353-c-variables b659d8ca0. Parked by owner order: no more epic-353 work beyond #435. Nothing pushed. Full report: ~/tmp/autorun/jnext/2026-10-30/gh437-report.md

## Commits
- 910f482c3 write_c_value(), c_encode_value, signed-bitfield read fix (F1), ZRCP `set-register CVAR(path)=value`, GDB `monitor set var`; rows CVW-01..12, SRCDBG-CVW-01..22, ZRCP-CVW-01..03, GDB-CVW-01..03
- 154e0e157 stage-1 docs (man, user and dev guides, diagram, design docs), cites re-anchored
- da4845fd5 float text parsed by an istringstream imbued classic (no std::from_chars)
- de2f70b30 matrix + dashboard regenerated (valid for this tree only)
- 23aa0f948 "PENDING OWNER APPROVAL (A1)": script form `set cvar(PATH) = v`, EVAL-CVW-01..05, EVAL-CVAR-07 clause removed, script docs (A1 since approved; commit not amendable)
- 13fd91ed4 CVALF-16 null-stop-off (CVALF-16 changed, CVALF-16a..c new), dev guide 7.10 wording
- d1a22f776 7.10 cites re-anchored

## Gates
- de2f70b30: make unit-test-dashboard + unit-test 15500/15500; unit-test-sdl 13806/13806. Green.
- HEAD: single suites only, green: c_value_test 63, source_debug_test 493, gdb_rsp_test 122, zrcp_adapter_test 230, script_eval_test 74, script_parse_test 265, script_events_test 153; docs-check green.
- Never run: unit-test-win (killed mid-build), the 5 regression rows (gdb-cli-func gdb-z88dk-func script-mutation-func script-load-error-func zrcp-func), any full gate on HEAD.
- Mutations: all stage-1 mutants killed (report has the table).

## To do on resume
1. `make unit-test-dashboard` twice (first run regenerates the matrix and fails traceability-check); commit matrix + dashboard (stale: EVAL-CVW, CVALF-16a..c, counts).
2. `make unit-test`, `make unit-test-sdl`, `make unit-test-win` (float parser untested under MinGW), `make regression-rows ROWS="gdb-cli-func gdb-z88dk-func script-mutation-func script-load-error-func zrcp-func"`.
3. Check Qt suites and regression rows for char-array text changed by CVALF-16.
4. Stage 2 (Qt edit path, QVARW-01..08, M32-M37) only after #435 is merged into the epic and the epic into this branch; re-check plan Q1..Q5.
5. Re-run `perl test/lint-devguide-cites.pl` after merging #435.

## Owner decisions taken
- A1: `set cvar(PATH) = v` allowed; EVAL-CVAR-07 loses only its read-only clause.
- CVALF-16: GDB null-stop-off. A char array is a string when every non-NUL byte is text (embedded NUL as `\0`), else a list.
- Trailing-NUL rule: NOT an owner decision. I drop NULs after the last text byte (keeps CVALF-17 `"hi"`); the brief's example `"hi\0lo\0\0\0"` would print `"hi\0lo"`. Ask the owner if trailing NULs should show (then CVALF-17 and others change).
- OD-W1 default (a): a dot command's global in DivMMC RAM is refused (Unsupported, DivMMC text); poke() and INS-02-30 untouched.
- Float parser: std::from_chars replaced (libc++ lacks floating from_chars before LLVM 20); my engineering decision, not an owner one.

## Expected conflicts with #435
debugger_cvalues.cpp (var_ctx, c_children facade; I added Found::scope_unknown and write_c_value at the end, eval_var untouched), debugger.h and c_value.h (adjacent declarations), test/unit-tests.conf (c_value_test and source_debug_test counts: recount from the file), dev guide 7.7/7.8/7.10 and srcdbg-frontends.dot + SVG, generated matrix and dashboard (regenerate from a real run), rendered guides, and every file:line cite in the dev guide.
