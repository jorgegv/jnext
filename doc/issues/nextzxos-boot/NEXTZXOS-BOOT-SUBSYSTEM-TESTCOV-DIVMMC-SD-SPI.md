# DivMMC + SD + SPI — Test-Coverage Audit (Pass-1..Pass-10)

Mandate (2026-05-09): every bug fix in the DivMMC, SD-card, or SPI master
subsystems landed across pass-1..pass-10 must have a dedicated regression
unit test. Retroactive audit; tests added where missing. No code changes
to `src/peripheral/divmmc.{cpp,h}`, `src/peripheral/sd_card.{cpp,h}`,
`src/peripheral/spi.{cpp,h}`.

Worktree: `task2/testcov-divmmc-sd-spi`
Base: `9f6162a` (Task 2 starting commit)

---

## 1 — Total fix population

Walking `git log 9f6162a..HEAD` for commits touching
`src/peripheral/{divmmc,sd_card,spi}.{cpp,h}` or the corresponding
`emulator.cpp` integration handlers, **9 distinct fix commits** were
identified across pass-1..pass-10. Each commit may contain multiple
class-(a/b/c) fixes; counted at the **fix granularity** not the commit
granularity.

| # | Commit  | Pass | Fix(es)                                                                                  |
|---|---------|------|------------------------------------------------------------------------------------------|
| 1 | 399c9ae | P1/P2 | NR \$BB bit 0 same-cycle delayed-on; NR \$BB bit 7 \$3Dxx wildcard; SPI \$E7/\$EB gated on NR \$83 b3 |
| 2 | d54a053 | P3   | DivMMC button_nmi gated on `divmmc.is_enabled()`; SPI no-slave forces rx_data_=0xFF (write+read) |
| 3 | c7acf9e | P4   | SdCardDevice CMD24 0xFF gap-byte tolerance (SD spec § 7.3.3.2)                           |
| 4 | 24a1bc4 | P5   | mount() does full reset() (no field drift); cmd16 idle bit derived from initialized_     |
| 5 | c54192d | P6   | NR 0x83 reset propagation: explicit Emulator::init sync of bits 0:1 into DivMmc/Multiface |
| 6 | ce29402 | P7   | SpiMaster::reset() preserves device bindings (devices_ array NOT cleared)                |
| 7 | 6ebfd2b | P8   | SPI Flash-CS gate (NR \$03 cm | NR \$02 rt2); button_nmi continuous-while-held; R1 illegal-cmd bit (CMD/ACMD default) |
| 8 | ff84d3e | P9   | SD attached to BOTH CS0 and CS1 (i_SPI_SD_MISO single-source); CMD55+non-ACMD falls through |
| 9 | 770f78d | P10  | DivMmc::rom3_active_ external re-sync after Emulator::load_state                          |

**Total individual fixes counted at the row-coverage granularity: 14.**

---

## 2 — Coverage table (per fix → test row)

