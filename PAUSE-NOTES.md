# GH #510 stage 1 pause notes (2026-11-19)

Branch gh510-pages-demos, base bd0d737ad (main, v1.3.26), 3 commits on top. Never pushed.

DONE
- web_controller.cpp: jnext_web_nmi (Multiface; refused under no_sdcard); jnext_api.js.in player.nmi().
- Widget NMI button is OPT-IN via `data-nmi` only (owner answered Q1: NO). The NMI_ALWAYS switch was removed; WIDGET-01 untouched.
- web/pages/index.html.in (NextZXOS demo, sets data-nmi), packaging/web/pages-site.sh, `make pages-site`, `make pages-site-test`,
  .github/workflows/pages.yml, release.yml `pages` job. Action pins: upload-pages-artifact@v5, deploy-pages@v5 (latest majors per gh api; checkout@v5/cache@v5 kept as in release.yml).
- Rows: PSITE-01..05 (test/packaging/pages-site-test.sh), WEBNMI-01..04 (nmi.mjs), PAGES-01..02 (pages.mjs); browser-smoke.sh SPECS nmi.mjs:4 pages.mjs:2 + native NMI oracle (headless, NMI_PRESS=1300, NMI_SHOT=1800, stability check at +100).
- Docs edited and re-rendered (man, user guide 8.x incl. data-nmi, devguide 5.x, RELEASE-PROTOCOL, FEATURES).

VERIFIED (before the stop)
- PSITE 5/5, WEBNMI 4/4, PAGES 2/2 on the real wasm build; web-package-test 50/50; docs-check, cli-check green; unit-test 16975/16975 (rc=0).
- Mutations killed: #5 (PSITE-01), #6 incl. case-sensitive (PSITE-03), #1+#2+#3 (WEBNMI-04 spy, 01, 02), #4 (WEBNMI-04 disabled), #7 (PAGES-01/02). All restored from cp backups; tree clean.

VOID / NOT DONE
- unit-test-sdl, unit-test-wasm and the full wasm-browser-test were killed mid-run: results void, re-run them.
- Stage 1 report file not yet written in final form; stage 2 not started.

NEXT STEP (exact)
1. `slot.sh env LANG=C make unit-test-sdl`, `unit-test-wasm`, `wasm-browser-test` (all rows, count must include nmi.mjs:4 pages.mjs:2).
2. Write gh510-report.md (commits, rows, mutations, gate totals). Scratch ~/tmp/gh510-scratch (mut backups, card.img) must be deleted at the end.
3. Hand back stage 1 for review; then stage 2 (card_extract, file:{card}, demo links, rows per plan).
