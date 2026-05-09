# Verify-Audit Pass-8 — DivMMC + SD card + SPI bus

**Date**: 2026-05-09
**Branch**: `task2/verify8-divmmc-sd-spi`
**Worktree**: `.claude/worktrees/task2-verify8-divmmc-sd-spi`
**Files audited**:

- `src/peripheral/divmmc.{cpp,h}`
- `src/peripheral/sd_card.{cpp,h}`
- `src/peripheral/spi.{cpp,h}`
- Cross-cutting: `src/core/emulator.cpp` (NR 0x02 / NR 0x03 / SD-card lifecycle / save-load)

VHDL oracle: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
SD spec oracle: SD Physical Layer Simplified Spec 6.00.

---

## Verdict

**Convergence achieved.** Three class-(b) findings from the deep
re-audit were RESOLVED with VHDL/spec-faithful fixes. **Zero remaining
class-(a)** and **zero remaining class-(b)**.

| Class | Count | Status |
| ----- | ----- | ------ |
| (a) emulator-faithful but incorrect — boot-impact | 0 | n/a |
| (b) latent / out-of-boot-path / spec-divergent | 3 | **all resolved** |
| (c) modelling-acknowledged divergences (cycle accuracy, etc.) | small | catalogued |

The pre-crash agent had drafted these three fixes; the resumption
review verified them against VHDL line-by-line, kept all three, and
re-ran tests.

---

## Pre-crash review (5 modified files)

