# Task 3 — Audio Subsystem SKIP-Reduction Plan

Staged, VHDL-faithful plan to reduce the 73 skips in `test/audio/audio_test.cpp`
(200/127/0/73) to a minimum residual set. Mirrors the structure of
`TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md` (closed 2026-04-24) and the
ULA re-home pattern from `TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md`
(closed 2026-04-23).

## Context

### Current state

- **`audio_test.cpp`**: 200 rows / 127 pass / 0 fail / **73 skip**.
- Plan doc: [`doc/testing/AUDIO-TEST-PLAN-DESIGN.md`](../testing/AUDIO-TEST-PLAN-DESIGN.md).
- Source: `src/peripheral/ay_chip.*`, `turbosound.*`, `dac.*`, `beeper.*`, `mixer.*`.
- VHDL oracles:
  - `audio/audio_mixer.vhd` — 13-bit stereo sum of EAR + MIC + AY + DAC + I2S.
  - `audio/dac.vhd` — Soundrive 4-channel DAC.
  - `audio/ym2149.vhd` — PSG core (envelope, noise, tone, vol tables).
  - `audio/turbosound.vhd` — 3× AY wrapper, channel select, stereo pan.
  - `zxnext.vhd` various — port decode, NextREG handlers, beeper XOR,
    exclusive-mode gating.

### Scoping-audit verdict

An independent scoping audit (2026-04-24) classified every skip:

| Category | Count | Disposition |
|---|---:|---|
| **A** — real audio feature gap | **1** | Phase 1 scaffold (I2S stub) + Wave A flip |
| **B** — RE-HOME to port dispatch | **16** | Wave B — new `audio_port_dispatch_test.cpp` |
| **C** — RE-HOME to NextREG integration | **20** | Wave C — new `audio_nextreg_test.cpp` |
| **D** — RE-HOME to Input/ULA integration | **5** | Wave D — extend `input_integration_test.cpp` |
| **E** — audit-rule comment candidates (duplicate / tautology) | **31** | Phase 0 — `// A:` / `// B:` comments, NOT `skip()` |

Unusually for a Task 3 subsystem, there is **only one real audio feature
gap** (MX-06 — Pi I2S stream not emulated). The other 72 skips are either
(a) test-harness limits that integration-test re-homing resolves, or
(b) redundant rows covered elsewhere that the unobservable-audit rule
demotes to comments.

### Dependencies + crossover

- **Input plan** (2026-04-23): `Keyboard::read_rows()` with extended matrix
  is already in main → unblocks Wave D (port 0xFE read composition).
- **UART+I2C plan** (2026-04-24): no overlap.
- **NextREG integration test** (`test/nextreg_integration_test.cpp`): already
  in main → Wave C extends it with a new "Audio Config" group.
- **Port dispatch integration test**: does NOT exist yet as a dedicated
  suite (`port_test.cpp` covers `PortDispatch` class in isolation). Wave B
  authors `test/audio/audio_port_dispatch_test.cpp` as a new companion
  (mirroring `uart_integration_test.cpp`), NOT extending `port_test.cpp`.

## User decisions taken (2026-04-24)

1. **MX-06 I2S**: implement stub (`src/audio/i2s.{h,cpp}` 1-row class
   mirroring `audio_mixer.vhd:89-90`). Wave A flips MX-06 to `check()`.
2. **Phase 0 E-class audit**: OK to execute test-code-only directly on
   main per `feedback_pragmatic_remediation_workflow.md`.
3. **Parallel agent budget**: up to 5 authorised; this plan fits 4
   (A + B + C + D all in one wave, no coupling).

## Open questions

None carried forward from scoping. All Phase-0 prerequisites resolved.

## Phase 0 — unobservable audit + E-class rewrite (self, direct to main)

**Goal**: reclassify the 31 E-class rows per `feedback_unobservable_audit_rule`.
Rows whose "skip reason" is "covered by AY-X" or "covered structurally
by TS-Y" should be `// A:` (existing-test-duplicate) comments, NOT
`skip()` calls. Architectural-freeze rows (AY-41, I_SEL_L) and hard
unreachable-without-accessor rows (AY-30..34, AY-43, AY-64) remain `skip()`
with an **F** reason pointing at the missing accessor API, reclassified
as **G** (unobservable-without-refactor) if the accessor wouldn't land
without a src/ API change out of scope for this plan.

