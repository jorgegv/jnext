# Known Functionality Gaps and Proposed Implementation Plan

Authored 2026-04-26. Aggregates known-but-deferred functionality gaps
across the JNEXT ZX Spectrum Next emulator. Sourced from 4 parallel
section scans (A: Video & GUI; B: Audio & peripherals; C:
CPU/memory/boot; D: Test infra) consolidated by an independent
reviewer pass.

This document is **descriptive**, not prescriptive — items here are
candidates for future sessions, prioritised by user impact. Each item
links back to its authoritative plan / issue / memory reference where
one exists; items without prior documentation are flagged as such.

## Methodology

- Sources scanned by the 4 section authors: `doc/design/`,
  `doc/testing/`, `doc/issues/`, `.prompts/` (recent 6 sessions),
  project memory index, source greps for TODO/FIXME/PHASE-B/`// G:`,
  `CHANGELOG`, `FEATURES.md`, `TODO.md`, plus 16 subsystem test-plan
  docs.
- Items already shipped per `FEATURES.md` / `ChangeLog` excluded from
  the main list (see appendix).
- Items already covered by passing unit / integration tests excluded.
- Cross-section overlaps merged into single G-IDs; combined sources
  cited.
- WONT items moved to a separate appendix.
- Reviewer ran source / test greps to verify each "X is missing"
  claim; errors found in the section drafts are flagged in the
  Surprises appendix at the bottom.

## Summary table

Effort: H = >2 sessions, M = 1–2 sessions, L (small) = <1 session.
Priority: High / Medium / Low based on user-visibility and blast
radius.

| ID  | Item                                                    | Cat | User-visible impact                                        | Effort | Priority |
| --- | ------------------------------------------------------- | --- | ---------------------------------------------------------- | ------ | -------- |
| G01 | LoRes mode (NR 0x15 bit 7) + scroll                     | A   | parallax.nex broken; LoRes demos broken                    | M      | High     |
| G02 | Per-scanline NR 0x15 (LoRes/sprite/priority) replay     | A   | parallax/Beast-style Copper layer splits flat              | L      | High     |
| G03 | Per-scanline Layer 2 X/Y scroll replay (NR 0x16/17/71)  | A   | L2 parallax effects render flat                            | L      | High     |
| G04 | Per-scanline transparency replay (NR 0x14/4B/4C)        | A   | Sky/foreground transparency-key swaps render flat          | L      | Medium   |
| G05 | Per-scanline clip-window replay (NR 0x18-0x1B)          | A   | Split-screen / picture-in-picture demos blocked            | M      | Medium   |
| G06 | Per-scanline NR 0x6B / NR 0x70 (TM/L2 mode)             | A   | Mixed-resolution / rolling-mode demos blocked              | M      | Low      |
| G07 | Per-scanline port 0xFF Timex screen mode                | A   | Timex mid-frame split demos render flat                    | M      | Low      |
| G08 | Per-scanline NR 0x26 / NR 0x27 ULA scroll               | A   | Latent — non-square-tile scroll demos                      | L      | Low      |
| G09 | Per-scanline NR 0x12 / NR 0x13 L2 active bank           | A   | Exotic per-line page-flip demos blocked                    | L      | Low      |
| G10 | Per-scanline active-palette select (NR 0x43/6B)         | A   | Latent palette-bank-flip demos                             | L      | Low      |
| G11 | Per-scanline NR 0x68 other bits (stencil, ULA+, blend)  | A   | Mid-frame ULA+ split-screen / blend-mode flips render flat | L      | Medium   |
| G12 | Nirvana-class memory-write multiplexers (Ram::write)    | A   | Whole class of 48K demoscene multicolour effects broken    | H      | High     |
| G13 | Per-scanline sprite-attribute multiplexing              | A   | 128+ effective-sprites demos render wrong                  | M      | Medium   |
| G14 | Layer 2 enable/write-paging per-scanline (port 0x123B)  | A   | Latent L2-on-stripe effects                                | L      | Low      |
| G15 | Sprite-pattern reload mid-frame (port 0x5B)             | A   | Niche >64-pattern animation                                | M      | Low      |
| G16 | Beast.nex residual: NEX-loader bank-5 collision         | A   | Cosmetic only                                              | L      | Low      |
| G17 | Parallax.nex "two-copies" mystery (post-LoRes)          | A   | Parallax fully usable depends on this                      | M      | High     |
| G18 | Screenshot vertical scaling for 80x32 / 640x256 modes   | A   | Squished screenshots in 640-mode                           | L      | Low      |
| G19 | Save screenshot in `.SCR` format                        | A   | Developer workflow gap                                     | L      | Low      |
| G20 | Auto-named screenshots (no dialog)                      | A   | Workflow friction                                          | L      | Low      |
| G21 | Raster / ULA-read indicator overlay                     | A   | Developer diagnostic only                                  | L      | Low      |
| G22 | ASM-only clipboard copy in disassembly panel            | A   | Dev workflow                                               | L      | Low      |
| G23 | Redefinable / preset debugger keybindings               | A   | Usability for users from other emulators                   | M      | Low      |
| G24 | Main-window settings persistence (size/scale/CRT/speed) | A   | Every launch resets user preferences                       | L      | High     |
| G25 | Debugger window stickiness to main window               | A   | Debugger floats freely on main-window drag                 | L      | Low      |
| G26 | Compositor open questions (NR 0x68 mode 01 / L2 promote)| A   | Latent edge in modes 110/111                               | L      | Low      |
| G27 | Compositor `rgb_blank_n_6` pipeline edge test           | A   | Cosmetic edge; coverage gap                                | L      | Low      |
| G28 | Layer 2 G9-06 column-pipeline observable                | A   | Test coverage only                                         | L      | Low      |
| G29 | Pi I2S real audio emulation upgrade                     | B   | I2S contribution silent (no published Z80 software uses)   | H      | Low      |
| G30 | AY GPIO ports (PORTA / PORTB)                           | B   | Vintage AY-GPIO software (keymux/lightgun/MIDI) silent     | M      | Low      |
| G31 | DAC per-clock write-priority model (SD-09)              | B   | Edge: Specdrum + Covox at high rates slightly off          | M      | Low      |
| G32 | DAC continuous-buzz playback artefact                   | B   | Audible quality degradation on DAC software                | H      | Medium   |
| G33 | Tape SAVE (write to TAP/TZX/WAV)                        | B   | Cannot save BASIC programs / data — major gap              | M      | High     |
| G34 | `.z80` snapshot loader                                  | B   | Most-popular legacy snapshot format unsupported            | M      | High     |
| G35 | Snapshot save (.sna out / .szx out / .nex out) wired    | B   | Cannot save mid-game state to file                         | M      | High     |
| G36 | TZX Direct-Recording (DeciLoad 0x15)                    | B   | Many turbo-loaded games / demos won't load                 | M      | High     |
| G37 | WAV DeciLoad real-time loading                          | B   | Same as G36 via WAV pipeline                               | M      | Medium   |
| G38 | DSK / +3 disk image loading + uPD765 FDC                | B   | All +3 disk software unrunnable                            | H      | Medium   |
| G39 | ESP-01 / Wi-Fi UART bridge                              | B   | NextZXOS networking and multiplayer Z80 software silent    | H      | Low      |
| G40 | SD card command coverage (CMD9/10/13/16/23/25 etc.)     | B   | CSD/CID probes silent; multi-block writers fall back       | M      | Low      |
| G41 | MMC card support (vs SDHC only)                         | B   | Raw-MMC software (rare) won't init                         | L      | Low      |
| G42 | Joystick / gamepad host wiring (Kempston/Sinclair/MD)   | B   | Gamepad / USB joystick unusable; keyboard-only             | M      | High     |
| G43 | Kempston Mouse host wiring                              | B   | Art Studio Next, mouse demos unusable                      | M      | Medium   |
| G44 | Keyboard issue-2 EAR/MIC composition                    | B   | Issue-2 16K tape-loading detection edge                    | L      | Low      |
| G45 | Expansion bus / cartridge framework (FE-05 / ROMCS)     | B   | Interface 1/2, Multiface (ext), Currah µSpeech absent      | H      | Low      |
| G46 | NextZXOS boot ladder (firmware-faithful + bypass)       | B,C | NextZXOS does not reach BASIC / dot-command shell          | H      | High     |
| G47 | NextZXOS post-boot regression / dot-command surface     | B   | No automation for NextZXOS-native software regressions     | L      | Medium   |
| G48 | Multiface peripheral (Task 8) + RETN-alias band-aid     | B,C | No NMI freeze/cheat menu; 8 DivMMC + Copper rows skipped   | M      | Medium   |
| G49 | NR 0xC0 stackless-NMI execution (CTC NR-C0-02)          | C   | NMI-PUSH suppression edge — minimal real-world impact      | H      | Low      |
| G50 | Contention `delay()` runtime wiring (Phase 2)           | C,D | Cycle-accurate contention is wrong on +3 / Pentagon / Next | L      | Medium   |
| G51 | Contention NextREG dispatch + NR 0x07/0x08 hc(8) (Phase 3) | C | Turbo-mode contention edges                                | M      | Low      |
| G52 | Contention Phase-4 screenshot rebaseline                | C   | Risk: noisy 48K/128K/+3 screenshot rebaseline pass         | L      | Low      |
| G53 | FUSE-table retirement decision                          | C   | Two contention paths post-Phase-2; divergence risk         | L      | Low      |
| G54 | Contention port_7ffd_active term (CT-IO-05/06)          | C   | 128K/+3 port-contention edge                               | L      | Low      |
| G55 | NR 0xD8 IO-trap (FDC NMI source) — stub                 | C   | +3 floppy-trap NMI edge (rare)                             | L      | Low      |
| G56 | NextReg `regs_[]` shadow-store systemic bug             | C,D | NR 0x09/0x0A/0x15/0x22/0x23/0x34 readback returns garbage  | M      | Medium   |
| G57 | MMU `current_rom_bank()` — three documented gaps        | C   | 48K-DivMMC edge; altrom mask; port_1ffd-bit-2 gating       | L      | Low      |
| G58 | MMU shadow-screen routing (TASK-MMU-SHADOW-SCREEN)      | C,D | 128K games with shadow-screen double-buffer render wrong   | L      | Medium   |
| G59 | NextZXOS bypass-tbblue-fw boot path                     | C   | Pragmatic instant-boot mitigation for G46                  | H      | Medium   |
| G60 | config.ini / menu.ini / menu.def parsing                | C   | NextZXOS user-config UX once bypass mode lands             | M      | Low      |
| G61 | Z80N undocumented RETN-alias coverage edge              | C   | Test gap protecting C01 band-aid removal                   | L      | Low      |
| G62 | NR 0x03 soft-reset config_mode preservation question    | C   | Edge between reset and first NR 0x03 write                 | L      | Low      |
| G63 | NR 0x03 machine-type latch read-back                    | C   | Subset of G56 specifically for NR 0x03                     | L      | Low      |
| G64 | NR 0x06/keymap & altROM 0x06/0x07 layout (bypass deps)  | C   | Open VHDL questions blocking G59                           | L      | Low      |
| G65 | CPU/Copper cycle-accurate NR-write priority             | C   | ARB-* tests order stimulus manually; latent                | H      | Low      |
| G66 | Save-state schema versioning + per-subsystem framing    | C,D | ANY save_state field reorder corrupts older snapshots      | M      | High     |
| G67 | Rewind buffer pre-allocated bound + assertion           | C   | Save-state widening silently overflows ring slots          | L      | Medium   |
| G68 | Rewind sub-frame granularity                            | C   | Step Back stops at frame boundaries only (WONT-leaning)    | H      | Low      |
| G69 | Traceability matrix structurally stale + extractor      | D   | Audit / theatre-detection get wrong numbers                | M      | Medium   |
| G70 | Requirements DB (SQLite proposal)                       | D   | Plan/matrix/dashboard drift remains grep-gymnastics        | M      | Low      |
| G71 | `VideoTiming` pulse-counter surface is test-only        | D   | Two state stores for one VHDL signal; blocks 3 test rows   | M      | Low      |
| G72 | UART pin-7 / IoMode UART-mode injectors not fed         | D   | Pin-7 multiplex unit-correct but not driven at runtime     | L      | Low      |
| G73 | Audio I2S has zero runtime wiring                       | D   | I2S-source NextREGs silent (no consumer in production)     | L      | Low      |
| G74 | No CI pipeline; regression depends on dev discipline    | D   | Visual regressions can slip past PR review                 | M      | Medium   |
| G75 | Regression tolerance hard-zero; perceptual diff missing | D   | Spurious diff failures; no incremental change signal       | M      | Low      |
| G76 | RZX determinism long-form regression                    | D   | Long captures may desync from hidden host-time leaks       | M      | Low      |
| G77 | Reopened-suite skips: Compositor NR 0x68 + MMU shadow   | D   | Plan-doc backlog; G58 is the runtime side                  | M      | Low      |
| G78 | Agent worktree-stale-base helper (harness)              | D   | Parallel-wave merge-overhead; not user-facing              | L      | Low      |
| G79 | Test-output uniformity lint                             | D   | New suite with wrong summary string drops out of dashboard | L      | Low      |
| G80 | Headless-mode host-time leakage audit                   | D   | Regression flake risk; D14/G76 dependency                  | M      | Low      |
| G81 | DEVELOPMENT-SESSIONS doc currency                       | D   | Effort accounting under-reports                            | L      | Low      |
| G82 | Z80N matrix Summary row cosmetic mismatch               | D   | Matrix says "0 in-test, 30 missing"; reality 85/85         | L      | Low      |
| G83 | Profiling/benchmark mode + 400% speed bottleneck        | D   | Speed-control >200% observably broken; no perf data        | M      | Medium   |
| G84 | Integration-test design doc missing                     | D   | Each integration suite reinvents fixture conventions       | M      | Low      |
| G85 | Lint baseline tautology coverage stops at substring     | D   | Reviewer attention catches what lint doesn't               | M      | Low      |
| G86 | FEATURES.md "Accurate memory contention" overclaim      | D   | User expectation vs reality — narrative gap                | L      | Low      |

