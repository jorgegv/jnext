# Task 8 — Multiface Peripheral Emulation

**Status:** Planned (not yet started).
**Last updated:** 2026-04-20.
**Predecessors:** Task 7 (DivMMC automap pipeline) must land first — Task 8
consumes the `button_nmi_` latch infrastructure added by
`fix(divmmc): gate PC=0x0066 instant-on automap on button_nmi` (commit
`1a8e307`, 2026-04-19) and needs the DivMMC automap state machine to be
behaviourally settled.

## 1. Purpose

Emulate the ZX Spectrum Next's **Multiface** hardware cheat / debug
cartridge. On real hardware, the Multiface is a physical peripheral with
an NMI button; pressing the button traps the Z80 via /NMI, pages in a
Multiface ROM + 8 KB RAM over slot 0, and runs a menu ROM that can
inspect / modify memory, save snapshots, and POKE cheats. The Next
emulates all three historical variants (MF1, MF128, MF3) in software
via the `multiface` VHDL module and a single NMI-button signal.

In the current JNEXT codebase, Multiface is a **deferred placeholder**:
eight DivMMC SKIP rows, one Copper SKIP row (`ARB-06`), one Port Dispatch
SKIP row (`NR82-02` blocked on a related Pentagon-DFFD gap), and the
`button_nmi_` latch added 2026-04-19 all carry "Task 8" annotations.

## 2. VHDL authority

Treat these as the authoritative spec. All emulator behaviour is
VHDL-faithful unless explicitly justified otherwise.

| File | Lines | Content |
|---|---|---|
| `cores/zxnext/src/device/multiface.vhd` | 1-197 | Complete module: port decode, NMI_ACTIVE / MF_ENABLE state machines for MF1 / MF128 / MF3, 0x0066 instant-enable logic, INVISIBLE mode for MF3 |
| `cores/zxnext/src/device/divmmc.vhd` | 72, 109-116, 120-121 | `button_nmi` latch source + automap-NMI gates (this is the hook JNEXT already wired in commit `1a8e307`) |
| `cores/zxnext/src/zxnext.vhd` | 2089-2170 | NMI state machine (`S_NMI_IDLE` / `FETCH` / `HOLD` / `END`), `nmi_mf`, `nmi_divmmc`, `nmi_assert_expbus`, `nmi_mf_button` gating |
| `cores/zxnext/src/zxnext.vhd` | 4274-4310 | `multiface_mod` entity instantiation and signal wiring |
| `cores/zxnext/src/zxnext.vhd` | 2415-2416 | `port_multiface_io_en` ← NR 0x83 bit 1 (`internal_port_enable(9)`) |
| `cores/zxnext/src/zxnext.vhd` | 2610-2616 | `port_mf_enable` / `port_mf_disable` I/O address decode (model-dependent) |
| `cores/zxnext/src/zxnext.vhd` | 2166-2180 | `hotkey_expbus_freeze`, `mf_is_active` |
| `cores/zxnext/src/zxnext.vhd` | 2910-2923 | Multiface SRAM region: 0x014000-0x017FFF (16 KB = 8 KB ROM + 8 KB RAM, physical bank 01 upper half) |
| `cores/zxnext/src/zxnext.vhd` | 1046-1098, 3577-... | `nr_02_generate_mf_nmi` programmatic NMI trigger |

The `multiface.vhd` header comment block (lines 22-48) summarises the
MF1 / MF3 I/O protocol verbatim — copy those tables into any row of the
test plan that tests port behaviour.

## 3. Scope

### In scope (MUST)

1. **Multiface model selection** via NR 0x0A `mf_type` (2-bit: 00 MF+3,
   01 MF128, 10 MF1, 11 reserved). Hardware supports three variants; we
   emulate at least MF1 + MF3 (MF128 as a stretch target since NextZXOS
   firmware would not typically depend on it).
2. **NMI-button source** — a new emulator input mechanism (GUI keyboard
   shortcut, e.g. F5 / configurable) that latches `nmi_mf_button = 1`
   for one cycle, mirroring the physical button press. Same mechanism
   feeds `nmi_divmmc_button` for DivMMC hotkey mode (currently also
   absent).
3. **NR 0x02 programmatic NMI** — bits that generate
   `nr_02_generate_mf_nmi` / `nr_02_generate_divmmc_nmi` (already wired
   in VHDL :3577-...). Allows firmware / software to trigger NMI without
   a physical button.
