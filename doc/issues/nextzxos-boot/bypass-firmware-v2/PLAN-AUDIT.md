# Task 18 — Audit of `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md`

Audit date: 2026-05-17 (re-check against `main` HEAD `1d4b82c7`).
Plan reviewed: `doc/design/FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` (Task 13/18).

The plan was authored before Task 8 Wave 0.3 and the G46(b)-v2 series landed.
This audit re-checks every assumption in the plan against current `main`.

---

## 1. Current-code state per file

### `src/core/sd_rom_extractor.{h,cpp}` (Task 8 Wave 0.2, already in place)

- **API** (`sd_rom_extractor.h:55-58`): a single, general-purpose
  `extract_sd_rom(sd_image_path, sd_path, out, bytes_read_out)` that
  reads ANY file from an MBR + FAT32-LBA partition by case-insensitive
  8.3 SFN lookup. It is NOT limited to ROMs by name — `sd_path` is
  user-supplied. It is also fully sufficient for plan §3.1 (locate ROMs
  on SD) and 80 % of the work for Branch 4 (`menu.def` / `config.ini`
  are just two more `extract_sd_rom(... ,"/MACHINES/NEXT/menu.def", ...)`
  calls, returning text bytes to parse host-side).
- **Robustness** (`sd_rom_extractor.cpp:259-308, 436-447`): bounded
  cluster-chain walks (V16-DIVMMC-02 cycle defense). Class-(c)-hardened.
- **Limitations**: no LFN, no FAT16/exFAT/GPT, read-only — irrelevant
  for the TBBlue distribution which is canonical FAT32-LBA / SFN.

The function is already used at `emulator.cpp:5119` (machine ROMs:
`/MACHINES/NEXT/48.rom`, `/MACHINES/NEXT/128.rom`, `/MACHINES/NEXT/plus3.rom`),
`:5272` (`/MACHINES/NEXT/enNxtmmc.rom`), `:5300` (`/MACHINES/NEXT/enNextMf.rom`),
`:5353` (`/MACHINES/NEXT/enAltZX.rom`). The plan's "Branch 4 needs a FAT32
reader" is OBSOLETE — we already have one in production, and it already
covers `enNextZX.rom` (we just call it `48.rom`/`128.rom`/`plus3.rom`
depending on machine type — see §3 below for the naming reconciliation).

**Note**: the plan's reference to `enNextZX.rom` (boot.c's
RAMPAGE_ROMSPECCY load) does NOT match jnext's current `/MACHINES/NEXT/*.rom`
filename scheme. Per `emulator.cpp:5141-5158`, jnext extracts
`48.rom` / `128.rom` / `plus3.rom` keyed off `cfg.type` — which IS the
file `tbblue.fw` `boot.c::load_roms()` ends up writing into SRAM pages
0x00..0x07 after parsing `menu.def`. Bypass mode just needs to pick
the right one host-side (same switch already in place).

### `src/core/embedded_nextboot_rom.h`

The FPGA boot ROM is silicon-baked into the jnext binary via
`objcopy --input-target=binary` (header has the symbol references at
`:23-25`). Invoked at `emulator.cpp:5185-5196`: copies bytes into
`boot_rom_` then calls `mmu_.set_boot_rom(...)`. Gate is "Next machine
+ sd_card non-empty + load_file empty". Bypass mode would want to
SKIP this entirely (the boot ROM is exactly what we want to NOT run).

### `src/core/emulator.cpp::Emulator::init()` (preserve_memory plumbing)

- `init(cfg, preserve_memory=false)` is the hard-reset / first-boot path;
  `preserve_memory=true` is the soft-reset path (`:47`).
- Machine ROMs are loaded from SD at `:5113-5161` (gated on
  `!preserve_memory && !cfg.sd_card_image.empty()` — runs only on hard reset).
- Boot ROM overlay is installed at `:5185-5196` (gated on `!preserve_memory`
  and Next + sd_card + !load_file).
