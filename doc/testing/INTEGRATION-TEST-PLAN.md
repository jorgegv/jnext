# Integration Test Plan

Task 69 (Phase 9). The per-subsystem unit and integration suites already
exist — this document is the missing piece: it catalogs them, states what
cross-subsystem behaviour each one actually proves, and says which
scenarios are *not* covered so a gap doesn't get mistaken for coverage.

Nothing below is aspirational. Every row in the table was read from the
suite's own source, not guessed from its name.

## 1. The three-layer test model

jnext's test authority is layered, and each layer answers a different
question:

| Layer | Question it answers | Oracle | Where |
|---|---|---|---|
| **Unit** | Does this one subsystem match the VHDL, in isolation? | The VHDL source at `ZX_Spectrum_Next_FPGA/cores/zxnext/src/`, cited file:line per assertion | `test/<subsystem>/<subsystem>_test.cpp` |
| **Integration** | Do two or more subsystems compose correctly when driven through the *real* wiring (port dispatch, NextREG handlers, the Emulator's `run_frame`/`execute_single_instruction`) rather than a bare class API? | The same VHDL, but at the point where signals cross subsystem boundaries (`zxnext.vhd`, not the leaf module) | `test/<subsystem>/<subsystem>_integration_test.cpp` (or a differently-named sibling — see table) |
| **Regression (golden-output)** | Does the fully composed machine — CPU + video + audio + peripherals, running real code for real frames — produce the exact same pixels/log lines it produced yesterday? | A checked-in reference PNG or a grepped log line, not VHDL directly (VHDL authority is inherited transitively through the unit/integration layers that already passed) | `test/00regression/` (see [REGRESSION-TEST-SUITE.md](REGRESSION-TEST-SUITE.md)) |

The unit layer's rules — VHDL-as-oracle, the pass/fail/skip distinction, the
1:1:1 emulator-fix-to-unskip discipline, why a skip is not a fail dressed
down — are documented in full in
[UNIT-TEST-PLAN-EXECUTION.md](UNIT-TEST-PLAN-EXECUTION.md) and apply
unchanged to the integration suites: an integration test's `check()` still
carries a VHDL citation, still distinguishes fail (wrong behaviour) from
skip (unreachable surface), and still requires independent review before a
skip/fail is flipped to pass. The only thing that changes at the
integration tier is *what* is being driven — the real Emulator object
through its real port-dispatch/NextREG/run-loop entry points, instead of a
bare subsystem class constructed directly.

The regression layer's mechanics — headless mode, the screenshot/functional
manifests, tolerance, reference generation — are in
[REGRESSION-TEST-SUITE.md](REGRESSION-TEST-SUITE.md); the taxonomy of what
each screenshot/functional row actually exercises (full-OS SD boot vs. NEX
autoload vs. z88dk probe) is in
[TEST-TAXONOMY.md](TEST-TAXONOMY.md).

Why three layers instead of two: a subsystem can be unit-correct in
isolation and still be wired wrong one level up (a NextREG handler that
calls the right accessor with the wrong bit-shift; a port-enable gate
checked in the wrong order). The unit tier cannot catch that — it never
drives the wiring. The regression tier *can* catch it, transitively, but
only as an opaque pixel diff with no VHDL citation pointing at the broken
line. The integration tier exists to close exactly that gap: assert the
cross-subsystem behaviour directly, with a citation, at the boundary where
it actually lives.

## 2. Integration suites

All row counts are pinned in `test/unit-tests.conf` (see §4) and were
cross-checked against each suite's own `set_group()` calls, not estimated.

| Suite | Rows | Subsystems it spans | Key scenarios |
|---|---|---|---|
| `copper_integration_test` | 3 | Copper, NextREG, Emulator run-loop | G117: Copper now advances once per 28 MHz cycle (not once per Z80 instruction) inside `run_frame`, so a dense burst of MOVEs (e.g. 32/scanline) completes correctly within a few instructions instead of leaking into later frames; NR 0x60–0x63 driven through the real port path; Copper vs. CPU write priority (G65); wrap behaviour at max VC (T58). |
| `mmu_integration_test` | 59 | MMU, port dispatch, NextREG, DivMMC, contention, SZX/NEX savers, tape | Port 0xEFF7 IO-enable gate; NR 0x8C Alt-ROM paging and NR-driven contention/machine-type switching (V12); Layer 2 MMU overlay enable (V13); G156 MMU hold state; full SZX and NEX snapshot save/load round-trips through the real Emulator (register + memory + paging state survives a save→load cycle); the tape-save ROM trap (G33). |
| `nextreg_integration_test` | 283 | NextREG, MMU, Palette, Renderer, DMA, IM2, Copper, DivMMC, Multiface, ULA, port dispatch | The largest suite and the de-facto cross-subsystem hub: reset-default values for registers whose default is *owned* by another subsystem class (not bare `NextReg`) verified through the real port 0x243B/0x253B path; soft-reset re-arm behaviour; DMA↔IM2 interrupt delay; per-register port-enable gating (bus and 0x1F family); the NR 0x08/NR 0x06 clock-rate/reset clusters (G56); NR 0x68 ULA+ compose; port 0xFF NR-side fan-out (G108); line interrupt NR 0x22/0x23; NMI/Multiface port-visibility coverage; 50/60 Hz frame-period selection; NR 0x03 palette sub-index and NR 0x07 expansion-bus speed (V21); XADC read stubs (V19R, hardware absent so values are pinned constants). |
| `input_integration_test` | 17 | Keyboard, tape EAR, port dispatch, joystick dispatcher, hotkeys | Port 0xFE byte assembly (`'1' & EAR & '1' & KBD_COL`) through the real port-0xFE handler, including the tape-EAR override bit; joystick-to-port wiring; host hotkey dispatch. |
| `ctc_interrupts_test` | 45 | CTC, IM2, NextREG, ULA (line interrupt), port dispatch | IM2 vector configuration via NR 0xC0–0xC4/0xC6; legacy interrupt-status reads; IM2 decoder edge cases; single-instruction stepping through an interrupt acknowledge; IM2 quiescence; CTC channel-1 accumulate mode; the 60 Hz pulse-width case; and (re-homed from the ULA unit suite) ULA line-interrupt generation, because it is really "does the ULA's line match feed IM2's vector logic correctly," not a bare-ULA property. |
| `uart_integration_test` | 16 | UART, I2C, IM2, NextREG, port dispatch | NR 0xC6/0x83 → IM2 UART0/1 RX/TX priority-slot wiring; port gating on `internal_port_enable` bits 10 (I2C) and 12 (UART); dual-channel independence; NR 0xA0 interrupt-status readback. |
| `compositor_integration_test` | 8 | Compositor/Renderer, ULA, NextREG, Copper | NR 0x68 bit 7 ULA-disable reaching the actual render pipeline output, both via a direct NR write and via a *mid-frame Copper MOVE* (proving the Copper's raster-timed write lands at the right scanline in the composited output, not just in the NR shadow); port 0xFF `port_ff_reg` fan-out from NR 0x69/0x22/0xC4 (G108 — this is the compositor's own copy of the same signal `nextreg_integration_test`'s G108 group pins from the NextREG side; both are legitimate, they assert the same wire from its two owning classes). |
| `ula_integration_test` | 12 | ULA, NextREG, port dispatch | ULA scroll and ULA+/ULAnext palette/mode registers (NR 0x26/0x27/0x42/0x43/0x68) through their real NR handlers; ULA+ enable latch via ports 0xBF3B/0xFF3B; shadow-screen bank selection reaching the ULA's active-screen read (the MMU side of the same scenario is in `mmu_integration_test`/`mmu_test`); standard-vs-Alt-ROM screen-mode interaction. |
| `audio_nextreg_test` | 33 | NextREG, AY/TurboSound, DAC, Beeper, Mixer | NR 0x06 PSG-mode fan-out (YM/AY select, alias, AY reset) and its bit-6 speaker-beep latch; NR 0x08 stereo/speaker/DAC/TurboSound/Issue-2 bits; NR 0x09 per-chip mono routing; NR 0x2C–0x2E Soundrive channel mirrors; NR 0xA2; DAC hardware-enable gating. |
| `audio_port_dispatch_test` | 23 | Audio (DAC/AY/Beeper/Mixer), port dispatch, NextREG gating | DAC/AY/beeper port handlers registered by the Emulator (not the bare peripheral classes); NR 0x08 bit 3 gating the DAC write path; NR 0x84 bit 0 gating the AY port open/closed; Soundrive port aliases (SD2). |
| `nmi_integration_test` | 9 | NmiSource, DivMMC, Multiface, Z80 CPU, IM2/host hotkeys | The full button/software-NMI chain end to end on a real `Emulator`: `NmiSource` FSM latches a producer (DivMMC button, Multiface button, or software `NR 0x02` request) → the VHDL arbitration strobe reaches `DivMmc::set_button_nmi` → the Z80's `/NMI` line is asserted → the CPU actually takes the NMI and lands at PC 0x0066 with the correct ROM/RAM overlay already automapped → `RETN` clears the latch. Also covers the host F9/F10/F1/F4 hotkey dispatchers reaching the same pipeline (not just the internal `strobe_*` calls unit tests use directly). |

`nmi_test` (57 rows, declared in `test/unit-tests.conf` as a bare unit
suite) is `nmi_integration_test`'s unit-tier sibling: it drives
`NmiSource`/`DivMmc`/`Multiface` directly (RST/NR02/HK/MF/BOOT/DIS/CLR/
GATE/DMA groups) without a live CPU taking the interrupt. The split exists
because most of the NMI arbitration logic (priority, gating, latch timing)
is observable without a running Z80; only the "does the interrupt actually
land and automap actually engage" question needs the full machine, and that
question is what `nmi_integration_test` answers.

## 3. Cross-subsystem scenarios, narrated

The table above is per-suite; this section is per-scenario, because the
same wire is sometimes exercised from more than one owning class and the
per-suite view can make that look like duplication when it is really two
independent assertions on the same signal.

- **CTC → IM2 interrupt delivery.** `ctc_interrupts_test`'s `NR-C0-C4-C6`,
  `IM2-Decoder-Gaps`, `SingleStep` and `CIM2-Quiescence` groups drive a CTC
  channel to underflow through the real NextREG interrupt-mask registers and
  assert that IM2 picks the correct vector, honours priority, and settles
  (no spurious re-fire) — the bare `ctc_test.cpp`/unit `im2_*` tests cannot
  reach this because the interaction lives one level up, in
  `Emulator`'s wiring of `Ctc` to `Im2Controller`.
- **NextREG → audio.** `audio_nextreg_test` proves that writing NR
  0x06/0x08/0x09/0x2C-0x2E through the real port path changes the state a
  real `AyChip`/`TurboSound`/`Dac`/`Beeper`/`Mixer` instance reports —
  the bare `audio_test.cpp` cannot drive NR registers because it constructs
  those classes directly, with no `NextReg` in the loop.
- **MMU shadow-screen routing ↔ ULA.** Split across two integration suites
  by ownership: `mmu_integration_test`/`mmu_test` assert the MMU-side bank
  selection for the shadow (0x6000) vs. standard (0x4000) screen; `ula_
  integration_test`'s `INT-SHADOW` group asserts that the ULA's active-
  screen *read* actually follows that selection when rendering. Together
  they cover the full producer→consumer chain; neither alone would.
- **DivMMC/NMI ↔ Multiface.** `nmi_integration_test`'s `INT` and `HOST-HK`
  groups are the only place in the suite where a DivMMC-sourced or
  Multiface-sourced NMI is proven to actually interrupt a *live* CPU and
  automap the correct ROM overlay — see §2 above. The narrower per-producer
  gating/latch logic lives in the unit tier (`nmi_test`, `divmmc_test`
  NM-01..08, `copper_test` ARB-06, `ctc_test` DMA-04) per the cross-
  reference list in `nmi_integration_test.cpp`'s own header comment.
- **Copper ↔ render pipeline timing.** `compositor_integration_test`'s
  `UDIS-INT` group is the one integration-tier proof that a *mid-frame*
  Copper `MOVE` (not just a direct NR write) lands in the composited output
  at the raster line its preceding `WAIT` specified — i.e. that
  `Copper::execute` being driven per raster position inside `run_frame`
  actually produces the right pixel-visible effect, not just the right NR
  shadow value.
- **Port-enable gating as a cross-cutting concern.** Several suites
  (`uart_integration_test`'s `GATE` group, `audio_port_dispatch_test`'s
  gating checks, `nextreg_integration_test`'s `Port-Enable-0x1F`/`Port-
  Enable-Bus` groups) independently pin that a peripheral's I/O ports go
  dark when its `internal_port_enable`/NR 0x84-0x86 bit is clear — the same
  VHDL mechanism (`zxnext.vhd` port-enable bus), asserted from each gated
  peripheral's own suite rather than once centrally, because the gate's
  *effect* differs per peripheral (I2C vs. UART vs. AY vs. DivMMC).

## 4. Gaps / future

These are scenarios genuinely **not** covered at the integration tier today
— named explicitly rather than left implicit, per the project's no-
coverage-theatre rule:

- **DMA bus arbitration under a live frame.** `dma_test.cpp` drives
  `peripheral/dma.h` directly against a mock memory/bus harness (156 rows,
  22 groups) with no `Emulator`, no CPU, and no ULA in the loop. There is
  no `dma_integration_test` proving that a DMA burst mid-scanline actually
  stalls a running Z80 at an instruction boundary and that the ULA still
  renders correctly around the stall, the way `EMULATOR-DESIGN-PLAN.md`
  §5.7 describes. The design doc itself documents this as a known
  line-accurate-mode simplification (DMA modeled as a burst per logical
  operation, not mid-T-state), so this is a real, currently-unclosed gap,
  not an oversight in this document.
- **Multi-layer render-priority composition as a symbolic assertion.**
  Sprites/Layer2/Tilemap/Copper priority ordering (NR 0x15, the six blend
  modes) is exercised end-to-end only at the regression (pixel-diff) tier
  (`test/00regression`), via demo NEX files, not as a `check()`-style
  integration assertion with intermediate-state inspection. A regression
  failure there says "the picture changed," not "layer N's priority bit
  is wrong" — the compositor's own unit/integration suites (`compositor_
  test`, `compositor_integration_test`) cover the wiring logic, but not a
  full multi-layer frame composed together.
- **Full NextZXOS boot chain as a programmatic assertion.** The
  DivMMC+SPI+SD+MMU+ULA+IM2 chain that gets a real machine from FPGA boot
  ROM through TBBLUE.FW to the NextZXOS welcome screen is validated only at
  the regression screenshot tier (`boot-nextzxos-welcome`/`-menu`/
  `-splash`, "Layer 1" in [TEST-TAXONOMY.md](TEST-TAXONOMY.md)), not by any
  suite in this document. That is a deliberate layering choice — boot is
  inherently an emergent, many-subsystem property best proven by "does the
  real firmware reach the real screen," not by a synthetic integration
  harness — but it means a boot regression is diagnosed by falling back to
  `TEST-TAXONOMY.md`'s Layer 1 guidance, not by a targeted integration
  suite.
- **RZX/rewind as a cross-subsystem save/load property.** `rewind_test`
  (66 rows) exercises every subsystem's `save_state`/`load_state` pair, and
  is in that sense the broadest cross-subsystem suite in the project — but
  it is declared and reported as a unit suite (its own manifest row), not
  narrated here as an integration scenario, because it tests
  serialization round-trips rather than live signal wiring between two
  subsystems. Flagged here so its absence from §2/§3 above is not mistaken
  for an oversight.
- **Repo-hygiene note on `boot-nextzxos-dotls`'s reference (maintainer
  action, not this task's to fix).** The `boot-nextzxos-dotls` regression
  row types `.ls` and screenshots a *live SD-card directory listing*, so
  its checked-in `img/boot-nextzxos-dotls-reference.png` reflects the
  maintainer's local `roms/nextzxos-1gb-fat32fix.img` — which has
  accumulated dev-session files (DUMP.BAS, NRDUMP.BIN, SCREEN.BIN, …). A
  freshly-provisioned pristine distro image lists fewer files, so that one
  reference is not reproducible against a clean image. This is a
  pre-existing repo-hygiene item (the reference was captured against a
  non-pristine fixture), flagged here for the maintainer; regenerating any
  reference PNG requires explicit user authorization and is out of scope
  for the integration-plan task. See §6 for how hosted CI handles it.

## 5. The manifest guarantee — why an integration suite can't silently vanish

Every suite named in §2 is a row in `test/unit-tests.conf` with an exact
pinned row count (e.g. `nextreg_integration_test 283`), not just "must
exist." `test/run-unit-tests.sh` — invoked by `make unit-test` — refuses to
run at all (exit 2) if that manifest and the suites CMake actually
registered via `add_test()` disagree in either direction, and FAILs (exit
1) if a suite runs but reports a row count other than the pinned one, in
*either* direction, prints no parseable summary line, crashes, or times
out. This is not a hypothetical safeguard: three suites vanished from the
counts in one day (2026-07-12, `test/unit-tests.conf`'s own header
comment) purely by accident, each caught only because someone happened to
notice — a hand-kept list that never ran two suites for months, a `make
clean` deleting a binary and the harness printing no row instead of a
failure, and an unprovisioned worktree silently reporting 8 rows for a
26-row suite. The manifest is what makes a shrinking integration suite
(or the whole suite disappearing) a loud, blocking failure instead of a
quietly smaller "still green" number.

The harness is itself under test: `make harness-selftest` (also run every
regression as the `harness-selftest-func` functional-test row) injects
each of those fault classes against stub suites and asserts the harness
refuses/fails correctly. It exists because the harness shipped once with a
bug that only manifested when a suite failed — the one path nobody
exercises while everything is green.

The regression suite's screenshot/functional manifests
(`regression_tests.conf`, `functional_tests.conf`) apply the identical
discipline one layer up, with the same "declared count must match observed
count" assertion at the end of a full `regression.sh` run — see
[REGRESSION-TEST-SUITE.md](REGRESSION-TEST-SUITE.md).

## 6. What hosted CI runs (and the one row it excludes)

`.github/workflows/ci.yml` runs the whole triplet on `ubuntu-latest`:
`make unit-test` (all subsystem + integration suites), the FUSE Z80 suite,
and the golden-screenshot/functional regression. Because `roms/*` is
git-ignored, the workflow provisions the NextZXOS SD image at job time by
running jnext's own `--sdcard-download-confirm` flow — the same sanctioned
download+FAT32-patch path an end user hits — which yields a **pristine**
distro image.

The regression step there is deliberately two-phase:

1. `regression.sh --preflight-only` — asserts the manifest pins agree
   (every declared screenshot has a checked-in reference and vice-versa,
   the declared functional count matches). This is the declared-total
   integrity check; it needs no SD image.
2. A **filtered** run of every declared screenshot + functional row
   **except `boot-nextzxos-dotls`**. That single row screenshots a live SD
   directory listing (see §4's repo-hygiene note), whose reference was
   captured against the maintainer's non-pristine local fixture and
   therefore legitimately diverges from a freshly-provisioned pristine
   image at the suite's 0-pixel tolerance. welcome/menu/splash and every
   other row are pixel-identical against the pristine image; only the
   directory-listing row is content-dependent. It is verified locally by
   the maintainer against the canonical fixture instead of in hosted CI.

Passing positional filter args makes `regression.sh` treat step 2 as a
partial run and skip its own grand-total witness — which is exactly why
step 1 runs first, so manifest integrity is still asserted on every CI run.
The maintainer's local **full** `make regression` (no filter) still runs
all rows including `boot-nextzxos-dotls` and still enforces the full
declared-total witness; nothing about the CI filter weakens the local gate.