86 entries.

---

## A. End-user emulation experience — Video & GUI

### G01. LoRes mode (NR 0x15 bit 7) + scroll registers + clip
- **What**: VHDL `lores.vhd` defines a 128×96 chunky 256-colour layer
  fed from physical bank 5+0x0000 (12 288 bytes), gated by NR 0x15
  bit 7. NEX-loader handles `SCREEN_LORES = 0x04`
  (`src/core/nex_loader.cpp:189-198`); renderer half deferred at
  `src/core/emulator.cpp:456`. NR 0x32 (X scroll) and NR 0x33 (Y
  scroll) have no `set_write_handler` (verified by grep). NR 0x1A
  clip-window already wired.
- **User impact**: parallax.nex broken; any LoRes demo blocked.
- **Source ref**: `doc/issues/PARALLAX-NEX-INVESTIGATION.md`;
  `doc/issues/BEAST-NEX-INVESTIGATION.md` "Parallax — separate
  finding"; `EMULATOR-DESIGN-PLAN.md:767`.
- **Test coverage today**: zero passing rows reference `lores`.
- **Dependencies**: foundational. NR 0x14 transparency + NR 0x68
  bit 3 ulap_en already wired.
- **Proposed**: author `doc/design/TASK-LORES-PLAN.md` — `Lores`
  class modelled on Layer2; wire NR 0x15 bit 7, NR 0x32/0x33,
  NR 0x1A; bare + integration tests + critic.
- **Effort**: M.

### G02. Per-scanline NR 0x15 (LoRes/sprite/priority) replay
- **What**: Mid-frame Copper-driven NR 0x15 toggles (LoRes-on /
  sprites-off split) collapse to last-value-wins.
- **User impact**: parallax / sprite-LoRes / Beast-style Copper
  layer-flip demos render incorrectly.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 3.
- **Coverage today**: none — frame-end render only.
- **Dependencies**: G01 (otherwise no observable effect).
- **Proposed**: log-pattern clone of `TASK-PER-SCANLINE-PALETTE-PLAN`.
- **Effort**: L.

### G03. Per-scanline Layer 2 X/Y scroll replay (NR 0x16 / 0x17 / 0x71)
- **What**: Renderer reads scroll regs once per frame; Beast.nex
  writes NR 0x16 1818 times/5 s — most per-frame, but per-line is
  the parallax driver.
- **User impact**: any L2 parallax effect renders flat.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 1;
  `src/core/emulator.cpp:398-413`.
- **Coverage today**: per-frame in `layer2_test`; per-line untested.
- **Dependencies**: independent of G01.
- **Proposed**: log-pattern clone.
- **Effort**: L.

### G04. Per-scanline transparency replay (NR 0x14 / 0x4B / 0x4C)
- **What**: Mid-frame transparency-key swap (sky vs foreground)
  collapses to last-write.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A
  rows 4–5; `TASK-PER-SCANLINE-PALETTE-PLAN.md` "Out of scope".
- **Coverage today**: per-frame only (compositor BL group).
- **Dependencies**: independent; demo-driven.
- **Effort**: L (3 sub-items).

