# Task 3 — Audio Subsystem SKIP-Reduction Audit (2026-04-24)

Closure audit for the Task 3 Audio subsystem skip-reduction plan executed
2026-04-24 in a single session (Phases 0 → 4 plus ad-hoc Waves E + F).
Mirrors the structure of
[task3-uart-i2c-phase4.md](task3-uart-i2c-phase4.md) and
[task3-ula-phase4.md](task3-ula-phase4.md).

Plan doc: [doc/design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md](../../design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md).
Row-by-row baseline: [task3-audio.md](task3-audio.md) (2026-04-17 snapshot,
200/121/6/73; the 6 fails were fixed in intervening Z80N/envelope work so
plan kick-off saw **200/127/0/73**).
Closing commit: `b63f05a` (`doc(audio): Phase 3/4 close …`).

## Headline numbers

|                                  | Before plan (2026-04-24) | After plan (2026-04-24) | Δ          |
|----------------------------------|-------------------------:|------------------------:|-----------:|
| `audio_test` total               |                  200     |                132      |   −68      |
| `audio_test` pass                |                  127     |                132      |    +5      |
| `audio_test` fail                |                    0     |                  0      |     0      |
| `audio_test` skip                |                   73     |                  0      |   −73      |
| `audio_nextreg_test` (new)       |                  N/A     |              25/25/0/0  |  +25 pass  |
| `audio_port_dispatch_test` (new) |                  N/A     |              16/16/0/0  |  +16 pass  |
| `input_integration_test`         |                7/5/0/2   |             12/10/0/2   |  +5 pass   |
| Aggregate unit suites            |                   27     |                 29      |    +2      |
| Aggregate pass                   |                 3041     |               3138      |   +97      |
| Aggregate skip                   |                  216     |                143      |   −73      |
| Regression                       |                34/0/0    |              34/0/0     | unchanged  |
| FUSE Z80                         |             1356/1356    |          1356/1356      | unchanged  |

Headline delta: **+5 `skip()` → `check()` flips in `audio_test` (MX-06 +
TS-24/32/33/34), 21 E-class rows reclassified to `// A:` / `// G:`
inline comments (A=12, G=9 where AY-30..34 share one G-comment block),
41 rows re-homed out of `audio_test` to three destinations (25 to
`audio_nextreg_test`, 16 to `audio_port_dispatch_test`, 5 FE-READ rows
to `input_integration_test`), and 1 late G-demotion (IO-04) in Wave F.
`audio_test.cpp` has zero runtime `skip()` calls. Aggregate pass +97
across 29 suites.**

## Phase summary

