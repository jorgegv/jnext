# Pass-6 Verify Audit — DivMMC + SD-Card + SPI Master

**Worktree**: `.claude/worktrees/task2-verify6-divmmc-sd-spi`
**Branch**: `task2/verify6-divmmc-sd-spi`
**Date**: 2026-05-09
**Auditor**: ultrathink, blind (did not read prior `doc/issues/nextzxos-boot/*.md`).

## Verdict

**One class-(a) finding** — NR 0x83 reset propagation gap.
**Convergence: very near** for the divmmc/sd/spi triplet — the surface
area covered by previous five passes is now consistent with the VHDL at
every spot I re-checked except for the one finding below. No additional
class-(a) bugs surfaced after a thorough sweep of all eight Pass-6
angles (tbblue byte-stream replay, SPI cycle precision, SD response
timing, mid-CMD byte stream, AUTOMAP edge cases, DivMMC RAM hard-reset,
SPI multi-device arbitration, and a fresh `divmmc.vhd` differential
sweep against the C++).

## Methodology

VHDL-as-oracle. Re-derived every signal in `device/divmmc.vhd` (153
lines) and `serial/spi_master.vhd` (179 lines) from scratch, then
mapped each to the C++ counterparts in `divmmc.{h,cpp}`,
`sd_card.{h,cpp}`, and `spi.{h,cpp}` (plus the `Emulator` integration
glue at port-decode and reset sites). Walked `zxnext.vhd:3270-3332`
(SPI port decode), `:4102-4188` (DivMMC instantiation + port_e3
register), `:2848-2908` (entry-point decode), `:2392-2412` (NR 0x82-0x85
internal_port_enable concat), `:5052-5067` (port-enable reset gates),
`:5191-5198` (NR 0x0A field decode), and `:6340-6371` (hotkey
producers). Cross-checked tbblue's `diskio.c` (`select`, `deselect`,
`wait_response`, `rcvr_datablock`, `xmit_datablock`, `send_cmd`) for
the boot-time SD-byte expectations and verified jnext's `SdCardDevice`
emits the byte streams the firmware actually polls for.

## Pass-6 angle sweep

### 1. tbblue.fw exact byte-stream replay verification

Walked through `disk_initialize()` in tbblue's diskio.c step by step
(CMD12 stuff-byte cancel → CMD0 → CMD8 R7 → ACMD41 loop → CMD58 OCR →
CMD16 if needed). For each command, traced what jnext emits on MISO and
confirmed the firmware's `wait_response()` poll lands on the right R1
byte and the trailing `rcvr_mmc(buf, 4)` reads pick up the correct R7/R3
trailer bytes. CMD8 voltage-echo + check-pattern, CMD58 CCS bit, CMD12
8-stuff-byte preamble, CMD0 idle persistence — all consistent. **No
finding.**

### 2. SPI clock cycle precision

VHDL `spi_master.vhd:82-100` runs a 5-bit state counter that takes 16
master-clock cycles per byte (`state_r` walks 0..15 then idles). VHDL
`spi_master.vhd:177` exposes `o_spi_wait_n <= state_idle or
state_last_d`, consumed by DMA at `zxnext.vhd:3297`. jnext's
`SpiMaster::write_data()` and `read_data()` are zero-latency: no
state machine, no cycle counting; `spi_wait_n()` returns `true`
unconditionally (already documented at `spi.h:79-90`). For the boot
path the firmware uses programmed I/O at 3.5 MHz, where each `OUT
(0xEB), A` is ~12 T-states (~3.4 µs) — far longer than the 16
master-cycles (~570 ns) a real SPI transfer would take, so the byte is
always ready when the next OUT/IN fires. DMA-via-SPI (the only
consumer of `wait_n`) isn't on the boot critical path. **No
finding** — VHDL deviation is documented and inconsequential for boot.

### 3. SD CMD response timing

Real SD: response arrives 0..N clocks after the CMD packet (N_CR =
1..8 in SPI mode). Tbblue's `wait_response()` polls for up to 250
non-0xFF bytes, so any `N_CR ∈ [0, 250]` is acceptable. jnext queues
NCR=`{0xFF, R1}` (one stuff byte + R1) for most commands and
`{0xFF×8, 0xFF, R1}` for CMD12 (matches the firmware's hard-coded
`rcvr_mmc(buf, 8)` on CMD12 at `diskio.c:241`). Both within spec.
**No finding.**