### G05. Per-scanline clip-window replay (NR 0x18-0x1B)
- **What**: Rotating-index 4-write clip-window registers don't
  replay per scanline — non-trivial because of rotating state.
- **User impact**: split-screen / picture-in-picture demos blocked.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 6.
- **Coverage today**: per-frame only.
- **Dependencies**: independent.
- **Proposed**: snapshot all 4 X1/X2/Y1/Y2 per layer per line; care
  for rotating-register state.
- **Effort**: M.

### G06. Per-scanline NR 0x6B tilemap control + NR 0x70 L2 mode
- **What**: Mid-frame mode flip changes width path (40/80, 256/320/
  640) — reroutes renderer dispatch.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A
  rows 7–8.
- **Effort**: M.

### G07. Per-scanline port 0xFF Timex screen mode replay
- **What**: Mid-frame Timex STANDARD/HI_COLOUR/HI_RES switch
  reroutes ULA path.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A.
- **Effort**: M.

### G08. Per-scanline ULA hardware scroll NR 0x26 / NR 0x27
- **What**: Functionality landed during ULA closure 2026-04-23 but
  per-scanline replay was not in scope.
- **Coverage today**: per-frame in `ula_test` S5.x.
- **Effort**: L.

### G09. Per-scanline NR 0x12 / NR 0x13 L2 active bank
- **What**: Per-line page-flipping for double-buffered scroll
  (exotic).
- **Effort**: L.

### G10. Per-scanline active-palette select (NR 0x43 b1-3 + NR 0x6B b4)
- **What**: Mid-frame palette-bank flip independent of palette
  CONTENT writes; out-of-scope of landed PALETTE plan.
- **Effort**: L.

### G11. Per-scanline NR 0x68 bits other than bit 7
- **What**: Bit 7 (ULA enable) per-scanline landed in UDIS-01/02.
  Bit 0 (stencil), bits 6:5 (blend mode UDIS-03), bit 3 (ULA+ gate)
  landed flat-frame only.
- **User impact**: any Copper-driven mid-frame blend-mode toggle
  renders flat.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A;
  `TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md` (UDIS-03 closed).
- **Effort**: L.

### G12. Nirvana-class memory-write multiplexers (`Ram::write` hook)
- **What**: Renderer reads ULA pixel/attribute bytes from physical
  bank 5/7 at frame-end. **Nirvana**, **BIFROST*2**, **multicolour**
  demos rewrite the same attribute byte multiple times per frame
  timed to the beam — frame-end render sees only the last value.
- **User impact**: **major** for the Spectrum demo scene. Whole
  class of classic per-scanline attribute-multiplexed demos render
  wrong.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat B.
- **Coverage today**: none.
- **Dependencies**: architectural — needs `Ram::write` callback /
  watch-range. Two design options in audit (sparse hook ~128 KB cap
  vs always-on per-line attr snapshot ~240 KB).
- **Proposed**: dedicated plan doc, demo-driven (pick a Nirvana title
  as canonical reference).
- **Effort**: H.

### G13. Per-scanline sprite-attribute multiplexing
- **What**: Sprite attrs (port 0x57, NR 0x75-0x79) read at frame-end;
  mux demos rewrite slot X/Y mid-frame to draw two visually-distinct
  sprites from one slot.
- **User impact**: any 128+ effective-sprites demo. Possibly partly
  contributes to parallax.nex side-by-side artefact.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat C row 1;
  `PARALLAX-NEX-INVESTIGATION.md` required-work table row 5.
- **Effort**: M.

### G14. Layer 2 enable / write-paging per-scanline (port 0x123B)
- **What**: Mid-frame L2-enable toggle for "L2 only on rows N-M".
- **Effort**: L.

### G15. Sprite-pattern reload mid-frame (port 0x5B uploads)
- **What**: Mid-frame pattern-RAM upload for animation beyond 64-
  pattern cap. Niche.
- **Effort**: M.

### G16. Beast.nex residual: NEX-loader bank-5 collision
- **What**: Beast main render RESOLVED via shadow-screen fix; small
  attribute leak remains in some paths.
- **Source ref**: `BEAST-NEX-INVESTIGATION.md` §"Verdict".
- **Proposed**: NEX loader option to pre-zero pages 10/11 OR audit
  ULA clean-transparency path under specific NR 0x68 / NR 0x4A.
- **Effort**: L.

### G17. Parallax.nex "two-copies" mystery (post-LoRes)
- **What**: After G01-G02 land, side-by-side duplication may persist
  (PARALLAX-NEX-INVESTIGATION root-cause shortlist #3 — NOT
  explained by missing LoRes alone). Likely sprite X-wrap or L2-
  width handling.
- **Source ref**: `PARALLAX-NEX-INVESTIGATION.md` required-work table.
- **Dependencies**: G01 + G02 must come first.
- **Effort**: M.

### G18. Screenshot vertical scaling for 80x32 / 640x256 modes
- **What**: 640px-wide framebuffer saved as 640×256 with non-square
  pixels (1:2). Real screen aspect requires ×2 vertical scale.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1152`.
- **Proposed**: width-branch in `EmulatorWidget::save_screenshot`.
- **Effort**: L.

### G19. Save screenshot in `.SCR` format
- **What**: PNG only; no Spectrum-native 6912-byte bitmap+attr
  export. Common in ZX-scene tooling.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1156`.
- **Effort**: L.

### G20. Auto-named screenshots (no dialog)
- **What**: `Save Screenshot...` always prompts. Add fast path with
  auto-generated timestamp/sequence name.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1157`.
- **Effort**: L.

### G21. Raster / ULA-read indicator overlay
- **What**: Live overlay marking current raster position (vc/hc) +
  ULA-reading flag — diagnostic for beam-racing / Copper / Nirvana.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1171`.
- **Effort**: L.

### G22. Disassembly panel: ASM-only clipboard copy
- **What**: ASM panel copy includes addr / opcode / labels. Add
  selectable ASM-only copy.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1170`.
- **Effort**: L.

### G23. Redefinable / preset debugger keybindings
- **What**: F5/F6/F7/F8/F11/Ctrl+S hardcoded. Add presets ("borland",
  "cspect", "zesarux") and/or per-action remap.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1155`.
- **Effort**: M.

### G24. Main-window settings persistence (size/scale/CRT/speed/etc.)
- **What**: Debugger window persists position via
  `QSettings("JNEXT", "Debugger")`. Main emulator window does NOT
  persist position, scale (2×/3×/4×), CRT filter, fullscreen,
  machine type, or speed. Verified `QSettings` not used in
  `src/gui/main_window.{h,cpp}`.
- **User impact**: every launch resets user prefs.
- **Source ref**: `debugger_window.cpp:56,397`; project memory
  `project_emulator_phase1_status`.
- **Proposed**: `QSettings("JNEXT", "MainWindow")` save in
  `closeEvent`, restore in `show()`. Standard Qt idiom (~20 lines).
- **Effort**: L.

### G25. Debugger window stickiness to main window
- **What**: Debugger position saved / restored but does not track
  main-window drag in real time.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1213`; `TODO.md` Debugger.
- **Effort**: L.

### G26. Compositor open questions — ula_blend_mode 01 swap + L2 promotion
- **What**: `COMPOSITOR-TEST-PLAN-DESIGN.md` §Open Questions records
  6 honest semantic questions about NR 0x68 blend modes 110/111
  (`mix_top` vs `mix_bot` swap on `tm_pixel_below_2` looks inverted)
  and L2 promotion priority. Tests encode VHDL as-is; clarification
  needed from FPGA team.
- **Source ref**: `COMPOSITOR-TEST-PLAN-DESIGN.md:619-664`.
- **Dependencies**: needs FPGA-team confirmation.
- **Effort**: L (once oracle clarification available).

### G27. Compositor `rgb_blank_n_6` vs `rgb_blank_n` delay edge
- **What**: Stage-3 blanking uses pipelined `_6` version; drift
  would show as one-pixel edge artefact.
- **Source ref**: `COMPOSITOR-TEST-PLAN-DESIGN.md:659-663`.
- **Effort**: L.

### G28. Layer 2 G9-06 column-pipeline observable
- **What**: `hc_eff <= hc + 1` VHDL pipeline signal sat as a weak-
  case doc comment; should become observable per-column assertion if
  cycle-accurate refactor lands.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1159`.
- **Effort**: L (gated on cycle-accurate refactor).

---

## B. End-user emulation experience — Audio, I/O & peripherals

### G29. Pi I2S real audio emulation (stub upgrade)
- **What**: `src/audio/i2s.{h,cpp}` only latches a 10-bit sample pair
  on demand; no host-driven sample stream during real emulation.
- **User impact**: Pi I2S audio software (rare; not used by
  NextZXOS) silent on the I2S contribution.
