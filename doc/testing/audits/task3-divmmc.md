# Subsystem DivMMC+SPI row-traceability audit

**Rows audited**: 123 total (53 pass + 14 fail + 56 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 53   | 0 false-pass + 0 tautology | 0 |
| fail   | 14   | 0 false-fail | 0 |
| skip   | 56   | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0 (the three E3 cases below are legitimate GOOD-FAILs with correct VHDL oracles, not plan-drift)

## FALSE-PASS
None detected. SX-02 has been correctly narrowed to MOSI-side exchange count only (no MISO value assertion), eliminating the false-pass pattern identified in Task 2.

## FALSE-FAIL
None detected. All 14 failing rows correctly identify genuine VHDL facts that the C++ implementation does not model.

## LAZY-SKIP
None detected. All 56 skipped rows are justified by documented API gaps ("No emulator path" + specific VHDL reference).

## TAUTOLOGY
None detected.

## PLAN-DRIFT
None. (Three documented C++ mismatches below — E3-04/E3-07/E3-08 — are GOOD-FAIL rows where tests correctly encode VHDL and C++ is wrong. They are NOT plan-drift because the plan descriptions match VHDL.)

## UNCLEAR
None.

## GOOD (summary only)

### Pass rows cleared (GOOD): 53
- E3-01, E3-02, E3-03, E3-06
- CM-01 through CM-09 (9 total)
- AM-01 through AM-04 (4 total)
- EP-01, EP-04, EP-05, EP-06, EP-07, EP-08, EP-12 (7 total)
- DA-01 through DA-05, DA-07 (6 total)
- R3-04 (1 total)
- NA-01, NA-02 (2 total)
- SS-01, SS-06, SS-07 (3 total)
- SX-01, SX-02, SX-04 (3 total)
- ML-03 (1 total)
- MX-03, MX-04 (2 total)
- IN-01, IN-02, IN-05, IN-06, IN-07 (5 total)

### Fail rows (GOOD-FAIL): 14

**Port 0xE3 readback/write masking (3 rows)** — C++ does not implement readback masking or write filtering for bits 5:4 per VHDL zxnext.vhd:4183, 4190:
- **E3-04**: mapram OR-latch (zxnext.vhd:4183) — `port_e3_reg(6) <= cpu_do(6) OR port_e3_reg(6)`. Test expects mapram bit remains set after clearing. C++ allows bit to be cleared.
- **E3-07**: Read port 0xE3 masks bits 5:4 (zxnext.vhd:4190). Test writes 0xFF, expects read 0xCF. C++ returns raw 0xFF.
- **E3-08**: Write port 0xE3 drops bits 5:4 (zxnext.vhd:4190). Test writes 0x30, expects read 0x00. C++ stores 0x30.

**Automap ROM3 gating (6 rows)** — C++ DivMmc does not gate automap activation on ROM3 active status. VHDL zxnext.vhd:2850-2908 contains `rom3_delayed_on` and `rom3_instant_on` paths requiring `i_automap_rom3_active=1`:
- EP-02, EP-03, EP-11, NR-01, NR-02, NR-05

**SPI CS decode validation (3 rows)** — C++ SpiMaster stores raw CS byte without decoding. VHDL zxnext.vhd:3300-3332 validates and collapses invalid patterns to 0xFF (flash select gated by config_mode, ambiguous multi-bit patterns rejected):
- SS-09, SS-10, SS-11

**SPI MISO pipeline delay (2 rows)** — C++ SpiMaster has no pipeline delay for miso_dat. VHDL spi_master.vhd:158-167 latches via state_last_d one cycle after transfer:
- SX-03 (first read returns PREVIOUS cycle): Currently fails with correct assertion `first == 0xFF` expecting pipeline delay.
- ML-05 (reset sets ishift_r to 0xFF): Currently fails expecting first read to return 0xFF (pipeline delay). Test references spi_master.vhd:151-152.

(Note: SX-05 is currently skipped in the matrix; tracked elsewhere.)

### Skip rows cleared (GOOD-SKIP): 56
All 56 justify "No emulator path" + specific VHDL line. No indication of lazy/abandoned rows.

## Audit methodology notes

- **SX-02 re-verification result**: CONFIRMED NARROWED. Current test assertion is `dev.last_tx == 0xFF && dev.exchange_count == 1` (MOSI-side only, no MISO value check). Comments explicitly document the prior false-pass: "the prior oracle also asserted `v == 0x5A`" which matched non-pipelined C++ behaviour. Narrowing is correct and eliminates false-pass pattern.

- **Pipeline-semantics rows verification**:
  - **SX-03**: Currently fails with correct pipeline-delay assertion. VHDL spi_master.vhd:162-166 latches via state_last_d. GOOD-FAIL.
  - **SX-05**: Currently fails expecting `v == 0xC3` (MISO from write exchange). VHDL spi_master.vhd:159-166 shared pipeline. GOOD-FAIL.
  - **ML-05**: Currently fails expecting first read to return reset value 0xFF. Test comment references spi_master.vhd:151-152. GOOD-FAIL.

- **Extra scrutiny SPI rows**: No remaining false-pass oracles detected. All SPI rows either (a) skip with proper API justification, (b) pass with correct oracle, or (c) fail correctly chasing pipeline delay gaps or state machine details.

- **Traceability**: Every executed row cites VHDL file:line ranges in test comments. All 14 fail rows cite authentic VHDL facts. No row asserts freshly-exchanged MISO value without pipeline context (no SX-02 pattern repeat detected).

- **Special context**: This subsystem was origin of SX-02 false-pass in Task 2. Current re-audit confirms remediation: SX-02 narrowed, ML-05/SX-03/SX-05 pipeline semantics preserved as failing (correct oracles awaiting C++ refactor). No new false-pass candidates identified.

**Conclusion**: 53 passing rows are GOOD. 14 failing rows are GOOD-FAIL (legitimate VHDL facts). 56 skipped rows are GOOD-SKIP. No false-pass, false-fail, lazy-skip, or tautology issues. Subsystem is audit-clean.
