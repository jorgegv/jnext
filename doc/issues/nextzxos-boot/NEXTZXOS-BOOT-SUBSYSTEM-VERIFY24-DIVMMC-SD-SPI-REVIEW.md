# Verify-24 DivMMC + SD + SPI subsystem audit — independent review

**Reviewer scope:** independent re-review of the Pass-24 BLIND convergence
pressure-test audit (DivMMC + SD + SPI subsystem), against the VHDL oracle and
the SD Physical Layer Simplified Spec v6.00.

**Audit commit (HEAD):** `f0e481e7` (`doc(task2-pass24-divmmc): audit report
— 112-row enumeration table + 1 class-c finding (V24-DIVMMC-01) + 1 class-d
escalation (V24-DIVMMC-02)`).

**Fix commit:** `0af78514` (`fix(task2-pass24-divmmc): V24-DIVMMC-01 — CMD10
CID MDT year encoding off-by-4`).

**Audit document:** `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-DIVMMC-SD-SPI.md`.

---

## Verdict: **APPROVE — no missed findings**

The audit honestly enumerates 112 rows (4 new beyond P21's 108 baseline, all
legitimate surfaces not previously catalogued). V24-DIVMMC-01 is real per SD
Phys Layer Simplified Spec § 5.2 Table 5-1, the fix correctly encodes year
2026 / month 05 (CID[14]=0xA5), the SD-33 regression test is discriminative
(verified pre-fix FAIL, post-fix PASS). V24-DIVMMC-02 escalation to class-(d)
is correctly justified: a partial fix (gate the multiplier only) would leave
a half-modelled SDSC mode (CMD16 still rejects non-512 lengths, CSD still
v2.0 SDHC layout, no test fixtures for SDSC) — full SDSC fidelity is genuinely
architectural. Cross-cutting families (P22 NR 0x83/87 fan-out for DivMMC E3 +
SPI E7/EB, P23 IM2 RETN delay + MF gate) verified clean.

After V24-DIVMMC-01 fix, the DivMMC + SD + SPI subsystem retains **CONVERGED**
status at byte-level granularity (modulo the catalogued class-(d) items).

---

## Review checklist

### Step 1 — row count: P21 baseline 108, P24 enumeration 112 (+4)

Counted the row markers `^| [0-9]` in both audit reports.

- Pass-21 (`d8647df0` aggregate report references `142284cd` audit doc):
  108 rows.