The pre-crash uncommitted diff modified five files. Each was verified
against the VHDL oracle at `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
and the SD Physical Layer Simplified Spec 6.00.

| File | Decision | Reason |
| ---- | -------- | ------ |
| `src/peripheral/spi.{cpp,h}` | **KEEP** | Implements VHDL :3319 Flash-CS gate |
| `src/peripheral/divmmc.cpp` | **KEEP** | Continuous-while-held button_nmi clear (VHDL :112-113) |
| `src/peripheral/sd_card.cpp` | **KEEP** | Symmetric unmount/reset + spec-faithful R1 illegal bit |
| `src/core/emulator.cpp`     | **KEEP** | Producer-side fan-out of Flash-CS gate (4 sites) |

Build: clean. Unit tests: 37/37 pass. Focused tests
(divmmc/sdcard/sd_rom/fuse_z80): 4/4 pass. Regression: 32/1 (parallax-demo
failure pre-exists on clean baseline — not caused by pass-8).

---

## Class-(b) resolution table

### B-1. SPI Flash-CS gate ungated (VHDL :3319 missed)

- **VHDL evidence**: `zxnext.vhd:3319`
  ```vhdl
  elsif cpu_do = X"7F" and ((nr_03_config_mode = '1') or
                             (nr_02_reset_type(2) = '1')) then
     port_e7_reg <= X"7F";
  ```
- **Pre-fix behaviour**: `SpiMaster::write_cs(0x7F)` always dropped to
  0xFF (= "all deselected"), even when the VHDL gate would admit it.
  Comment in pre-fix code: "Config mode is not modelled at this level,
  so Flash select is not recognized here."
- **Impact**: Boot-time FPGA flash IPL path (the only legitimate
  caller of port `0xE7 ← 0x7F`) had no way to reach the SPI flash
  device. In jnext, no flash backend is attached on the bit-7 CS line
  (the FPGA boot ROM is silicon-baked into the binary), so the
  immediate symptom was MISO-stuck-at-0xFF on a flash-CS write — but
  the model itself was VHDL-divergent and would silently break a
  future flash-attached test/feature.
- **Fix** (kept):
  - `spi.h`: added `flash_cs_enable_` member and
    `set_flash_cs_enable(bool)` setter.
  - `spi.cpp`: added the `else if (val == 0x7F && flash_cs_enable_)`
    branch, mirroring VHDL :3319 verbatim. When the gate is closed,
    the value falls through to the `else` (= 0xFF, all deselected),
    matching VHDL line 3322.
  - `emulator.cpp`: producer-side fan-out at four sites — NR 0x02
    write handler (post-strobe), NR 0x03 write handler (post-config-
    mode update), `init()` post-attach seed (power-on default), and
    `load_state()` (re-sync from canonical loaded state).
- **Save/load**: `flash_cs_enable_` is a derived field. Not persisted
  in `SpiMaster::save_state` (intentionally — it's recomputed by the
  emulator's `load_state` re-sync). Mirrors the existing pattern for
  `i2c_.set_pi_i2c1_en` re-sync. Verified producer-state ordering:
  `nextreg_.load_state` (5965) → `spi_.load_state` (5994) →
  `nmi_source_.load_state` (6051) → final re-sync (6128). No SPI
  accesses occur between these load-state steps.
- **Power-on seed**: `nextreg_.nr_03_config_mode()` returns true at
  power-on (VHDL :1102 `:= '1'`, no reset clause). Therefore
  `flash_cs_enable_` is true at boot, matching the firmware's
  flash-IPL window.

### B-2. DivMMC button_nmi rising-edge-only clear (VHDL :112-113 misread)

- **VHDL evidence**: `device/divmmc.vhd:107-114`
  ```vhdl
  if rising_edge(i_CLK) then
     if i_reset = '1' or i_automap_reset = '1' or i_retn_seen = '1' then
        button_nmi <= '0';
     elsif i_divmmc_button = '1' then
        button_nmi <= '1';
     elsif automap_held = '1' then
        button_nmi <= '0';
     end if;
  end if;
  ```
- **Pre-fix behaviour**: `DivMmc::check_automap` cleared `button_nmi_`
  only on the 0→1 rising edge of `automap_held_`. VHDL clears it
  every clock cycle while `automap_held` is high (subject to higher-
  priority resets and the `i_divmmc_button` re-setter).
- **Divergence**: a button-strobe arriving AFTER held=1 was already
  latched would re-set `button_nmi_=1`; pre-fix would never clear it
  again until held=0→1, but VHDL clears it on the very next clock.
  Practical impact: `NmiSource::is_nmi_hold()` consumes
  `held OR button_nmi`; pre-fix masked the divergence on the rising
  edge but exposed it at the falling edge of held — jnext kept the
  NMI hold asserted while VHDL released it.
- **Fix** (kept): replaced the 0→1 one-shot guard
  (`if (!prev_held && automap_held_)`) with a continuous-while-held
  guard (`if (automap_held_ && button_nmi_)`). The clear now fires
  every M1 fetch while held=1, matching VHDL's continuous-clear
  semantics. Higher-priority paths
  (`i_reset` / `i_automap_reset` / `i_retn_seen`) still pre-empt via
  the existing `reset()` / `apply_enabled_transition_()` /
  `on_retn()` / `on_m1_retn_delay()` handlers. The
  `i_divmmc_button` re-setter (`set_button_nmi(true)`) wins for the
  duration of one M1 (the same approximation pre-fix used).

### B-3. SD-card R1 illegal-command bit not asserted on unsupported commands

- **VHDL evidence**: SD spec § 7.3.2.1 R1 layout — bit 0 = idle,
  bit 2 = illegal command. SD card emulation is an external SPI
  peripheral, so the oracle here is the SD spec, not the FPGA VHDL.
- **Pre-fix behaviour**: `process_command()` default branch returned
  `R1 = (initialized_ ? 0x00 : 0x01)` for any unhandled CMD —
  i.e. only the idle bit, never the illegal-command bit. ACMD path
  hard-coded `0x05` (= idle + illegal), incorrectly asserting the
  idle bit on a post-init card. CMD16 with a non-512 blocklen had
  the same hard-code bug (already partially fixed in pass-5 — verify8
  audit caught the residual `cmd_arg() != 512` path still emitting
  `0x05`; resolved by deriving `idle_bit` from `initialized_`).
- **Impact**: latent — boot path (TBBlue / NextZXOS) only issues
  CMDs in the supported set. But silently swallowing illegal-CMD
  signals lets firmware error paths be skipped during bring-up of
  new firmware versions. Not a boot-blocker; spec compliance issue.
- **Fix** (kept):
  - Default CMD branch: `R1 = (initialized_ ? 0x00 : 0x01) | 0x04`.
  - ACMD default branch: same shape with `idle_bit | 0x04`.
- **Side effect**: none on tested boot paths (sdcard_tests still
  pass). Restores spec-faithful illegal-command signalling for
  future / non-tbblue firmware.

---

## Class-(c) catalogue (modelling-acknowledged, no fix)

These are documented divergences that would require multi-subsystem
re-architecture (out-of-scope for verify-audit) or are intentional
firmware-faithful design choices.

| ID | Site | Note |
| -- | ---- | ---- |
| C-1 | `spi.cpp` `spi_wait_n()` | Always returns true (no-wait). VHDL `o_spi_wait_n <= state_idle or state_last_d` would gate DMA cadence; jnext models SPI as zero-latency byte exchange. Captured at the `bool spi_wait_n() const` accessor with a long comment. Cycle-accurate replay would require an FSM rewrite (G137 long-term). |
| C-2 | `spi.cpp` SD-swap with single attached device | When `sd_swap=1` and firmware writes `val=0x02`, decode targets CS1 — but jnext only attaches a card on CS0. No device responds. Faithful to a hardware setup with only one slot populated. The user's responsibility. |
| C-3 | `divmmc.cpp` button_nmi continuous-clear within an M1 | VHDL clears `button_nmi` every clock while held=1; jnext clears it once per M1 fetch (which spans multiple clocks). Approximation is correct at M1 granularity — sufficient for all consumer logic, which itself only samples on M1. |
| C-4 | `sd_card.cpp` CSD/CID register layout | Hand-rolled CSD v2.0 + generic CID. Not from a reference card. CRC bytes are 0x00 (all firmware ignores them). Sufficient for boot. |
| C-5 | `sd_card.cpp` write protocol does not model BUSY tail | After CMD24 data response token, real cards hold MISO low (BUSY) for some time. jnext returns immediately to IDLE. Firmware tolerates absent BUSY (verified for tbblue). |

---

## SD lifecycle coherence audit

**Cold boot / hard reset**: `Emulator::reset()` calls `init()`. `init()` runs
`spi_.reset()` (clears `cs_`, `rx_data_`; preserves device bindings —
pass-7 fix), then `sd_card_.reset()` (only on `!preserve_memory` =
hard reset). After `attach_device()`, `flash_cs_enable_` is seeded
from the post-reset `nextreg_/nmi_source_` state. **Coherent.**

**Soft reset (NR 0x02 ← 0x01)**: `Emulator::soft_reset()` routes through
`init()` with `preserve_memory=true`. `sd_card_.reset()` is **NOT**
called (the SD card is an external SPI peripheral, not in the FPGA
reset domain — verified against VHDL at G46(b) earlier). The
firmware-soft-reset preserves `initialized_` so subsequent CMD17/18
reads don't fall into a re-init storm. `flash_cs_enable_` is
re-seeded post-reset to track the new `reset_type` FSM state.
**Coherent.**

**Mount**: `mount(path)` calls `unmount()` (closes file, calls
`reset()`), then opens new file, then calls `reset()` again. Idempotent.
The runtime SPI/SD path is independent from the host-side ROM
extractor (see CLAUDE.md). **Coherent.**

**Unmount**: pass-8 fix made this symmetric with mount() — uses the
canonical `reset()` to clear the full SPI-protocol state, not just the
`state_/initialized_/file_size_/pending_write_after_r1_` subset.
**Coherent.**

**Swap (`--sd-card` change)**: equivalent to unmount → mount. Both
now full-`reset()` so no protocol state can leak from the prior
image. **Coherent.**

**Save/load**: `SdCardDevice` is intentionally NOT a `Saveable` (see
sd_card.h:141-146 comment). The rewind ring skips the SD back end.
Mid-CMD18 stream snapshots are not captured — caller must terminate
multi-block before snapshotting. `SpiMaster::save_state` persists
`cs_/rx_data_/sd_swap_`; `flash_cs_enable_` is re-derived in
`Emulator::load_state` after `nextreg_` and `nmi_source_` are loaded.
**Coherent.**

**CS deassert**: `SdCardDevice::deselect()` clears the SPI-protocol
mid-stream state but preserves `initialized_`. Aborts CMD18 and
clears the CMD24 R1-bridge. **Coherent.**

---

## SPI bus arbitration audit

VHDL :3308-3322 ensures `port_e7_reg` always has at most one CS line
asserted (canonical patterns: 0xFE / 0xFD / 0xFB / 0xF7 / 0x7F /
0xFF). Our `write_cs` decodes only into these patterns; unrecognized
inputs collapse to 0xFF.

`active_device()` iterates 0..kMaxDevices-1 (= 0..3); finds the first
selected device. With `kMaxDevices=4`, Flash CS (bit 7, value 0x7F)
does not match any device slot — the loop returns nullptr.
`read_data()/write_data()` return 0xFF when no device is active,
matching VHDL `spi_miso <= '1'` default-else.

**Multi-device case**: when CS transitions from one selected device to
another (e.g. SD-CS0 → Flash-CS7 → SD-CS0), the loop in `write_cs`
fires `deselect()` on the previously-selected device exactly once per
transition. Verified for SD-CS0 → Flash-CS7: cs_=0xFE → 0x7F flips
bit 0 from 0→1, calling `sd_card_.deselect()`. Correct.

---

## Convergence verdict

- **Class-(a)**: 0 found in pass-8.
- **Class-(b)**: 3 found, **3 RESOLVED** (B-1 SPI Flash-CS gate, B-2
  DivMMC button_nmi continuous clear, B-3 SD R1 illegal-command bit).
  No deferrals.
- **Class-(c)**: 5 catalogued. All have explicit comments at the
  modelling site. None require fix at this pass.

**Both convergence criteria met**: zero class-(a), zero class-(b)
backlog. **Subsystem audit: CLOSED.**

---

## Test status

| Suite | Result |
| ----- | ------ |
| `divmmc_tests` | 100% pass |
| `sdcard_tests` | 100% pass |
| `sd_rom_extractor_tests` | 100% pass |
| `fuse_z80_tests` | 100% pass |
| All 37 ctest unit suites | 37/37 pass |
| Regression suite | 32 pass / 1 fail (`parallax-demo`) — **failure pre-exists on clean baseline; NOT caused by pass-8 changes** |

Verified by stashing the pass-8 diff, rebuilding, running regression
on clean HEAD: same `parallax-demo` failure with same 44636 pixel
delta.

---

## Branch / commit metadata

- Branch: `task2/verify8-divmmc-sd-spi`
- Worktree: `.claude/worktrees/task2-verify8-divmmc-sd-spi`
- Files modified: 5 (counts above)
- Net diff: +112 / −18 lines

No push, no merge per session rules. Commit follows on local branch
only.