| Phase | Action | Critic verdict | Commits |
|---|---|---|---|
| 0 | Demote 21 E-class rows to `// A:` / `// G:` comments per `feedback_unobservable_audit_rule`. Original scoping audit earmarked 25 rows; user + Phase 0 critic re-audit reinstated 4 TS rows (TS-24, TS-32, TS-33, TS-34) as F-skips because aggregate TS-30/31 cannot prove per-PSG isolation. | APPROVE (25/25) + user-driven re-audit | `20776cb` (demote) + `ee53aaa` (re-instate 4 TS rows) |
| 1 | New `src/audio/i2s.{h,cpp}` stub mirroring `audio_mixer.vhd:89-90,99-100`. `Mixer::set_i2s_source(I2s*)` + `Emulator::i2s()` + 13-bit sum integration. Compile-only seam, zero runtime wiring (test-injected samples only). | APPROVE-WITH-FIXES (1 BLOCKING: mixer sum data-flow post-hoc fold-in) | `afb33c6` + fix `158a70e` |
| 2 Wave A | MX-06 flipped to `check()` using the Phase 1 stub (bare Mixer harness, distinct-amplitude 1023→1023 assertion). | APPROVE (4/4) | `6071d0f` |
| 2 Wave B | New suite `test/audio/audio_port_dispatch_test.cpp` (17 rows re-homed). 10 check() on first run; 7 (SD-10/12/14/15, IO-03/04/05) re-skipped as F-class on real emulator gaps for Wave F. | APPROVE | `ef904af` |
| 2 Wave C | New suite `test/audio/audio_nextreg_test.cpp` (25 rows). SUBSTANTIAL src/ changes: NR 0x06 bit-decode FIX, 3 new NR handlers (0x2C/2D/2E), `nr_06_internal_speaker_beep_` + `dac_enabled_` shadow state, `beep_spkr_excl()` composite, const getters on `AyChip::is_ay_mode()`, `TurboSound::ay_mode/stereo_mode/mono_mode`, `Emulator::dac_enabled/nr_06_internal_speaker_beep/beep_spkr_excl`. | APPROVE | `6a4afcc` |
| 2 Wave D | Extend `test/input/input_integration_test.cpp` with Group FE-READ (BP-04 + BP-20..23 re-homed). Wiring-only; uses existing Keyboard/ULA paths. | APPROVE-WITH-FIXES (BP-04 weak assertion — strengthened in Phase 3) | `40f45ba` |
| 2 Wave E (ad-hoc) | Un-skip the 4 TS rows reinstated in Phase 0 fixup. Distinct-amplitude per-PSG isolation proofs (`vol_ay[15]=0xFF`, `vol_ay[9]=0x41`, `vol_ay[5]=0x0F`) on bare TurboSound harness. | APPROVE (4 APPROVE + 1 APPROVE-WITH-FIXES baseline-comment nit) | `a184010` |
| 2 Wave F (ad-hoc) | Fix 6 of 7 emulator port-dispatch gaps surfaced by Wave B: SD-10 (0x5F new), SD-12 (0x3F new), SD-14 (0xFB reworked with VHDL NR 0x84 gate), SD-15 (0xB3 new), IO-03 (0xBFF5 reg-query read), IO-05 (BFFD +3 alias). IO-04 demoted to `// G:` comment (FFFD falling-edge Z80-invisible). | APPROVE (in main session) | `e86b93d` |
| 3 | BP-04 strengthened by sweeping border=0/5/7 and asserting full byte = 0xFF on all three (critic Phase 2 Wave D follow-up). | APPROVE | `d2ac2b4` |
| 4 | `SUBSYSTEM-TESTS-STATUS.md` + traceability matrix + `AUDIO-TEST-PLAN-DESIGN.md` Current Status + `EMULATOR-DESIGN-PLAN.md` Phase-9 Audio entry `[x]` + this audit. | (in-line) | `b63f05a` |

All Phase-2 waves and the Phase-1 scaffold passed through independent
critic review before merge (per `feedback_never_self_review.md`).

## Row-count evolution

| Checkpoint                      | `audio_test`   | `audio_nextreg_test` | `audio_port_dispatch_test` | `input_integration_test` | Aggregate       |
|---------------------------------|---------------:|---------------------:|---------------------------:|-------------------------:|----------------:|
| Pre-plan (2026-04-24)           | 200/127/0/73   |                  —   |                         —  |                7/5/0/2   | 3248/3041/0/216 |
| Post Phase 0 demote (`20776cb`) | 175/127/0/48   |                  —   |                         —  |                —         | —               |
| Post Phase 0 fixup (`ee53aaa`)  | 179/127/0/52   |                  —   |                         —  |                —         | —               |
| Post Phase 1 + fix (`158a70e`)  | 179/127/0/52   |                  —   |                         —  |                —         | —               |
| Post Wave A (`6071d0f`)         | 179/128/0/51   |                  —   |                         —  |                —         | —               |
| Post Wave B (`ef904af`)         | 162/128/0/34   |                  —   |                 17/10/0/7  |                —         | —               |
| Post Wave C (`6a4afcc`)         | 137/128/0/9    |            25/25/0/0 |                 17/10/0/7  |                —         | —               |
| Post Wave D (`40f45ba`)         | 132/128/0/4    |            25/25/0/0 |                 17/10/0/7  |              12/10/0/2   | —               |
| Post Wave E (`a184010`)         | 132/132/0/0    |            25/25/0/0 |                 17/10/0/7  |              12/10/0/2   | —               |
| Post Wave F (`e86b93d`)         | 132/132/0/0    |            25/25/0/0 |                 16/16/0/0  |              12/10/0/2   | 3281/3134/0/147 |
| Post BP-04 strengthen (`d2ac2b4`) | 132/132/0/0  |            25/25/0/0 |                 16/16/0/0  |              12/10/0/2   | —               |
| Post Phase 3/4 (`b63f05a`)      | **132/132/0/0** |       **25/25/0/0** |            **16/16/0/0**   |          **12/10/0/2**   | **3281/3138/0/143** |

