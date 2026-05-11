# Pass-21 NMI + Multiface + Port + NextREG — Independent Reviewer Report

**Branch reviewed:** `task2/verify21-nmi-mf-port`
**Audit HEAD:** `30d6d2c`
**Reviewer HEAD (worktree):** `task2/verify21-nmi-mf-port-reviewer` (off audit HEAD)
**Reviewer date:** 2026-05-11
**Verdict:** **APPROVE-WITH-NITS**

The 3 class-(c) findings (V21-NMP-01, V21-NMP-02, V21-NMP-03) are
correctly identified vs the VHDL spec and the fixes are
VHDL-faithful in the bits-that-matter (no missed-INT or missed
side-effect regressions). However V21-NMP-02 has a residual
class-(c) readback divergence enshrined by the test flip
(VHDL returns 0x00, jnext returns 0xFF), and one of the V21-NMP-02
test rows is non-discriminative. Both are NITs, not blockers; the
fix is a strict improvement over the pre-fix code.

## Baseline test sandwich (reviewer worktree, Release)

| Suite | Result |
|---|---|
| ctest | 38/38 PASS |
| FUSE Z80 | 1356/1356 PASS |
| regression.sh | 33/0/0 PASS |
| nextreg_test | 21/21 PASS |
| nextreg_integration_test | 272/272 PASS (272 incl. +10 V21 rows) |
| port_test | 101/101 PASS / 1 SKIP (pre-existing NIT-01c) |

## Row-count validation

Audit claims "270+" rows across 7 sections. Reviewer counted by grep
`^| [A-G][0-9]`:

| Section | Audit claim | Reviewer count | Match |
|---|---|---|---|
| A — Port-decode handlers | 52 | 52 | ✓ |
| B — NextREG handler matrix | 126 | 126 | ✓ |
| C — IO-enable gates | 28 | 28 | ✓ |
| D — Expansion-bus AND-mask | 10 | 10 | ✓ |
| E — Multiface state | 24 | 24 | ✓ |
| F — NMI Source pipeline | 30 | 30 | ✓ |
| G — Save-load fields | 31 | 31 | ✓ |
| **Total** | **301** | **301** | ✓ |

Row counts match exactly. No gap.

## Spot-check 10 ✓ rows

Reviewer verified the following rows against the cited VHDL lines:

| Row | VHDL | Verification |
|---|---|---|
| A02 (port 0x7FFD) | zxnext.vhd:2593 | ✓ A15=0, (A14=1 or NOT p3_timing), port_fd, NOT port_1ffd, port_7ffd_io_en — matches |
| A03 (port 0x2FFD trap) | zxnext.vhd:2601 | ✓ A13:12=10 + port_xffd + NR 0xD8 b0 gate |
| A05 (port 0x1FFD) | zxnext.vhd:2599 | ✓ A13:12=01 + port_xffd + port_1ffd_io_en |
| A08 (port 0xFE) | zxnext.vhd:2582 | ✓ A0=0 only (LSB-only even) |
| A37-A40 (UART) | zxnext.vhd:2639 | ✓ A15:11=00010 + XOR(A10, A9 AND A8)='1' + port_3b_lsb yields 0x133B/0x143B/0x153B/0x163B |
| B001 (NR 0x00) | zxnext.vhd:5885 | ✓ g_machine_id RO |
| B063 (NR 0x68) | zxnext.vhd:6093 | ✓ NOT ula_en & blend_mode(2) & cancel_keys & ulap_en & fine_x & '0' & stencil_mode |
| B065 (NR 0x6A) | zxnext.vhd:6099 | ✓ "00" & lores_radastan & xor & palette_offset |
| C01-C28 IO-en gates | zxnext.vhd:2392-2442 | ✓ all 28 internal_port_enable indices match |
| F01-F17 NMI pipeline | zxnext.vhd:2089-2170 | ✓ all gates and FSM transitions match |

All 10+ spot-checks pass. The table is a faithful enumeration.

## V21-NMP-01 — NR 0x03 b7 palette_sub_idx readback

