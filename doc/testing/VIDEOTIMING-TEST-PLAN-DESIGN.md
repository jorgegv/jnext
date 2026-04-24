# VideoTiming Expansion Compliance Test Plan

VHDL-derived compliance test plan for the per-machine expansion of the
`VideoTiming` subsystem of the JNEXT ZX Spectrum Next emulator. All
expected behaviour is taken directly from the FPGA VHDL sources
(`video/zxula_timing.vhd`, cross-referenced with `video/zxula.vhd`),
not from the C++ implementation.

## Purpose

`VideoTiming` owns the raster-counter state shared by every display
subsystem (ULA, Layer 2, Sprites, Tilemap, Copper, compositor). The
VHDL `zxula_timing` block parameterises four per-machine timing
variants — 48K 50 Hz, 48K 60 Hz, 128K 50 Hz, +3 50 Hz, 128K 60 Hz, and
Pentagon — via a process that selects `c_min_hactive`, `c_min_vactive`,
`c_max_hc`, `c_max_vc`, `c_int_h`, and `c_int_v` from `i_timing` and
`i_50_60` (`zxula_timing.vhd:147-312`). The current `VideoTiming`
exposes `hc_max_`/`vc_max_` for each of 48K, 128K/+3, Pentagon, but
does **not** expose the per-machine active-display origin
(`c_min_hactive`/`c_min_vactive`), does not expose the
12-cycle-prefetch ULA origin (`c_min_hactive - 12`), and does not
expose per-machine interrupt position (`c_int_h`/`c_int_v`). 60 Hz
variants are not modelled at all.

This plan defines the compliance surface for the per-machine accessors
owned by `VideoTiming`, under the VHDL-as-oracle rule in
[doc/testing/UNIT-TEST-PLAN-EXECUTION.md](UNIT-TEST-PLAN-EXECUTION.md).
Every row's expected value is a direct reading of
`zxula_timing.vhd`; a row that fails is a claim about the emulator,
never about the plan.

## Current status

0 rows live. The plan opens with **7 rows re-homed from**
`test/ula/ula_test.cpp` **§13 + §14** during the ULA Phase-0 closure
on 2026-04-23 (see Task `doc/design/TASK-VIDEOTIMING-EXPANSION-PLAN.md`
— the authoritative re-home manifest). Rows are labelled `VT-NN`
within this plan; the prior `S13.05`/`S13.06`/`S13.07`/`S13.08` and
`S14.01`/`S14.02`/`S14.03` IDs are preserved in the "Origin" column
for traceability.

- `VideoTiming` class exists at `src/video/timing.h:29-160` +
  `src/video/timing.cpp:1-103`; reset/init/advance/frame-done and the
  48K-flavour `DISPLAY_LEFT/TOP/W/H` display-window constants are
  live, along with test-only line-interrupt pulse counters.
- Neither per-machine display origin nor per-machine interrupt
  position is exposed. **All 7 rows start as `skip()` with an
  `F-VT-ACCESSOR`-class reason** and flip to live `check()` when the
  accessors land (phase ladder in `TASK-VIDEOTIMING-EXPANSION-PLAN.md`
  §Approach).
- No existing failing rows in `videotiming_test.cpp` (the suite does
  not yet exist; the test-plan-execution protocol requires the
  skip-scaffolded suite to materialise before any row flip).
- The Wave E pulse-counter surface already added to the class during
  the ULA plan closure (`src/video/timing.h:97-134`,
  `src/video/timing.cpp:53-94`) is **test-only dead code in
  production** — see the "Coupling with production-wiring backlog"
  section below.

## Scope

| Area                                              | VHDL Source                                       | Section |
|---------------------------------------------------|---------------------------------------------------|---------|
| Per-machine max_hc/max_vc (frame envelope)        | `zxula_timing.vhd:147-312`                        | 1       |
| Per-machine active-display origin (hactive/vactive) | `zxula_timing.vhd:147-312`                      | 2       |
| ULA prefetch origin (`hactive - 12`) / vc reset   | `zxula_timing.vhd:423-451`                        | 3       |
| Per-machine interrupt position (`c_int_h`/`c_int_v`) | `zxula_timing.vhd:155-293`, `zxula_timing.vhd:548-557` | 4       |
| 60 Hz variant (48K / 128K)                        | `zxula_timing.vhd:214-308`                        | 5       |
| Line-interrupt target mapping (`int_line_num`)    | `zxula_timing.vhd:566-570`                        | 6       |