### 4. SD-card byte-stream consistency mid-CMD

CMD17/CMD18 read `1 + 512 + 2 = 515` bytes of payload. Walked the
state machine: `State::SENDING_DATA` increments `resp_idx_`,
`data_idx_`, then `data_crc_count_` sequentially, with a host-stop
behaviour where the index is preserved and the next read picks up
exactly where the previous one left off. CMD18's between-blocks
re-prime emits one 0xFF inter-block filler byte before the next 0xFE
token, matching the firmware's "skip until non-0xFF" token-poll at
`diskio.c:161-164`. **No finding.**

### 5. AUTOMAP entry edge cases

Walked all eight RST entries plus NMI@$0066 plus tape-trap variants
plus the $1FF8-$1FFF off-trigger. Verified `automap_held` rising-edge
clear of `button_nmi` (divmmc.vhd:112-113) is honoured. Walked the
case where an off-trigger fires while `i_automap_active=0` — VHDL's
`(automap_held AND NOT (i_automap_active AND i_automap_delayed_off))`
correctly carries `held` forward, and jnext's `off_match` gating on
`main_path_eligible` reproduces this. **No finding.**

### 6. Hard reset clears DivMMC RAM

`DivMmc::reset()` correctly preserves `ram_` across both soft and
hard reset, matching VHDL where the FF-based control state (button_nmi,
automap_hold, automap_held) is the only thing the `i_reset` /
`i_automap_reset` signals zero. The 128 KB RAM is in the constructor's
zero-init only — power-on / `Emulator::reset()` (= F1) doesn't touch
it because the DivMmc object isn't reconstructed. This matches the
real-hardware semantics where DivMMC SRAM survives a warm reset and
only blanks on a power cycle. **No finding.**

### 7. SPI multiple-device arbitration

VHDL `zxnext.vhd:3278-3280` arbitrates MISO with priority Flash > RPI >
SD > '1' (the default-else when no SS line is asserted). jnext's
`SpiMaster::active_device()` walks `devices_[0..3]` lowest-first; with
the production wiring (only SD on slot 0) the priority chain is moot.
Flash on bit 7 isn't reachable through `kMaxDevices = 4`, but no
emulator code attaches it. **No finding** for boot — class-(c) latent
for any future Flash routing.

### 8. Final differential signal sweep against `divmmc.vhd`

Re-derived every signal in `divmmc.vhd` and confirmed each has a
modelled C++ equivalent:

| VHDL signal | C++ equivalent | Notes |
|---|---|---|
| `i_cpu_a_15_13` | `pc` (top 3 bits) | Used in PC == 0x0000/0x2000 decode. |
| `i_cpu_mreq_n`/`i_cpu_m1_n` | `is_m1` arg | Single-edge model collapses both. |
| `i_en` | `port_io_enable_` | NR 0x83 bit 0. |
| `i_automap_reset` | `enabled_=false` early-return | port_io ∧ nr_0a_4. |
| `i_automap_active` | `sram_pre_override_2` arg | Per-M1 from Mmu helper. |
| `i_automap_rom3_active` | `sram_pre_override_2 && _0 && !L2 && rom3` | Composite. |
| `i_retn_seen` | `on_m1_retn_delay()` | 1-M1 delay register, MF-gated. |
| `i_divmmc_button` | `button_nmi_` | Strobe via `set_button_nmi(true)`. |
| `i_divmmc_reg` | `control_reg_` | Bits 5:4 zeroed at read time. |
| `i_automap_*_on` | `entry_points_0_/1_` decoded with `valid`/`timing` | NR 0xB8/B9/BA/BB. |
| `o_divmmc_rom_en` | `is_rom_mapped()` | (active ∧ page0 ∧ ¬mapram). |
| `o_divmmc_ram_en` | `is_ram_mapped(addr)` | per VHDL formula. |
| `o_divmmc_rdonly` | `is_read_only(addr)` | page0 always RO; bank 3 RO with mapram. |
| `o_divmmc_ram_bank` | `bank_` (page0 forces 3) | divmmc.cpp:466-471. |
| `o_disable_nmi` | `is_nmi_hold()` | `automap_held_ \|\| button_nmi_`. |
| `o_automap_held` | `automap_held()` | accessor. |