### Row-by-row dispositions

| Row | Current skip reason | Phase 0 action |
|---|---|---|
| AY-30 | no port_a_i injection | **G** `// G: AY-30: port_a_i accessor not on AyChip surface` |
| AY-31 | no port_a_i injection | **G** (same) |
| AY-32 | no port_b_i injection | **G** |
| AY-33 | no port_b_i injection | **G** |
| AY-34 | turbosound tie-high | **G** |
| AY-41 | I_SEL_L=0 unreachable | **G** `// G: AY-41: /16 divider unreachable — turbosound.vhd:164 hard-wires I_SEL_L='1'` |
| AY-43 | noise_cnt_ not observable | **G** |
| AY-63 | poly17(0) covered by AY-62 | **A** `// A: AY-63: covered by AY-62 bit-sequence check` |
| AY-64 | noise rate not observable | **G** |
| AY-103 | covered by AY-07/AY-117 | **A** |
| AY-120 | covered by AY-110 | **A** |
| AY-121 | covered by AY-111 | **A** |
| AY-125 | covered by AY-114/AY-118 | **A** |
| AY-126 | covered by AY-112/AY-116 | **A** |
| AY-127 | 32-level step implied | **A** |
| AY-128 | period reset covered by AY-102 | **A** |
| TS-17 | covered by TS-16 | **A** |
| TS-24 | covered by TS-20/TS-21 | **A** |
| TS-32 | covered by TS-30/TS-31 | **A** |
| TS-33 | covered by TS-30/TS-31 | **A** |
| TS-34 | covered by TS-30/TS-31 | **A** |
| TS-40 | pan=11 covered by TS-10 | **A** |
| SD-09 | per-clock priority not modelled | **G** `// G: SD-09: if/elsif per-clock write priority lives above standalone Dac` |
| MX-15 | non-saturation confirmed by MX-05/MX-14 | **A** |
| MX-21 | exc_i=0 covered by MX-01/MX-02 | **A** |

**Phase 0 result**:
- 18 rows → `// A:` comments (duplicate coverage).
- 7 rows → `// G:` comments (unobservable-without-refactor; distinct
  from WONT per `feedback_wont_taxonomy.md`).
- **6 rows remain `skip()`** — all legitimately F-class, unblocked by
  Phase 2 waves below.

**Row-count delta**: `200 / 127 / 0 / 73` → `175 / 127 / 0 / 48` (25 rows
demoted from runtime-counted skips to inline comments). No src/ changes.

**Independent critic**: one agent reviews the Phase 0 commit (test-code
only) per `feedback_never_self_review.md`. Flag any row reclassified as
**A** whose "covering" row does not actually cover the claimed behaviour.

## Phase 1 — `I2s` stub scaffold (single agent, ~100 LOC)

**Scope**: new `src/audio/i2s.{h,cpp}` class modelling `audio_mixer.vhd:89-90`:

```cpp
// src/audio/i2s.h
class I2s {
public:
    void set_sample(uint16_t left_10bit, uint16_t right_10bit);
    uint16_t left() const;    // 0..1023 (zero-extended to 13 bits at mixer)
    uint16_t right() const;
    void reset();
    // save/load state — no-op at stub level; state = latched sample pair
    void save_state(StateWriter&) const;
    void load_state(StateReader&);
private:
    uint16_t left_{0};
    uint16_t right_{0};
};
```

**Mixer hook**: `src/audio/mixer.h/cpp` gains `set_i2s_source(I2s*)` plus
I2S contribution in `generate_sample()`:

```cpp
int32_t i2s_l = i2s_ ? i2s_->left() : 0;   // 0..1023, zero-extended
int32_t i2s_r = i2s_ ? i2s_->right() : 0;
pcm_l_ += i2s_l;  // already in 13-bit sum domain
pcm_r_ += i2s_r;
```

**Emulator wiring**: `Emulator` owns `I2s i2s_` member and passes
`&i2s_` into `mixer_.set_i2s_source()` at construction.