### VHDL anchor
- `zxnext.vhd:5894`: `port_253b_dat <= nr_palette_sub_idx & nr_03_machine_timing & nr_03_user_dt_lock & nr_03_machine_type;`
- `zxnext.vhd:1182`: signal declaration.
- Writers: reset `:5000`, NR 0x40 `:5376`, NR 0x41 `:5382`, NR 0x43 `:5395`, NR 0x44 toggle `:5403`.

### C++ fix
- `src/video/palette.h:120-127`: new public accessor `nine_bit_first_written()`.
- `src/core/emulator.cpp:2400-2407`: NR 0x03 read handler rewired to compose bit 7 from `palette_.nine_bit_first_written()`.
- `src/video/palette.cpp:330-351`: `nine_bit_first_written_` lifecycle (toggle on NR 0x44, false on second 9-bit write) matches VHDL :5403 (`nr_palette_sub_idx <= not nr_palette_sub_idx`).
- Reset paths in PaletteManager: ctor `:86`, set_control `:209` (NR 0x43), set_index `:220` (NR 0x40), write_8bit `:230` (NR 0x41) — all set `nine_bit_first_written_ = false`, matching VHDL `:5376/:5382/:5395` and reset path `:5000`.

### Test coverage
- 6 new rows in nextreg_integration_test (V21-NMP-01-A..F):
  - A: reset → bit 7 = 0
  - B: single NR 0x44 → bit 7 = 1 (toggle 0→1)
  - C: double NR 0x44 → bit 7 = 0 (toggle 0→1→0)
  - D: NR 0x44 + NR 0x40 → bit 7 = 0 (NR 0x40 reset)
  - E: NR 0x03 writes don't touch sub_idx → lower bits invariant under NR 0x44 toggle
  - F: bit 7 toggles independent of lower fields
- All 6 PASS.

### Verdict: ✓ CORRECT

**NIT V21R-NMP-NIT-01** (doc-only, no code regression): the fix
commit message and code comment cite "NR 0x40 / 0x41 / 0x43 / 0x28
writes reset to '0' (:5376 / :5382 / :5395 / :5000)". The first three
are accurate writers of `nr_palette_sub_idx <= '0'`. The fourth
entry, **NR 0x28**, is wrong: VHDL `:6301-6303` is the NR 0x28 write
handler driving `nr_keymap_sel` / `nr_keymap_addr`, NOT
`nr_palette_sub_idx`. Line `:5000` is the global reset block, not a
NR 0x28 write. This is a typo in the documentation; the code is
correct (PaletteManager does NOT reset `nine_bit_first_written_` on
NR 0x28 writes, in agreement with VHDL).

## V21-NMP-02 — CTC port mask 0xF8FF → 0xFCFF

### VHDL anchor
- `zxnext.vhd:2690`: `port_ctc <= '1' when cpu_a(15 downto 11) = "00011" and port_3b_lsb = '1' and port_ctc_io_en = '1' else '0';` (full 0x183B..0x1F3B).
- `zxnext.vhd:4067-4068`: NUM_CTC=4, NUM_CTC_LOG2=3 (instance override).
- `zxnext.vhd:4076`: `i_port_ctc_sel => cpu_a(10 downto 8)` (3-bit selector into 4-channel mux).
- `ctc.vhd:128-137`: sel(I) = '1' iff I = unsigned(i_port_ctc_sel), for I in 0..3 only.
- `ctc.vhd:141-146`: iowr(I) = port_ctc_wr AND sel(I) — for A10=1, all sel(I)=0 → all iowr(I)=0 (writes dropped).
- `ctc.vhd:164-176`: o_cpu_d = OR-fold of (dout(I) AND sel(I)) — for A10=1, all terms=0 → o_cpu_d = 0x00.

### C++ fix
- `src/core/emulator.cpp:4420`: mask change `0xF8FF → 0xFCFF` (A10 now masked in).
- After fix: handler matches 0x183B/0x193B/0x1A3B/0x1B3B (A9:8 select channel 0..3), and A10=1 addresses (0x1C3B-0x1F3B) fall through to `PortDispatch::in` no-match path → returns 0xFF.

