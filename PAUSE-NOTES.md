# GH #541 PAUSE NOTES (2026-11-19)

Done (commits on this branch, main v1.3.22 merged in): feature code (program_archive choose/has_data_files/extract_program/in_archive_directory, load_nex serving, main.cpp/web_controller/GUI wiring, browser_page::archive_program_allowed), all new rows (ZDIR-01..22, NXZIP-01..09, BRR-09, ZIPG-18..19, zip-data-func, WEBAPI-32..34 written), pins, docs (man page, user/dev guides, FEATURES, regenerated).
Verified: Qt tree build/ builds clean; program_archive_test 98/98, esxdos_stub_test 351/351, browser_policy_test 30/30, load_error_test 49/49; regression rows zip-data-func, esxdos-nexdir-func, zip-load-func, esxdos-hostfs-func all PASS.
Mutations killed (own rows): 1,2,3,4,5,6,7,8,9,10,11,12,14,18,19,20. NOT run: 13 (writable ignored; NXZIP-03), 15 (BRR-09), 16/17 (WEBAPI-34, ZIPG-18/19: wasm/GUI). Mutation driver: ~/tmp/gh541/mut/run.py (run.py 13 15). Tree is clean (emulator.cpp restored from backup).
NOT run: make unit-test, unit-test-sdl, unit-test-win, unit-test-wasm, wasm-browser-test (WEBAPI-32..34 never executed), docs-check.
Next step: run mutations 13 and 15 (python3 ~/tmp/gh541/mut/run.py 13 15), then make unit-test, unit-test-sdl, unit-test-win, unit-test-wasm, wasm-browser-test, all via slot.sh; fix WEBAPI rows if they fail.
Build dirs: build/ (Qt, built), build/gui-release-non-pgo (built by regression-rows). No wasm/sdl/win trees built.