**No port / NextREG wiring in Phase 1** — the stub exposes a
test-programmatic `set_sample()` only. Real Pi-driven I2S audio
emulation is a v2 feature outside this plan's scope.

**Critic**: independent review of `dc0566b`-style commit — flag any
saturation/overflow risk at the 13-bit mixer sum, any initialisation
path that leaves `i2s_l/r` non-zero at Emulator reset.

## Phase 2 — 4 parallel waves (A + B + C + D)

All four waves are **independent** — no shared src/ touch surface beyond
the Phase 1 `I2s` stub (Wave A only), existing NextReg handler surface
(Wave C only), existing `Keyboard` API (Wave D only), and existing
`PortDispatch` surface (Wave B only). Launch as up to 5 parallel
worktree agents (budget available).

### Per-agent flow (mirror UART+I2C Phase 2)

Each agent:

1. **Entry check**: verify worktree base matches current main tip
   (the Agent-tool stale-base bug from 2026-04-24 session — see
   `feedback_agent_worktree_stale_base.md`). Rebase if not current.
2. Implement the wave's deliverable.
3. Run `LANG=C make unit-test` + `bash test/regression.sh` in the
   worktree. Must pass.
4. Independent critic on the wave branch (separate agent instance).
5. Main session merges to main.

### Wave A — I2S flip (SMALL)

**Rows**: MX-06 (1).

**Deliverable**: in `test/audio/audio_test.cpp`, flip `skip("MX-06", ...)`
→ `check("MX-06", "Pi I2S input zero-extended 10→13-bit contributes to pcm_L/R",
...)` using the Phase 1 stub. Test sequence:

```cpp
Emulator emu;
emu.mixer().set_ear(false); emu.mixer().set_mic(false);
emu.i2s().set_sample(1023, 1023);   // max
int32_t l = emu.mixer().pcm_l();
check("MX-06", "...", l == 1023, ...);
```

VHDL oracle: `audio_mixer.vhd:89-90, 99-100`.

### Wave B — port-dispatch integration (LARGE)

**Rows**: 16 — SD-10, SD-11, SD-12, SD-13, SD-14, SD-15, SD-16, SD-18,
BP-01, BP-06, IO-01, IO-02, IO-03, IO-04, IO-05, IO-11, IO-12.

**Deliverable**: new suite `test/audio/audio_port_dispatch_test.cpp`.
Register in `test/CMakeLists.txt` + `Makefile`. Mirror
`uart_integration_test.cpp` template.

**Coverage per row**:

| Row | Test | VHDL oracle |
|---|---|---|
| SD-10 | port 0x1F/0x0F/0x4F/0x5F → DAC chan A/B/C/D (Soundrive mode 1) | `zxnext.vhd:2429` |
| SD-11 | port 0xF1/0xF3/0xF9/0xFB → DAC chan A/B/C/D (Soundrive mode 2) | `zxnext.vhd:2432` |
| SD-12 | port 0x3F/0x5F → DAC (Profi Covox) | `zxnext.vhd:2658` |
| SD-13 | port 0x0F/0x4F → DAC (Covox) | `zxnext.vhd:2659` |
| SD-14 | port 0xFB → DAC mono (Pentagon/ATM) | `zxnext.vhd:2660` |
| SD-15 | port 0xB3 → DAC (GS Covox) | `zxnext.vhd:2661` |
| SD-16 | port 0xDF → DAC (SpecDrum) | `zxnext.vhd:2662` |
| SD-18 | mono-port aliasing fan-out to 4 channels | same VHDL area |
| BP-01 | OUT (0xFE), A → port_fe_reg <= A[4:0] | `zxnext.vhd:3593` |
| BP-06 | A0=0 address decode for port 0xFE | port dispatch |
| IO-01 | OUT (0xFFFD), A → AY register select | `zxnext.vhd:2647` |
| IO-02 | OUT (0xBFFD), A → AY data write | `zxnext.vhd:2648` |
| IO-03 | port BFF5 (reg query mode) | `zxnext.vhd:2649` |
| IO-04 | FFFD falling-edge latch | `zxnext.vhd:2771-2773` |
| IO-05 | BFFD → FFFD alias on +3 | same VHDL |
| IO-11 | port→channel alias fan-in | port-dispatch fan |
| IO-12 | FD F1/F9 AY-conflict guard | `zxnext.vhd:2777` |