4. **NMI state machine** (`zxnext.vhd:2120-2170`) — four-state FSM in
   the CPU/NMI routing layer: IDLE → FETCH (on NMI activated) → HOLD
   (on M1+MREQ at 0x0066) → END (on RETN seen) → IDLE. Gates the
   CPU /NMI line and drives `nmi_mf_button` only while IDLE.
5. **`button_nmi_` wiring** — already latched in `DivMmc`; Task 8 wires
   it to the NMI-button source and clears it on all four VHDL-specified
   paths: reset, `i_automap_reset`, `i_retn_seen`, `automap_held=1`
   (the academic follow-up from the 2026-04-19 critic report).
6. **Multiface ROM paging** — when MF_ENABLE=on, overlay 8 KB ROM
   (`enNextMf.rom` from SD) + 8 KB RAM onto slot 0 (0x0000-0x3FFF with
   the classic Multiface split). Interacts with DivMMC overlay priority
   per VHDL `mf_is_active` + `divmmc_is_active` composition.
7. **I/O port decode** — per-model `port_mf_enable` / `port_mf_disable`
   addresses (MF1: 1F/9F, MF3: BF/3F, MF128: intermediate). Gated on
   `port_multiface_io_en` (NR 0x83 bit 1). MF3's `INVISIBLE` mode
   affects subsequent port reads and must be modelled.
8. **Test rows unblocked** — re-enable the 8 deferred DivMMC skips +
   Copper `ARB-06` + Port Dispatch `NR82-02` + any new Multiface-
   specific rows. Discriminative rows, not tautological.

### Out of scope (explicit)

- **Multiface menu ROM UI.** The `enNextMf.rom` loaded from SD contains
  an actual menu that draws on the ULA screen. We do NOT emulate the
  menu presentation logic — our job ends at "firmware executes from
  enNextMf.rom after NMI", and whatever the ROM draws with standard ULA
  writes is rendered by our existing display path.
- **MF1 tape snapshot save.** The physical MF1 could save game state to
  tape. No JNEXT equivalent; refuse any tape-output port write with a
  single-line comment.
- **Hardware debouncing** (NR 0x81 `expbus_nmi_debounce_disable`) — only
  relevant for external /NMI via expansion bus. JNEXT keyboard-sourced
  NMI is a clean edge; we model debounce as always-disabled.
- **Real Multiface+3 INVISIBLE-read of port 0x1FFD / 0x7FFD** — MF3
  exposes these ports via read at specific addresses. Model the decode
  but route reads through existing `Mmu::port_1ffd()` / `port_7ffd()`
  accessors; no new MF state needed.

## 4. Dependencies

Task 8 is **blocked** on Task 7 (DivMMC automap pipeline):
- Task 7 must finalise the automap FSM semantics (`automap_hold_`,
  `automap_held_`, `i_automap_reset`, `i_retn_seen` all correctly
  clocked and cleared per VHDL).
- The `button_nmi_` clear paths listed in VHDL `divmmc.vhd:108-116`
  (`i_automap_reset` and `automap_held=1`) are currently NOT wired in
  JNEXT; Task 7 adds them.
- Without Task 7, a Multiface NMI would set `button_nmi_=1` and never
  clear it via the automap-reset path — DivMMC would stay auto-mapped
  after the first NMI forever.

Task 8 is **not blocked** by the current NextZXOS boot hang (the infinite
RAM-test loop), because the RAM test is not NMI-dependent. However:
- If Task 8 lands and NextZXOS then calls Multiface NMI code during
  early boot (unlikely but possible — some firmware paths use software
  NMI for init), Task 8 could incidentally help or hurt the boot
  debugging.

## 5. Implementation plan (branches)

Each branch is one author + one independent critic cycle per the
never-self-review rule (see `feedback_never_self_review.md`).

> **2026-04-24 supersede note.** Branches A and D below, and the
> DivMMC-button half of Branch C, are superseded by
> `doc/design/TASK-NMI-SOURCE-PIPELINE-PLAN.md` (NMI source pipeline
> plan). That plan lands the central NMI arbiter FSM, the NR 0x02
> software-NMI routing, and the DivMMC `button_nmi_` producer + clear
> paths, and leaves MF-side hooks (`mf_is_active`, `mf_nmi_hold`) as
> stubs for Task 8 to consume. When Task 8 becomes active, start at
> Branches B / E / F plus the reduced Branch C below.