- **Source ref**: `TASK3-AUDIO-SKIP-REDUCTION-PLAN.md` backlog 1.
- **Proposed**: driver producing samples at 48 kHz / I2S clock; fed
  by `--i2s-input file.wav` or host capture.
- **Effort**: H.

### G30. AY GPIO ports (PORTA / PORTB)
- **What**: AY R14/R15 (port A/B I/O) emulation absent. AyChip stores
  reg values but no `port_a_i / port_b_i` injection / read fan-in.
- **User impact**: Vintage 128K software using AY as GPIO (keyboard
  mux, lightgun, MIDI, multifaces) silent.
- **Source ref**: `TASK3-AUDIO` backlog 2; AY-30..34 (5 G-skips).
- **Proposed**: `AyChip` GPIO surface mirroring `ym2149.vhd`; single
  dummy `IoBus` consumer in `Emulator`.
- **Effort**: M.

### G31. DAC per-clock write-priority model (SD-09)
- **What**: Multiple Soundrive/Covox aliases targeting same DAC
  channel within one frame collapse to last-write-wins; VHDL has
  per-CLK_28 if/elsif priority.
- **Effort**: M (re-evaluate when scanline-level audio refactor lands).

### G32. DAC continuous-buzz playback artefact
- **What**: Soundrive DAC demo produces continuous low-frequency
  buzz alongside expected tone, even with DI + 28 MHz + pure-asm
  timing. Reproduces in ZEsarUX.
- **Source ref**: `EMULATOR-DESIGN-PLAN` §11; `TODO.md`.
- **Coverage today**: no automated audio-spectrum regression — buzz
  invisible to tests.
- **Proposed**: profile mixer DAC sampling cadence vs scanline vs
  CLK_28; compare with FUSE+CSpect; consider per-sample (44.1 kHz)
  DAC tap with linear interpolation; add spectral-FFT regression.
- **Effort**: H.

### G33. Tape SAVE (write to TAP / TZX / WAV)
- **What**: ROM SAVE-BYTES trap + EAR/MIC → file write entirely
  absent. `tap_loader` / `tzx_loader` are read-only. Verified — no
  matches for `SAVE_BYTES` / `tape_saver` / `TapSaver` in `src/`.
- **User impact**: cannot save BASIC programs / data — major gap vs
  every legacy emulator.
- **Proposed**: Phase 1 trap-based SAVE→TAP (ROM 0x04C2); Phase 2
  analogue MIC→TZX 0x10/0x11; Phase 3 WAV writer.
- **Effort**: M.

### G34. `.z80` snapshot loader
- **What**: SNA + SZX supported; `.z80` (most-popular legacy
  snapshot) absent.
- **Source ref**: `TODO.md` "Z80 file format loading";
  `EMULATOR-DESIGN-PLAN.md` Phase 11.
- **Proposed**: `Z80Loader` supporting V1/V2/V3 headers, compressed
  / uncompressed pages, full register restore + 128K paging.
- **Effort**: M.

### G35. Snapshot save (.sna out / .szx out / .nex out) wired
- **What**: `SnaSaver` exists but not wired to GUI/CLI. `SzxSaver` /
  `.nex` writer don't exist. Verified — no `SnaSaver` references in
  `src/gui/`.
- **Proposed**: File→Save Snapshot (Ctrl+Shift+S); add `SzxSaver`
  mirroring `szx_loader.cpp`; optional `.nex` saver.
- **Effort**: M.

### G36. TZX Direct-Recording (DeciLoad 0x15)
- **What**: TZX 0x15 blocks with DeciLoad 12k8 (77 T-states/sample)
  fail in real-time mode. FUSE handles same file.
- **User impact**: many turbo-loaded games (Xevious, Dizzy ZX0
  ports, demos) cannot load.
- **Source ref**: `EMULATOR-DESIGN-PLAN` §11; `DECILOAD-TZX-LOADING.md`.
- **Proposed**: per `DECILOAD-TZX-LOADING.md` steps 1–5 (compare
  with FUSE; fix ZOT or override the DIRECT phase locally).
- **Effort**: M.

### G37. WAV DeciLoad real-time loading
- **What**: Same DeciLoad 12k8 turbo class fails when sourced from
  a `.wav`; threshold-to-EAR conversion not tight enough for
  loader's edge timer.
- **Dependencies**: G36 — share fix between TZX 0x15 and WAV.
- **Effort**: M.

### G38. DSK / +3 disk image loading + uPD765 FDC
- **What**: No floppy emulation. +3 has built-in FDC used by
  NextZXOS for tape→disk software.
- **Source ref**: `TODO.md` "DSK file format loading";
  `EMULATOR-DESIGN-PLAN.md` Phase 11.