**Test pattern**: build `Emulator`, use `cpu_out_port(port, data)` and
observe `dac().pcm_left()` / `ay_chip().read_register()` changes.

In `audio_test.cpp`, convert SD/BP/IO rows to `// RE-HOME:` comments
pointing at the new suite.

### Wave C — NextREG integration (LARGE)

**Rows**: 20 — NR-01, NR-02, NR-03, NR-04, NR-05, NR-06, NR-10, NR-11,
NR-12, NR-13, NR-14, NR-20, NR-21, NR-30, NR-31, NR-32, BP-10, BP-11,
BP-12, BP-13, MX-03, MX-20, MX-22, SD-17, IO-10.

(26 rows listed — 20 flips + 5 RE-HOME comments for the already-covered-
via-NR ones; the scoping audit tagged them C-class because the NR
write handler is the test entry point even if the effect is beeper /
mixer / dac-side.)

**Deliverable**: new suite `test/audio/audio_nextreg_test.cpp`. Register
in `test/CMakeLists.txt` + `Makefile`.

**Coverage per NR handler**:

| NR | Rows | Behaviour | VHDL |
|---|---|---|---|
| 0x06 bits [1:0] | NR-01..05 | PSG mode YM/AY/alias/reset | `zxnext.vhd:5163-5170, 6379, 6389` |
| 0x06 bit 6 | NR-06 | internal speaker beep enable | `zxnext.vhd:6504` |
| 0x08 | NR-10..14 | stereo / speaker / DAC / turbosound / issue2 | `zxnext.vhd:5176-5182` |
| 0x09 | NR-20, NR-21 | per-PSG mono flags | `zxnext.vhd:5186` |
| 0x2C | NR-30 | DAC left channel NR mirror | `zxnext.vhd:4852` |
| 0x2D | NR-31 | DAC mono channel NR mirror | `zxnext.vhd:4853` |
| 0x2E | NR-32 | DAC right channel NR mirror | `zxnext.vhd:4854` |
| 0x06 b6 + 0x08 b4 | BP-10..13, MX-03/20/22 | `beep_spkr_excl` derivation + EAR/MIC gating | `zxnext.vhd:6503, 6504` |
| 0x08 b3 | SD-17, IO-10 | `dac_hw_en` gate | `zxnext.vhd:5179, 6436` |

**Test pattern**: build `Emulator`, use `nextreg_write(reg, data)` and
observe downstream state (`ay_chip().chip_mode()`, `mixer().exc_mode()`,
`dac().enabled()`, etc.). Where an accessor doesn't exist on the src
object, **prefer extending the accessor read-only** (no new state, just
`const` getter over an existing field) rather than restructuring the
class.

In `audio_test.cpp`, convert NR/BP/MX/SD/IO rows to `// RE-HOME:`
comments pointing at the new suite.

### Wave D — Input/ULA integration (SMALL)

**Rows**: 5 — BP-04, BP-20, BP-21, BP-22, BP-23.

**Deliverable**: extend `test/input/input_integration_test.cpp` with a new
**Group FE-READ** (5 rows). Do NOT create a new suite — these rows exist
at the keyboard-matrix + EAR_in + fixed-bits boundary.

**Coverage per row**:

| Row | Test | VHDL |
|---|---|---|
| BP-04 | IN (0xFE), A → border not present in data bits (bits [2:0] are ULA-side border output, NOT read-back) | `zxnext.vhd:3604` |
| BP-20 | IN (0xFE), A → EAR (bit 6) OR port_fe_ear | `zxnext.vhd:3453` |
| BP-21 | bit 5 fixed-high | `zxnext.vhd:3460` |
| BP-22 | bits [4:0] = keyboard column mux for A[15:8] | `zxnext.vhd:3463-3468` |
| BP-23 | bit 7 fixed-high | `zxnext.vhd:3467` |

**Test pattern**: build `Emulator` with `Keyboard` extension, press/release
keys via `keyboard().press(row, col)`, observe `cpu_in_port(0xFE)`
responses.