### Branch A: NMI state machine in Z80Cpu / NMI router — **SUPERSEDED**

Landed by `TASK-NMI-SOURCE-PIPELINE-PLAN.md` (Phase 1 + Wave A/B).
The four-state FSM (`S_NMI_IDLE / FETCH / HOLD / END`) lives in
`src/peripheral/nmi_source.{h,cpp}` with VHDL-faithful priority
(MF > DivMMC > ExpBus). `Z80Cpu::request_nmi()` is driven from that
module. Task 8 Branch B consumes the existing
`NmiSource::set_mf_is_active(bool)` / `set_mf_nmi_hold(bool)` stubs by
wiring the real `Multiface::is_active()` / `is_nmi_hold()` accessors.

### Branch B: `Multiface` C++ module

- New class `Multiface` in `src/peripheral/multiface.{h,cpp}`.
- Mirrors `multiface.vhd` ports: `reset()`, `tick_m1(uint16_t pc)`,
  `tick_retn()`, `button_press()`, `set_mode(uint8_t mf_type)`,
  `read_port(uint16_t port)` / `write_port(uint16_t port)`.
- Returns `is_mem_active()` + `is_nmi_hold()` for overlay + /NMI gating.
  These accessors feed into `NmiSource::set_mf_is_active(bool)` /
  `set_mf_nmi_hold(bool)` (stubbed to `false` by the NMI plan; Task 8
  replaces the stubs with live values).
- `button_press()` is routed via `NmiSource::set_mf_button(true)` — the
  NMI plan's MF-button producer API.
- ROM loaded from SD (`enNextMf.rom`) at init; cached as 8 KB byte
  buffer. RAM as 8 KB zero-init buffer.
- Save/load state via project's `StateWriter` / `StateReader`.

### Branch C: MF-button source (scoped down)

- GUI keyboard shortcut (configurable, default F5) bound to "press
  Multiface button". Routes to `NmiSource::set_mf_button(true)`.
- Headless: new CLI flag `--press-multiface-button-at-frame N` for
  deterministic test scenarios.
- DivMMC-button half (F10 hotkey → `DivMmc::set_button_nmi`) is already
  landed by the NMI plan Wave B; nothing for Task 8 to do there beyond
  optionally adding a separate GUI keybinding.

### Branch D: NR 0x02 programmatic NMI — **SUPERSEDED**

Landed by `TASK-NMI-SOURCE-PIPELINE-PLAN.md` Wave A. NR 0x02 bits 2/3
decode into `NmiSource::nr_02_write(v)` which strobes the MF /
DivMMC software-NMI paths. Copper `ARB-06` is unblocked by the NMI
plan (via the DivMMC path only, since MF is absent pre-Task-8). Task 8
adds no new work here; once the `Multiface` consumer lands, the MF
path of NR 0x02 becomes end-to-end observable.

### Branch E: Memory overlay priority

- Integrate Multiface overlay into `Mmu::read` / `Mmu::write`. Priority
  stack: Multiface > DivMMC > layer2 > legacy paging. Mirrors VHDL
  :2937 precedence comment.
- Critical: Multiface ROM page is physical SRAM 0x014000-0x017FFF
  (bank 01 upper half). Our RAM is 2 MB starting at logical page 0x00;
  this range overlaps with standard 128K bank 5 upper half. Route
  Multiface reads to a separate backing buffer, NOT our `Ram` class,
  to avoid aliasing.

### Branch F: Test plan + row un-skips

- Write `test/multiface/multiface_test.cpp` — reset defaults, MF1/MF3
  port protocol, NMI_ACTIVE state transitions, MF_ENABLE pairing with
  0x0066 fetch, RETN clearing, INVISIBLE mode (MF3 only).
- Un-skip the 8 DivMMC rows tagged Task 8 (see
  `test/divmmc/divmmc_test.cpp` — grep for "Task 8").
- Un-skip Copper `ARB-06` (requires Branch D landed).
- Un-skip Port Dispatch `NR82-02` (requires port_multiface_io_en live).

## 6. Acceptance criteria

Task 8 is DONE when ALL of the following hold:

- [ ] Branches A-F merged via author+critic cycles, no self-review.
- [ ] Every test row tagged "Task 8" in the traceability matrix is either
      un-skipped as PASS or has a written, VHDL-cited justification for
      remaining SKIP (category D/E/F/G per the unobservable-audit rule).