- **Proposed**: integrate permissive-license uPD765 model
  (e.g. FUSE's wd_fdc) + DSK reader; gate behind `--machine plus3`.
- **Effort**: H.

### G39. ESP-01 / Wi-Fi UART bridge
- **What**: UART 0 wired to "ESP" but no AT-command parser / TCP
  socket bridge.
- **User impact**: NextZXOS-side networking apps + multiplayer Z80
  software can't talk to anything.
- **Proposed**: stub AT command set + TCP socket bridge; optional
  `--esp-bridge HOST:PORT`.
- **Effort**: H.

### G40. SD card command coverage gaps
- **What**: `SdCardDevice` supports CMD0/1/8/12/17/18/24/55/58 +
  ACMD41. Missing: CMD9 (SEND_CSD), CMD10 (SEND_CID), CMD13 (STATUS),
  CMD16 (BLOCKLEN), CMD23 (BLOCK_COUNT), CMD25 (WRITE_MULTIPLE),
  ACMD22/23/51.
- **Source ref**: `src/peripheral/sd_card.cpp:259-285`.
- **Proposed**: read-side first (CMD9/10/13/16/23); defer multi-
  block write (CMD25) until a writer client appears.
- **Effort**: M.

### G41. MMC card support (vs SDHC only)
- **What**: SD model targets SDHC byte-addressed; MMC uses CMD1 +
  sector addressing only.
- **Effort**: L.

### G42. Joystick / gamepad host wiring (Kempston/Sinclair/MD pads)
- **What**: `Joystick`, `KempstonMouse`, `Md6ConnectorX2`,
  `MembraneStick` classes exist + protocol-tested. Verified — no
  `Joystick::inject`, `set_joy_left`, `set_joy_right` calls in
  `src/platform/sdl_input.cpp` or `src/gui/main_window.cpp`. SDL
  `SDL_INIT_GAMECONTROLLER` IS initialised but no events dispatched
  to `Joystick`.
- **User impact**: gamepad / USB joystick unusable — keyboard-only.
- **Proposed**: SDL gamepad event poll + dispatch; Qt Gamepad
  fallback; GUI menu Joystick→Mode (Kempston/Sinclair1/2/Cursor/MD/
  UserDef); save mapping in `QSettings`. Includes wiring
  `Md6ConnectorX2::set_raw_left/right()` and `MembraneStick`
  composers (production wiring of a test-ready surface).
- **Effort**: M.

### G43. Kempston Mouse host wiring
- **What**: `KempstonMouse` class with X/Y/buttons/wheel + DPI
  exists; no SDL `SDL_MOUSEMOTION` / `_WHEEL` / `_BUTTON*`
  translation.
- **User impact**: Art Studio Next, mouse demos, GUI ports cannot
  drive cursor.
- **Proposed**: `Ctrl+M` toggles capture; feed deltas into
  `KempstonMouse::inject_delta`; DPI from NR 0x0A.
- **Effort**: M.

### G44. Keyboard issue-2 EAR/MIC composition
- **What**: FE-04 row F-skipped — no analogue EAR/MIC plumbing
  distinct from issue-3.
- **User impact**: issue-2 16K tape-loading detection edge; rare.
- **Proposed**: `set_machine_issue(int)` on Beeper + Keyboard
  composer; only active for `--machine 48k --issue 2`.
- **Effort**: L.

### G45. Expansion bus / cartridge framework (FE-05 + ROMCS)
- **What**: FE-05 F-skipped — no expansion-bus model. VHDL composes
  port 0xFE bit 5 + ROMCS via expansion-bus tap.
- **User impact**: Interface 1/2, external Multiface, Currah µSpeech,
  ZX Printer, Beta Disk all absent.
- **Proposed**: defer; v1.2+ feature behind `--cartridge FILE.{rom,
  kit}`.
- **Effort**: H.

### G46. NextZXOS boot ladder (firmware-faithful + bypass) [merged]
- **What**: **Single multi-blocker root entry.** NextZXOS boot via
  real `TBBLUE.FW` reaches `enNextZX.rom` and stalls. Discrete
  blockers:
  - **(a)** RETI/RETN-alias band-aid in
    `src/core/emulator.cpp:251-264` (KNOWN DIVERGENCE) — VHDL
    `divmmc.vhd:131` "delayed-off" clear path is the proper fix
    [orig C01].
  - **(b)** RAM-test outer-loop infinite-loop hang — firmware cycles
    112 RAM banks via NR 0x56, ~208 passes/bank in 15 s, never
    exits. Possibly NR 0x1E/0x1F (active video line) timing skew
    [orig C02].
  - **(c)** Missing logo + earlier loader log lines (real Next shows
    4-entry log + diagonal colour bars; jnext shows enNextZX.rom
    only). Diagnosis: rendering gap, not peripheral [orig C03].
- **User impact**: NextZXOS does not reach BASIC / dot-command
  shell; cannot run NextZXOS-native software.
- **Source ref**: `NEXTZXOS-BOOT-INVESTIGATION.md` (full chrono);
  `TODO.md` "NextZXOS Boot (v1.1)".
- **Coverage today**: no end-to-end boot regression test.
- **Proposed**: parallel (a) firmware-faithful — model
  `divmmc.vhd:131` properly + remove RETN-alias band-aid + RAM-loop
  RE; (b) **G59 bypass** ships value sooner.
- **Effort**: H.

### G47. NextZXOS post-boot regression / dot-command surface
- **What**: Once boot lands (G46), no automated test or CLI surface
  for "load NextZXOS, run dot command, screenshot result".
- **Source ref**: `NEXTZXOS-BOOT-INVESTIGATION.md`; bypass plan
  §"Acceptance".
- **Dependencies**: G46.
- **Proposed**: `test/regression/nextzxos-boot.sh` once any boot
  path lands.
- **Effort**: L.

### G48. Multiface peripheral (Task 8) + RETN-alias band-aid removal [merged]
- **What**: Task 8 fully scoped, unstarted.
  `src/peripheral/multiface.{h,cpp}` does not exist — verified.
  `NmiSource::set_mf_is_active(false)` / `set_mf_nmi_hold(false)`
  are stubs (`nmi_source.h:150-154`). The RETN-alias band-aid in
  G46(a) co-depends on landing the proper DivMMC delayed-off path,
  which is co-scheduled with Multiface.
- **Blocks**: 8 DivMMC NM-class rows + Copper ARB-06 + Port-Dispatch
  NR82-02; `enNextMf.rom` is on SD but never paged in.
- **User impact**: no NMI freeze/cheat menu via F5 or NR 0x02
  software trigger.
- **Source ref**: `TASK-8-MULTIFACE-PLAN.md`.
- **Proposed**: execute Task 8 plan §5 (Branches B/E/F); add Edit→NMI
  GUI affordance + F-key.
- **Effort**: M.

---

## C. CPU, memory, firmware, boot

### G49. NR 0xC0 stackless-NMI execution (CTC NR-C0-02)
- **What**: `src/cpu/im2.h:85,182`: `stackless_nmi_` is F-deferred.
  Wave D was CUT from NMI plan — patching FUSE Z80 core for
  NMI-PUSH suppression risks the 1356-row regression.
- **User impact**: nil unless software relies on stack-less NMI
  vector return-address inspection (rare).
- **Effort**: H.

### G50. Contention `delay()` runtime wiring (Phase 2)
- **What**: `ContentionModel::delay(hc, vc)` is implemented
  (`src/memory/contention.cpp:53`) but no runtime caller exists;
  `src/cpu/z80_cpu.cpp:33-122` still uses FUSE's `ula_contention[]`
  (48K-pattern, keyed off tstates not hc/vc). +3 `WAIT_n` path
  unreachable. Verified — `delay_pattern[8]` lives at
  `z80_cpu.cpp:451`; `ContentionModel` not consulted.
- **User impact**: cycle-accurate contention wrong on +3, Pentagon
  (zero by accident-of-table-shape), Next turbo. 48K contention-
  timing demos misbehave.
- **Coverage today**: 28 Phase-A `check()` rows live, ~40 Phase-B
  `skip()` rows pending. Phase 2 currently in flight in this
  session.
- **Dependencies**: this session's Phase-2 wave should land most of
  it; G51, G52, G53, G54 are downstream.
- **Effort**: L (residual after current Phase 2).

### G51. Contention NextREG NR 0x07/0x08 hc(8) commit edge (Phase 3)
- **What**: NR 0x07 / NR 0x08 must land on
  `ContentionModel::set_cpu_speed()` /
  `set_contention_disable()` respecting `hc(8)` commit edge at
  `zxnext.vhd:5822-5823`. Test row CT-TURBO-06 unblocks 4 rows.
- **Dependencies**: G50.
- **Effort**: M.

### G52. Contention Phase-4 screenshot rebaseline
- **What**: G50 wiring changes frame length on 48K/128K/+3; every
  screenshot reference under `test/img/` for those machines must be
  re-baselined. Plan calls this "primary risk".
- **Dependencies**: G50.
- **Effort**: L.

### G53. FUSE-table retirement decision
- **What**: Once G50 lands, two contention paths coexist (FUSE
  table at `z80_cpu.cpp:33-122` + new ContentionModel). Coexistence
  risks divergence; retirement requires removing the table consumer.
- **Dependencies**: G50.
- **Effort**: L.

### G54. Contention port_7ffd_active term (CT-IO-05/06)
- **What**: Bare-class `port_contend()` does not consume
  `port_7ffd_active` — gated by full machine-timing-128/-p3 +
  `port_7ffd_io_en` (NR 0x82 b1) + valid `port_7ffd` decode. Calling
  with `cpu_a == 0x7FFD` returns odd-bit term only.
- **Source ref**: `src/memory/contention.h:73-79`.
- **Effort**: L.

### G55. NR 0xD8 IO-trap (FDC NMI source) — stub
- **What**: `src/peripheral/nmi_source.h:117,279` strobe stub; no
  test row. VHDL `zxnext.vhd:3830-3872` (`nmi_gen_iotrap`).
- **User impact**: +3 floppy-trap NMI edge (rare).
- **Effort**: L.

### G56. NextReg `regs_[]` shadow-store systemic bug
- **What**: `NextReg::write` (`src/port/nextreg.cpp:114`) stores
  raw 8-bit value pre-handler-dispatch. Handlers that mask bits do
  not propagate the mask. Reads return raw byte → disagrees with
  VHDL `port_253b_dat`. Fixed locally for NR 0x12/0x13; likely
  affects NR 0x09/0x0A/0x15/0x22/0x23/0x34. Verified — no
  `SCHEMA_VERSION` / per-NR read-handler scaffolding for these regs.
  Subset for NR 0x03 machine-type latch (separate concern).
- **Source ref**: memory `project_systemic_nextreg_shadow_store.md`.
- **Proposed**: per-register read_handlers (low risk, opportunistic)
  OR systemic NextReg::write rework (higher blast radius). Add VHDL-
  oracled read-back rows to `nextreg_test.cpp`.
- **Effort**: M.

### G57. MMU `current_rom_bank()` — three documented gaps
- **What**: `src/memory/mmu.h:530-545`:
  - 48K-mode `sram_rom3` hardwire (zxnext.vhd:2985) — we report
    bank 0 regardless of machine type. Impact nil today (DivMMC
    tests in Next mode).
  - NR 0x8C altrom factor (zxnext.vhd:3138) ignored.
  - Next-mode port_1ffd bit 2 normally gated by NR 0x82 bit 3;
    direct port_1ffd write on Next mode could spuriously claim ROM3.
- **Effort**: L.

### G58. MMU shadow-screen routing (TASK-MMU-SHADOW-SCREEN-PLAN)
- **What**: Plan authored 2026-04-23. 0x7FFD bit-3 shadow-screen
  toggle wiring + 2 test rows P7F-16/17 + screenshot rebaseline.
  Note: P7F-16/17 have already landed live (verified in recent
  commits `b8d3ea6`); the residual is the broader plan rows for
  NR-driven shadow-screen handling. Confirm scope with reviewer.
- **User impact**: 128K games using shadow-screen for double-
  buffering still render incorrectly in some paths.
- **Source ref**: `TASK-MMU-SHADOW-SCREEN-PLAN.md`.
- **Effort**: L.

### G59. NextZXOS bypass-tbblue-fw boot path
- **What**: `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` — fully-scoped
  4-branch plan (CLI / SRAM populate / synthetic RESET_SOFT + post-
  firmware NR state / host-side FAT32 reader). Unstarted. Provides
  instant-boot NextZXOS bypassing tbblue.fw — mitigates G46 if
  firmware-faithful proves intractable.
- **Dependencies**: 8 open VHDL/state questions (Q1-Q8) need pre-
  implementation answers (G62, G63, G64).
- **Effort**: H (Branches 1-3; Branch 4 optional).

### G60. config.ini / menu.ini / menu.def parsing
- **What**: Bypass plan §4 Cons: ~29 user customisations
  (50/60Hz, scandoubler, joystick mapping, DivMMC/MF enables,
  turbosound, DAC, mouse DPI) parsed by firmware today; bypass
  skips. v1 hard-coded defaults; v2 needs JNEXT-side parser.
- **Dependencies**: G59 lands first.
- **Effort**: M.

### G61. Z80N undocumented RETN-alias coverage edge
- **What**: Test gap — `fuse_z80_test` covers Z80 base; `z80n_test`
  covers 30 Z80N extensions; nothing pins **Z80 ED-undocumented**
  behaviour for the 6 RETN aliases (0x55/5D/65/6D/75/7D). A
  discriminative row would catch a future regression in G46(a)'s
  band-aid removal.
- **Effort**: L.

### G62. NR 0x03 soft-reset `config_mode` preservation question
- **What**: `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` Q1: VHDL behaviour
  of `nr_03_config_mode` across soft reset vs `src/port/nextreg.cpp:
  47-51` which sets `nr_03_config_mode_ = true` unconditionally on
  reset(). If real-VHDL preserves config_mode across soft reset
  (no reset branch), our re-assert is a divergence.
- **Effort**: L (1-hour VHDL check + possible fix).

### G63. NR 0x03 machine-type latch read-back
- **What**: Q2: VHDL nr_03 machine-type latches to a separate
  signal (zxnext.vhd:5137); `NextReg::reset()` zeroes
  `regs_[0x03]`, losing machine-type read-back. Subset of G56 for
  NR 0x03.
- **Effort**: L.

### G64. Bypass plan open VHDL questions (keymap / altROM 0x06,0x07)
- **What**: Q3-Q8 in `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` —
  keymap.bin programmatic state if Z80 reads NR 0x28-0x2B back;
  altROM page layout in enNextZX.rom blob.
- **Dependencies**: G59.
- **Effort**: L (documentation + blob inspection).

### G65. CPU/Copper cycle-accurate NR-write priority
- **What**: VHDL `zxnext.vhd:4769-4777` enforces Copper-wins
  priority on same-28-MHz-cycle NR writes with CPU deferred. JNEXT
  serialises — priority implicit in tick-loop order, not enforced.
  ARB-01/02/03 tests order stimulus manually.
- **Effort**: H (cycle-accurate scheduler refactor).

### G66. Save-state schema versioning + per-subsystem framing [merged C20+D01+D02]
- **What**: `src/core/saveable.h` exposes `StateWriter` / `StateReader`
  as raw byte streams. No magic, no schema version, no per-subsystem
  framing. Verified — no `SCHEMA_VERSION` / `schema_version` /
  `SaveStateVersion` matches in `src/`. Field order silently couples
  to source order; `i2c.cpp:361` flags this manually as "Must mirror
  save_state field order exactly". `divmmc.cpp:368` already documents
  silent-deserialise hazard ("pre-NA-03 snapshots do not carry them").
- **User impact**: ANY change to subsystem `save_state` field order /
  type / count silently corrupts older snapshots and may
  crash/UB on rewind reload. Affects rewind ring buffer +
  Save/Load Snapshot menu (once G35 lands).
- **Source ref**: `src/core/saveable.h:13-100`; per-subsystem
  hand-rolled ordering.
- **Coverage today**: `rewind_test.cpp` round-trips current build
  only; no cross-version compat.
- **Proposed**: 4-byte magic + `SCHEMA_VERSION u32` head; per-
  subsystem `tag(u16) + length(u32) + payload` framing; reject load
  on magic mismatch; registered migrators on version mismatch.
  Update RZX-embedded SNA path (`sna_saver.cpp`). Add per-subsystem
  save→load round-trip lock test.
- **Effort**: M.

### G67. Rewind buffer pre-allocated bound + assertion
- **What**: `src/debug/rewind_buffer.{h,cpp}`: ring of `max_frames *
  snapshot_bytes`. `snapshot_bytes` computed once at construction;
  if subsystem `save_state` widens, bound goes stale silently and
  writes overflow into next slot. Verified Apr 19 — `rewind_test.
  cpp:224 snap_size < 2 MB` had to be widened to 3 MB on Ram-2 MB
  bump. Combined with G66, schema drift here causes corrupt
  restores instead of clean rejects.
- **Proposed**: runtime assertion in `take_snapshot` that measured
  size matches construction-time `snapshot_bytes_`.
- **Effort**: L.

### G68. Rewind sub-frame granularity
- **What**: Snapshots at frame boundaries only; rewind cannot stop
  at arbitrary T-state. Listed in `EMULATOR-DESIGN-PLAN.md` Phase 8
  Step 4 as "frame snapshots ring buffer" — design choice. WONT-
  leaning unless a user asks.
- **Effort**: H if pursued.

---

## D. Test / verification infrastructure

### G69. Traceability matrix structurally stale + extractor multi-file aware
- **What**: `doc/testing/TRACEABILITY-MATRIX.md` Summary still
  references "1796 plan rows / 1361 pass / 307 skip / 128 missing"
  while dashboard is 3326/3210/0/116 across 32 suites. ~10
  subsystems have no Summary row (NMI Source Pipeline,
  audio_nextreg, sd_card, compositor_integration, ula_integration,
  ctc_interrupts, floating_bus, videotiming, contention,
  nextreg_integration). Z80N row says `missing` for all 30 opcodes
  despite 85/85 in dashboard.
- **User impact**: any audit / theatre-detection / plan-drift check
  uses wrong numbers.
- **Source ref**: `doc/testing/TRACEABILITY-MATRIX.md:1-39`.
- **Proposed**: extend `refresh-traceability-matrix.pl` to (a) accept
  `subsystem → [test_files]` mapping, (b) recognise `// RE-HOME:`/
  `// COVERED AT`/`// TRACKED AT` comments, (c) accept sub-letter
  rows. Add Summary rows for the 10 missing suites. CI check on
  Summary-vs-binary drift.
- **Effort**: M.

### G70. Requirements DB (SQLite proposal — queued)
- **What**: `doc/design/REQUIREMENTS-DATABASE.md` proposes a SQLite
  `test/requirements.db` populated from plans + test source + VHDL
  citations, with priority/blocker tags + `comment-rehome`. Queued
  since 2026-04-20 behind SKIP-reduction.
- **User impact**: queries like "all skip rows blocking NextZXOS
  boot" remain grep-gymnastics. Plan + matrix + dashboard + skip-
  string + `.prompts/` backlog still drift vs each other.
- **Dependencies**: G69 extractor improvements; `// RE-HOME:` /
  `// COVERED AT` adoption across all 16 subsystems (currently only
  NextREG + ULA fully use it).
- **Effort**: M (v1) + M (v2).

### G71. `VideoTiming` pulse-counter surface is test-only dead code
- **What**: `src/video/timing.h:97-134` and `timing.cpp:53-94`
  exposed only via test getters; `Emulator` scheduler still owns
  `line_int_enabled_` / `line_int_value_` directly. Two state
  stores for one VHDL signal.
- **User impact**: classified by user 2026-04-23 as "purely academic"
  — but blocks 3 S14.04/05/06 rows currently `// G:` walked back.
- **Proposed**: drop the two `Emulator` fields; route scheduler
  through `VideoTiming::next_int_pos()` / `set_line_interrupt_*`.
- **Effort**: M.

### G72. UART pin-7 / IoMode UART-mode injectors not fed at runtime
- **What**: 2026-04-24 input re-audit added UART-mode + injectors
  `set_uart0_tx`, `set_uart1_tx`, `set_joy_left_bit5`,
  `set_joy_right_bit5`. Production wiring is the documented follow-up.
- **Proposed**: Emulator per-tick callback pulling
  `Uart::tx_line()` and `Joystick::bit5_*()` into `IoMode`.
- **Effort**: L.

### G73. Audio I2S has zero runtime wiring
- **What**: `src/audio/i2s.{h,cpp}` exists as stub; only the test-
  programmatic `Emulator::i2s()` / `I2s::set_sample()` hook reaches
  it. No NextREG / port write routes data to it. Practical impact
  nil — no Z80 software uses it. Distinct from G29 (which is the
  source-side stub upgrade).
- **Effort**: L.

### G74. No CI pipeline; regression depends on dev discipline
- **What**: No `.github/` directory; verified absent.
  `EMULATOR-DESIGN-PLAN.md:1120,1137` flags CI integration TODO.
- **User impact**: visual regressions can slip past PR review (has
  happened before).
- **Proposed**: GitHub Actions Linux job with Qt6 + SDL +
  ImageMagick + xvfb; cache build dir; run unit-test + regression
  under xvfb. Pre-commit hook for matrix drift (G69).
- **Effort**: M.

### G75. Regression tolerance hard-zero; perceptual diff missing
- **What**: `test/regression.sh:21-22` uses `JNEXT_TEST_TOLERANCE=0`.
  No SSIM / ΔE perceptual diff. RZX has no roundtrip-replay
  regression. FFmpeg MP4 has no integrity test.
- **Proposed**: `JNEXT_TEST_PERCEPTUAL` toggle for SSIM /
  ΔE2000. Add `rzx-roundtrip` row that records fixed input, plays
  back, asserts byte-identical screenshot. Add `mp4-roundtrip`.
- **Effort**: M.

### G76. RZX determinism long-form regression
- **What**: RZX recording captures IN values + per-frame
  instruction count. No regression stresses long-form playback for
  drift — any subsystem with hidden host-time dep silently desyncs.
- **Dependencies**: G66 (RZX embeds SNA snapshot).
- **Proposed**: 30-sec RZX of known demo, re-play headless,
  screenshot at frame 1500 must match baseline byte-for-byte.
- **Effort**: M.

### G77. Reopened-suite skips: Compositor NR 0x68 + MMU shadow-screen
- **What**: Meta entry tracking the re-home pipeline. ULA Phase-4
  re-homed 5 rows back into Compositor / MMU plans (intentionally
  re-opening previously-green suites). Compositor NR 0x68 + MMU
  shadow-screen plans still PENDING (G58 is the runtime side).
  Closed plans: Floating Bus (2026-04-25), VideoTiming (2026-04-26),
  Contention Phase-1 (in-flight).
- **Source ref**: `feedback_rehome_to_owner_plan.md`;
  `EMULATOR-DESIGN-PLAN.md:1086-1117`.
- **Effort**: M each (tracked as G58 + this).

### G78. Agent worktree-stale-base launcher helper
- **What**: When launching ≥3 parallel Agent worktrees rapidly, some
  branch from a CACHED older main tip. Standing-mitigation: every
  parallel-agent prompt includes "rebase onto current main". Helper
  script would automate.
- **Source ref**: `feedback_agent_worktree_stale_base.md`.
- **Effort**: L (script helper); harness fix is upstream.

### G79. Test-output uniformity lint
- **What**: `Makefile` dashboard parses `Total: ... Passed: ...
  Failed: ... Skipped: ...` summary line. No lint / CI assertion
  enforces format on new suites.
- **Source ref**: `feedback_uniform_test_output.md`.
- **Proposed**: `test/lint-summary-line.sh` greps every `*_test.cpp`
  for required regex; runs in pre-commit + CI.
- **Effort**: L.

### G80. Headless-mode host-time leakage audit
- **What**: `--frames-instead-of-seconds` switch (commit `1e2f498`)
  decoupled one host-time leak. Other potential leaks (RTC, audio
  mixer sample-rate phases, joystick poll, mouse delta, video-rec
  wall-clock-stamping, RZX pacing) have no audit.
- **Proposed**: "deterministic mode" assertion under `--headless`
  panicking on `std::chrono::system_clock` / `gettimeofday` calls
  in subsystem code; convert to emulated / injected fixture clock.
- **Dependencies**: G76.
- **Effort**: M.

### G81. DEVELOPMENT-SESSIONS doc currency
- **What**: `doc/DEVELOPMENT-SESSIONS.md` ends at `24/4`. Sessions
  2026-04-25 (Floating Bus close, beast.nex resolved, splash bisect)
  and 2026-04-26 (VideoTiming close, Contention Phase-1 partial)
  not appended. Required current at version-bump per CLAUDE.md.
- **Effort**: L.

### G82. Z80N matrix Summary row cosmetic mismatch
- **What**: Z80N runs via FUSE-style data-driven runner;
  `UNIT-TEST-PLAN-EXECUTION.md` §6a marks Z80N permanently `missing`
  per row in the matrix. Today matrix shows `0 in-test, 30
  missing` while dashboard shows 85/85. Misleads anyone consulting
  the matrix.
- **Dependencies**: G69 extractor.
- **Proposed**: matrix Summary row keeps `Pass=85, Fail=0, Skip=0`
  from dashboard with footnote.
- **Effort**: L.

### G83. Profiling/benchmark mode + 400% speed bottleneck
- **What**: `doc/design/PROFILING-OPTIMIZATION-PLAN.md` written but
  unstarted. Phase A (`--benchmark N` + `--profile`) has zero
  deliverables. Concrete known regression: "400% speed only reaches
  ~75 FPS with 100% CPU instead of expected 200 FPS"
  (`EMULATOR-DESIGN-PLAN.md:1127`).
- **User impact**: speed-control >200% observably broken; no perf
  data to fix it. No perf regression detection in CI.
- **Effort**: M (Phase A only).

### G84. Integration-test design doc missing
- **What**: 8+ integration suites already exist but no unifying
  design doc covers cross-subsystem scenarios, fixture conventions,
  tier boundaries.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1119`.
- **Proposed**: `doc/testing/INTEGRATION-TEST-PLAN-DESIGN.md`
  covering fixture shape (Emulator-fixture vs subsystem-fixture),
  tier boundaries, cross-subsystem scenarios.
- **Dependencies**: G70 Requirements DB tier column would surface
  duplicate / missing integration coverage.
- **Effort**: M.

### G85. Lint-baseline tautology coverage stops at substring match
- **What**: `test/lint-assertions.sh` rejects raw `check(x, true,
  ...)`, `|| true`, `a == b || a != b`. Baseline-locked classes
  not caught: `check(x, x, ...)` where `x` was just assigned by
  setter under test; equality between two outputs of same getter;
  `regs_[reg]` to itself via shadow-store path.
- **Proposed**: extend lint with libclang AST pass detecting
  `check(EXPR, EXPR, ...)` literal equality and `check(F(), F(),
  ...)` same-side-effect calls.
- **Effort**: M.

### G86. FEATURES.md "Accurate memory contention" overclaim
- **What**: `FEATURES.md:7` states "Accurate memory contention for
  48K, 128K, +3, and Pentagon timing models". Reality per G50:
  cycle-accurate contention is wrong on +3, Pentagon, Next turbo;
  +3 today runs the 48K pattern; ContentionModel::delay is not
  wired. Honest narrative gap with users.
- **User impact**: user expectation vs reality.
- **Proposed**: soften FEATURES.md text once G50 is honest; or
  qualify with "approximate (FUSE-table-based; full VHDL fidelity
  in progress)".
- **Effort**: L (text tweak); blocked on a defensible state.

---

## E. Cross-cutting dependencies

A simplified graph of the highest-impact dependencies:

```
G46 NextZXOS boot  ──── (a) G48 Multiface + RETN-alias proper fix
                  ├──── (b) RAM-test loop RE
                  ├──── (c) Logo + early-loader render gap
                  └──── G59 bypass path (mitigation, ships sooner)
                              └── G60 config.ini parser
                              └── G62/G63/G64 VHDL questions answered
G47 NextZXOS post-boot regression  ←── G46

G01 LoRes  ←── parallax.nex bringup
G01  ──── G02 NR 0x15 per-line replay
G01 + G02 ──── G17 parallax "two-copies" mystery

G03/G04/G05/G06/G07/G08/G09/G10/G11 per-scanline replays
   ←── all clones of TASK-PER-SCANLINE-PALETTE-PLAN (landed 2026-04-25)
G12 Nirvana memory-mux  ←── architectural; demo-driven plan needed

G50 Contention Phase-2 (in flight) ──── G51, G52, G53, G54
G50 ──── G86 FEATURES.md honesty

G66 Save-state schema versioning ──── G35 Save Snapshot menu
                                  ──── G67 rewind buffer assertion
                                  ──── G76 RZX long-form regression

G42 Joystick host wiring ←── pulls G43 mouse, G24 settings persist (mapping)
G44 issue-2 Beeper independent

G69 Traceability extractor ──── G70 Requirements DB ──── G82 Z80N row
G74 CI pipeline             ──── G69 + G79 lint

G36 TZX DeciLoad ──── G37 WAV DeciLoad

G48 Multiface ──── 8 NM rows + Copper ARB-06 + NR82-02 unblock
G56 NextReg shadow-store ──── G63 NR 0x03 latch
```

Active session work (Phase 2 contention) is concurrent with this
audit and lands part of G50 today.

---

## F. Suggested next-session priorities

If a user were to schedule the next 3 sessions, the highest-leverage
bundles are:

**Session-1 — NextZXOS boot, pragmatic path**
- G59 NextZXOS bypass-tbblue-fw Branches 1-3 (CLI + SRAM populate +
  synthetic RESET_SOFT). Ships value sooner than firmware-faithful
  fix. Mitigates G46(b) + G46(c) for the user-visible "give me a
  prompt" goal. Pre-work: G62/G63/G64 quick VHDL answers.

**Session-2 — Save-state safety + GUI persistence**
- G66 schema versioning (M, foundational; unblocks G35 + G76).
- G67 rewind-buffer assertion (L; cheap, deserves to ship with G66).
- G24 main-window settings persistence (L; every-launch user pain).
- G35 Save Snapshot (M; standard emulator feature). 4 items, 1
  session, broad user impact.

**Session-3 — Joystick / gamepad + LoRes**
- G42 Joystick / gamepad host wiring (M).
- G43 Kempston Mouse host wiring (M).
- G01 LoRes mode + scroll (M; unblocks parallax).
- G02 per-scanline NR 0x15 replay (L; unblocks parallax half).
  These four items unblock the most-frequently-noticed UX gaps
  (no-pad and no-LoRes) plus a parked investigation.

**Top-10 ordered list** (the F-section pick):

1. **G46 NextZXOS boot ladder** — gates the entire NextZXOS UX.
2. **G42 Joystick / gamepad host wiring** — keyboard-only emulator
   is a first-impression UX miss.
3. **G33 Tape SAVE** — every other Spectrum emulator has it.
4. **G24 Main-window settings persistence** — cheapest possible
   user-pain reducer (every launch).
5. **G66 Save-state schema versioning** — silent-corruption hazard;
   small effort, large blast radius.
6. **G35 Snapshot save (.sna/.szx/.nex out)** — mid-game state save;
   chains on G66.
7. **G34 `.z80` snapshot loader** — large body of legacy Spectrum
   software is `.z80`.
8. **G36 TZX DeciLoad (0x15)** — many turbo-loaded games / demos.
9. **G01 LoRes mode (NR 0x15 b7)** — unblocks parallax.nex and any
   LoRes demo; foundational.
10. **G12 Nirvana-class memory-write multiplexers** — whole class
    of Spectrum-demoscene effects render wrong; large effort but
    the right time to plan it.

---

## Appendix A: Already-implemented (excluded from main list)

Items earlier docs flag as gaps but are verifiably closed (verified
by source / test / commit grep during this audit):

- **Per-scanline palette replay (NR 0x40/0x41/0x44)** — landed
  2026-04-25 (`TASK-PER-SCANLINE-PALETTE-PLAN.md`). Beast.nex sky
  gradient now correct.
- **Floating-bus subsystem** — closed 2026-04-25; 26→0 skips. +3
  port 0x0FFD surface live.
- **VideoTiming subsystem** — closed 2026-04-26; 22→0 skips,
  per-machine `int_position` + 60Hz toggle.
- **Contention Phase 1** — partial closure 2026-04-26; 28/68 rows
  live (Phase-A enable-gate + port_contend). G50 Phase-2 in flight
  in this session.
- **NMI source pipeline** — closed end-to-end 2026-04-24; 32 plan
  rows, 5 integration rows.
- **Beast.nex shadow-screen rendering** — RESOLVED 2026-04-25 via
  `vram_use_bank7_` wiring + per-scanline palette feature.
- **MMU page ≥0xE0 RAM-slot** (orig C15) — closed via floating-bus
  Phase 0 (commit `19ca74e`); `Mmu::rebuild_ptr` now nulls slot
  ≥2/page ≥0xE0, returns 0xFF, drops writes per VHDL.
- **MMU port-0x7FFD bit-3 shadow-screen** — P7F-16/17 landed live
  recently (commit `b8d3ea6`). Partial overlap with G58 — confirm
  residual scope.
- **Compositor UDIS-01/02/03** — landed 2026-04-24 (NR 0x68 bit 7
  ULA-en + bits 6:5 blend mode flat-frame).
- **`--delayed-screenshot-frames` CLI** — already present at
  `src/main.cpp:42,130` (orig A23 partly obsolete).
- **MD6 connector tick** — `md6_.tick(master_cycles)` IS called
  from `src/core/emulator.cpp:2746` (orig D07 was wrong; verified).
  D08 host-side feeder is still missing (folded into G42).

---

## Appendix B: WONT items (out of scope by user decision)

Explicit "we will NOT implement this" decisions, captured per the
WONT taxonomy (`feedback_wont_taxonomy.md`):

- **NextBUS expansion-bus emulation** (`expbus_en` / `expbus_speed`,
  zxnext.vhd:5816-5820) — `src/memory/contention.cpp:70-77`
  documents WONT. NMI plan also leaves `set_expbus_nmi_n()` stub.
  `hotkey_expbus_freeze` (zxnext.vhd:2166) inherits.
- **PS/2 keyboard / mouse protocol** — `EMULATOR-DESIGN-PLAN.md`
  §3.1 marks `ps2_*.vhd` as `no` — replaced by SDL/Qt key events.
- **AY I_SEL_L /16 clock divider unreachable** (B03) — VHDL hard-
  ties I_SEL_L='1' (`turbosound.vhd:164`); defensive only.
- **UART TX FIFO write-pulse edge detection** (B06) — sub-instruction
  granularity; impossible at Z80-instruction scope.
- **UART prescaler LSB readback** (BAUD-02/03) — write-only per VHDL.
- **DUAL-05 UART pin-routing assertion tautological** — pins don't
  exist in software.
- **DS1307 NVRAM 0x08-0x3F** — no NextZXOS firmware path uses it.
- **T-state-accurate mid-scanline mutation** — out-of-scope of
  per-scanline replay; would need sub-scanline interleave.
- **CTC NR-C5-04 bus-arbitration** (C25) — kept as `skip()` per user
  decision 2026-04-21; revisit as WONT once requirements DB lands.

---

## Appendix C: Source coverage / methodology

Of 116 raw entries authored by the 4 parallel section agents
(A: 32, B: 26, C: 32, D: 26):

- **Already-implemented / re-classified out**: 9 (per-scanline
  palette, Floating Bus, VideoTiming, NMI pipeline, beast.nex
  resolution, MMU page ≥0xE0, MMU shadow P7F-16/17, Compositor
  UDIS-01/02/03, `--delayed-screenshot-frames`).
- **Already test-covered (full or substantial)**: A04 per-frame L2
  scroll has live rows; G50 Contention Phase-1 already has 28/68
  live rows (gap is the residual). These are kept as gap entries
  describing the residual.
- **Cross-section duplicates merged**:
  - C20 (save-state SCHEMA_VERSION) + D01 (D01 same) + D02 (field
    order) → **G66**.
  - B25 NextZXOS boot + C02 RAM-test + C03 missing-logo + C17
    bypass-tbblue-fw → **G46** (firmware-faithful root) **+ G59**
    (bypass mitigation) — split because they ship in different
    sessions. C18 config.ini → G60 (downstream of G59).
  - C01 RETN-alias band-aid + C04 Multiface stub + C19 NMI MF stubs
    + C24 enNextMf.rom load → **G48**.
  - C13 NextReg shadow-store + D11 same → **G56**. C29 NR 0x03
    latch is the NR-0x03-specific subset → **G63**.
  - C28 NR 0x03 config_mode preservation question kept separate as
    G62 because it answers a Q1 of the bypass plan.
  - D07 MD6 tick gap **dropped** — verified MD6 IS tick'd in
    production (`emulator.cpp:2746`). The host-side feeder for MD6
    is folded into G42.
  - D08 Joystick GUI feeder folded into G42.
  - A11 ULA+ wiring audit folded into G11 (NR 0x68 other bits).
  - C32 `.z80` snapshot loader + B12 same → **G34**.
  - B16 DSK + FDC and the implicit "+3 tape→disk" → **G38**.
  - D16 + D17 reopened-suite skips → **G77** (with G58 as runtime
    side).
- **Final unique gap entries**: **86** (A: 28, B: 19, C: 20, D: 18,
  cross-cutting in two categories).
- **Active session note**: G50 contention `delay()` Phase-2 is
  in-flight in this session; entry describes the residual after
  Phase 2 lands. G77 / G58 reopen notes assume Compositor NR 0x68
  + MMU shadow-screen plans are still in their "PENDING" state.

The 4 section drafts at
`/home/jorgegv/src/spectrum/jnext/.claude/worktrees/agent-{aa8d6e16,
a3be1106,afc8d8a5,ab323d9f}/doc/issues/gaps-T4-{A,B,C,D}-*.md` are
the authoritative source for citations not surfaced here.