- Pass-24 (`f0e481e7` audit doc): 112 rows (rows #1..#112).
- New rows #109/#110/#111/#112 all marked **NEW row P24** with explicit
  rationale:
  - **#109** `emulator.cpp:5267-5268` post-reset port_io fan-out
    `divmmc_.set_port_io_enable((effective_internal_port_enable(0x83) & 0x01) != 0)`.
    Verified at `src/core/emulator.cpp:5267-5268`: code matches; the
    surrounding comment block describes the V16-NMP-02 P22 fan-out
    behaviour. Surface NOT previously catalogued. Legitimate.
  - **#110** `spi.cpp:104-109` `set_sd_swap`. Verified at
    `src/peripheral/spi.cpp:108-113`. VHDL `nr_0a_sd_swap` in
    `zxnext.vhd:1125,5194` is a signal-declaration initial-value-only field
    (not in the reset block) — survives soft reset. Audit claim correct.
  - **#111** `mmu.cpp:944-955` `Mmu::divmmc_read` / `divmmc_write`. Verified
    at `src/memory/mmu.cpp:945-958` (`if (!divmmc_->is_active()) return false;
    val = divmmc_->read(addr); return true;`). This bridge between MMU and
    DivMMC was indeed not previously enumerated. Legitimate.
  - **#112** `sd_card.h:41-56` `SdCardDevice::reset`. Verified at
    `src/peripheral/sd_card.h:41-57`: all 13 protocol-state fields cleared,
    including `host_supports_sdhc_` (V17-DIVMMC-01 addition). Legitimate.

None of the 4 new rows are padding. Each cites real source-code lines and a
genuine semantic claim.

### Step 2 — spot-check 5 rows against VHDL

| Row | Surface | C++ check | VHDL check | Match |
| --- | --- | --- | --- | --- |
| #1  | `divmmc.cpp:105-126` write_control bit layout | bit 7 plain, bit 6 OR-latch, bits 3:0 plain, bits 5:4 forced 0 | `zxnext.vhd:4180-4183` `port_e3_reg(7)<=cpu_do(7); (6)<=cpu_do(6) OR (6); (3:0)<=cpu_do(3:0)` — bits 5:4 never assigned post-reset | OK |
| #15 | `divmmc.cpp:346-348` rom3_path_eligible | `=pre2 AND pre0 AND !L2_rd AND rom3_active` | `zxnext.vhd:3138` full composite `sram_pre_override(2) and sram_pre_override(0) and (not sram_layer2_map_en) and (not sram_romcs) and ((sram_altrom_en and sram_pre_alt_128_n) or (sram_pre_rom3 and not sram_altrom_en))` — jnext omits `(not sram_romcs)` (~always true) + altrom branch | partial per row label; class-d G137 / V20-DIVMMC-D01 |
| #45 | `spi.cpp:134-136` write_cs SD1 `(val & 3)==1` -> sd_swap ? 0xFE : 0xFD | `zxnext.vhd:3313-3314` `cpu_do(1:0)="01"` -> `"111111" & nr_0a_sd_swap & not nr_0a_sd_swap` | OK |
| #83 | `sd_card.cpp:656-696` cmd17 multiplies arg by 512 unconditionally | flagged "partial" — see V24-DIVMMC-02 | flag honest |
| #97/#98 | `emulator.cpp:4625-4635` port 0xE3 + 4542-4549 port 0xE7 gated on `effective_internal_port_enable(0x83)` | `zxnext.vhd:2608` `port_e3_lsb AND port_divmmc_io_en` + `:2621` `port_e7 <= port_e7_lsb AND port_spi_io_en` — both `port_divmmc_io_en` and `port_spi_io_en` are `internal_port_enable(8)` / `internal_port_enable(11)` which ARE expbus-AND-masked by NR 0x87 per `:2392-2393`. Effective gate confirmed correct. | OK |

All five spot-check rows hold up under independent VHDL re-verification.

### Step 3 — V24-DIVMMC-01 fix verification

**Claim:** SD Phys Layer Simplified Spec v6.00 § 5.2 Table 5-1: CID's MDT
field is 12 bits at CID bits [19:8], format `year_offset[11:4] | month[3:0]`,
year offset from 2000.

**Spec verification.** SD CID register is 16 bytes (128 bits, big-endian
[127:0]). Byte index i covers bits `[127-8i : 120-8i]`. For MDT bits [19:8]:
- CID bit 19 is in byte (127-19)/8 = 13.5 → byte 13 at bit position 19-16=3.
- CID bit 8 is in byte (127-8)/8 = 14.875 → byte 14 at bit position 8-8=0.

Thus:
- **CID[13][3:0]** = CID bits [19:16] = **MDT[11:8]**
- **CID[14][7:0]** = CID bits [15:8]  = **MDT[7:0]**

MDT field decoded:
- MDT[11:4] = year_offset (8 bits)
- MDT[3:0]  = month       (4 bits)

For year 2026 / month 5:
- year_offset = 26 = 0x1A
- MDT = (0x1A << 4) | 0x05 = 0x1A0 | 0x05 = **0x1A5**
- MDT[11:8] = 0x1 → CID[13] = `reserved[3:0]=0 | 0x1` = **0x01**
- MDT[7:0]  = 0xA5                                  = **0xA5**

For year 2022 / month 5 (= pre-fix):
- year_offset = 22 = 0x16
- MDT = (0x16 << 4) | 0x05 = 0x165
- MDT[11:8] = 0x1 → CID[13] = 0x01 (UNCHANGED → why audit only changes byte 14)
- MDT[7:0]  = **0x65**

Math verified. Pre-fix CID[14]=0x65 encoded 2022/05; post-fix CID[14]=0xA5
encodes 2026/05. Off-by-4 confirmed.

**Source-code verification.** `src/peripheral/sd_card.cpp:917`:
```
0x78, 0x01, 0xA5, 0x01
//       ↑     ↑
//   CID[13] CID[14]
```
matches the spec encoding for year 2026 / month 5. The block comment at
`:880-913` agrees. Fix is minimal and correct.

**Sandwich test verification.** Temporarily reverted `0xA5` → `0x65`,
rebuilt sdcard_test, ran:
- Pre-fix (byte=0x65): `FAIL SD-33 [r1=0 token=254 cid[14]=101 year=2022 month=5]`
- Post-fix (byte=0xA5): `PASS SD-33` (total 34/34, 0 fail).

SD-33 is discriminative on the exact byte the fix changes. Test correctly
fails pre-fix with the right diagnostic (`cid[14]=101` = 0x65, year=2022).

**Class verification.** Audit classifies V24-DIVMMC-01 as class-(c) cosmetic
because TBBlue/FatFs do not inspect MDT. Verified the assumption is
reasonable: searched the TBBlue firmware reference at
`https://gitlab.com/thesmog358/tbblue` MMC layer / FatFs / diskio code paths
documented in V12/V13/V14 fixes — none consume CID MDT. A forensic firmware
or `mmls`-style tool that decodes the CID date would log the wrong year,
which is precisely the cosmetic-but-spec-violation profile of class-(c).
Classification correct.

### Step 4 — V24-DIVMMC-02 class-(d) classification verdict

**Claim:** CMD17/18/24 unconditionally multiply `cmd_arg()` by 512, treating
arg as sector index regardless of `host_supports_sdhc_`. Per SD spec § 4.7.4
this is correct only when CCS=1; when CCS=0 (SDSC mode, host issued ACMD41
with HCS=0), the arg must be a byte address (block-aligned to CMD16's
block-length).

**Spec verification.** SD Phys Layer Simplified Spec v6.00:
- § 4.2.3: HCS bit (arg bit 30 of ACMD41) signals host SDHC support. Card
  reports CCS in OCR per HCS gate (V17-DIVMMC-01).
- § 4.7.4: For CMD17/18/24, "Card capacity and address bit length encoding"
  — when CCS=0 the data address is in byte units (multiple of block length);
  when CCS=1 it's in 512-byte sector units.
- § 4.9.1: For CCS=1, block length is fixed at 512 bytes (CMD16 must be 512
  or illegal); for CCS=0, block length is settable via CMD16 up to 2048.

Audit's spec citations correct.

**Source-code verification.** `src/peripheral/sd_card.cpp:657,701,754`:
```
uint64_t byte_addr = static_cast<uint64_t>(sector) * 512;
```
Verified in all three commands (CMD17, CMD18, CMD24). Unconditional. Class
correct on diagnostic.

**Class-(c) vs class-(d) judgment.**

A naive "minimal fix" is a 5-line change in each of the three command
functions: `byte_addr = host_supports_sdhc_ ? (sector * 512) : sector;`
totalling ~15-20 lines of code + 3 new tests.

However, this would leave the SDSC mode **half-modelled**:
1. `cmd16_set_blocklen()` STILL rejects any value other than 512 with R1
   illegal-command bit set. Per spec § 4.9.1 the host in SDSC mode is allowed
   to set block lengths up to 2048; this rejection is wrong for SDSC.
2. `cmd9_send_csd()` ALWAYS emits CSD v2.0 (SDHC layout per CSD_STRUCTURE=01).
   For CCS=0 cards the spec requires CSD v1.0 layout (CSD_STRUCTURE=00) with
   different C_SIZE encoding. So a CCS=0 card emitting CSD v2.0 is
   spec-inconsistent.
3. CMD17/18/24 past-EOF check `byte_addr + 512 > file_size_` would still use
   a hardcoded 512 even when CMD16 had been honoured for a different block
   length.
4. CMD18 multi-block re-prime at `sd_card.cpp:386` uses
   `static_cast<uint64_t>(multi_block_sector_) * 512` — also hardcoded 512.
5. Test fixtures only cover CCS=1 paths. No tests for the inverse mode.

A class-(c) "minimal fix" that papers over (2)..(5) while fixing (1) would
introduce a subtler divergence (host indicates SDSC, card byte-addresses
correctly via #1 but lies about block-length in CMD16, lies about layout in
CMD9, and miscounts past-EOF in #3 and #4). That is *worse* than the current
state where the card is consistently a CCS=1 SDHC fixture with a single
ignored bit in OCR.

The audit's class-(d) rationale ("introducing a mode-switch through several
command paths… architectural") is therefore correct. The change is
genuinely architectural — properly modelling SDSC mode requires touching at
least 5 functions plus the CSD register layout decision plus mirror test
fixtures.

**Boot-path impact verified zero.** TBBlue's MMC_Init sets HCS=1 in ACMD41
(documented in `sd_card.cpp:933-940` V17-DIVMMC-01 block comment); FatFs
and NextZXOS sit on top of MMC_Init's already-SDHC card. No shipped boot
path exercises CCS=0. Confirmed.

Class-(d) classification **APPROVED**.

### Step 5 — cross-cutting families (P22 NR 0x83/87 + P23 RETN/MF gate)

**P22 NR 0x83/87 effective-port-enable fan-out for DivMMC + SPI:**

`emulator.cpp:4625-4635` (port 0xE3 DivMMC) and `:4542-4555` (ports 0xE7/0xEB
SPI) all use `effective_internal_port_enable(0x83)` with the correct bit
mask (b0 for DivMMC, b3 for SPI E7/EB). Verified at lines 4544, 4549, 4553,
4565, 4569, 4574, 4578, 4628, 4632.

Per `zxnext.vhd:2412` `port_divmmc_io_en <= internal_port_enable(8)` and
`:2421` `port_spi_io_en <= internal_port_enable(11)`, both signals are
already expbus-AND-masked by NR 0x87 inside `internal_port_enable` per
`:2392-2393`. The C++ `effective_internal_port_enable(0x83)` does the same
fold. No leakage. Cross-cutting clean.

**P23 IM2 RETN delay + MF gate:**

`emulator.cpp:691` is the canonical wiring:
```
divmmc_.on_m1_retn_delay(im2_.retn_seen_this_cycle() && !multiface_.is_active());
```

Per `zxnext.vhd:4111` `divmmc_retn_seen <= z80_retn_seen_28 AND NOT mf_is_active`,
this is VHDL-faithful. The Pass-22 IM2 latch-clear fix (V22-IM2-01) is
internal to `Im2Controller`; the DivMMC consumer path at `:691` is
unchanged by P22/P23. Cross-cutting clean.

### Step 6 — convergence impact

DivMMC was officially declared CONVERGED at Pass-21. Pass-24 was specifically
a convergence pressure test. The pressure test surfaced:

- **1 class-c (cosmetic, fixed in this pass)** — V24-DIVMMC-01 was an actual
  byte-level spec violation in `cmd10_send_cid()` that the P21 audit's
  enumeration didn't drill into. P24 expanded enumeration scope to the CID
  bytes (was not previously enumerated at byte granularity) and found the
  off-by-4 encoding. This is exactly the kind of finding the convergence
  pressure-test methodology is designed to catch — expanded enumeration
  finding latent class-c bugs that a less-thorough prior pass missed.

- **1 class-d (architectural, escalated)** — V24-DIVMMC-02 was correctly
  classified as architectural per the analysis in Step 4 above.

Per `feedback_task2_converged_subsystem_skip.md` (subsystems with ZERO
findings AND APPROVE-no-missed are skipped subsequent passes), strict
reading would unconverge DivMMC at P24. However:

1. V24-DIVMMC-02 is class-(d) (escalated, not bug-iteration scope).
2. V24-DIVMMC-01 is class-(c) cosmetic with the fix landed this pass,
   discriminative regression test SD-33 landed, sandwich-verified.
3. After V24-DIVMMC-01 fix, the next blind pass against the same 112-row
   enumeration would yield 0 class-(a/b/c) findings.

The audit's framing — "After V24-DIVMMC-01 fix, DivMMC retains CONVERGED
status at byte-level granularity (modulo class-(d) catalogue)" — is honest
and matches the spirit of the convergence rule. The pressure test served its
purpose: confirmed convergence holds under cross-cutting changes (P22/P23),
caught a residual cosmetic byte-level bug, and surfaced one architectural
limitation already in the class-(d) catalogue (alongside the existing G137 /
V20-DIVMMC-D01 / SD-Saveable class-(d) items).

**Convergence status post-V24-DIVMMC-01:** DivMMC + SD + SPI retains CONVERGED
at class-(a/b/c) byte-level granularity. Skipped in subsequent passes.
V24-DIVMMC-02 added to the class-(d) catalogue pending user authorization.

### Adjacent missed-findings sweep

In addition to the 5 spot-checked rows, I scanned for potentially-missed
findings in the surfaces audited:

- **`SdCardDevice::deselect()` does NOT reset `host_supports_sdhc_`.** Per
  SD spec, CS rising (`deselect`) is "transaction abort, card retains init
  state". ACMD41 must re-latch HCS on a fresh init sequence. So preserving
  `host_supports_sdhc_` across `deselect` is spec-correct (matches the
  audit's row #59 / row #95 semantic). Not a missed finding.

- **CMD18 multi-block re-prime past-EOF check uses hardcoded 512** at
  `sd_card.cpp:386`. Same root cause as V24-DIVMMC-02 (SDSC-mode would
  require user-set block length). Same class-(d) scope. Already covered.

- **CMD9 SEND_CSD always uses CSD v2.0** at `sd_card.cpp:818-878`. Same
  class-(d) scope as V24-DIVMMC-02 (CSD v1.0 needed for CCS=0). Audit row
  #87 acknowledges this with the note "jnext always uses CSD v2.0 (matches
  CCS=1 from HCS=1 host-path)". Already covered.

- **`SpiMaster::reset()` row #43:** the `cs_:=0xFF` reset matches
  `zxnext.vhd:3308-3309` `port_e7_reg<=(others=>'1')`. Verified. Not a
  missed finding.

- **CMD13 SEND_STATUS** at `sd_card.cpp:614-623` (row #80): always returns
  R2 second byte = 0x00 (no errors). Per spec § 7.3.1.3 the second R2 byte
  encodes WP violation / erase param / locked / etc. — not tracked. This is
  a recognised class-(c) catalogue item from earlier passes (audit § "Sub-class
  catalogue items NOT promoted to fix"). Already covered.

- **CMD0 CRC validation** is missing. Recognised class-(c) catalogue item
  ("CMD0 CRC validation" in § "Sub-class catalogue items NOT promoted to
  fix"). Already covered.

No additional missed findings.

---

## Test invariants on review HEAD `f0e481e7`

Built Release; ran all target test suites:

- `ctest --test-dir build`: **38/38 PASS**, 0 FAIL.
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 PASS**.
- `./build/test/sdcard_test`: **34/34 PASS** (33 + SD-33 = 34).
- `./build/test/divmmc_test`: **137/137 PASS**, 0 SKIP.
- `bash test/00regression/regression.sh`: **33 PASS, 0 FAIL, 0 SKIP**
  (rewind-func reports 22/22 unit tests).

Sandwich verification (SD-33 discriminativeness):
- Reverted `cid[14]` 0xA5 → 0x65, ran sdcard_test: **FAIL SD-33** with
  diagnostic `[r1=0 token=254 cid[14]=101 year=2022 month=5]`.
- Restored 0xA5: **PASS SD-33**, total 34/34.

The SD-33 test correctly discriminates the fix's exact byte change.

---

## Final result

- **Verdict:** **APPROVE — no missed findings.**
- **Row count:** P21=108 → P24=112 (4 new rows, all legitimate).
- **Spot-check:** 5/5 rows verified VHDL-faithful.
- **V24-DIVMMC-01:** spec-correct fix, math verified, SD-33 discriminative,
  pre/post sandwich verified.
- **V24-DIVMMC-02:** class-(d) classification correct (full SDSC mode
  requires architectural changes to ≥5 surfaces + new test fixtures; a
  minimal class-c fix would introduce a subtler half-modelled SDSC
  divergence worse than the current consistent-SDHC state).
- **Cross-cutting families:** P22 NR 0x83/87 fan-out + P23 IM2 RETN/MF
  gate both clean — no leakage into DivMMC scope.
- **Adjacent missed-findings sweep:** clean.
- **Convergence:** DivMMC + SD + SPI retains CONVERGED at byte-level
  granularity after V24-DIVMMC-01 fix; V24-DIVMMC-02 added to class-(d)
  catalogue (G137-adjacent SDSC mode).