In `audio_test.cpp`, convert BP-04, BP-20..23 rows to `// RE-HOME:`
comments pointing at the extended Input integration suite.

### Phase 2 totals

| Wave | Rows flipped (check) | New integration rows | Size |
|---|---:|---:|---|
| A — I2S flip | 1 | — | S |
| B — port dispatch | 1 (re-home comment is not a flip) | 16 | L |
| C — NextREG | 1 (ditto) | 20 | L |
| D — Input/ULA | — | 5 | S |
| **Total** | **3** | **41** | |

Plus `audio_test.cpp` loses 41 `skip()` calls (re-homed rows get
`// RE-HOME:` comments instead).

Expected post-Phase-2 `audio_test.cpp`: 175/127/0/48 → **~134/128/0/6**
(loses 41 rows to re-home; 1 flipped to check on MX-06; 6 remain as
G-class / F-class skips).

New suites:
- `audio_port_dispatch_test.cpp`: 16/16/0/0.
- `audio_nextreg_test.cpp`: 20/20/0/0.

### Phase 2 actuals + Waves E/F expansion (2026-04-24)

Phase 2 as originally scoped left gaps that drove two additional waves:

- **Wave B discovery**: of 17 port-dispatch rows, 10 passed on first
  build; 7 (SD-10, SD-12, SD-14, SD-15, IO-03, IO-04, IO-05) hit
  genuine emulator port-decode gaps. Re-skipped as F-class for Wave F.
- **Wave C scope creep**: the NextReg::write() handlers for NR 0x06
  bit-decode, NR 0x2C/2D/2E were missing or buggy. Wave C added
  minimal VHDL-faithful handlers + const getters (`is_ay_mode`,
  `stereo_mode`, `mono_mode`, `dac_enabled`, `beep_spkr_excl`,
  `nr_06_internal_speaker_beep`). Also fixed a pre-existing bug:
  NR 0x06 aymode decode was `(v & 0x03) == 1` (mode 01 only) —
  corrected to `(v & 0x01)` per `zxnext.vhd:6389` (aymode_i =
  psg_mode[0]; modes 01 and 11 both set aymode=1, with mode 11 also
  firing the separate `audio_ay_reset` path).
- **Wave D critic**: BP-04 "border bits [2:0] not exposed" assertion
  was weak (`low3 == 0x07` can pass by accident). Phase 3 strengthened
  by sweeping border=0/5/7 and asserting full byte = 0xFF on all three.
- **Phase 0 fixup**: user re-audit downgraded 4 A-class TS rows
  (TS-24, TS-32, TS-33, TS-34) to F-skips since aggregate TS-30/31
  can't prove per-PSG isolation or one-bit-governs-all-three
  semantics. Wave E picked these up.

#### Wave E — TS per-PSG isolation (4 rows, test-only)

Distinct-amplitude setups: 3 PSGs with vol_ay amplitudes {0xFF, 0x41,
0x0F}. TS-24 proves stereo_mode governs ALL three panners by
asserting `L_abc > 0 && L_acb == 0` (channel C zeroed, ACB mode maps
all three PSGs to silent L). TS-32/33/34 prove per-PSG isolation by
silencing one PSG's R8 and asserting the aggregate L drops by exactly
that PSG's contribution. Commit `a184010`.

#### Wave F — port-dispatch gap fixes (src/ + test, 6 of 7)

Real emulator bugs surfaced by Wave B:

- **SD-10** — new `0xFFFF`/`0x005F` handler → `dac_.write_channel(3,v)`.
  Avoids sprite-pattern 0x5B overlap via 16-bit match.
- **SD-12** — new `0xFFFF`/`0x003F` handler → `dac_.write_channel(0,v)`.
- **SD-14** — reworked existing 0xFB handler. VHDL `zxnext.vhd:2433`:
  `port_dac_mono_AD_fb_io_en = NR 0x84 bit 5 AND NOT bit 2`. Default
  NR 0x84=0xFF keeps the mono gate CLOSED (bit 2 set); test opens it
  via NR 0x84=0x20 and verifies ch A fan-out alongside existing ch D.
  Unconditional fan-out would have regressed the Wave B SD-11 row.
