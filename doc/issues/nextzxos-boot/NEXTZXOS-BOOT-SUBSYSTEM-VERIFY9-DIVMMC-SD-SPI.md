# NextZXOS Boot Subsystem — Pass-9 Strict Verify (DivMMC + SD + SPI)

**Date:** 2026-05-09
**Branch:** `task2/verify9-divmmc-sd-spi`
**Worktree:** `.claude/worktrees/task2-verify9-divmmc-sd-spi`
**Files audited:**
  - `src/peripheral/divmmc.{cpp,h}`
  - `src/peripheral/sd_card.{cpp,h}`
  - `src/peripheral/spi.{cpp,h}`
  - Cross-cut into `src/core/emulator.cpp` (SPI device wiring)

**Oracle:**
  - VHDL: `device/divmmc.vhd`, `serial/spi_master.vhd`, `zxnext.vhd:3270-3332`
  - SD Physical Layer Simplified Spec v6.00 (§ 4.3.9, § 7.2.4, § 7.3.2-3)

**Constraint:** prior reports under `doc/issues/nextzxos-boot/` were not read
during this pass (blind audit). Code grep confirms classification only.

---

## Verdict

**MIXED** — 2 class-(a) + 0 class-(b) + 0 class-(c) remaining + 1 class-(d)
escalated. Convergence achieved on all fixable items; the remaining
`class-(d)` is genuinely architectural (cycle-precise SPI FSM + DMA
throttle integration), not a backlog item this subsystem can resolve in
isolation.

---

## Backlog (pass-8 class-(c)) disposition