**One finding from the sweep**: NR 0x83 reset propagation (see below).

## Findings

### Finding 1 (CLASS-A): NR 0x83 reset value not propagated to DivMmc / Multiface

**VHDL reference**: `zxnext.vhd:1227, 2392, 2412, 5052-5057`.

`port_divmmc_io_en <= internal_port_enable(8) <= nr_83_internal_port_enable(0)`
(combinational, `:2412`). NR 0x83 reloads to `0xFF` whenever the FPGA
sees `reset='1'` (i.e. on every soft + hard reset) **provided
`nr_85_internal_port_reset_type='1'`** — which is the FPGA power-on
default per `:1230`. So the post-reset VHDL state is:
`port_divmmc_io_en = NR 0x83 bit 0 = 1`.

In jnext, `NextReg::reset()` correctly sets `regs_[0x83] = 0xFF` on
the reset_type_1 path (and preserves the saved value otherwise), but
the **registered NR 0x83 write_handler at `emulator.cpp:2099` is NOT
fired by the reset path**. The handler is what propagates bit 0 →
`DivMmc::set_port_io_enable()` and bit 1 → `Multiface::set_enabled()`.
Meanwhile `DivMmc::reset()` deliberately preserves `port_io_enable_` /
`nr_0a_4_enable_` across reset (per the comment at `divmmc.cpp:30-32`,
which is correct per VHDL — those flags live in the NR / port lever
domain, not the divmmc FF domain).

The collision: a firmware sequence that clears NR 0x83 bit 0 (e.g. for
diagnostic isolation) followed by a soft reset (NR 0x02 bit 0) leaves
`DivMmc::port_io_enable_=false` even though VHDL has reloaded NR 0x83
to `0xFF` (= bit 0 high). The DivMMC overlay stays disabled in jnext
where VHDL has it enabled. Identical concern for `Multiface::enabled_`
(NR 0x83 bit 1).