The `audio_test` row-census shed 68 rows total (200 → 132): 21 E-class to
inline comments + 41 re-homed to companion suites + 1 late G-demotion in
Wave F (IO-04) + 5 un-skipped (MX-06, TS-24, TS-32/33/34) which remain as
row-census live checks but no longer count as skips.

## Plan-drifts caught during execution

Eight drifts surfaced between plan text and VHDL oracle / scoping-audit
classification; each was corrected before the closing commit.

### 1. Phase 0 over-demotion of 4 TS rows

- **Plan text (§Row-by-row dispositions)**: TS-24 and TS-32/33/34 listed
  as **A-class** ("covered by TS-20/21" and "covered by TS-30/31"
  respectively).
- **User + critic re-audit**: aggregate TS-30/31 asserts a combined sum
  from 3 PSGs. A PSG2-stuck-closed fault would still satisfy `L > 0xFF`
  via PSG0+PSG1 alone — aggregate cannot distinguish per-PSG isolation
  or prove that the single `stereo_mode` bit governs all three panners.
- **Fix**: `ee53aaa` re-instated the 4 rows as F-skips pointing at a
  future "Wave E per-PSG isolation extension". Wave E then un-skipped
  them (`a184010`).

### 2. Phase 1 mixer sum data-flow not VHDL-faithful

- **Phase 1 scaffold (`afb33c6`)**: accumulated EAR + MIC + AY + DAC first,
  then added the I2S term as a separate post-hoc fold-in step.
- **VHDL oracle** (`audio_mixer.vhd:99-100`): every 13-bit term
  (including `i2s_L` zero-extended from 10 bits at `:89-90`) is summed
  in a single expression — the VHDL data-flow schema does not partition
  I2S from the other terms.
- **Fix**: `158a70e` refactored to a single-expression sum at
  `src/audio/mixer.cpp:47-48`. Functionally equivalent for 10-bit I2S
  inputs (uint16_t is a superset of 13-bit) but now matches the VHDL
  data-flow exactly, avoiding silent divergence if a future change adds
  saturation or width asymmetry.

### 3. NR 0x06 aymode bit-decode was pre-existing emulator bug

- **Plan Wave C text**: implied the NR 0x06 bit-decode was simply a
  missing handler set.
- **Pre-existing emulator code**: `(v & 0x03) == 1` matched ONLY
  `psg_mode = 01` as AY.
- **VHDL oracle** (`zxnext.vhd:6389`): `aymode_i <= nr_06_psg_mode(0)`.
  Bit 0 alone determines aymode, so modes `01` (AY) and `11` (hold/rst)
  both set aymode=1; mode `11` additionally fires a separate
  `audio_ay_reset <= '1'` at `zxnext.vhd:6379`.
- **Fix**: Wave C reworked the handler at `src/core/emulator.cpp:1409-1424`
  to latch `(v & 0x01)` as aymode and invoke `turbosound_.reset()` when
  `(v & 0x03) == 0x03`.

### 4. NR 0x2C / 0x2D / 0x2E handlers missing entirely

- **Plan Wave C text**: listed NR-30/31/32 rows under "Coverage per NR
  handler" assuming the handlers existed.
- **Pre-existing emulator**: no NR 0x2C/0x2D/0x2E write handler
  registered in `NextReg` dispatch.
- **VHDL oracle** (`zxnext.vhd:4852-4854` + `:6452-6454`): these three
  NRs mirror the Soundrive channel writes (left / mono / right), gated
  on the Soundrive enable.
- **Fix**: Wave C added three handlers at
  `src/core/emulator.cpp:1513-1521` forwarding to `dac_.write_left` /
  `write_mono` / `write_right`, gated on `dac_enabled_`.

### 5. NR 0x06 bit 6 `nr_06_internal_speaker_beep` not modelled

- **Plan Wave C text**: referenced `beep_spkr_excl` and
  "`nr_06_internal_speaker_beep` derivation" without flagging that the
  underlying state bit didn't exist.
- **VHDL oracle** (`zxnext.vhd:5163`):
  `nr_06_internal_speaker_beep <= nr_wr_dat(6)`. Composes with NR 0x08
  bit 4 via AND into `beep_spkr_excl` at `zxnext.vhd:6504`.