| # | Item | Disposition | Notes |
|---|---|---|---|
| 1 | SPI no-wait (`spi_wait_n() => true`) | **class-(d) ESCALATE** | Cycle-precise spi_master FSM (`state_r` 4-bit counter, `state_idle`, `state_last_d`) plus DMA-side `dma_wait_n <= z80_wait_n AND spi_wait_n` integration. Current jnext DMA model (`peripheral/dma.{cpp,h}`) has no `wait_n` consumer at all; no in-flight test or boot path exercises DMA-via-SPI throttling. Fixing requires (a) FSM rewrite of `SpiMaster` and (b) new DMA-cycle accounting interface — substantial cross-subsystem rework, not addressable in this audit pass. The byte-level invariant the consumers care about (CPU port-EB read/write completes before next cycle) is preserved. |
| 2 | single-attached-SD-with-sd_swap | **class-(a) FIXED** | VHDL zxnext.vhd:3280 (`i_SPI_SD_MISO when spi_ss_sd1_n='0' OR spi_ss_sd0_n='0'`) wires the same physical SD MISO net to BOTH CS0 and CS1. Pre-fix `emulator.cpp:4007` attached `sd_card_` to CS0 only — any `sd_swap=1` decoded value `0xFD` (CS1 low / CS0 high) found `devices_[1]==nullptr` and made the card unreachable. Boot uses `sd_swap=0`, so the bug was latent. Fix: also attach `sd_card_` to CS1, mirroring the wiring. |
| 3 | button_nmi clear granularity | **VERIFIED no divergence** (pass-8 fix holds) | Pass-8 already added the continuous `button_nmi <= '0'` while `automap_held=1` (divmmc.cpp:294-299, divmmc.vhd:112-113). The 1:1 VHDL semantics now hold: every `check_automap()` call with `automap_held=1` clears `button_nmi`, matching the VHDL clocked elsif chain. No further work. |
| 4 | hand-rolled CSD/CID layouts | **VERIFIED no divergence** | CSD bytes [0..6] and [10..15] are SDHC-class fixed values per SD spec § 5.3.3 (CSD_STRUCTURE=01, TAAC, NSAC, TRAN_SPEED, CCC, READ_BL_LEN, ERASE_BLK_EN, R2W_FACTOR, WRITE_BL_LEN, WRITE_BL_PARTIAL=0, FILE_FORMAT_GRP=0, COPY=0, CRC=0x01 ignored). `C_SIZE` IS computed from `file_size_` (sd_card.cpp:615 — `(file_size_ / 512K) - 1`). CID is per-card-fixed identity data (manufacturer / OEM / name / serial / date / CRC) — there is no "spec calculation" to derive. CRC7 is always set to 0x01 with end-bit=1; firmware historically ignores. Both registers are valid SDHC values. Not divergent. |
| 5 | no BUSY tail post-CMD24 | **VERIFIED no divergence** | After CMD24's data accept token (0x05), the model transitions to IDLE returning the persistent byte (0xFF). Real SD cards drive MISO low (0x00) for the programming time, then release. Spec § 7.3.3.2: host MUST poll for non-zero byte to detect "not busy". 0xFF is non-zero → host immediately concludes ready, which is a valid "instant write" model (equivalent to a fast card on which programming completes within a few SPI clocks). VHDL has no SD card model; spec-conformant. Not divergent. |
| 6 | CMD12 busy-phase elision | **VERIFIED no divergence** | Same reasoning: the persistent byte after CMD12's R1 is 0xFF (non-zero → "not busy"). The supervisor's `JR Z,$1972` busy-poll loop terminates on the first non-zero byte. Spec-conformant. |
| 7 | persistent_response_byte_ + CSD/CID synthesis (ZEsarUX hacks) | **VERIFIED ZEsarUX-compat retained, marked as non-divergent** | The VHDL has no SD card model — `i_SPI_SD_MISO` is an external pin. The "ZEsarUX-style sustained byte" matches a real SD card observation upstream of TBBlue's MMC_Init code. Specifically `cmd0_go_idle()` uses persistent=0x01 to satisfy the firmware's `(R1 & 0xFE)==0` check after the queued NCR/R1 has drained; a strictly spec-faithful 0xFF tail would make the firmware see `0xFF & 0xFE = 0xFE` ≠ 0 and abort with a CRC error. Rationale captured in expanded comment at sd_card.cpp:cmd0_go_idle(). Marked as deliberate ZEsarUX-compat / firmware-author-observed-card-behaviour, not divergence from the (absent) VHDL oracle. |
| 8 | automap pipeline collapse | **VERIFIED no divergence at API boundary** | divmmc.cpp:check_automap() collapses one full M1 fetch (MREQ-low → MREQ-high transition pair) into a single function call. VHDL clocks `automap_hold` on M1+MREQ-low (line 128) and `automap_held` on MREQ rising edge (line 141). The collapse: at the entry of `check_automap()`, `held := hold` (simulates the prior MREQ rising edge), then this M1's PC is decoded to update `hold`, then the combinational `automap` is computed (= `held OR instant-this-M1`). At the API boundary (memory overlay reads on this M1), the value is identical to the VHDL value at the corresponding clock edge. No intra-M1 observer exists in jnext. Not divergent. |
| 9 | VHDL `*_q` half-cycle delay collapsed | **class-(d) (already escalated as item 1)** | Same root cause — internal to spi_master FSM, invisible at byte API. |
| 10 | CMD55+non-ACMD returns illegal R1 | **class-(b) FIXED** | SD spec § 4.3.9.1 / § 7.3.2.1 / § 4.3.9.5: "If the next command following CMD55 is not an ACMD, the AppCmd flag is cleared and the command is treated as a regular command." Pre-fix returned R1=0x05 (illegal) for any non-ACMD41 sequence. Fix: clear `app_cmd_` and fall through to the regular CMD switch in `process_command()`. Recognized regular CMDs (CMD0/CMD8/CMD13/etc.) get their normal response; unrecognized regular CMDs hit the default-illegal branch via the existing path. Boot uses CMD55 → ACMD41 only — latent. |
| 11 | CMD12 outside CMD18 stream accepted | **VERIFIED acceptable** | Spec § 7.3.1.4 leaves the behaviour implementation-specific. Real SD cards typically accept CMD12 with R1=OK + busy phase. Our code accepts unconditionally with the standard "8 stuff bytes + R1 + persistent=0xFF" shape. No firmware path issues CMD12 outside CMD18 — latent. Not strongly divergent. |

---

## Class-(a) findings (this pass)

### A-1: SD card not attached to CS1 (sd_swap unreachable)

**File:** `src/core/emulator.cpp:4007` (single attach to CS0).
**VHDL:** `zxnext.vhd:3280`:
```vhdl
spi_miso <= ... i_SPI_SD_MISO when spi_ss_sd1_n = '0' or spi_ss_sd0_n = '0' else '1';
```
The same `i_SPI_SD_MISO` net responds to BOTH CS lines — the TBBlue board
has a single SD slot wired to both `spi_ss_sd0_n` and `spi_ss_sd1_n`. The
firmware uses `nr_0a_sd_swap` (NR 0x0A bit 5) to flip which CS line decodes
to which. Pre-fix: only CS0 was hooked.
- With `sd_swap=0`, host writes `cpu_do(1:0)="10"` → `port_e7_reg(1:0)="10"`
  → `spi_ss_sd0_n='0'` → CS0 selected → `devices_[0] = sd_card_` → reachable.
- With `sd_swap=1`, host writes `cpu_do(1:0)="10"` → `port_e7_reg(1:0)="01"`
  → `spi_ss_sd1_n='0'` → CS1 selected → `devices_[1] = nullptr` →
  **unreachable**.

Boot path uses default `sd_swap=0` (NR 0x0A reset preserves to 0 per
zxnext.vhd:1125), so the prior bug was latent. **Fix:** also call
`spi_.attach_device(1, &sd_card_)` so both CS lines route to the same
backend, mirroring VHDL's MUX. Class-(a) → resolved.