The **hard-reset path is fine** because `init()` at `emulator.cpp:3987`
explicitly calls `divmmc_.set_enabled(true)` after extracting
`enNxtmmc.rom` from the SD image, but that's gated on `!preserve_memory
&& !cfg.sd_card_image.empty()` — so neither soft reset nor unit-test
fixtures (which run with empty `sd_card_image`) hit it. The
**reset_type_1=false** soft-reset path is also fine because
`Emulator::soft_reset()` at `emulator.cpp:5215` explicitly does
`nextreg_.write(0x83, save_83)` which **does** fire the write_handler.

The gap is specifically: **soft reset with `reset_type_1=true` and a
prior firmware NR 0x83 bit 0 = 0**. Latent for the NextZXOS boot path
(supervisor doesn't clear NR 0x83 bit 0), but a real VHDL deviation by
the verify-rule.

**Class**: **(a)** — VHDL-faithfulness gap.
**Likelihood of biting boot**: low (latent).
**Fix**: see below.

#### Fix

`emulator.cpp` — at the end of `Emulator::init()` (after handler
registration is complete and the optional `divmmc_.set_enabled(true)`
on the SD-ROM-extract block has run), explicitly synchronise DivMmc
and Multiface state from the cached NR 0x83 byte:

```cpp
// Pass-6 verify-audit fix (Task 2 verify6-divmmc-sd-spi, 2026-05-09):
// sync NR 0x83 bits 0:1 into DivMmc / Multiface after the NextReg reset
// and handler-registration sequence...
divmmc_.set_port_io_enable((nextreg_.cached(0x83) & 0x01) != 0);
multiface_.set_enabled((nextreg_.cached(0x83) & 0x02) != 0);
```

Both setters are idempotent — calling them with the value the state
already has is a no-op — so the hard-reset case (where `set_enabled`
was already called above) and the reset_type_1=false soft-reset case
(where the explicit `nextreg_.write(0x83, save_83)` will fire later)
both stay consistent.

#### Test fallout

Test `nmi/nmi_test.cpp::HK-07b` ("F10 honours port_divmmc_io_en gate")
relied on the pre-fix invariant that `DivMmc::port_io_enable_` defaults
to `false` post-reset. With the fix, the post-reset default is `true`
(matching VHDL — NR 0x83 bit 0 = 1 in the default reset state). The
test was updated to **explicitly** clear `port_io_enable` before
exercising the gate-off path; the test is now genuinely exercising the
gate (`set_port_io_enable(false)` → F10 strobe blocked) instead of
silently riding a stale jnext invariant.

## Convergence assessment

Pass-6 finds **one** class-(a) bug. Trend by pass:

| Pass | Class-(a) found |
|---|---|
| 1 | 3 |
| 2 | 0 |
| 3 | 2 |
| 4 | 1 |
| 5 | 2 |
| 6 | 1 |

The trend is bouncy-low (1-2 per pass after the initial sweep). Each
pass exercises a different angle, and the divmmc/sd/spi surface is
small enough (≈2400 lines of C++ + 332 lines of VHDL) that most of the
straightforward VHDL deviations have now been resolved. The remaining
risk surface is specific and well-scoped:

- **Reset-domain alignment** (this pass's finding) — class of "C++
  caches that don't get refreshed when the underlying register file is
  reset because the registered handlers aren't fired". The fix here
  patches NR 0x83's two consumers; the same idiom may apply to
  *other* cross-handler reset sync points (NR 0x82 for Kempston,
  NR 0x84 for sprite-tie / port_p3_floating_bus, NR 0x85's various
  consumers, NR 0x86-0x89 bus-port enables) but those subsystems are
  out of scope for the divmmc/sd/spi triplet and would be picked up
  by future passes for those areas.
- **Multi-device SPI arbitration** (Flash on bit 7) — class-(c) for
  boot, class-(b) once a Flash backing device gets attached.
- **DMA-SPI cycle precision** — class-(c) until DMA-via-SPI is on a
  hot path.

I judge this triplet **at honest convergence** — the next blind pass
would either find no class-(a) bugs at all, or surface a deeper
architectural mismatch (DMA cycle precision, full Flash routing) that
isn't a "soak more carefully" issue.

## Open questions (deferred)

1. **Should `NextReg::reset()` invoke registered write_handlers with
   the new register values?** That would generalise the fix-pattern
   used here. Risk: handlers may not be idempotent; firing them
   during a global reset could double-fire side-effects (e.g.
   peripheral resets that already ran in the subsystem reset loop).
   Out-of-scope for this pass; flagged for `Emulator::init()` /
   `NextReg::reset()` design review.
2. **DivMMC RAM cold-reset semantics**. Currently the 128 KB RAM is
   only zeroed in the constructor; F1-hard-reset preserves it because
   it doesn't reconstruct the DivMmc object. Real-Next F1 reloads the
   FPGA bitstream → wipes everything. jnext's F1 is a soft-internal
   reset that mimics power-cycle imperfectly. Class-(c) for boot
   (DivMMC RAM is not used by the boot ROM as persistent storage), but
   worth a future audit if any test ever depends on cold-RAM=zeros
   semantics across F1.

## Test status

```
unit tests        37/37 pass (after HK-07b update)
divmmc_tests       9/9 pass
sdcard_tests       19/19 pass
sd_rom_extractor   3/3 pass
fuse_z80           1356/1356 pass
regression suite   32 pass / 1 fail (parallax-demo, pre-existing in
                   baseline — unrelated to this fix; verified by
                   stash-and-rebuild on the unmodified branch)
```

## Files touched

- `src/core/emulator.cpp` — added 27-line NR 0x83 sync block at end of
  `Emulator::init()` (after the SD-ROM-extract block, before the rewind
  buffer setup, so `save_state()` snapshots the post-sync state).
- `test/nmi/nmi_test.cpp` — `HK-07b` now explicitly clears
  `port_io_enable` before exercising the F10 gate-off path. The test
  comment was updated to flag this as a Pass-6 verify-audit
  consequence.

## Branch HEAD

(See git log on `task2/verify6-divmmc-sd-spi`.)