### Test coverage
- 2 new rows in port_test (BUS group): V21-NMP-02-A (alias-write invariance), V21-NMP-02-B (alias read returns 0xFF).
- 1 existing row flipped: NR85-03b (`rd_alias == 0xFF` instead of `rd != 0xFF`).
- port_test 101/101 PASS.

### Verdict: ✓ CORRECT direction, but with NITs

**NIT V21R-NMP-NIT-02** (test enshrinement, class-(c) residual): The
fix correctly stops the pre-fix channel-aliasing of writes/reads in
the 0x1C3B-0x1F3B range. But a careful trace through VHDL shows the
post-fix readback value diverges from VHDL more subtly than the
audit acknowledges:

- VHDL `port_ctc_rd <= iord AND port_ctc` (`:2797`): for IN at 0x1C3B with port_ctc_io_en=1, this is '1' (port_ctc='1' for whole 0x183B..0x1F3B range).
- VHDL `port_internal_rd_response <= ... or port_ctc_rd` (`:2803-2806`): therefore '1', meaning the bus driver path is "internal port" not "floating bus".
- VHDL `cpu_di <= port_rd_dat` (`:1873`) when `port_internal_rd_response='1'`.
- VHDL `port_rd_dat <= ... or port_ctc_rd_dat` (`:2837-2840`).
- VHDL `port_ctc_rd_dat <= port_ctc_dat when port_ctc_rd='1' else X"00"` (`:2833`): = ctc_do.
- VHDL `ctc_do` = OR-fold of (dout(I) AND sel(I)) for I in 0..3 (`ctc.vhd:164-176`): for A10=1, all sel(I)=0, so ctc_do = 0x00.

Therefore **VHDL returns 0x00** (not 0xFF) for IN at 0x1C3B-0x1F3B
when CTC IO-enable is on. The audit fix has jnext return 0xFF (the
no-handler-matched floating-bus default), and the audit report and
test comment both state "VHDL no-decode for A10=1 / floating-bus
default" — that is **inaccurate**: VHDL DOES drive the bus (via
the OR-fold of port_*_rd_dat), just with value 0x00.

The residual divergence is still class-(c) (no observed boot
consumer of 0x1C3B-0x1F3B reads), and is smaller than the pre-fix
divergence (which returned channel data). The test V21-NMP-02-B
enshrines `rd_alias == 0xFF`, which encodes jnext's behaviour, not
VHDL's. A strictly VHDL-faithful fix would register a separate
handler for 0x1C3B-0x1F3B (or widen the matched range) that returns
0x00 on reads and drops writes. Not blocking, but flagged as a
follow-up item for a future pass.

**NIT V21R-NMP-NIT-03** (non-discriminative test): V21-NMP-02-A
("alias-write invariance") writes 0x87 to 0x1C3B and reads channel
0 via 0x183B before and after. The value 0x87 has bit 0 set,
meaning a CTC control word, NOT a time constant (TC write requires
the channel to be in RESET_TC or RUN_TC state). Per
`src/peripheral/ctc.cpp:33-72`, a control word does NOT mutate the
`counter_` field; it only updates state machine bits. Since
`CtcChannel::read()` (`:142-144`) returns `counter_`, the
pre-write and post-write reads BOTH return 0 (default counter),
whether or not the write is aliased to channel 0. The test passes
with `pre == post = 0` regardless of the fix.

A truly discriminative variant would:
- Set the channel to RESET_TC first (via a non-alias control write at 0x183B with bits[2:0]=111 = control+TC+enable), then
- Write a TC value to the alias (0x1C3B); this WOULD mutate `counter_` pre-fix and be a no-op post-fix.

V21-NMP-02-B (`rd_alias == 0xFF`) IS discriminative between the
pre-fix code (which returned channel-3 data = 0x00 by default) and
the post-fix code (which returns 0xFF), but as flagged above it
enshrines the wrong VHDL spec value.