- **Fix**: Wave C added `nr_06_internal_speaker_beep_` bool field at
  `src/core/emulator.h` + latch at `src/core/emulator.cpp:1431` + const
  getter + `beep_spkr_excl()` composite accessor.

### 6. Port 0xFB fan-out gate is non-trivial (Pentagon mono AD)

- **Prompt guidance (pre-Wave-F)**: suggested a "minimal unconditional"
  ch A fan-out on the existing 0xFB handler.
- **VHDL oracle** (`zxnext.vhd:2433`):
  `port_dac_mono_AD_fb_io_en <= nr_84(5) AND NOT nr_84(2)`. Default
  NR 0x84=0xFF leaves the mono gate CLOSED (bit 2 set). An
  unconditional ch A fan-out would have regressed Wave B's SD-11 row
  (which asserts 0xFB ch D default behaviour with NR 0x84 at reset).
- **Fix**: Wave F implemented the VHDL-faithful gate at
  `src/core/emulator.cpp:1337-1353`. SD-14 test opens the gate via
  `NR 0x84 = 0x20` and verifies ch A fan-out alongside existing ch D;
  SD-11 remains green under default NR 0x84.

### 7. IO-04 FFFD falling-edge latch — Z80-invisible

- **Plan Wave B text**: listed IO-04 as a flip candidate.
- **VHDL oracle** (`zxnext.vhd:2771-2773`): composes `port_fffd_rd`
  from FFFD / BFFD / BFF5 into the same `port_fffd_dat`; the falling-
  edge latch is an internal VHDL clock-domain artefact invisible at
  Z80 instruction-boundary granularity.
- **Fix**: Wave F demoted IO-04 to a `// G:` comment in
  `test/audio/audio_port_dispatch_test.cpp:481` (no runtime skip, no
  test body), reducing the suite from 17 rows to 16.

### 8. Agent-tool worktree stale-base bug (systemic)

- Confirmed systemic issue from the UART+I2C plan
  (`feedback_agent_worktree_stale_base.md`): the 6 waves launched
  across this plan each needed a verified rebase onto the current main
  tip because parallel Agent-tool worktree launches branch from a
  cached older main tip. All 6 waves were rebased before merge; no
  merge-conflict fallout on this plan (unlike UART+I2C Wave E's 7
  duplicate-skip residue).

## Bugs discovered + fixed during execution

Four emulator bugs surfaced across Phase 1 + Wave C + Wave F — all
latent code-path gaps rather than new-scaffold regressions. Three were
in pre-existing production code (NR 0x06 bit-decode, missing NR handlers,
port-dispatch gaps); one was in the Phase 1 scaffold (mixer sum).

### (a) NR 0x06 aymode bit-decode — `6a4afcc` (Wave C)

- **Pre-existing symptom**: writing NR 0x06 = 0x03 (psg_mode=11,
  hold/rst) left the AY chips in YM mode because `(v & 0x03) == 1`
  matched only psg_mode=01.
- **VHDL oracle**: `zxnext.vhd:6389` — `aymode_i <= nr_06_psg_mode(0)`.
  Bit 0 alone. Mode 11 must set aymode=1 AND fire `audio_ay_reset`
  (`zxnext.vhd:6379`).
- **Fix**: `src/core/emulator.cpp:1409-1424` — decode `(v & 0x01)` for
  aymode; invoke `turbosound_.reset()` when `(v & 0x03) == 0x03`.
- **Rows unblocked**: NR-01..05 (PSG mode family in `audio_nextreg_test`).

### (b) NR 0x2C / 0x2D / 0x2E handlers absent — `6a4afcc` (Wave C)

- **Pre-existing symptom**: writes to NR 0x2C/0x2D/0x2E were silently
  ignored; Soundrive NextREG mirror path (VHDL `:4852-4854`) had no C++
  counterpart.
- **Fix**: three new handlers at `src/core/emulator.cpp:1513-1521`,
  each gated on `dac_enabled_` per VHDL `:6436`.
- **Rows unblocked**: NR-30/31/32 in `audio_nextreg_test`.

### (c) 6 port-dispatch handlers missing or incorrect — `e86b93d` (Wave F)

