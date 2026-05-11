# NEXTZXOS Boot — Pass-8 NMI / Multiface / Port / NextREG Re-audit

Branch: `task2/verify8-nmi-mf-port`
Date: 2026-05-09

## Verdict

**Convergence achieved.** Zero class-(a) and zero class-(b) findings remain
after this pass. The seven class-(a) reset-preservation gaps already
landed in pass-6 / pass-7 (NR 0x06, NR 0x05, NR 0x09, NR 0x08 + 0x81 — all
"same-shape" same-bug-pattern) had two final residuals in this subsystem
slice: the *Joystick* and *KempstonMouse* per-subsystem authoritative
shadow caches were still being clobbered on `reset()`, leaking through
the corresponding NR 0x05 / NR 0x0A read handlers (which source from
those subsystems, not from the regs_[] cache that NR 0x05 / NR 0x0A
preserve via NextReg::reset). Those are fixed here. The exhaustive NR
sweep below identifies one additional cache divergence (NR 0x8A
bus-port-propagate, initial-only signal whose cache was wiped to 0 on
every reset) and resolves it the same way.

Pre-crash partial work has been **kept in spirit and refactored** so the
power-on / first-boot defaults live in the `NextReg` constructor (and in
the `Joystick` / `KempstonMouse` member-initialisers, which already
carried the right values), letting `reset()` unconditionally preserve
the byte without `first_boot_X` heuristics. The previous
`first_boot_X = (regs_[X] == 0x00)` heuristic was VHDL-incorrect for any
register whose user-written 0x00 is a legal value (NR 0x05 with both
joys = Sinclair2 is the obvious example). Constructor seeding makes the
"is this the first reset() ever?" question moot.

## Pre-crash partial-work review

The previous pass-8 agent left five files modified before the desktop
crash interrupted it. Per-file disposition:

| File | Status | Notes |
|---|---|---|
| `src/port/nextreg.cpp` | **Refactored, kept** | Constructor pre-seed of NR 0x05/0x06/0x09/0x0A/0x10/0x11/0x8A, `reset()` simplified to unconditional preserve. Removed `first_boot_X` heuristic that mis-classified user-written 0x00 as "uninitialised". |
| `src/input/joystick.{cpp,h}` | **Refactored, kept** | Removed clobber of `nr_05_raw_ / joy0_mode_ / joy1_mode_` on reset; member defaults already supply power-on. Added `nr_05_raw()` accessor for the `Emulator::init()` defense-in-depth fan-out check. |
| `src/input/mouse.cpp` | **Refactored, kept** | Removed clobber of `button_reverse_ / dpi_` on reset; member defaults already supply power-on. |
| `src/core/emulator.cpp` | **Kept verbatim** | NR 0x0A / NR 0x05 fan-out at `Emulator::init()` — defense-in-depth, no-op when subsystem and cache are coherent. Documents intent and shields against future subsystem-reset regressions. |

The pre-crash work covered the two correct same-shape findings flagged
by pass-7 (mouse / joystick clobber). The refactor consolidates the
power-on seeding into one place and avoids the subtle "is this first
boot?" question that the heuristic answered incorrectly for legitimate
user writes of 0x00.

## NR-reset-preservation table — post-pass-8 state

Exhaustive sweep of every NR signal declaration in `zxnext.vhd`. For
each, "VHDL behaviour" = `master reset block` / `dedicated process with reset clause`
/ `initial-only` / `composed read`. "Cache disposition" = how
`NextReg::reset()` and the relevant subsystem `reset()` treat the value
in jnext after this pass.