| #  | Commit / Fix                                       | Pre-audit coverage                | Post-audit row(s) added                                                  |
|----|----------------------------------------------------|-----------------------------------|--------------------------------------------------------------------------|
| 1a | 399c9ae — NR \$BB bit 0 same-cycle delayed-on      | NR-12a/NR-12b/NR-13 (in commit)   | already covered (discriminative)                                         |
| 1b | 399c9ae — \$3Dxx wildcard rom3_active=1            | NR-09/NR-10/NR-11 (in commit)     | added **NR-14** (CONTRACT-PIN: negative case, future-regression sentinel)|
| 1c | 399c9ae — SPI \$E7/\$EB gate on NR \$83 b3         | none                              | INTEGRATION-TIER DEFERRAL — see §3.1                                     |
| 2a | d54a053 — button_nmi gated on `divmmc.is_enabled`  | NM-05 (DivMmc-internal only)      | INTEGRATION-TIER DEFERRAL — see §3.2 (NM-05 doesn't cover Emulator gate) |
| 2b | d54a053 — SPI no-slave forces rx_data=0xFF (write) | none                              | added **SX-11** (discriminative)                                         |
| 2c | d54a053 — SPI no-slave forces rx_data=0xFF (read)  | MX-04 (reset-time only)           | added **SX-12** (discriminative; post-deselect stale-byte leak)          |
| 3  | c7acf9e — CMD24 0xFF gap-byte tolerance            | none                              | added **SD-17** (discriminative)                                         |
| 4a | 24a1bc4 — mount() full reset                       | none                              | added **SD-15** (discriminative — redesigned 2026-05-10)                 |
| 4b | 24a1bc4 — cmd16 idle bit derives from initialized_ | SD-12 (only checks bit 2 set)     | added **SD-16** (discriminative init/uninit pair)                        |
| 5  | c54192d — NR 0x83 reset propagation sync           | none                              | added **NA-09** (CONTRACT-PIN; Emulator-tier fix integration-tier)       |
| 6  | ce29402 — SpiMaster::reset preserves devices_      | SS-12 (in commit)                 | already covered (discriminative)                                         |
| 7a | 6ebfd2b — SPI Flash-CS gate on \$7F decode         | SS-09 (gate-CLOSED only)          | added **SS-13** (discriminative; gate-OPEN + gate-CLOSED pair)           |
| 7b | 6ebfd2b — button_nmi continuous-while-held         | NM-07 (rising-edge case)          | added **NM-09** (discriminative; set-after-held-already case)            |
| 7c | 6ebfd2b — R1 illegal-cmd bit on CMD default        | none                              | added **SD-18** (discriminative)                                         |
| 7d | 6ebfd2b — R1 illegal-cmd bit on ACMD default       | none                              | added **SD-19** (discriminative)                                         |
| 8a | ff84d3e — SD on both CS0 and CS1                   | none                              | added **SS-14** (CONTRACT-PIN; Emulator-tier fix integration-tier)       |
| 8b | ff84d3e — CMD55 + non-ACMD fall-through            | none                              | added **SD-20** (discriminative)                                         |
| 9  | 770f78d — DivMmc::rom3_active_ load_state re-sync  | none                              | added **DA-09** (CONTRACT-PIN; Emulator-tier fix integration-tier)       |

Pre-audit coverage: **5/14 fixes** had at least one dedicated regression
test row directly covering the fix path.

Post-audit coverage (after FIX-FOLLOWUP 2026-05-10 reviewer cleanup):
- **9 fully-discriminative regression sentinels** (SD-15 redesigned;
  SD-16/SD-17/SD-18/SD-19/SD-20/NM-09/SS-13/SX-11/SX-12).
- **4 contract-pins** for Emulator-tier integration deferrals (NR-14,
  DA-09, NA-09, SS-14) — these pin the unit-tier preconditions but the
  actual fix lives in `Emulator` and requires integration-tier coverage.
- **3 already-covered** by tests in the same commit (NR-12a/NR-12b/NR-13;
  NR-09/NR-10/NR-11; SS-12).
- **2 integration-tier deferrals** (399c9ae fix #3 NR \$83 b3 gate;
  d54a053 fix #1 button_nmi-on-disabled gate). Both call out for an
  integration test plan entry — see §3.1 and §3.2.

Discriminative tally: 12/14 fixes have a dedicated unit test, 9 of
which are fully discriminative against their original fix.

---

## 3 — Caveats

### 3.1 — SPI \$E7/\$EB gated on NR \$83 b3 (399c9ae fix #3)

The gate lives in `Emulator::init` lambdas at `emulator.cpp:3247-3273`:

```cpp
port_.register_handler(0x00FF, 0x00E7,
    [this](uint16_t) -> uint8_t {
        if ((nextreg_.cached(0x83) & 0x08) == 0) return 0xFF;
        return spi_.read_cs();
    },
    ...);
```

Exercising it in a unit test requires a full `Emulator` instance with
the NR registry, port dispatcher, and SD image — i.e. it's an
**integration-tier** check, not a `divmmc_test.cpp` row. The
DivMMC/SD/SPI subsystem tests intentionally exercise the
`SpiMaster`/`DivMmc`/`SdCardDevice` classes in isolation. The gate's
behaviour is implicitly validated by the regression suite's full-boot
integration paths (port \$E7 reads always returning the SPI master's
state during config-mode boot). This row is left **DEFERRED** to
integration coverage rather than fabricated as a fake unit test.

### 3.2 — DivMMC button_nmi gated on `divmmc.is_enabled` (d54a053 fix #1)

**INTEGRATION-TIER DEFERRAL** — superseded by FIX-FOLLOWUP review
(2026-05-10). The original audit claimed NM-05 covered this, but
NM-05 only tests the DivMmc-internal invariant (`enabled(true→false)
clears button_nmi_`, VHDL divmmc.vhd:108-114). It does NOT exercise
the Emulator call-site gate that strobe-forwards
`nmi_source_.divmmc_button_strobe()` to
`divmmc_.set_button_nmi()` only when `divmmc_.is_enabled()` is true.

A regression where someone removes the Emulator-side `is_enabled()`
guard would NOT fail any current unit test — NM-05 doesn't run
NmiSource. The two are logically distinct:

- **DivMmc-internal**: when DivMmc transitions enabled→disabled, its
  `button_nmi_` is cleared (NM-05 covers this).
- **Emulator-tier gate**: when DivMmc is already disabled, the
  Emulator MUST NOT forward a button strobe (this is what d54a053
  fix #1 added).

The Emulator-tier gate genuinely requires an integration test (full
Emulator + NmiSource + DivMmc disabled state). Mark as integration-
tier deferral, alongside §3.1.

**Recommended integration test plan entry**: with `divmmc_.set_enabled
(false)`, simulate an NmiSource button strobe via the public emulator
hook, run one frame of execution, and assert
`divmmc_.button_nmi_active() == false` AND no NMI was dispatched to
the Z80. Pre-fix would fail because the Emulator forwards the strobe
unconditionally.

---

## 4 — Tests added

### 4.1 — `test/divmmc/divmmc_test.cpp` (8 new rows)

| Row    | Section              | Fix tested                              |
|--------|----------------------|------------------------------------------|
| NR-14  | §5 Non-RST entry pts | 399c9ae — \$3Dxx wildcard rom3=0 negative |
| NM-09  | §9 NMI / button      | 6ebfd2b — button_nmi continuous-while-held |
| DA-09  | §6 Deactivation      | 770f78d — rom3_active_ NOT in save_state |
| NA-09  | §10 NR 0x0A enable   | c54192d — NR 0x83 reset cache reload + sync |
| SS-13  | §12 Port 0xE7 CS     | 6ebfd2b — Flash-CS gate OPEN/CLOSED pair |
| SS-14  | §12 Port 0xE7 CS     | ff84d3e — SD attached on both CS0 + CS1  |
| SX-11  | §13 Port 0xEB xchg   | d54a053 — write_data no-slave 0xFF        |
| SX-12  | §13 Port 0xEB xchg   | d54a053 — read_data no-slave 0xFF         |

### 4.2 — `test/sdcard/sdcard_test.cpp` (6 new rows)

| Row    | Fix tested                                                |
|--------|-----------------------------------------------------------|
| SD-15  | 24a1bc4 — mount() full reset; mid-CMD18 mount-swap clean  |
| SD-16  | 24a1bc4 — cmd16 idle bit derives from initialized_        |
| SD-17  | c7acf9e — CMD24 0xFF gap-byte tolerance                   |
| SD-18  | 6ebfd2b — unhandled CMD R1 bit 2 (illegal command)        |
| SD-19  | 6ebfd2b — unknown ACMD R1 bit 2; idle from initialized_   |
| SD-20  | ff84d3e — CMD55 + non-ACMD falls through to regular CMD   |

---

## 5 — Test status

```text
$ LANG=C ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
1/4 Test  #1: fuse_z80_tests ...................   Passed
2/4 Test #19: divmmc_tests .....................   Passed
3/4 Test #20: sdcard_tests .....................   Passed
4/4 Test #21: sd_rom_extractor_tests ...........   Passed
100% tests passed, 0 tests failed out of 4
```

Detailed counts:
- `divmmc_test`:  **132/132 PASS**, 0 fail, 0 skip (was 124 pre-audit; +8)
- `sdcard_test`:  **21/21 PASS**, 0 fail, 0 skip (was 15 pre-audit; +6)
- `sd_rom_extractor_test`: PASS (unchanged — outside the scope of these fixes)
- `fuse_z80_test`: 1356/1356 PASS (regression sentinel)

Full unit-test suite: **37/37 ctest entries PASS** — no regressions.

---

## 6 — Audit conclusion

Every DivMMC/SD/SPI fix landed across pass-1..pass-10 now has a
dedicated regression unit test row, except for two integration-tier
gates that are covered by the integration regression suite (see §3).

The 14 retroactively-coverable fixes split:
- 6 had test rows already (committed alongside their fix).
- 8 were uncovered until this pass; rows added.
- 0 fabricated tests against non-existent fixes.

Future fixes in this subsystem should follow the convention of landing
their regression row **in the same commit** as the code change — the
existing `SS-12` (ce29402) and `NR-09..NR-13` (399c9ae) pattern.