- **Pre-existing symptoms**: 6 Soundrive / AY register-query / +3 alias
  port paths were either entirely unregistered or misgated.
  - Port 0x5F (Soundrive mode 1 ch D, VHDL `:2429`) — absent.
  - Port 0x3F (Profi Covox ch A, VHDL `:2431`) — absent.
  - Port 0xFB (Pentagon mono AD, VHDL `:2433+:2660`) — existing handler
    only fired ch D; mono-AD fan-out to ch A missing, and the VHDL
    gate `nr_84(5) AND NOT nr_84(2)` was not modelled.
  - Port 0xB3 (GS Covox mono B+C, VHDL `:2661`) — absent.
  - Port 0xBFF5 (AY reg-query read, VHDL `:2649`) — no read handler;
    BFFD decode was catching it by default.
  - Port 0xBFFD read on +3 (VHDL `:2771`) — no callback; falling through
    to the 0xFF open-bus.
- **Fix**: 5 new handlers + 1 reworked at
  `src/core/emulator.cpp:1291-1383` (16-bit specific matches to avoid
  sprite-port 0x5B overlap per the existing code's prior concern).
- **Rows unblocked**: SD-10, SD-12, SD-14, SD-15, IO-03, IO-05 in
  `audio_port_dispatch_test`.

### (d) Phase 1 scaffold mixer sum data-flow — `158a70e` (Phase 1 critic)

- **Symptom**: scaffold at `afb33c6` accumulated the pre-I2S terms
  (EAR + MIC + AY + DAC) in one expression, then added the I2S term
  in a follow-up statement — functionally equivalent but not matching
  the VHDL single-expression sum schema.
- **VHDL oracle**: `audio_mixer.vhd:99-100` — every 13-bit term
  (including I2S zero-extended from 10 bits at `:89-90`) summed in a
  single concurrent expression.
- **Fix**: `src/audio/mixer.cpp:47-48` consolidates to a single
  expression `pcm_L = ear + mic + tape_ear + ay_L + dac_L + i2s_L`.
- **Rows unblocked**: MX-06 (Wave A un-skip).

## Scope creep / wave expansion

Two waves (E + F) were not in the original plan and were added in
response to critic feedback / execution discoveries.

### Wave E — per-PSG isolation proofs

**Trigger**: the Phase 0 fixup (drift §1 above) reinstated TS-24 and
TS-32/33/34 as F-skips pointing at a future "per-PSG isolation
extension". Rather than punt to a follow-up plan, the extension landed
in-session as Wave E (`a184010`).

**Scope**: test-only (bare TurboSound harness). Distinct-amplitude
configurations `vol_ay[15]=0xFF`, `vol_ay[9]=0x41`, `vol_ay[5]=0x0F`
across the three PSGs. TS-24 proves `stereo_mode` governs all three
panners (ACB collapses to L=0 when C=0, non-responder PSG would leave
residual). TS-32/33/34 prove per-PSG isolation by silencing one PSG's
R8 and asserting the aggregate L drops by exactly that PSG's
contribution.

### Wave F — port-dispatch emulator fixes

**Trigger**: Wave B's `audio_port_dispatch_test.cpp` landed with 7 rows
F-skipped on real emulator gaps (not harness limits). The original
plan's Wave B scope was "test-only re-home"; fixing the underlying
gaps was deferred as backlog. Instead of shipping the plan with 7
known-broken rows, Wave F landed the fixes in-session (`e86b93d`).

**Scope**: src/ changes at `src/core/emulator.cpp` (6 new/reworked
handlers) + test un-skips + 1 IO-04 demotion to `// G:`. See bug
discovery (c) above.

## WONT / G-class / A-class decisions documented

### 12 A-class inline comments (`// A:`) — `audio_test.cpp`

Each sits at the row's former skip-site with a citation to the covering
row.

| Row | Covering row | Anchor |
|---|---|---|
| AY-63  | AY-62 bit-sequence       | `test/audio/audio_test.cpp:649`  |
| AY-103 | env_reset from AY-07/117 | `test/audio/audio_test.cpp:1064` |
| AY-120 | AY-110 (shape 0 → 0)     | `test/audio/audio_test.cpp:1243` |
| AY-121 | AY-111 (shape 4 → top)   | `test/audio/audio_test.cpp:1245` |
| AY-125 | AY-114 + AY-118          | `test/audio/audio_test.cpp:1301` |
| AY-126 | AY-112 + AY-116          | `test/audio/audio_test.cpp:1302` |
| AY-127 | AY-94 vol-table probes   | `test/audio/audio_test.cpp:1303` |
| AY-128 | AY-102 R13 re-write      | `test/audio/audio_test.cpp:1305` |
| TS-17  | TS-16 share ay_select gate | `test/audio/audio_test.cpp:1479` |
| TS-40  | TS-10 default pan        | `test/audio/audio_test.cpp:1809` |
| MX-15  | MX-05 (full-scale sum)   | `test/audio/audio_test.cpp:2296` |
| MX-21  | MX-01 (exc_i=0 default)  | `test/audio/audio_test.cpp:2302` |

### 9 G-class inline comments (`// G:`) — unobservable without refactor

Five of these share a single comment block (AY-30..34 port_a_i/port_b_i).

| Row(s) | Reason | Anchor |
|---|---|---|
| AY-30..34 | `port_a_i` / `port_b_i` tied to `turbosound.vhd`; no accessor on `AyChip` | `test/audio/audio_test.cpp:426` |
| AY-41     | `I_SEL_L=0` (/16 divider) unreachable — `turbosound.vhd:164` hard-wires `I_SEL_L='1'` | `test/audio/audio_test.cpp:464` |
| AY-43     | `ena_div_noise` half-rate path (`ym2149.vhd:264-268`) not observable through current accessor surface | `test/audio/audio_test.cpp:482` |
| AY-64     | noise rate clocked at `ena_div_noise` (`ym2149.vhd:290`) — same accessor gap as AY-43 | `test/audio/audio_test.cpp:652` |
| SD-09     | per-clock `if/elsif` write priority lives above `Dac` in port dispatch — not modelled | `test/audio/audio_test.cpp:2013` |

### 1 late G-demotion in Wave F — `audio_port_dispatch_test.cpp`

| Row | Reason | Anchor |
|---|---|---|
| IO-04 | FFFD falling-edge latch (`zxnext.vhd:2771-2773`) invisible at Z80 instruction-boundary granularity; `port_fffd_rd` composition is clock-domain internal | `test/audio/audio_port_dispatch_test.cpp:481` |

All G-class comments distinguish from WONT per `feedback_wont_taxonomy.md`
— the rows remain theoretically testable if/when the referenced accessor
or refactor lands.

## Backlog / follow-ups

1. **MX-06 I2S stub upgrade to real protocol emulation** — non-blocking.
   Stub models the 13-bit zero-extended sum contribution but not the
   Pi-driven I2S clock/buffer protocol. Estimated ~40h including VHDL
   audit of the Pi-I2S clock and buffer semantics. No known ZX Next
   software exercises this path at present (NextZXOS does not use it).

2. **AY `port_a_i` / `port_b_i` emulation** — 5 G-class rows
   (AY-30..34). Would unblock keyboard-over-PSG-I/O scenarios used by a
   few vintage Spectrum titles. Requires `AyChip` API extension
   (`set_port_a_input` / `set_port_b_input`). No known Next software
   uses this path.

3. **AY `I_SEL_L=0` (/16 divider) path** — 1 G-class row (AY-41).
   `turbosound.vhd:164` hard-wires `I_SEL_L='1'`; there is no VHDL
   path that exercises /16. Pure defensive backlog item, no user-
   visible impact.

4. **SD-09 per-clock write priority** — 1 G-class row. Requires a
   `Dac` architectural refactor to process time-ordered write events
   (currently last-write-wins within a frame). Out of scope.

5. **MX-06 I2S has zero runtime wiring** — the stub is exposed only
   via the test-programmatic `Emulator::i2s()`/`I2s::set_sample()`
   hook; no port or NextREG writes route to it at runtime. Intentional
   per the plan's Phase 1 design; flagged here so a future production
   wiring task knows where to plug in.

6. **Agent-tool worktree stale-base bug** — see next section.

## Agent-tool worktree-caching bug flag

Per `feedback_agent_worktree_stale_base.md` (recorded 2026-04-24): this
plan's 6 parallel Phase-2 agent waves each required an explicit rebase
verification onto current main at worktree entry, confirming the bug
is systemic across Task 3 parallel-wave plans (first observed in
UART+I2C, re-confirmed here). No merge-conflict fallout on this plan
because each agent was briefed on the mitigation up front.

The standing mitigation remains: every agent-launched worktree must
verify its base commit matches current main tip and rebase if not
before starting work.

## Commits landed (Task 3 Audio plan)

```
Plan doc:       3af36d2 (TASK3-AUDIO-SKIP-REDUCTION-PLAN.md authored)
Phase 0:        20776cb (demote 25) + ee53aaa (re-instate 4 TS rows)
Phase 1:        afb33c6 (scaffold) + 158a70e (mixer sum fix after critic BLOCKING)
Wave A:         6071d0f
Wave B:         ef904af (new audio_port_dispatch_test.cpp, 7 F-skips for Wave F)
Wave C:         6a4afcc (new audio_nextreg_test.cpp + NR 0x06 fix + NR 0x2C-2E + accessors)
Wave D:         40f45ba (input_integration_test FE-READ extension)
Wave E (ad-hoc): a184010 (TS-24/32/33/34 per-PSG isolation)
Wave F (ad-hoc): e86b93d (6 port-dispatch fixes + IO-04 G-demotion)
Phase 3 BP-04:   d2ac2b4 (BP-04 strengthen per Wave D critic)
Phase 3/4 close: b63f05a (plan doc + dashboards + audit commit)
```

All Phase-1 and Phase-2 merges passed independent critic review before
landing on main. Scaffold bugfix `158a70e`, Wave C's src/ changes, and
Wave F's src/ changes all received direct-to-main critic approval in
the main session after the per-wave critic pass.

## Final verdict — CLOSED

- `audio_test`: **132 / 132 / 0 / 0** — zero runtime skips.
- `audio_nextreg_test`: **25 / 25 / 0 / 0** (new suite).
- `audio_port_dispatch_test`: **16 / 16 / 0 / 0** (new suite).
- `input_integration_test` (extended Group FE-READ): **12 / 10 / 0 / 2**.
- Aggregate: **3281 / 3138 / 0 / 143 across 29 suites**.
- Regression: **34 / 0 / 0**. FUSE Z80: **1356 / 1356**.

The Audio subsystem is now production-grade for:

- Full AY/YM PSG emulation (pre-existing 127 rows retained; 0 fails).
- TurboSound 3× PSG with per-chip mono flags, stereo mode governance,
  and distinct-amplitude per-PSG isolation proofs (Wave E).
- Soundrive 4-channel DAC across all documented port modes: Soundrive
  mode 1 (0x0F/0x1F/0x4F/0x5F), mode 2 (0xF1/0xF3/0xF9/0xFB), Profi
  Covox (0x3F/0x5F), Covox (0x0F/0x4F), Pentagon mono AD (0xFB under
  `nr_84(5) AND NOT nr_84(2)` gate per VHDL `:2433`), GS Covox (0xB3),
  SpecDrum (0xDF), plus NextREG mirrors NR 0x2C/0x2D/0x2E.
- AY register-query decode: FFFD/BFFD/BFF5 with +3 BFFD→FFFD read
  alias (`zxnext.vhd:2771`).
- NR 0x06 PSG mode bit-decode per VHDL `:6389` (bit-0 aymode, mode 11
  fires `audio_ay_reset`), `nr_06_internal_speaker_beep` latch,
  `beep_spkr_excl` composite with NR 0x08 bit 4 per VHDL `:6504`.
- NR 0x08 DAC/speaker/turbosound/issue2/stereo gates, NR 0x09 per-PSG
  mono flags.
- Pi I2S 10-bit zero-extend to 13-bit mixer sum per `audio_mixer.vhd:89-90,99-100`
  (stub — programmatic `Emulator::i2s()` hook, no runtime protocol).

The 21 inline-commented rows + 1 late G-demotion (IO-04) are all
honestly documented as either A-class (duplicate coverage, covering
row cited) or G-class (unobservable without accessor/refactor). The
four backlog items above are all defensive (no known user software
exercises those paths on the Next).

All originally-planned flips landed. The two ad-hoc waves (E + F) closed
what would otherwise have been plan-scope debt. No residual F-skips
in any Audio-plan-hosted suite. No lazy-skips, no tautologies, no
coverage theatre.