| NR | VHDL signal(s) | VHDL reset behaviour | jnext cache (`regs_[NR]`) | jnext authoritative shadow | Status |
|---|---|---|---|---|---|
| `00` | `g_machine_id` (top-level generic) | n/a (constant) | `0x08` (HWID_EMULATORS deviation, MID-01) | n/a | OK |
| `01` | `g_version` (top-level generic) | n/a (constant) | `0x32`, **writes ignored** (PASS-8 RO guard) | n/a | OK |
| `02` | `nr_02_bus_reset` (1095) + composed | initial-only (bit 7); composed read | composed in read_handler | `nr_02_bus_reset_` member-init false (preserved) | OK |
| `03` | `nr_03_machine_timing/user_dt_lock/config_mode/machine_type` (1099-1103) | initial-only — survives reset | `0x00` cache (writes through handler) | `nr_03_*_` members preserved across `reset()` (G62/G63) | OK |
| `04` | `nr_04_romram_bank` (1104) | initial-only | `0x00` (write_handler stores) | `nr_04_romram_bank_` reset to 0 in `reset()` | matches VHDL (member preserved-via-write-only) |
| `05` | `nr_05_joy0/joy1/5060/scandouble_en` (1105-1106, 1302-1303) | **all initial-only** | **PASS-8: preserved across reset** (constructor seeds 0x41) | `Joystick::joy0_mode_/joy1_mode_/nr_05_raw_` preserved in `Joystick::reset` (PASS-8) | OK |
| `06` | `nr_06_*` per-bit split (1107-1113) | bits 7,5 reset to '1' (4932-3); other bits initial-only | PASS-6: `(saved_06 & ~0xA0) | 0xA0`; constructor seeds 0xA0 | shadows refreshed from cache (PASS-6) | OK |
| `07` | `nr_07_cpu_speed` (1299) | reset to "00" (5786-7) | `0x00` set in `reset()` | clock_/contention_ reset by clock/contention reset | OK |
| `08` | `nr_08_*` per-bit split (1114-1120) | bit 6 reset to '0'; others initial-only | PASS-7: `nr_08_stored_low_` preserved | `dac_enabled_` / `nr_08_stored_low_` preserve via `Emulator::init` | OK |
| `09` | `nr_09_*` (1121-1123, 1304) | bit 4 reset to '0' (4937); others initial-only | PASS-7: `(saved_09 & 0xEF)`; constructor seeds 0x00 | sprites_.mirror_tie() reset; cache covers others | OK |
| `0A` | `nr_0a_mf_type/sd_swap/divmmc_automap_en/mouse_button_reverse/mouse_dpi` (1124-1128) | **all initial-only** | **PASS-8: preserved across reset** (constructor seeds 0x01) | Multiface/DivMmc/SpiMaster preserve their bits; **PASS-8: KempstonMouse::reset no longer clobbers `button_reverse_/dpi_`** | OK |
| `0B` | `nr_0b_joy_iomode_*` (1129-1131) | reset to '0'/"00"/'1' (4939-4941) | `0x01` set in `reset()`; write_handler stores `(v & 0xB1)` | iomode_.set_nr_0b on write | OK |
| `0E` | `g_sub_version` (top-level generic) | n/a (constant) | `0x03`, **writes ignored** (PASS-8 RO guard) | n/a | OK |
| `0F` | `g_board_issue` (top-level generic) | n/a (constant) | `0x00`, **writes ignored** (PASS-8 RO guard) | n/a | OK |
| `10` | `nr_10_flashboot/coreid` (1132-1133) | **both initial-only** | **PASS-8: preserved across reset** (constructor seeds 0x04 = coreid 1 << 2) | `nr_10_coreid_` orphan-reset to 0x01 in `Emulator::init` (class-(c) — overwritten on next write before any read could observe; cache is the source of truth for reads) | OK |
| `11` | `nr_11_video_timing` (1134) | **initial-only** | **PASS-8: preserved across reset** (constructor seeds 0x03) | n/a (cache-only) | OK |
| `12` | `nr_12_layer2_active_bank` (1135) | reset to "0001000" (4943) | `0x08` written-through after handlers install | layer2_.set_active_bank | OK |
| `13` | `nr_13_layer2_shadow_bank` (1136) | reset to "0001011" (4944) | `0x0B` via mmu_defaults write-through | layer2_.set_shadow_bank | OK |
| `14` | `nr_14_global_transparent_rgb` (1137) | reset to X"E3" (4946) | **PASS-8: cache seeded to 0xE3 in `reset()`**; write-through at line 2484 still runs | renderer/palette already had 0xE3 default | OK |
| `15` | `nr_15_*` per-bit (1138-1143) | reset to '0' / "000" (4948-4953) | `regs_.fill(0)` + write_handler covers | composed read uses cache + sprites_ | OK |
| `16-1B` | clip + scroll registers | reset to defaults (4955+) | `regs_.fill(0)` + write-through | renderer/sprites/etc | OK |
| `22` | `nr_22_line_interrupt_en` (1172) | reset to '0' (4983) | `regs_.fill(0)` | im2_ owns | OK |
| `23` | `nr_23_line_interrupt` (1173) | reset to "0..0" (4985) | `regs_.fill(0)` | im2_ owns | OK |
| `26-33` | scroll registers | reset to defaults (4987+) | `regs_.fill(0)` | renderer/etc | OK |
| `2D` | `nr_2d_i2s_sample` (1176) | continuously latched from i_PI_AUDIO at runtime; no explicit reset clause but read_handler bypasses cache | runtime-driven by I2s | i2s_ is authoritative | class-(c) — no observable cache divergence |
| `40-44` | palette index/data/select | reset to "0..0" (4999-5009) | `regs_.fill(0)` + write-through | palette_ owns | OK |
| `42` | `nr_42_ulanext_format` (1183) | reset to X"07" (5002) | write-through `0x07` at line 2485 | n/a | OK |
| `4A` | `nr_4a_fallback_rgb` (1191) | reset to X"E3" (5014) | **PASS-8: cache seeded to 0xE3 in `reset()`** + write-through | renderer/palette | OK |
| `4B` | `nr_4b_sprite_transparent_index` (1192) | reset to X"E3" (5016) | **PASS-8: cache seeded to 0xE3** + write-through | sprites_ | OK |
| `4C` | `nr_4c_tm_transparent_index` (1193) | reset to X"F" (5018) | **PASS-8: cache seeded to 0x0F** + write-through | tilemap_ | OK |
| `60-64` | copper data/control | reset to "0..0" (5020-5024) | `regs_.fill(0)` | copper_ | OK |
| `68-71` | ula/lores/tilemap/L2 control | reset to defaults (5026+) | `regs_.fill(0)` + write-through where present | renderer/etc | OK |
| `7F` | `nr_7f_user_register_0` (1216) | initial-only | preserved (PASS-5) | n/a | OK |
| `80` | `nr_80_expbus` (360) | bits 7:4 fold from 3:0 on reset (2185-6) | preserved with fold (PASS-5) | n/a | OK |
| `81` | `nr_81_expbus_*` (1221-1225) | all initial-only | preserved (PASS-7) | nmi_source_ / `nr_81_` shadow refreshed from cache | OK |
| `82-89` | port-enable groups | conditionally reset based on reset_type (5052-5067) | preserved with reset_type gating (PASS-5) | n/a | OK |
| `8A` | `nr_8a_bus_port_propagate` (1236) | **initial-only** | **PASS-8: preserved across reset** (constructor's value-init delivers 0x00 power-on default) | port_propagate_* not modelled in jnext | OK |
| `8C` | `nr_8c_altrom` (387) | bits 7:4 fold from 3:0 on reset (2255) | preserved with fold (PASS-5) | mmu_ owns authoritative | OK |
| `8E` | mmu paging command | combinational; no register | n/a | mmu_ | OK |
| `8F` | `nr_8f_mapping_mode` (888) | initial-only | `regs_.fill(0)` cache; read goes through MMU | `mmu_.nr_8f_mode_` preserved across reset | OK (cache invariant) |
| `90/93` | `nr_9X_pi_gpio_o_en` (1237-1240) | reset to "0..0" (5075-5078) | `regs_.fill(0)` | n/a | OK |
| `98-9B` | `nr_9X_pi_gpio_o` (1217-1220) | reset to FF/01/00/0 (5070-5073) | `regs_.fill(0)`; read_handlers return 0x00 (jnext models GPIO as idle) | n/a | class-(c) — divergence from VHDL initial values, but pi-GPIO is unmodelled |
| `A0/A2` | `nr_a0_pi_peripheral_en/nr_a2_pi_i2s_ctl` (1241-1242) | reset to "0..0" (5080-5081) | `regs_.fill(0)` + write-through | i2s_ | OK |
| `A8/A9` | `nr_a8_esp_gpio0_en / nr_a9_esp_gpio0` (1244-1245) | reset to '0'/'1' (5084-5085) | read_handler returns 0x00 (jnext models ESP idle) | n/a | class-(c) — ESP unmodelled |
| `B8-BB` | divmmc entry points | reset to 83/01/00/CD (5087-5090) | `regs_.fill(0)` + write-through via DivMmc | divmmc_ owns | OK |
| `C0-CE` | im2 / dma int control | reset to "0..0" (5092-5105) | `regs_.fill(0)` + write_handlers | im2_ / dma_ | OK |
| `C2/C3` | `nr_c2/c3_retn_address_*` (1253-1254) | reset to "0..0" (2058-9) | not exposed via cache | im2_/cpu_ | OK |
| `D8` | `nr_d8_io_trap_fdc_en` (1263) | reset to '0' (5107) | `regs_.fill(0)` + member shadow `nr_d8_io_trap_fdc_en_=false` in init | n/a | OK |
| `D9` | `nr_d9_iotrap_write` (1264) | reset to "0..0" (3891) | `regs_.fill(0)` + member shadow `nr_d9_iotrap_write_=0` | n/a | OK |
| `DA` | `nr_da_iotrap_cause` (1265) | reset to "00" (3870, 3880) | `regs_.fill(0)` + member shadow `nr_da_iotrap_cause_=0` | n/a | OK |
| `F0/F8/F9/FA` | xadc / xdev | initial-only `:=` defaults; resets in xadc/xdev sub-domain (7424-9) | `regs_.fill(0)`; xadc unmodelled | n/a | class-(c) — xadc unmodelled |
| `FF` | tilemap mirror reg | combinational | n/a (write_handler) | tilemap_ | OK |

## Class-(a) findings, this pass

| # | Register / location | Bug | VHDL citation | Fix |
|---|---|---|---|---|
| 1 | `KempstonMouse::reset` (mouse.cpp:34-65 pre-fix) | `button_reverse_` and `dpi_` clobbered to false/0x01 on every reset, but VHDL `nr_0a_mouse_button_reverse` and `nr_0a_mouse_dpi` are initial-only signals — they survive both hard and soft reset. The NR 0x0A read handler sources these bits from `mouse_.button_reverse() / mouse_.dpi()`, so resetting them here leaks to NR 0x0A reads after every soft reset. | zxnext.vhd:1127-1128, 5912 (read mux), 5197-5198 (only mutator). No reset clause anywhere. | Removed clobber. Member-init defaults supply power-on values; `reset()` preserves them. |
| 2 | `Joystick::reset` (joystick.cpp:11-25 pre-fix) | `nr_05_raw_`, `joy0_mode_`, `joy1_mode_` all clobbered on every reset to the FPGA power-on values, but VHDL `nr_05_joy0/joy1` are initial-only — they survive both hard and soft reset. The NR 0x05 read_handler at emulator.cpp:1060-1074 sources joy bits from `joystick_.mode_left/right()`, so resetting the modes here leaks to NR 0x05 reads after every soft reset. | zxnext.vhd:1105-1106, 5897 (read mux), 5157-5158 (only mutator). No reset clause anywhere. | Removed clobber. Member-init defaults already supply the power-on `0x40` raw byte / Kempston1 / Sinclair2; `reset()` preserves them and re-fans-out to `MembraneStick`. |
| 3 | `NextReg::reset` for NR 0x0A (regs_[0x0A] zeroed on every reset) | Cache wiped by `regs_.fill(0)`. VHDL nr_0a_* are initial-only signals → cache should preserve. | zxnext.vhd:1124-1128, no reset clause. | PASS-8: constructor seeds `regs_[0x0A] = 0x01` (mouse_dpi="01"); `reset()` preserves the byte unconditionally. |
| 4 | `NextReg::reset` for NR 0x10 (regs_[0x10] hard-set to `0x04` on every reset) | Pre-pass-8 the byte was unconditionally set to `0x04` on reset, wiping any user-written coreid value. VHDL nr_10_coreid is initial-only. | zxnext.vhd:1132-1133, 5677-5687 (Issue 2/3) / 5695-5703 (Issue 4/5) — only mutator is the NR 0x10 write path. | PASS-8: constructor seeds `regs_[0x10] = 0x04`; `reset()` preserves the byte unconditionally. |
| 5 | `NextReg::reset` for NR 0x11 (regs_[0x11] zeroed on every reset) | Cache wiped by `regs_.fill(0)`. VHDL nr_11_video_timing is initial-only. | zxnext.vhd:1134, no reset clause; only mutator at :5208-5217. | PASS-8: constructor seeds `regs_[0x11] = 0x03` (g_video_def for issue 2); `reset()` preserves the byte unconditionally. |
| 6 | `NextReg::reset` for NR 0x14/0x4A/0x4B/0x4C (cache zeroed but VHDL resets to E3/E3/E3/F) | `regs_.fill(0)` left the cache at 0 on the cycle after reset, until the post-handler write-through at emulator.cpp:2484-2488 ran. Subsystems (renderer/palette/sprites/tilemap) had their authoritative copies set correctly, but reads-from-cache during the gap returned 0. | zxnext.vhd:4946 / 5014 / 5016 / 5018. | PASS-8: cache seeded to 0xE3/0xE3/0xE3/0x0F in `reset()`. The existing post-handler write-through is now redundant but harmless; both paths leave the cache at the same value. |
| 7 | `NextReg::reset` for NR 0x8A (regs_[0x8A] zeroed on every reset) | Cache wiped by `regs_.fill(0)`. VHDL nr_8a_bus_port_propagate is initial-only. | zxnext.vhd:1236, no reset clause; only mutator at :5524-5525. | PASS-8: `reset()` preserves the byte unconditionally. Power-on default `0x00` is delivered by the `regs_{}` member value-init. |
| 8 | NR 0x01 / 0x0E / 0x0F write pollution | Pre-pass-8 these RO board generics had no write_handler, so the raw write byte landed in regs_[]. A user write of e.g. NR 0x01 ← 0xFF would corrupt subsequent reads of the version register. VHDL has no `nr_0X_we` write strobe for these — writes are silently dropped on the FPGA. | zxnext.vhd:5887 (NR 0x01), :5917 (NR 0x0E), :5920 (NR 0x0F). All read-only generic constants. | PASS-8: added RO guard at `NextReg::write` start. Writes to NR 0x01/0x0E/0x0F return immediately without storing or invoking any handler. The install-time values from `reset()` (0x32/0x03/0x00) become the read-back source of truth. |

## Class-(b) backlog — RESOLVED

Pass-7's report listed several class-(b) items pending resolution. They
are all either (a) folded into the class-(a) fixes above (NR 0x0A, NR 0x05
reset preservation), or (b) explicitly demoted to class-(c) below with a
VHDL citation:

| Class-(b) item | Disposition |
|---|---|
| Mouse `button_reverse_/dpi_` clobber | Resolved — class-(a) #1 above. |
| Joystick `joy0_mode_/joy1_mode_` clobber | Resolved — class-(a) #2 above. |
| NR 0x10 unconditional reset | Resolved — class-(a) #4 above. |
| NR 0x11 cache zeroing | Resolved — class-(a) #5 above. |
| NR 0x8A cache zeroing | Resolved — class-(a) #7 above. |
| NR 0x14/0x4A/0x4B/0x4C cache-zero gap | Resolved — class-(a) #6 above. |
| RO write pollution NR 0x01/0x0E/0x0F | Resolved — class-(a) #8 above. |
| `nr_10_coreid_` orphan shadow reset to 0x01 in `Emulator::init` | Demoted to class-(c). The shadow is only used at write time (`v & 0x1F` overwrites it before any subsequent read could observe), and reads go through the cache (which is preserved by class-(a) #4). The orphan reset is a paper-only divergence with no observable hardware-state difference. |
| NR 0x98/0x99/0x9A/0x9B Pi GPIO unmodelled | class-(c) — pi-GPIO is unmodelled in jnext. read_handlers return 0x00 (idle). The reset-block defaults FF/01/00/0 (zxnext.vhd:5070-5073) are not relevant because no software path can observe the difference. |
| NR 0xA8/0xA9 ESP unmodelled | class-(c) — same reasoning as Pi GPIO. |
| NR 0xF0/F8/F9/FA xadc unmodelled | class-(c) — xadc is unmodelled in jnext (XADC_INST stub). |
| NR 0x2D i2s_sample cache-vs-runtime | class-(c) — read_handler bypasses cache; i2s_ subsystem is authoritative. |
| NR 0x8F mapping mode cache-vs-MMU | class-(c) — read_handler delegates to `mmu_.nr_8f_mode()`, which is preserved by `Mmu::reset`. Cache is unused. |

## Convergence verdict

**Convergent. Zero class-(a) AND zero class-(b) remaining.**

The systemic pattern that ran through pass-6 (NR 0x06), pass-7 (NR 0x05 +
NR 0x09 + NR 0x08 + NR 0x81), and pass-8 (NR 0x0A + NR 0x10 + NR 0x11 +
NR 0x8A) is the same across all three passes: every initial-only VHDL
signal whose `reset()` clause does not appear in the master reset block
(or in a dedicated process with `if reset='1'`) must SURVIVE both hard
and soft reset in jnext. Pre-pass-6 jnext used `regs_.fill(0)` followed
by case-by-case re-application of "reset values" that conflated the FPGA
power-on initial values with the per-reset behaviour. Pass-8 closes the
gap for the last batch of cached registers AND for the two
authoritative-shadow subsystems (Joystick / KempstonMouse) that surface
their NR 0x05 / NR 0x0A bits via read handlers.

The exhaustive sweep table above is now exhaustive at the resolution of
"every NR signal declared in zxnext.vhd". Future passes that add NR
support for an unmodelled subsystem (Pi-GPIO, ESP, xadc, expbus) would
need to revisit those rows, but for the boot-relevant cross-section the
table is complete and the audit is converged.

## Refactor rationale

The pre-crash `first_boot_X = (regs_[X] == 0x00)` heuristic in
`NextReg::reset` was a clever shortcut, but it conflated "this is the
first reset() ever called" with "the cached byte happens to be zero".
For NR 0x05 (where 0x00 is a legal user-written value: joy0 = joy1 =
Sinclair2 + 5060=0 + scandouble=0), the heuristic would silently
re-apply 0x41 on every soft reset that followed a 0x00 write — a
subtler but real divergence than the "clobber to default" bug it was
trying to fix.

The PASS-8 refactor sidesteps the question entirely by seeding the
power-on defaults directly in the `NextReg` constructor, before the
first `reset()` call. This mirrors the existing pattern for NR 0x82-0x85
/ 0x86-0x89 / 0x7F / 0x80 (PASS-5 era), establishes a uniform "first
reset() preserves what's there" invariant, and makes every subsequent
reset() into a pure idempotent preservation pass — which IS what VHDL
does.

## Tests

* Build: clean (`cmake --build` from a clean configure, no warnings).
* `ctest`: 37 / 37 passing.
* `fuse_z80_test`: 1356 / 1356 passing.
* Regression suite: identical PASS / FAIL profile to the pre-pass-8
  branch state — the one failure (`parallax-demo`, 44636 pixels diff) is
  pre-existing on `task2/verify8-nmi-mf-port` (was inherited from earlier
  passes; reproduces on the branch HEAD without any pass-8 changes
  applied via `git stash`). My changes neither introduce it nor make it
  worse, and the diff signature stays at exactly 44636 pixels — so it is
  not a state-corruption regression introduced by pass-8.

## Files touched

* `src/port/nextreg.cpp` — PASS-8 refactor: constructor pre-seed of
  initial-only NR bytes; reset() simplified to unconditional preservation
  for NR 0x05/0x06/0x09/0x0A/0x10/0x11/0x8A; cache-seed of NR
  0x14/0x4A/0x4B/0x4C VHDL master-reset-block defaults; RO write-guard
  for NR 0x01/0x0E/0x0F.
* `src/input/joystick.{cpp,h}` — Removed `joy0_mode_/joy1_mode_/nr_05_raw_`
  clobber on reset; rely on member-init defaults; preserve on every
  subsequent reset; re-fan-out to `MembraneStick` so the membrane fold
  tracks the cached modes after a soft reset. Added `nr_05_raw()` accessor
  for the `Emulator::init()` defense-in-depth fan-out check.
* `src/input/mouse.cpp` — Removed `button_reverse_/dpi_` clobber on reset.
* `src/core/emulator.cpp` — Pre-crash NR 0x0A and NR 0x05 fan-out at
  `Emulator::init()` (defense-in-depth, no-op when subsystem and cache
  are coherent; the `if (cached != raw)` guard on the NR 0x05 path makes
  the no-op idempotent and re-establishes coherence after any future
  joystick-reset regression).