- [ ] Pressing the GUI Multiface button from the TBBlue BASIC ROM prompt
      maps in enNextMf.rom and the resulting screen differs from the
      no-press baseline (ground truth: any recognisable Multiface menu
      pattern; pixel-exact comparison not required).
- [ ] NR 0x02 write with `generate_mf_nmi` bit set produces an /NMI on
      the Z80, observable in unit test via PC=0x0066 M1 fetch.
- [ ] Multiface ROM + RAM survives save-state round-trip.
- [ ] Regression: 34/34 screenshot + functional tests pass.
- [ ] No FUSE Z80 or ZXN test regression.
- [ ] Unit aggregate: +N pass, -N skip (not -N pass).
- [ ] Investigation journal updated with any NextZXOS boot impact
      observed during Task 8 landing.

## 7. Risks and open questions

### Risk 1 — DivMMC / Multiface overlay precedence
Both peripherals overlay slot 0. VHDL :2937 documents the priority
(Multiface wins when both active). Our `Mmu::read` currently only
checks DivMMC. Getting the priority wrong silently corrupts which ROM
the Z80 executes during NMI — same class of bug as the 2026-04-19
PC=0x0066 spurious-automap issue. Mitigation: pin a test row that
exercises both overlays simultaneously and asserts the correct bank is
visible.

### Risk 2 — MF3 INVISIBLE port-read decode
MF3 exposes `port 1ffd` / `port 7ffd` via reads at `0x1F3F` / `0x7F3F`
when INVISIBLE=off. Full 16-bit port decode — our port dispatch framework
supports this via 16-bit match masks. But: INVISIBLE is toggled by MF3's
own port writes, so there's a chicken-and-egg — the MF3 ROM likely
toggles INVISIBLE as part of entry. Mitigation: read the MF3 VHDL port
decode carefully, include an INVISIBLE-state-transition test.

### Risk 3 — Button signal latching window
`button_nmi_` in DivMMC is cleared by `automap_held=1`. If our
timing doesn't match VHDL, button_nmi_ could clear too early / too late
and the 0x0066 automap fails to fire. Mitigation: cycle-accurate test
for the button→automap latency.

### Risk 4 — enNextMf.rom loading path
Currently `enNextMf.rom` is on the SD image but never loaded. TBBlue
firmware loads it via the SD FatFs path during the boot loader phase.
Task 8 must either (a) hook the SD read and cache the bytes for later
Multiface overlay use, or (b) have JNEXT load `enNextMf.rom` directly
at startup (simpler, but diverges from real-HW firmware load path).
Decision deferred to Branch B.

### Risk 5 — MF model selection via NR 0x0A
NR 0x0A `mf_type` defaults to 00 (MF+3) on hard reset per VHDL. If
firmware expects a different default (e.g. MF1 on a "retro mode"
config), the port decode will be wrong. Mitigation: verify NR 0x0A
soft-reset behaviour before Branch B.

## 8. Relation to NextZXOS boot

As of 2026-04-19, NextZXOS boot hangs in an infinite RAM-test loop at
`enNextZX.rom:0x0168` (see `doc/issues/NEXTZXOS-BOOT-INVESTIGATION.md`,
session 2026-04-19 journal entry). The diagnosis explicitly rules out
Multiface as the blocker:
- All four SD-loaded ROMs (`keymap.bin`, `enNxtmmc.rom`, `enNextMf.rom`,
  `enNextZX.rom`) are served correctly by our SD driver (113 sector
  reads complete in <1 s emulated time).
- The RAM-test loop is a direct CPU+MMU cycle; no /NMI involvement.
- The `button_nmi_` gate fix (1a8e307) moved the boot forward past the
  spurious-automap hang; the new hang is downstream of any Multiface
  involvement.

Therefore Task 8 is **not on the critical path** for NextZXOS boot.
If it incidentally helps by making some Multiface-adjacent peripheral
behaviour more VHDL-faithful, that's bonus, not goal.

## 9. Open for discussion

- Should the MF button be a single-press GUI shortcut, or should we
  expose a sticky toggle (press = latch on until cleared) for scripted
  tests?
- Do we want a `--multiface-rom FILE` CLI flag (bypass SD load of
  enNextMf.rom for repeatable tests)?
- Priority vs. Task 9+ (NextZXOS RAM-test RE, Sinclair logo rendering,
  etc.) — decide when Task 8 becomes active.