- ROM-in-SRAM seed loop at `:5208-5215`: on hard reset only, copies
  `rom_.page_ptr(p)` → `ram_.page_ptr(p)` for pages 0..7, then
  `mmu_.set_rom_in_sram(true)` at `:5216`. Plan's "we'd skip this in
  bypass mode" is still valid — but as the plan itself notes, our manual
  copy supersedes it idempotently, so simpler to just let it run and
  overwrite.
- DivMMC/Multiface ROM load at `:5269-5283` and `:5298-5310` — both
  gated `!preserve_memory && !cfg.sd_card_image.empty()`.
- AltROM load at `:5350-5370` (4 × 8 KB pages 0x0C..0x0F).
- SD-card mounting at `:5384-5390` (block-level transport for runtime Z80,
  independent path from the host-side extractor).

The "plan §3.7 synthesised RESET_SOFT" path uses `soft_reset()` at
`emulator.cpp:6610-6678`. The bracketing save/restore around `init()`
is intact (`:6625-6636`, `:6665-6677`). The `init(config_, /*preserve_memory=*/true)`
call at `:6663` re-runs init in soft-reset mode and preserves all the
"rom-loaded SRAM" we'd set up.

### `src/core/emulator.cpp::Emulator::soft_reset()`

Verified at `:6610-6678`. Behavior matches the plan exactly:
- Saves NR 0x82/0x83/0x84 (and NR 0x86/0x87/0x88/0x89 — added since the
  plan was written) before the bracketed `init()`.
- Restores them per `reset_type_1` and `bus_reset_type_0` flags
  (mirroring VHDL `zxnext.vhd:5052-5057` and `:5061-5067`).
- The comment at `:6638-6651` explicitly references that the
  `bootrom_en` config_mode gate now lives in `Mmu::reset()` per VHDL —
  so we DON'T need to bracket it here.

### `src/port/nextreg.cpp::NextReg::reset()` — Q1 / Q2 RESOLVED