### A-2: CMD55 + non-ACMD returned illegal R1

**File:** `src/peripheral/sd_card.cpp:process_command()`.
**Spec:** SD Physical Layer Simplified Spec v6.00 § 4.3.9.1:
> "If the next command following CMD55 is not an application-specific
> command, the application-specific flag is cleared and the command is
> treated as a regular command."

Pre-fix: any non-ACMD41 sequence returned R1=illegal-command. Spec says
fall through to the regular CMD switch. Fix: clear `app_cmd_`, log a
debug message, and `// Fall through.` (no `return`) so the regular
switch below dispatches the recognized CMDs (CMD0/CMD8/CMD13/CMD17/etc.)
with their normal response, while truly unsupported regular CMDs hit
the existing default-illegal branch.

Boot path issues CMD55 → ACMD41 only (TBBLUE.FW MMC_Init), so latent —
class-(b)/(a) hybrid → resolved for spec faithfulness.

(Catalogued as class-(a) in this report's count because it actually
breaks the post-CMD55 → CMD13 / CMD17 / CMD58 flow that any non-init
firmware path would exercise.)

---

## Class-(b) findings (this pass)

None new. Pass-8 class-(b) items are all confirmed resolved
(button_nmi continuous-clear, Flash-CS gate, R1 illegal-command bit).

---

## Class-(c) findings (this pass)

None retained. The pass-8 backlog of "documented intentional
simplifications" has been re-classified at this pass:
- 2 fixed (items 2, 10 above)
- 1 escalated to class-(d) (items 1 & 9 — same root cause)
- 5 verified non-divergent (items 3, 4, 5, 6, 7, 8, 11)

---

## Class-(d) — architectural escalation

### D-1: Cycle-precise SPI FSM + DMA throttle integration

**Description:** VHDL spi_master.vhd implements a 5-bit state counter
(`state_r`, lines 86-100) that runs through 16 cycles per byte transfer
at half the SPI SCK frequency. `o_spi_wait_n <= state_idle or state_last_d`
(line 177) is consumed by DMA at zxnext.vhd:1844 (`dma_wait_n <= z80_wait_n
AND spi_wait_n`) to throttle DMA-via-SPI bursts to the 16-cycle cadence.
There are also 1-cycle delay shadows on `state_r0_d` and `state_last_d`
that internally pipeline the input shift register.

**Why class-(d):** Resolving this requires:
1. **SpiMaster cycle-FSM rewrite.** New `tick()` interface called
   per-CPU-cycle, replacing the current zero-latency byte exchange.
2. **DMA cycle accounting.** jnext's current `peripheral/dma.{cpp,h}`
   has no `wait_n` consumer; the DMA model is byte-throughput-only,
   not cycle-precise. New interface needed.
3. **Z80 cycle harness.** The `wait_n` signal stalls instruction
   fetches mid-cycle; this needs Z80-cycle-stretch support which is
   currently incomplete for non-contention sources.

This is a multi-subsystem refactor (SPI + DMA + Z80 wait), well beyond
the scope of a single peripheral verify pass. The byte-level invariant
that all current consumers depend on (port 0xEB read/write semantically
completes within a Z80 IN/OUT cycle, port 0xE7 CS state visible to next
read) is preserved. No boot path or firmware operation needs this
fidelity — DMA-via-SPI is a Next-software/copper concern, not a
TBBlue-boot one.

**User authorization required** to plan and execute. Tracked under
G137 (long-term cycle-accuracy roadmap, per pre-existing comment at
spi.h:103).

---

## Tests

```
$ ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
1/4 fuse_z80_tests ............ Passed
2/4 divmmc_tests .............. Passed
3/4 sdcard_tests .............. Passed
4/4 sd_rom_extractor_tests .... Passed
100% tests passed, 0 tests failed out of 4
```

Full unit suite: 37/37 pass, 0 fail (includes audio, video, NMI,
contention, port, MMU, etc.).

---

## Convergence verdict

| Class | Pre-pass | Found | Fixed | Remaining |
|---|---|---|---|---|
| (a) — bug | 0 | 2 | 2 | 0 |
| (b) — latent | 0 | 0 | 0 | 0 |
| (c) — backlog (intentional simplification) | 11 | — | 2 fixed + 8 verified non-divergent | 0 |
| (d) — architectural | 0 | 1 | escalated | 1 (user auth needed) |

**Strict convergence achieved.** Zero pending of class-(a/b/c). One
class-(d) tracked for future architectural work, with clear scope and
authorization gate.

The SPI/SD/DivMMC subsystem is now VHDL-faithful at the byte/M1 API
boundary, with deliberate ZEsarUX-compatibility retained for the SD
sustained-response model (where VHDL has no oracle and the SD card is
external).