- **SD-15** — new `0xFFFF`/`0x00B3` handler → ch B+C mono fan-out
  (GS Covox).
- **IO-03** — new `0xC00F`/`0x8005` handler (more specific than
  BFFD's `0xC007`/`0x8005`) routes BFF5 reads to
  `turbosound_.reg_read(true)` per `zxnext.vhd:2649`.
- **IO-05** — existing BFFD handler's read callback gated on
  `config_.type == ZX_PLUS3` per `zxnext.vhd:2771` (BFFD aliases FFFD
  read on +3 only).
- **IO-04** — demoted to `// G:` comment. VHDL `zxnext.vhd:2771`
  composes `port_fffd_rd` from FFFD/BFFD/BFF5 into the same
  `port_fffd_dat`; falling-edge latching is an internal VHDL clock-
  domain artefact invisible at Z80 instruction-boundary granularity.

Architectural surprises surfaced by Wave F:

- AY#0 ID is **3** (not 0) per `TurboSound` ctor order
  `AyChip(3), AyChip(2), AyChip(1)`.
- AY register 1 reads are 4-bit masked per `ay_chip.cpp:86-87`.
- `reg_read(bool reg_mode)` already existed on TurboSound — no new API.

Commit `e86b93d`.

Extended suite:
- `input_integration_test.cpp`: 7/5/0/2 → 12/10/0/2 (adds 5 pass rows).

## Phase 3 — un-skip + plan-doc refresh (self)

Per-wave critics have already APPROVED before merge. Phase 3:

1. Audit the ~6 residual skips in `audio_test.cpp` after Phase 2 merges:
   - AY-30..34 (5) + AY-41 (1) — all G-class, already commented in
     Phase 0; verify no runtime `skip()` calls remain.
   - SD-09 (1) — G-class.
   - AY-43, AY-64 (2) — G-class.
   - Any residual MX/BP/IO that didn't fit a wave.
2. Refresh `doc/testing/AUDIO-TEST-PLAN-DESIGN.md` Current Status block
   to live numbers.
3. Launch retrospective cross-wave critic (per
   `feedback_never_self_review.md`).

**Expected Phase 3 state**:
- `audio_test`: ~134/128/0/6 (all G-class; zero F-class skips).
- `audio_port_dispatch_test` (new): 16/16/0/0.
- `audio_nextreg_test` (new): 20/20/0/0.
- `input_integration_test`: 12/10/0/2.

## Phase 4 — dashboard + audit (self + independent audit)

Mirror `task3-uart-i2c-phase4.md`:

1. Update `test/SUBSYSTEM-TESTS-STATUS.md`:
   - Audio row → new numbers.
   - Add `Audio (port dispatch)` row.
   - Add `Audio (NextREG)` row.
   - Update Input (int) row to reflect +5 new pass rows.
   - Recompute aggregate: expected **~3303/3087/0/216** → **~3318/3128/0/6**
     across **29 suites**. (net: +15 total rows = 41 re-home + 5 I2S + 16/20
     new suite rows = +41 integration passes, -25 skip demoted to comments,
     -42 F-class re-homed out of audio_test, +1 MX-06 flip).
2. Update `doc/testing/TRACEABILITY-MATRIX.md` — per-row statuses, new
   suite summary rows.
3. Update `doc/design/EMULATOR-DESIGN-PLAN.md` Phase-9 Audio entry
   to `[x]` with plan summary.
4. Update `doc/testing/AUDIO-TEST-PLAN-DESIGN.md` "Current status" block.
5. **Independent audit agent** (different instance per
   `feedback_never_self_review.md`) produces
   `doc/testing/audits/task3-audio-phase4.md` with:
   - phase summary table
   - row-count evolution
   - plan-drifts caught
   - bugs surfaced (none expected in audio — no new state-machine logic)
   - WONT / G-class decisions
   - residual F-skip list (target: 0)
6. Update `.prompts/<today>.md` Task Completion Status.
7. Update MEMORY.md session handover.

## Backlog / future work (not in scope)

1. **MX-06 stub upgrade to real I2S emulation** — stub models the 13-bit
   contribution path but not the Pi-driven I2S protocol. Real
   emulation needs a driver producing samples at the I2S clock rate,
   backed by host audio input or a test-file WAV stream. Estimated
   ~40h including VHDL audit of the Pi-I2S clock and buffer semantics.
   Not blocking any real user software (NextZXOS doesn't use it; games
   that drive Pi I2S are rare).
2. **AY port_a_i / port_b_i emulation** — 5 G-class rows (AY-30..34).
   Would unblock keyboard-over-PSG-I/O-port scenarios (vintage Speccy
   software using AY as GPIO). Requires API extension on `AyChip`.
   No known ZX Next software uses this path.
3. **AY I_SEL_L=0 /16 divider path** — 1 G-class row (AY-41). Would
   require adding a selectable clock divider in `AyChip::tick()`.
   `turbosound.vhd:164` hard-wires I_SEL_L='1' so there's no VHDL path
   that exercises /16 — a pure defensive backlog item.
4. **SD-09 per-clock write priority model** — requires architectural
   refactor to have `Dac` process time-ordered write events (currently
   last-write-wins in a frame). Out of scope.

## Risk register

| ID | Risk | Mitigation |
|---|---|---|
| R1 | Phase 0 E-class audit misclassifies an A-row as not-really-covered-elsewhere | Independent critic verifies claimed coverage by reading the cited row. |
| R2 | Phase 1 I2S stub overflow at 13-bit mixer sum | Stub clamps input to 10 bits; mixer sum already uses 32-bit intermediate. |
| R3 | Wave A race with Phase 1 — Wave A depends on Phase 1 landing first | Sequential: Phase 1 must merge before Wave A agent starts. |
| R4 | Wave B/C accessor-addition requirements creep | Agents add ONLY const getters; no new state, no API refactors. If an accessor requires new state, that's out of scope — re-skip with F reason pointing to this plan. |
| R5 | Wave D depends on Input plan keyboard matrix being stable | Input plan closed 2026-04-22; matrix is on main, unchanged since. No rebase pressure. |
| R6 | Agent-tool worktree-caching bug (2026-04-24) | Every agent must verify base = current main on entry. See `feedback_agent_worktree_stale_base.md`. |
| R7 | Wave C "NextREG handler lives in NextReg::write()" — the handler code may not actually exist yet in NextReg::write() | At Wave C kickoff, grep `src/port/nextreg.cpp` for NR 0x06/0x08/0x09/0x2C-2E handlers. If absent, Wave C adds them (it's still test-side because the VHDL semantics are tiny — single-line latches into enable flags). If any NR write requires new state, escalate to main session. |

## Timeline

| Step | Agents | Hours |
|---|---:|---:|
| Phase 0 (self, direct to main) | 1 | 1 |
| Phase 0 critic | 1 | 0.5 |
| Phase 1 (scaffold) | 1 | 2 |
| Phase 1 critic | 1 | 0.5 |
| Phase 2 Wave A/B/C/D (parallel) | 4 | ~3 each |
| Phase 2 per-wave critics (4 parallel) | 4 | 0.5 each |
| Phase 3 un-skip | 1 | 1 |
| Phase 3 retrospective critic | 1 | 0.5 |
| Phase 4 dashboard | 1 | 0.5 |
| Phase 4 audit agent | 1 | 1 |
| **Total wall-clock** | — | **~8 hours** |

Single-session close plausible if no surprises surface.

## Launch order

1. **Phase 0** — main session, direct to main, test-code only.
2. **Phase 0 critic** — launch immediately after Phase 0 commit.
3. **Phase 1** — single agent in worktree, scaffold the I2S stub.
4. **Phase 1 critic** — launch immediately after Phase 1 merge.
5. **Phase 2 Waves A + B + C + D** — 4 parallel agents, all rebased to
   current main (post Phase 1). Each produces a merge-ready branch.
6. **Per-wave critics** — launched as each wave's branch is ready.
7. **Sequential merges to main**.
8. **Phase 3** — self, on main.
9. **Phase 3 retrospective critic**.
10. **Phase 4 dashboard + audit agent**.

Plan doc authored 2026-04-24. First session of execution TBD.