- `nr_03_config_mode_`: NOT reset (`:355` comment block: "VHDL latch
  survives reset"). Cited VHDL `zxnext.vhd:1102` — declaration with
  initial-only `:= '1'`, no reset clause anywhere. The plan's Q1 worry
  is RESOLVED.
- `nr_03_machine_type_`: NOT reset (`:379` comment, citing G63 + VHDL
  `:5137-5145` config_mode-gated mutator + `:1103` initial-only default).
  Machine type composed-read latch survives. Plan's Q2 worry RESOLVED.
- `nr_04_romram_bank_`: reset to 0 (`:358`, VHDL `:1104` default).
- `regs_[0x03]`: zeroed by `regs_.fill(0)` at `:210` then explicitly
  re-zeroed at `:226`. The plan's worry that "we DO clobber it" was
  correct, but the composed-read of NR 0x03 at `nextreg.cpp` is now
  routed through dedicated latches (`nr_03_machine_type_`,
  `nr_03_machine_timing_`, `nr_03_user_dt_lock_`), not `regs_[0x03]`.
- NR 0x82/0x83/0x84/0x85 reset-type-1 reload at `:301-315`; NR 0x86/.../0x89
  bus-reset-type-0 reload at `:330-342`.

### `src/memory/mmu.cpp::Mmu::reset()`

Verified at `:52-166`. Key points for bypass-mode handoff:
- `slots_[]` reset to `RESET_PAGES[]` = `{0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01}`
  (`:13`, VHDL `:4611-4618`). Slot 0/1 are ROM sentinels.
- `map_rom_physical(0, 0)` / `map_rom_physical(1, 1)` (`:158-159`):
  re-route slots 0/1 to physical ROM pages 0/1.
- `rebuild_ptr` at `:168-180`: when `rom_in_sram_=true`, `read_ptr_[slot]`
  points at `ram_.page_ptr(rom_page)` (the SRAM ROM-window) — exactly
  what we need.
- `config_mode_` mirror NOT cleared at reset (`:144-148` comment block).
- `boot_rom_en_` re-enable gate: at `:149`, `if (boot_rom_ && config_mode_) boot_rom_en_ = true;`
  i.e. boot ROM is re-armed ONLY when config_mode was set at reset time.
  Bypass mode after a soft reset would have already cleared config_mode
  via the NR 0x03=0xB3 write — so `boot_rom_en_` stays off. Good.

Plan §6 Q3 is RESOLVED: `map_rom_physical(0, 0)` + `rom_in_sram_=true`
correctly hand the Z80 first-fetch byte off to `ram_.page_ptr(0)[0]`.

### `src/main.cpp`

No `--bypass*` flag exists (grep confirms — only references to "bypass"
are unrelated comments about ULA/expbus etc.). The CLI surface is the
~30 flags at `:24-69`, parsed at `:117-213`. The recent flag additions
(`--esxdos-stub`, `--magic-port-mode`, `--rzx-*`, `--compositor-trace*`)
show the parsing scaffolding is mature and idiomatic — adding a single
`--bypass-tbblue-fw` boolean would be ~3 LOC at the parser, 1 LOC at
the `cfg.bypass_tbblue_fw = ...` propagation, 1 line of `print_usage`.

### `src/core/emulator_config.h`

Verified — no `bypass_tbblue_fw` field exists yet. Adding it (a single
`bool` next to `esxdos_stub` at `:100`) is trivial.

---

## 2. Gap analysis per FUTURE plan branch

### Branch 1 — CLI plumbing only

**Status**: trivially addable. ~5 LOC across `main.cpp` + `emulator_config.h`.
The plan's recommendation to add a unit test that asserts the flag is
parsed is straightforward in `test/main_args_test.*` style — but there's
no existing such test (no `main_args_test.*` file), so it would either
need creating or just deferred to integration test coverage. Risk: zero.

### Branch 2 — Host-side SRAM population

**Plan as written**: receive ROM paths via `--bypass-next-rom`, `--bypass-divmmc-rom`,
`--bypass-mf-rom` CLI flags, then memcpy them into `ram_.page_ptr()`.

**Gap**: the plan's CLI scheme is OBSOLETE because Wave 0.3 already
extracts these three ROMs from the SD image via `extract_sd_rom()`.
The plan's 2026-05-04 in-place update acknowledges this — "this future
bypass branch will use `extract_sd_rom()` from the start". So Branch 2
becomes:

1. Pick the right SD path keyed off `cfg.type` (e.g. `plus3.rom` for
   ZX_PLUS3, 128.rom for ZX128K, 48.rom for ZX48K, and for Next mode
   the same `48.rom` fallback that `:5156` uses).
2. Call `extract_sd_rom()` once per ROM (machine + divmmc + multiface),
   then `std::memcpy` into `ram_.page_ptr()` for pages 0x00..0x07 / 0x08 /
   0x0A.

The current `init()` already does effectively step 2 via the
`rom_.load_bytes()` → `rom_.page_ptr()` → seed-into-SRAM loop. We can
either short-circuit that path or just let it run and (in bypass mode)
also pre-populate pages 0x08 / 0x0A directly. Risk: low.

**Open question**: alt-ROM pages 0x06/0x07 (plan §6 Q7). Inspecting
`enNextZX.rom` layout requires opening one. Since jnext's current
`/MACHINES/NEXT/48.rom` is 16 KB (just 1 bank), the question only
matters for `plus3.rom` (64 KB = 4 banks = pages 0..7). For now,
populating pages 0..7 byte-for-byte from the plus3.rom matches what
`tbblue.fw load_roms()` does, so this is the correct behavior even if
some of pages 0x06/0x07 happens to be "alt-ROM".

### Branch 3 — NR state setup + synthetic RESET_SOFT

**Status**: NR write routing is intact. `nextreg_.write()` at
`nextreg.cpp:417-469` correctly dispatches through registered handlers;
the NR 0x02/0x03/0x05/.../0x0A/0x82..0x85 handlers are all registered
at `emulator.cpp:2068-2135` etc. The plan's recipe — "write a sequence
of NR values via `nextreg_.write()` then call `soft_reset()`" — works
as designed.

**Refinement**: the plan §3.7 worries about "the `nr_03_config_mode_`
re-assertion on reset is arguably WRONG per VHDL". This is now
RESOLVED in current code (`nextreg.cpp:355` no longer touches config_mode
in `reset()` — comment explicitly cites VHDL `:1102`). So step 3.7's
workaround ("explicitly call `nextreg_.write(0x03, 0xB3)` AFTER `soft_reset()`")
is NOT needed — it is correct to issue the NR 0x03=0xB3 write BEFORE
`soft_reset()` as a real firmware does.

**Risk areas**:
- NR 0x82/0x83/0x84 reset semantics depend on NR 0x85 bit 7. Default
  post-construction is 0x8F (bit 7 = 1 = reload to 0xFF on reset).
  Bypass needs to set NR 0x82..0x85 BEFORE calling `soft_reset()` —
  the soft-reset bracketing in `emulator.cpp:6625-6636` will then
  preserve whatever was set.
- NR 0x86..0x89 same shape (bit 7 of 0x89, inverse polarity).

**Open question**: does NextZXOS at PC=0x0000 actually care about every
NR value `init_registers()` would have set? Likely not — most are
peripheral config that NextZXOS sets itself. The minimum sufficient
set is probably:
- NR 0x03 = 0xB3 (machine type + clear config_mode)
- NR 0x07 = 0x00 (3.5 MHz — NextZXOS expects boot-rate speed)
- NR 0x82..0x85, NR 0x86..0x89 = sensible defaults (which `NextReg()`
  ctor already supplies)
- Boot ROM disabled (set by the NR 0x03 write or by directly clearing
  `boot_rom_en_`).

The plan's full ~10-NR-write recipe is conservative; an empirical
minimal-write set is likely much smaller. To be discovered by trial.

### Branch 4 — Host-side FAT32 reader for menu.def / config.ini

**Status**: the host-side FAT32 reader ALREADY EXISTS (`sd_rom_extractor.{h,cpp}`).
Branch 4 reduces to: (a) parse `menu.def` / `config.ini` as text
host-side; (b) optionally honor a `--bypass-menu-index N` override.

Both files are small (a few KB). Parsing them with `std::ifstream`-like
splits is ~50-100 LOC. Risk: low. This is mostly a UX feature, not
a correctness feature — the default first entry covers the canonical
case.

---

## 3. Risk re-evaluation (plan §6, Q1-Q8)

| Q | Plan-time status | 2026-05-17 status | Notes |
|---|---|---|---|
| **Q1** config_mode preserved across soft reset? | OPEN, suspected divergence | **RESOLVED** | `nextreg.cpp:355` cites VHDL `:1102`, latch correctly survives. |
| **Q2** machine_type latch in `regs_[0x03]`? | OPEN | **RESOLVED** | Live in `nr_03_machine_type_` (preserved); `regs_[0x03]` is a fall-through cache. |
| **Q3** MMU slot 0 ↔ `ram_.page_ptr(0)` after soft reset? | OPEN, "traceability marginal" | **RESOLVED** | `map_rom_physical(0,0)` + `rom_in_sram_=true` + `rebuild_ptr` gives correct read_ptr. |
| **Q4** Z80 reset values at PC=0? | LOW RISK | **STILL LOW** | NextZXOS' first instruction is `DI; JP $00EF` (per memory `EOD-30i+14`), so SP/IM don't matter — first thing it does is set them. |
| **Q5** FAT32 library needed? | YES for Branch 4 | **RESOLVED** | Already have `sd_rom_extractor.cpp`. |
| **Q6** menu.def default entry? | OPEN | OPEN | UX detail; v1 hard-codes index 0. |
| **Q7** alt-ROM pages 0x06/0x07 in `enNextZX.rom`? | OPEN, needs inspection | **DEFERRED** | Inspection of `plus3.rom` bytes (4×16 KB). Per memory the AltROM is `enAltZX.rom` and lives at pages 0x0C..0x0F, separate from the 0..7 main-ROM window — so plan's worry was misplaced. The `/MACHINES/NEXT/plus3.rom` is "pure +3 BASIC banks 0-3". |
| **Q8** display_bootscreen residue? | LOW RISK | **STILL LOW** | Bypass skips it entirely; pages 0x00..0x07 get overwritten by Branch 2's memcpy. |

**New risks discovered since the plan was written**:

- **Q9 (NEW)**: the canonical `nextboot.rom` shipped with jnext
  (`roms/nextboot.rom`, SHA `ee0b99c5...`) is reportedly **97 % different**
  from CSpect's tbblue.fw — see memory `[project_session_handover_2026-05-17_eod]`
  layer-7 finding. The whole G46(b) investigation has been parked on
  this. **For bypass mode, this is GOOD news**: bypass skips `nextboot.rom`
  entirely. Bypass mode is a way to side-step the wrong-binary problem
  without needing to find the right `nextboot.rom`. This argues for
  pursuing Task 18 with HIGHER priority than the plan originally assumed —
  it's no longer just a UX-fast-boot win, it's a workaround for the
  current main-blocking G46(b) defect.

- **Q10 (NEW)**: Wave 0.3 made `--sd-card` mandatory at the CLI but
  optional inside `Emulator::init()` for unit-test fixtures. Bypass
  mode needs `--sd-card` (to find the ROMs via `extract_sd_rom`).
  Trivial — bypass mode just rejects when `cfg.sd_card_image.empty()`.

- **Q11 (NEW)**: AltROM handling at `emulator.cpp:5350-5369` already
  pre-loads `enAltZX.rom` to SRAM pages 0x0C..0x0F (per the architectural
  decision that AltROM is FPGA-flash-pre-baked, not firmware-loaded —
  comment block `:5331-5349`). This is bypass-mode-compatible "for free":
  the AltROM seed runs on hard reset, and `soft_reset()` preserves it.

---

## 4. Empirical-capture-and-replay alternative

The user's Task 18 proposal: instead of reverse-engineering boot.c,
**capture CSpect's full state at PC=0x0000 (post-firmware, just before
NextZXOS runs)** and replicate it in jnext at init time.