Section 5 covers the missing 60 Hz machine variant. Section 6 is a
neighbour-expansion from VHDL: the `i_int_line` → `int_line_num`
mapping is already implemented by the pulse-counter surface
(`src/video/timing.h:152-159`) but has no deterministic row
pinning the wrap behaviour at the per-machine `c_max_vc`; it is
called out here for un-skip because the line-interrupt target
observer is the natural consumer of the per-machine `vc_max()`
accessor and should be verified against all four machines in one
sweep.

## Architecture

### Test approach

**Unit-level tests against a bare `VideoTiming` instance** are the
default. The seven re-homed rows ask purely about static per-machine
constants and the one-shot pulse conditions; they do not require a
full `Emulator` fixture and do not exercise cross-subsystem wiring.
The canonical harness pattern is:

1. Construct a fresh `VideoTiming`.
2. Call `init(MachineType::ZX48K | ZX128K | ZX_PLUS3 | PENTAGON)` —
   and, where the new 60 Hz accessor lands, a 60 Hz variant toggle.
3. Read the new accessors (`display_origin()`,
   `ula_prefetch_origin()`, `int_position()`) and compare byte-for-byte
   against the VHDL-derived constants.
4. For pulse-counter rows: drive `advance(tstates)` and read back
   `ula_int_pulse_count()` / `line_int_pulse_count()`.

The suite lives at `test/videotiming/videotiming_test.cpp` and
registers itself in `test/CMakeLists.txt` + the top-level `Makefile`
alongside `ula_test` / `ctc_test`. The scaffolding in the first
commit of this plan's execution will register 7 `skip()` rows so the
dashboard pass/fail counters stay honest. Flip to `check()` happens
only when the corresponding production accessor is committed
(1:1:1 per UNIT-TEST-PLAN-EXECUTION §4).

### Phase 0 design question — accessors on `VideoTiming` vs merged onto `Emulator`

`TASK-VIDEOTIMING-EXPANSION-PLAN.md` §Phase 0 leaves open: per-machine
origins + int-positions either (a) as lookup tables on `VideoTiming`
or (b) merged with `MachineTiming` on `Emulator`. **This plan
commits to option (a)** — `VideoTiming`-owned accessors — on three
grounds:

- `VideoTiming::init(MachineType)` already owns the per-machine
  `hc_max_`/`vc_max_` switch (`src/video/timing.cpp:17-44`). Adding a
  sibling switch for origin and int-position keeps the
  per-machine constant table in one place.
- The seven re-homed rows all ask questions that the VHDL answers
  inside `zxula_timing`; a bare `VideoTiming` instance is the minimal
  oracle.
- Option (b) is strictly weaker for unit-test isolation — it would
  require a full `Emulator` fixture in the suite for pure
  combinational queries.

Should a later refactor unify `VideoTiming` with a new
`MachineTiming` record on `Emulator`, the accessors can be forwarded
without breaking this suite. See the coupling section below for the
one case where option (b) might win.

### File layout

```
test/
  videotiming/
    videotiming_test.cpp          # Unit-level test runner (new suite)
doc/testing/
  VIDEOTIMING-TEST-PLAN-DESIGN.md # This document
doc/design/
  TASK-VIDEOTIMING-EXPANSION-PLAN.md  # 7-row re-home manifest (authoritative scope)
```

## Section 1: Per-machine frame envelope (max_hc / max_vc)

### VHDL reference

The timing process in `zxula_timing.vhd:147-312` drives
`c_max_hc`/`c_max_vc` from `i_timing` and `i_50_60`. Values:

| Machine      | `c_max_hc` | `c_max_vc` | VHDL cite                |
|--------------|-----------:|-----------:|--------------------------|
| 48K 50 Hz    | 447        | 311        | `zxula_timing.vhd:262,270` |
| 48K 60 Hz    | 447        | 263        | `zxula_timing.vhd:290,298` |
| 128K 50 Hz   | 455        | 310        | `zxula_timing.vhd:196,204` |
| +3 50 Hz     | 455        | 310        | `zxula_timing.vhd:196,204` (with `i_timing(0)='1'` selecting +3 `c_int_h`) |
| 128K 60 Hz   | 455        | 263        | `zxula_timing.vhd:230,238` |
| Pentagon     | 447        | 319        | `zxula_timing.vhd:160,168` |

