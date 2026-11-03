# GH #442 PAUSE NOTES (2026-11-03, stopped at coordinator request)

Branch gh442-turbosound-tick off main 8a3ee685d. Stage 1 only.

## Done (committed)
- Fix: TurboSound::tick() ticks all 3 chips, `|=` fold (src/audio/turbosound.cpp); F1 comments in turbosound.h.
- Rows TS-35..38 (audio_test.cpp, g_ts_clock), AYC-22 (ay_cache_test.cpp); pins audio_test 167, ay_cache_test 19.
- Ref::tick in ay_cache_test changed (owner-approved) + header comment.
- Docs: AUDIO-TEST-PLAN-DESIGN.md rows/ranges; dev guide 04-audio.md TurboSound paragraph, re-rendered.

## Evidence (scratch: /home/jorgegv/tmp/impl-gh442-scratch/)
- base_run.txt: rows on unfixed code: TS-35..38 FAIL, AYC-22 FAIL (vacuous, old Ref), audio_test 163/167, ay_cache 18/19.
- fix_oldref.txt: fix + OLD Ref: audio_test 167/167; ay_cache AYC-01 FAIL, AYC-22 FAIL, AYC-02..21 PASS (AYC-20/21 goldens unchanged, pass).
- fix_newref.txt: fix + new Ref: ay_cache 19/19.
- Mutants M1-M9 (mut1.txt, mut3.txt; M7 in mut2.txt): all killed. M1 TS-35,38,AYC-01,22; M2 TS-36,37,38,AYC-01,22; M3 TS-37,AYC-01,22;
  M4 AYC-01,02,04,05,06,19,21,22 (TS-35/36 NOT killed, plan predicted them; AYC-20 not listed); M5 TS-35..38 + most AYC; M6 TS-35,36,37,AYC-01,13,22 (TS-30 not killed);
  M7 (no noise/env on AY#1/#2) TS-38, AYC-22; M8 AYC-01, AYC-22; M9 TS-35..38, AYC-01, AYC-22. Run sequentially in one build dir (cp backups), not separate dirs.
  M7/M8 first run was polluted by a leftover M7 edit; M8/M9 re-run clean (mut3.txt). Tree verified clean after.

## Gates
- make unit-test: STARTED, KILLED at the pause (was at 100% build, suites not yet run). NO totals. Must re-run.
- unit-test-sdl: not run. Regression rows: not run. Bench / perf stat: NOT done (any number taken now is void).

## Next
1. make unit-test, make unit-test-sdl (load < 12), regression rows (check names in functional_tests.conf: audio-envelope-func audio-gain-func wav-record-func nextpi-audio-func rzx-playback-func rzx-play-route-func; subsystem-gain-func is it in conf? verify).
2. make docs-check (part of unit-test); commit regenerated traceability matrix / dashboard if changed.
3. Bench (base/branch/base, load < 2) + perf stat instructions:u tap_loader_test, rewind_test; stop rule 5% / 10%.
4. Report to /home/jorgegv/tmp/autorun/jnext/2026-10-30/gh442-report.md.

## F2/F3 impact (Stage 2, grep only)
- F2 (reset keeps generators): AY-83 (reset then reprogram, fixed-level, unaffected), AY-96 (outputs 0 after reset, unaffected: O_AUDIO still reset), AY-03/AY-06 (addr/regs, unaffected).
  AYC-01/AYC-19 use AyChip::reset in both Ref and real: follow automatically. Regression fingerprints audio-envelope-func/wav-record-func could shift if they follow a soft reset; check.
- F3 (psg_mode=11 held reset): AUD-NR-05 (audio_nextreg_test) writes NR6=3 then reads reg 0 == 0: still passes if the reset is applied on write; a hold-mode needs
  checking that later writes are ignored (new rows). audio_test TS-60/TS-61 call reset_ay_only() directly: unaffected unless the API changes.
  debugger_backend_test "0x06,0x03" hits are Z80 code bytes, not NR 0x06.