### Capture mechanics (feasible)

CSpect has a Plugin API (we've used it extensively for G46(b) — see
`tools/cspect_plugin/`). To capture the post-firmware handover state:

1. **Z80 registers**: trivially captured via Plugin `Z80Regs` at the
   moment PC hits 0x0000 the second time (first = boot ROM entry,
   second = post-firmware soft-reset). One Plugin BP fires at PC=0x0000
   and reads everything.
2. **All 256 NextREG values**: enumerable via Plugin `GetNextReg(i)` —
   already used in `JnextG46bTrace.cs` per memory.
3. **All 224 × 8 KB SRAM pages**: enumerable via `PeekPhysical()` —
   1792 KB total, dumpable as a single binary blob.
4. **MMU slot mapping** (slots 0..7 → page indices): readable via
   the Next register interface (NR 0x50..0x57) or via Plugin
   `Peek()` walks.
5. **Port 0x7FFD / 0x1FFD / 0xDFFD**: readable via NR mirror registers
   (NR 0x8E etc.) or by adding port-write hooks during boot.

Total state to capture: ~1.8 MB SRAM + 256 NR bytes + ~30 bytes regs/ports.
Storable as a single `cspect_post_firmware_handover.bin` (a "save-state"
in CSpect's terminology, but captured at a deterministic precise PC).

### Replay mechanics (feasible, but tricky)

At jnext init time with `--bypass-tbblue-fw-from-capture FILE`:
1. Load the dump.
2. Copy 1.8 MB → `ram_.page_ptr(p)` for p in 0..223.
3. Write 256 NR values via `nextreg_.write(reg, val)` (or directly to
   `regs_[]` for read-only registers).
4. Set MMU slot map.
5. Set Z80 registers (PC=0, SP=0xFFFF, IFF=0).
6. `cpu_.run()` and watch NextZXOS boot.

### Pros / cons vs reverse-engineering boot.c

| | Empirical capture | Reverse-engineering boot.c |
|---|---|---|
| Implementation effort | ~200 LOC (loader + state-set) | ~500-1000 LOC (NR sequence + SRAM seed + soft_reset) |
| Correctness per-state | Bit-exact to CSpect at the moment of capture | Approximated from boot.c reading |
| Maintenance | Re-capture when tbblue.fw revs | Re-read boot.c when it revs |
| VHDL-faithfulness | Inherits CSpect's faithfulness (and its bugs) | Cleaner — uses jnext's authoritative state machinery |
| Debug-ability | Opaque blob; hard to know which bytes matter | Auditable per-byte against boot.c citations |
| Robustness across SD images | LOW — captured state pins one SD image's contents | HIGH — reads any SD image's ROMs via extract_sd_rom |
| Failure mode | Silent divergence if anything in the capture is stale | Loud — incorrect NR value causes visible boot failure |

The biggest dealbreaker for **pure** empirical capture: the captured
SRAM contents are SPECIFIC to the SD image that CSpect booted from.
Different `enNextZX.rom`, different captured bytes — bypass mode becomes
single-image-only. Whereas FUTURE-plan replication uses
`extract_sd_rom(cfg.sd_card_image, ...)`, so it adapts per-invocation.

### Hybrid recommendation

**Use the FUTURE plan as the primary mechanism, use CSpect capture as the
validator**:

1. Implement FUTURE-plan Branches 1-3 (CLI + SRAM populate + NR sequence
   + synthetic soft_reset).
2. In parallel, do ONE CSpect capture of the post-firmware state.
3. After bypass-mode jnext boots to PC=0, compare its SRAM + NR + Z80-regs
   state against the CSpect capture byte-for-byte. Differences flag
   bypass-mode bugs.
4. (Optional) ship the captured state as a test fixture for a
   bypass-mode regression test: "bypass mode populates exactly these
   bytes; if a future jnext change breaks that, the test catches it".

The capture is a 1.8 MB binary in the test fixtures; cheap.

---

## 5. Final recommendation

**Pursue the FUTURE plan, in the original order (Branches 1→2→3→4),
with empirical CSpect capture used as a one-time validator.**

The plan is fundamentally sound and most of its open risks are now
resolved by Wave 0.3 + the G46(b)-v2 audits. Plan-time Q1/Q2/Q3 (the
config_mode + machine_type + slot-0 mapping worries) are RESOLVED in
current code, with VHDL-cited comments. Branch 4's "need a FAT32 reader"
prerequisite is RESOLVED (we have one). The remaining work is mechanical
glue plus a ~10-NR-write recipe (Branch 3). The G46(b) park at layer 7
(wrong `nextboot.rom`) is now an INDEPENDENT motivator: bypass mode
side-steps the broken firmware path entirely, so Task 18 may unblock
NextZXOS booting where Task 1 cannot. Empirical capture is best used
as a test oracle, not as the primary mechanism, because per-byte SRAM
captures pin a single SD image while extract_sd_rom adapts.

---

### Independent-verification summary

**(a) Plan branches still good vs needing rework**:
- Branch 1 (CLI plumbing): GOOD as-is. ~5 LOC.
- Branch 2 (SRAM population): NEEDS UPDATE — drop the per-file CLI
  flags, use `extract_sd_rom()` directly with the existing
  `/MACHINES/NEXT/{48,128,plus3}.rom` + `enNxtmmc.rom` + `enNextMf.rom`
  paths. ~50-100 LOC.
- Branch 3 (NR sequence + synthetic soft_reset): GOOD as-is, minus the
  "workaround for nr_03_config_mode reset" worry (which is now
  unneeded). ~30-50 LOC of NR writes.
- Branch 4 (menu.def parsing): GOOD as-is, simpler than the plan
  assumed (extract_sd_rom + text parse, no FAT32 lib to integrate).
  ~100-150 LOC.

**(b) Recommended order of work**: 1 → 2 → 3 → 4 unchanged. Add a
zeroth step: capture one CSpect post-firmware state (~1 hour Plugin
work) for use as a test oracle in Branch 3 validation.

**(c) Single biggest unknown not resolvable without an experiment**:
**which NR values does NextZXOS actually require at PC=0x0000?** The
plan's full ~10-NR recipe is conservative; empirically the minimal set
is likely 3-5 (NR 0x03, NR 0x07, maybe NR 0x82..0x85). Until we run
bypass mode and let NextZXOS try to boot, we can't know which NR
values are load-bearing vs cosmetic. This is what the CSpect capture
validator answers in one shot.