The C++ `VideoTiming::hc_max()`/`vc_max()` already return these for
48K / 128K / +3 / Pentagon / ZXN_ISSUE2
(`src/video/timing.cpp:17-44`); rows VT-01..VT-03 pin the mapping as
a precondition for the remaining sections and make the classification
check deterministic for future refactors. (Pixel-ticks-per-line is
`c_max_hc + 1`; the `VideoTiming::init` comments call this "448 /
456 / 448", VHDL values are one lower.)

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-01  | new    | 48K `hc_max()` and `vc_max()` after `init(ZX48K)` | 447, 311 | skip (F-VT-ACCESSOR) |
| 2 | VT-02  | new    | 128K `hc_max()` and `vc_max()` after `init(ZX128K)` | 455, 310 | skip (F-VT-ACCESSOR) |
| 3 | VT-03  | new    | Pentagon `hc_max()` and `vc_max()` after `init(PENTAGON)` | 447, 319 | skip (F-VT-ACCESSOR) |

Rows 1-3 are justified as neighbour-expansion: they pin the
VHDL-faithful value that the C++ `init()` comments currently quote
as "448 / 456 / 448 ticks-per-line" (off-by-one in the narrative vs
the VHDL `c_max_*` convention, which is max-reached-before-wrap, not
count). They are cheap, catch future drift, and are the precondition
for every origin/int-position row that follows.

## Section 2: Per-machine active-display origin

### VHDL reference

`c_min_hactive` / `c_min_vactive` define the top-left pixel of the
256x192 active-display window. The VHDL selects per machine (all
cites `zxula_timing.vhd`):

| Machine      | `c_min_hactive` | `c_min_vactive` | VHDL cite   |
|--------------|----------------:|----------------:|-------------|
| 48K 50 Hz    | 128             | 64              | :261, :269  |
| 48K 60 Hz    | 128             | 40              | :289, :297  |
| 128K 50 Hz   | 136             | 64              | :195, :203  |
| +3 50 Hz    | 136             | 64              | :195, :203  |
| 128K 60 Hz   | 136             | 40              | :229, :237  |
| Pentagon     | 128             | 80              | :159, :167  |

The current C++ class exposes only the 48K values as
`DISPLAY_LEFT = 128` / `DISPLAY_TOP = 64`
(`src/video/timing.h:36-37`). Pentagon's `min_vactive=80` and 128K's
`min_hactive=136` are absent. The proposed accessor is
`RasterPos VideoTiming::display_origin() const` returning
`{c_min_hactive, c_min_vactive}` for the current machine
(option (a) above). `in_display()` in
`src/video/timing.cpp:99-103` should also become machine-parameterised
as a natural follow-on, but that is not in this plan's re-home scope.

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-04  | S13.05 | 128K `display_origin()` after `init(ZX128K)` | hc=136, vc=64 | skip (F-VT-ACCESSOR) |
| 2 | VT-05  | S13.06 | Pentagon `display_origin()` after `init(PENTAGON)` | hc=128, vc=80 | skip (F-VT-ACCESSOR) |
| 3 | VT-06  | new    | 48K `display_origin()` after `init(ZX48K)` — symmetry baseline | hc=128, vc=64 | skip (F-VT-ACCESSOR) |

VT-06 is the symmetry partner of VT-04/05; it pins the 48K VHDL
constant (currently the class's de-facto default) so the three
50 Hz machines form a complete per-machine sweep. It is a direct
VHDL reading (`zxula_timing.vhd:261,269`), not plan invention.

## Section 3: ULA prefetch origin and vc_ula reset

### VHDL reference

The ULA fetches display data 12 pixel ticks early
(`zxula_timing.vhd:423`):

```vhdl
ula_min_hactive <= c_min_hactive - 12;
ula_max_hc <= '1' when hc = ula_min_hactive else '0';
ula_min_vactive <= '1' when vc = c_min_vactive else '0';
```

`hc_ula` resets to 0 at `ula_min_hactive`
(`zxula_timing.vhd:429-436`); `vc_ula` resets at `ula_min_vactive`
(`zxula_timing.vhd:441-451`). This 12-cycle prefetch is where
`zxula.vhd`'s `shift_reg_32` lane alignment is kicked off (see
`zxula.vhd:140-180` for the downstream `shift_reg_ld`
consumption) — cross-referenced per prompt requirement. The
prefetch is a constant VHDL offset independent of machine, so all
four machines must satisfy the same `ula_min_hactive == c_min_hactive
- 12` invariant.

The proposed accessor is `int VideoTiming::ula_prefetch_origin_hc()
const` returning `display_origin().hc - 12` (or equivalently the raw
`c_min_hactive - 12` value for the active machine). `vc_ula`
resetting at `c_min_vactive` is covered by `display_origin().vc`;
no new accessor is needed there.

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-07  | S13.07 | 48K `ula_prefetch_origin_hc()` = 128 - 12 = 116 | 116 | skip (F-VT-ACCESSOR) |
| 2 | VT-08  | new    | 128K `ula_prefetch_origin_hc()` = 136 - 12 = 124 | 124 | skip (F-VT-ACCESSOR) |
| 3 | VT-09  | new    | Pentagon `ula_prefetch_origin_hc()` = 128 - 12 = 116 | 116 | skip (F-VT-ACCESSOR) |

VT-08 and VT-09 are neighbour-expansion of the re-homed S13.07 row:
the VHDL applies the `-12` offset to every machine (constant
appears literally at `zxula_timing.vhd:423`), so a per-machine
sweep is the honest VHDL reading. All three check a single
arithmetic relation (`display_origin().hc - 12`) against the
accessor, catching future drift should someone hard-code `116`.

## Section 4: Per-machine interrupt position

### VHDL reference

`c_int_h` / `c_int_v` define the `(hc, vc)` at which the per-frame
ULA interrupt pulse fires (`zxula_timing.vhd:548-557`):

```vhdl
if (i_inten_ula_n = '0') and (hc = c_int_h) and (vc = c_int_v) then
   int_ula <= '1';
else
   int_ula <= '0';
end if;
```

Values, read directly from the timing process:

| Machine      | `c_int_h` (expression)      | `c_int_h` (value) | `c_int_v` | VHDL cite    |
|--------------|------------------------------|------------------:|----------:|--------------|
| 48K 50 Hz    | `128 + 0 - 12`               | 116               | 0         | :257, :265   |
| 48K 60 Hz    | `128 + 0 - 12`               | 116               | 0         | :285, :293   |
| 128K 50 Hz   | `136 + 4 - 12`               | 128               | 1         | :187, :199   |
| +3 50 Hz     | `136 + 2 - 12`               | 126               | 1         | :189, :199   |
| 128K 60 Hz   | `136 + 4 - 12` (128K)        | 128               | 0         | :221, :233   |
| 128K 60 Hz   | `136 + 2 - 12` (+3)          | 126               | 0         | :223, :233   |
| Pentagon     | `448 + 3 - 12`               | 439               | 319       | :155, :163   |

Proposed accessor: `RasterPos VideoTiming::int_position() const`
returning `{c_int_h, c_int_v}` for the current machine. Note the
`-12` relative shift: VHDL references `c_int_h` in the same
`hc`-domain as `c_min_hactive` (both come from the shared `hc`
counter, not `hc_ula`). The emulator's existing scheduler asks the
"where does the IRQ fire" question via local fields on `Emulator`
(`emulator.cpp:2138, :2154` per the 2026-04-23 prompt backlog
annotation), so un-skipping VT-10..VT-12 requires Phase 1 scaffold
plus the scheduler remaining the sole production consumer — the
accessor is an observation surface, not a behaviour change.

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-10  | S14.01 | 48K `int_position()` after `init(ZX48K)` | hc=116, vc=0 | skip (F-VT-ACCESSOR) |
| 2 | VT-11  | S14.02 | 128K `int_position()` after `init(ZX128K)` | hc=128, vc=1 | skip (F-VT-ACCESSOR) |
| 3 | VT-12  | S14.03 | Pentagon `int_position()` after `init(PENTAGON)` | hc=439, vc=319 | skip (F-VT-ACCESSOR) |
| 4 | VT-13  | new    | +3 `int_position()` after `init(ZX_PLUS3)` — VHDL `i_timing(0)='1'` selects the 126 variant | hc=126, vc=1 | skip (F-VT-ACCESSOR) |

VT-13 is the VHDL-justified neighbour of VT-11: the same
`zxula_timing.vhd:186-190` block distinguishes 128K (`c_int_h=128`)
from +3 (`c_int_h=126`). A plan that tests 128K but skips +3
silently claims these machines are equivalent — they are not.

## Section 5: 60 Hz variant

### VHDL reference

`i_50_60 = '1'` selects the 60 Hz variant
(`zxula_timing.vhd:214-248, :280-308`). Constants differ only in
vertical dimension + vertical-int position:

| Machine         | `c_max_vc` | `c_min_vactive` | `c_int_v` | VHDL cite   |
|-----------------|-----------:|----------------:|----------:|-------------|
| 48K 60 Hz       | 263        | 40              | 0         | :298, :297, :293 |
| 128K 60 Hz      | 263        | 40              | 0         | :238, :237, :233 |

Horizontal parameters and `c_int_h` match the 50 Hz machine. Frame
length is `(c_max_hc+1) * (c_max_vc+1) / 2` T-states:

- 48K 60 Hz: `448 * 264 / 2 = 59_136` T-states.
- 128K 60 Hz: `456 * 264 / 2 = 60_192` T-states.

(ULA plan §13 Row 8 quotes 59_136 for 48K 60 Hz —
`ULA-VIDEO-TEST-PLAN-DESIGN.md:644`. That row is now re-homed here as
VT-14.)

No 60 Hz machine enum / accessor exists in `VideoTiming` today; Phase
1 of `TASK-VIDEOTIMING-EXPANSION-PLAN.md` calls for a 60 Hz toggle
(either a new enum variant or a `set_refresh_60hz(bool)` switch).
Row VT-15 is the 128K 60 Hz symmetry partner; it is a direct
VHDL reading, not plan invention.

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-14  | S13.08 | 48K 60 Hz: `vc_max()=263`, frame length = 448 * 264 / 2 = 59_136 T-states | vc_max=263; 59_136 | skip (F-VT-ACCESSOR) |
| 2 | VT-15  | new    | 128K 60 Hz: `vc_max()=263`, frame length = 456 * 264 / 2 = 60_192 T-states | vc_max=263; 60_192 | skip (F-VT-ACCESSOR) |
| 3 | VT-16  | new    | 60 Hz `display_origin().vc` = 40 (VHDL `c_min_vactive`) for both 48K/128K 60 Hz | vc=40 | skip (F-VT-ACCESSOR) |
| 4 | VT-17  | new    | 60 Hz `int_position().vc` = 0 for both 48K 60 Hz and 128K 60 Hz | vc=0 | skip (F-VT-ACCESSOR) |

Frame-length rows use `advance(1)` stepping and observe
`frame_complete()` to confirm the wrap at the expected T-state
count, which is the existing observation pattern in the current
`VideoTiming` class (`src/video/timing.cpp:85-93`).

## Section 6: Line-interrupt target mapping

### VHDL reference

The line-interrupt target register (`i_int_line`) is mapped to the
comparison value (`zxula_timing.vhd:566-570`):

```vhdl
if i_int_line = 0 then
   int_line_num <= c_max_vc;
else
   int_line_num <= unsigned(i_int_line) - 1;
end if;
```

The pulse fires on the clock tick where `hc_ula = 255` and
`cvc = int_line_num` (`zxula_timing.vhd:577`). The C++ mirrors this
in `VideoTiming::int_line_num()` (`src/video/timing.h:155-159`) with
`vc_max() - 1` for target 0 — this is **one off** from the VHDL,
which uses `c_max_vc` (equal to `vc_max_` in the C++ naming where
the convention difference from Section 1 is applied). The discrepancy
surfaces only under a machine-parameterised read.

This is a bug-witness row rather than plan invention:
UNIT-TEST-PLAN-EXECUTION §3 requires the plan to preserve VHDL as
oracle; if the C++ differs from VHDL, the test **must fail** (not
skip) once the accessor lands.

### Test cases

| # | Row ID | Origin | Test | Expected | Status |
|---|--------|--------|------|---------:|--------|
| 1 | VT-18  | new    | 48K: target=0 → `int_line_num() == c_max_vc == 311` | 311 | skip (F-VT-ACCESSOR) |
| 2 | VT-19  | new    | 128K: target=0 → `int_line_num() == 310` | 310 | skip (F-VT-ACCESSOR) |
| 3 | VT-20  | new    | Pentagon: target=0 → `int_line_num() == 319` | 319 | skip (F-VT-ACCESSOR) |
| 4 | VT-21  | new    | Any machine: target=10 → `int_line_num() == 9` | 9 | skip (F-VT-ACCESSOR) |

Rows VT-18..VT-21 are justified from VHDL: the plan needs at least
one row pinning the target=0 special case per machine (the mapping
depends on `c_max_vc`, which is per-machine). VT-21 pins the
non-zero branch of the VHDL `if`.

**Note:** the current C++ uses `vc_max_ - 1` for target=0
(`src/video/timing.h:155-158`) — the Section 1 naming discrepancy
(C++ `vc_max_` semantics may differ from VHDL `c_max_vc`) must be
resolved during Phase 1 scaffold. If the C++ `vc_max_` already holds
311/310/319 (see Section 1 rows), the current `-1` is a bug; if it
holds 312/311/320 (tick-count semantics, not max-reached), the
current `-1` is the correct mapping and Section 1 rows need a
renaming. This plan asserts the VHDL reading (values 311/310/319)
per UNIT-TEST-PLAN-EXECUTION §1.

## Coupling with VideoTiming production-wiring backlog

`.prompts/2026-04-23.md` entry "VideoTiming pulse-counter production
wiring" (annotated "purely academic, no user-visible impact")
describes a ~60-line architectural refactor: drop the local
`line_int_enabled_`/`line_int_value_` fields on `Emulator`
(`emulator.cpp:2138, :2154`) and route the scheduler through
`VideoTiming` so the existing test-only pulse counters
(`src/video/timing.h:116-134`) become the single source of truth.

Two scenarios:

1. **Production-wiring refactor lands first.** The
   `VideoTiming::int_position()` accessor added in this plan becomes
   the natural consumer: the scheduler asks `videotiming_.next_int_pos()`
   each line. This plan's phase 1 scaffold slots in as a free
   pre-condition of the refactor.
2. **This plan lands first.** The accessors are added but read only
   by the unit test. The scheduler continues to consume local
   `Emulator` fields. A later wave merges the two state stores.

The decision criterion when a future session picks this up:

- If the session also closes the production-wiring backlog, **merge
  the two plan docs** — the accessor design and the scheduler
  refactor share a single set of regression risks (interrupt cadence,
  screenshot-test sensitivity).
- If the session closes only this plan, keep it separate. The
  accessors are pure observation, do not touch the hot path, and do
  not threaten screenshot-test interrupt cadence.

The pulse counters already present on the class
(`src/video/timing.h:97-134`, added during ULA Wave E, 2026-04-23)
are **not in this plan's scope**. They are covered by the
walked-back `S14.04/05/06` rows (now `// G:` source comments in
`test/ula/ula_test.cpp` post-ULA Phase-4 amendments); resurrecting
them is the production-wiring refactor's job.

## Nominal test count summary

| Section | Area                                | Rows |
|---------|-------------------------------------|-----:|
| 1       | Per-machine frame envelope          | 3    |
| 2       | Per-machine active-display origin   | 3    |
| 3       | ULA prefetch origin                 | 3    |
| 4       | Per-machine interrupt position      | 4    |
| 5       | 60 Hz variant                       | 4    |
| 6       | Line-interrupt target mapping       | 4    |
|         | **Total**                           | **21** |

Of these 21 rows, **7 are the direct re-homes** from ULA §13+§14
(VT-04, VT-05, VT-07, VT-10, VT-11, VT-12, VT-14 — mapped from
S13.05/06/07/08 + S14.01/02/03). The remaining **14 rows** are
VHDL-justified neighbour expansions, all citing lines the author read
during plan authoring:

- 3 (VT-01..03): per-machine `hc_max`/`vc_max` readback, the
  precondition for every other row.
- 1 (VT-06): 48K `display_origin()` symmetry partner of VT-04/05.
- 2 (VT-08..09): 128K / Pentagon ULA prefetch origin,
  neighbour-expansion of the re-homed VT-07.
- 1 (VT-13): +3 `int_position()` — VHDL-distinct from 128K, must be
  tested separately.
- 3 (VT-15..17): 128K 60 Hz frame length, 60 Hz `display_origin().vc`,
  60 Hz `int_position().vc` — VHDL provides the 60 Hz branch, so the
  re-homed VT-14 row implicitly requires its sweep.
- 4 (VT-18..21): line-interrupt target mapping — one of these is a
  bug-witness candidate per Section 6 commentary.

## Open questions

1. **Phase 0 design.** This plan commits to option (a) —
   per-machine accessors on `VideoTiming` — per the rationale in
   §Architecture. If the user prefers option (b) (merged
   `MachineTiming` record on `Emulator`), the rows still hold but
   the calling convention changes. **Recommended: ratify option (a)
   before Phase 1 scaffold.**
2. **60 Hz machine representation.** The plan assumes Phase 1 adds
   either a new `MachineType` enum variant (e.g., `ZX48K_60HZ`) or a
   `set_refresh_60hz(bool)` switch on `VideoTiming`. Which one?
   The VHDL `i_50_60` input is a runtime signal, so the switch form
   is more VHDL-faithful; an enum variant is more C++-idiomatic.
   Sub-question: how does `ZXN_ISSUE2` (Next machine) select 60 Hz?
   Current `init()` hard-codes it to 128K 50 Hz parameters
   (`src/video/timing.cpp:37-41`).
3. **`vc_max_` semantics.** Section 6 flags a potential naming
   discrepancy: the C++ `vc_max_` holds 311/310/319/312 (the
   VHDL-faithful `c_max_vc` values), but `int_line_num()` uses
   `vc_max_ - 1` for target 0, whereas VHDL uses `c_max_vc` directly
   (`zxula_timing.vhd:567`). Either the C++ `vc_max_` has a different
   convention (tick count, not max-reached), or the `-1` is a bug.
   Resolve during Phase 1 scaffold.
4. **Merge with the production-wiring backlog.** See coupling
   section. User to decide at session-pick-up time.
5. **`in_display()` per-machine**. The current
   `VideoTiming::in_display()` (`src/video/timing.cpp:99-103`)
   hard-codes 48K constants. A per-machine variant is a natural
   follow-on of Section 2 but is **not** in this plan's re-home
   scope; tag as a post-closure amendment candidate.

## Implementation notes

### Accessor surface summary (proposed)

| Accessor                                       | Returns                        | VHDL source       |
|-----------------------------------------------|--------------------------------|-------------------|
| `int VideoTiming::hc_max() const` (exists)     | `c_max_hc` per machine         | :160/196/230/262/290 |
| `int VideoTiming::vc_max() const` (exists)     | `c_max_vc` per machine         | :168/204/238/270/298 |
| `RasterPos VideoTiming::display_origin() const` (new) | `{c_min_hactive, c_min_vactive}` | :159/167/195/203/229/237/261/269/289/297 |
| `int VideoTiming::ula_prefetch_origin_hc() const` (new) | `c_min_hactive - 12`     | :423              |
| `RasterPos VideoTiming::int_position() const` (new) | `{c_int_h, c_int_v}`        | :155/163/187/189/199/221/223/233/257/265/285/293 |
| `uint16_t VideoTiming::int_line_num() const` (exists; private) | per-machine target mapping | :566-570 |

The last entry exists but is `private` in
`src/video/timing.h:155-159`. Section 6 rows require either making
it `public` or adding a narrow friend-style observer accessor such as
`uint16_t VideoTiming::line_interrupt_num() const { return int_line_num(); }`.
Phase 1 author's discretion; the rows are neutral to the naming
choice.

### Priority of test sections

1. **Critical** (blocks 7 re-homed rows from ULA plan): Sections 2,
   3, 4, 5.
2. **High** (invariant scaffolding that the above rely on):
   Section 1.
3. **Medium** (VHDL-oracle witness for pre-existing code):
   Section 6.

### Relationship to existing suites

- `test/ula/ula_test.cpp` §13+§14 — the 7 rows here originated as
  skips there. Once this suite's rows un-skip, the ULA rows stay as
  `// RE-HOME:` source-comment pointers (no duplicate coverage).
- `test/ula/ula_integration_test.cpp` — full-Emulator fixture,
  unrelated; no overlap.
- `test/ctc/ctc_test.cpp` line-interrupt rows (ULA-INT-04/06 per
  `CTC-INTERRUPTS-TEST-PLAN-DESIGN.md` §Current status) already
  consume line-interrupt state; those rows live in the CTC plan and
  do not move here. If the production-wiring refactor lands and the
  scheduler routes through `VideoTiming`, the CTC plan's ULA-INT
  rows may flip to depend on the accessors defined here — track as a
  post-coupling amendment.