### NR85-03b flip legitimacy

The pre-flip test expected `rd != 0xFF` (= "0x1F3B is a CTC channel,
returns real channel data"). Post-fix the test was flipped to
`rd == 0xFF`.

- **Pre-flip was wrong**: VHDL does NOT decode 0x1F3B as channel 3
  (the CTC has only 4 channels, selected by cpu_a(10:8) and A10=1
  yields no sel match), so `rd != 0xFF` was a false invariant.
- **Post-flip is partially correct**: it correctly stops asserting
  the false "real CTC channel" claim, BUT it enshrines `rd == 0xFF`
  while the true VHDL behaviour is `rd == 0x00`.

The flip is **legitimate as a refutation of the pre-fix invariant
but not a faithful enshrinement of the VHDL spec**. It is a net
improvement (closer to VHDL truth), and not test enshrinement of
pre-fix behaviour. Approved with the residual NIT logged above.

## V21-NMP-03 — NR 0x07 read `act` field gated on expbus_eff_en

### VHDL anchor
- `zxnext.vhd:5902-5903`: `port_253b_dat <= "00" & cpu_speed & "00" & nr_07_cpu_speed;`
- `zxnext.vhd:5816-5820`: actual `cpu_speed` latched as `nr_07_cpu_speed` when `expbus_en='0'`, else `expbus_speed`.
- `zxnext.vhd:2203`: `expbus_speed <= nr_81_expbus_speed;`
- `zxnext.vhd:5496`: `nr_81_expbus_speed <= "00";` (writable arm commented out — hard-wired "00").

### C++ fix
- `src/core/emulator.cpp:793-795`: NR 0x07 read handler now composes
  `act = nmi_source_.expbus_eff_en() ? 0x00 : req;` then `(act << 4) | req`.
- `nmi_source_.expbus_eff_en()` accessor (`src/peripheral/nmi_source.h:262`) is the existing Pass-9 shadow committed in the NR 0x80 write handler (`src/core/emulator.cpp:4115`). VHDL latches `expbus_eff_en` on bus-idle (`:5811`), jnext commits it synchronously on the NR 0x80 write — same approximation Pass-9 used.

### Test coverage
- 4 new rows in nextreg_integration_test (V21-NMP-03-A/B/C-pre/C-post):
  - A: expbus_eff_en=0, req=2 → readback = 0x22 (act=req)
  - B: expbus_eff_en=1, req=3 → readback = 0x03 (act=0)
  - C-pre: expbus_eff_en=1 + write req=2 → readback = 0x02
  - C-post: expbus_eff_en=0 (after clear) + same req=2 → readback = 0x22
- All 4 PASS.

### Verdict: ✓ CORRECT, no NITs

The fix correctly surfaces the VHDL `cpu_speed` semantics for the
readback, using the existing expbus_eff_en shadow without
introducing duplicated state. The tests are discriminative: B and
C-pre would FAIL pre-fix (`act = req` would return 0x33 and 0x22
respectively); C-post serves as the regression guard for the
no-expbus path.

## Adjacent re-audit

### Other NR mux bit positions composed from VHDL state

Reviewer scanned VHDL `:5878-6289` for `"0+"` / `'0'` and `"1+"` /
`'1'` literals in `port_253b_dat <= ...` assignments. All entries
flagged by the audit (NR 0x0B b6/b3:1, NR 0x6A b7:6, NR 0xC6 b7/3,
NR 0xCE b7/3, NR 0x4C b7:4, NR 0x71 b7:1, NR 0x2F b7:2, NR 0xC4
b6:2, NR 0xCC b6:2, NR 0xA0 b7:6/b2:1, NR 0xA2 b5, NR 0xA8 b7:1, NR
0xC8 b7:2, NR 0x9B b7:4, NR 0xC0 b4, NR 0xD8 b7:1, NR 0xDA b7:2, NR
0xF8 b7) confirmed as literal placeholders (no missed state).

NR 0xA2 b1 = '1' literal (the audit notes this) — verified:
`nr_a2_pi_i2s_ctl(7 downto 6) & '0' & nr_a2_pi_i2s_ctl(4 downto 2) & '1' & nr_a2_pi_i2s_ctl(0)`.

No new candidates found.

### Other A10-don't-care port masks

Reviewer enumerated all `register_handler(0xF...` masks in
`src/core/emulator.cpp`. After the V21-NMP-02 fix the inventory is:
0xFFFF (many), 0xF003 (port_xFFD family + Kempston-derived), 0xF0FF
(port_eff7), 0xFCFF (CTC post-fix). No remaining 0xF8FF or wider
A-bit don't-cares. ✓

### Other NR-read with cpu_speed-like ungated fields

Reviewer scanned the NR mux for VHDL signals whose latching is
gated by `expbus_en` or similar runtime modes:
- `cpu_speed` (NR 0x07 b5:4) — closed by V21-NMP-03 this pass.
- `eff_nr_05_5060` (NR 0x05 read) — already composed via
  `effective_5060()` (`zxnext.vhd:5897`), gated on Pentagon timing.
- `expbus_eff_*` other consumers (port_internal_response gating,
  NMI assert) — handled in NmiSource fan-out.

No additional ungated readbacks. ✓

## Test invariants (re-verified post-audit)

| Suite | Reviewer baseline | Audit claim | Match |
|---|---|---|---|
| ctest | 38/38 | 38/38 | ✓ |
| FUSE Z80 | 1356/1356 | 1356/1356 | ✓ |
| regression | 33/0/0 | not enumerated | ✓ |
| nextreg_test | 21/21 | 21/21 | ✓ |
| nextreg_integration_test | 272/272 | 272/272 | ✓ |
| port_test | 101/101 + 1 SKIP | 101/101 (1 NIT-01c skip) | ✓ |

## Verdict

**APPROVE-WITH-NITS.**

Three class-(c) findings (V21-NMP-01, V21-NMP-02, V21-NMP-03)
correctly identify VHDL-spec divergences. All three fixes are in
the right direction and pass discriminative tests for the bits
that matter. The 270+ row enumeration (actually 301 rows) was
verified for count match and 10+ row spot-check. The NR85-03b
test flip is a legitimate refutation of a false pre-fix invariant,
not test enshrinement of pre-fix behaviour.

NITs (none blocking):

- **V21R-NMP-NIT-01** (doc-only): V21-NMP-01 fix comment cites NR
  0x28 as a sub_idx-resetter; VHDL does not actually do that on NR
  0x28 writes. The C++ code is correct (no NR 0x28 write path
  touches `nine_bit_first_written_`). Pure typo, no code fix needed.

- **V21R-NMP-NIT-02** (class-(c) residual): V21-NMP-02 closes the
  channel-aliasing bug but leaves the 0x00-vs-0xFF readback
  divergence for 0x1C3B-0x1F3B reads (VHDL drives 0x00 via the
  CTC mux OR-fold; jnext returns 0xFF via no-handler floating-bus
  default). The audit comment and the V21-NMP-02-B test both
  describe the post-fix behaviour as "VHDL no-decode / floating
  bus", which is inaccurate. Suggested follow-up: add a no-op
  handler for the 0x1C3B-0x1F3B range that returns 0x00 on reads
  and silently drops writes, mirroring the OR-fold zero output.

- **V21R-NMP-NIT-03** (non-discriminative test): V21-NMP-02-A
  writes 0x87 to alias 0x1C3B and checks invariance at 0x183B,
  but 0x87 is a CTC control word and does not mutate `counter_`
  (which is what `read()` returns), so the test passes regardless
  of whether the alias write reaches channel 0. Suggested
  follow-up: rewrite the test to load a TC value (would-be
  RESET_TC sequence) into the alias and observe channel 0
  counter_ pre/post.

## Final HEAD SHA (reviewer commit)

`56f03fe3c0f966acf0e705632acd7a3a77fbaf0b`

