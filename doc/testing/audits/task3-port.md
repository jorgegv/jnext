# Subsystem I/O Port Dispatch row-traceability audit

**Rows audited**: 97 total (74 pass + 18 fail + 5 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 72   | 2 false-pass + 0 tautology | 0 |
| fail   | 18   | 0 false-fail | 0 |
| skip   | 5    | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 1

## FALSE-PASS (currently-passing rows with wrong oracle)

### NR82-00
- **Current status**: pass
- **Test assertion**: `(nr_read(emu, 0x82) & 0x01) == 0`
- **Test location**: `test/port/port_test.cpp:633`
- **Plan row scope**: "0x82 b0"
- **VHDL fact**: `zxnext.vhd:2397` — `port_ff_io_en <= internal_port_enable(0);`
- **C++ behaviour**: Test clears NR 0x82 bit 0 and asserts readback shows 0. No actual port 0xFF write-then-observe; only NR state readback.
- **Why it's a false-pass**: The test merely asserts NR 0x82 is writable and readable. It does NOT verify that clearing bit 0 actually gates port 0xFF. There is no Timex SCLD write or observable state change to confirm the gate is functional. The test passes because NR read/write works, not because the gate works.
- **Suggested remediation**: Convert to behavioural test: write 0xFF with bit 0 clear, observe that Timex screen mode does not change. Currently the test is a NR-readback tautology dressed up as a port-gate test.

### NR82-02
- **Current status**: pass
- **Test assertion**: `(nr_read(emu, 0x82) & 0x04) == 0`
- **Test location**: `test/port/port_test.cpp:655`
- **Plan row scope**: "0x82 b2"
- **VHDL fact**: `zxnext.vhd:2400` — `port_dffd_io_en <= internal_port_enable(2);`
- **C++ behaviour**: Test clears NR 0x82 bit 2 and asserts readback shows 0. No Pentagon 0xDFFD handler probe.
- **Why it's a false-pass**: Identical pattern to NR82-00. Test verifies NR state persistence only. No observable Pentagon extended bank write-and-observe to confirm the gate is actually active. Passes trivially because NR register store works.
- **Suggested remediation**: Convert to behavioural test: probe that OUT 0xDFFD does not affect observable state when bit 2 = 0. Requires Pentagon handler registration first.

## FALSE-FAIL
None. All 18 failing rows have legitimate VHDL grounding and correctly expose emulator gaps documented in the plan header.

## LAZY-SKIP
None. All 5 skipped rows are properly justified with forward-compatible reasons (missing API, timing-only, no debug accessor).

## TAUTOLOGY
None additionally beyond the two NR82 false-passes above.

## PLAN-DRIFT

### REG-17
- **Plan row text**: "UART 0x133B rejected"
- **Plan file location**: `IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md` (preamble notes section)
- **VHDL reality**: `zxnext.vhd:2639` — `port_uart <= '1' when cpu_a(15 downto 11) = "00011" and port_3b_lsb = '1' and port_uart_io_en = '1' else '0';` — decodes 0x133B as part of the window. Port is NOT "rejected" — it is decoded and accepted.
- **Suggested remediation**: Change plan text to "UART 0x133B accepted via TX-status port" to match VHDL line 2639 reality.
- **Does test assertion match VHDL regardless?** yes — test correctly asserts 0x133B returns non-0xFF (has a handler). Test is right; plan wording is imprecise.

## UNCLEAR
None.

## GOOD (summary only)

- **pass rows cleared (GOOD)**: 72 (NR82-00/02 reassigned to FALSE-PASS above)
- **fail rows cleared (GOOD-FAIL)**: 18 (all tracked to real emulator implementation gaps per plan header, Task 3 backlog)
- **skip rows cleared (GOOD-SKIP)**: 5

### Detailed GOOD-FAIL inventory (all 18 failing rows)

1. **LIBZ80-05** — AY 0xBFFD gating missing: NR 0x84 bit 0 enable gate for AY not implemented (VHDL :2648 + :2428).
2. **REG-09** — 0x1FFD +3 extended handler absent.
3. **REG-10** — 0xDFFD Pentagon extended handler missing (VHDL :2596).
4. **REG-18** — Kempston 1 (0x001F) read handler missing (VHDL :2674).
5. **REG-19** — Kempston 2 (0x0037) read handler missing (VHDL :2675).
6. **REG-20** — Kempston mouse (0xFADF/0xFBDF/0xFFDF) read handlers missing (VHDL :2668–2670).
7. **REG-21** — ULA+ (0xBF3B/0xFF3B) read handler missing (VHDL :2685–2686).
8. **REG-26** — 0xDF routing collision (Specdrum vs Kempston): conditional combo gate missing.
9. **REG-27** — 0xDF re-routing when mouse enabled (VHDL :2670).
10. **NR82-01** — NR 0x82 bit 1 gate for 0x7FFD absent (VHDL :2399).
11. **NR85-03b** — NR 0x85 bit 3 gates Z80 CTC (0x183B) (VHDL :2690).
12. **NR-DEF-01** — Power-on defaults: NR 0x82–0x85 should default 0xFF (VHDL :1226–1230).
13. **NR-RST-01** — Soft reset reload: NR 0x85 bit 7 = 1 must reload NR 0x82 to 0xFF (VHDL :5052–5057).
14. **NR-85-PK** — NR 0x85 packing: middle bits 4–6 must read back as 0 (VHDL :5508–5509).
15. **BUS-86-01** — NR 0x86 inert when `expbus_eff_en = 0` (VHDL :2392).
16. **PR-01** — `register_handler` must reject overlapping (mask, value) ranges (known regression per plan).
17. **AMAP-03** — NR 0x83 bit 0 gates 0xE3 DivMMC writes (VHDL :2412).
18. **BUS-02** — Disabled AY 0xFFFD read (NR 0x84 bit 0 = 0) should yield floating byte (VHDL :2428 + :2800–2840).

## Audit methodology notes

- **VHDL line citations verified**: Every VHDL reference cross-checked against actual source lines in `zxnext.vhd` (lines 2397, 2428, 2439, 2442, 2668–2690, 1226–1230, 5052–5057, 5508–5509, 2392–2393).
- **False-pass detection**: NR82-00/02 tested NR register latch persistence only, not functional gating of actual port I/O. Gate behaviour requires observable peripheral state change (write + verify), not NR readback. These are technically pass-rows with the wrong oracle — they pass because NR 0x82 storage works, not because the port-gate works.
- **All 18 fail rows**: Trace to concrete C++ implementation gaps consistent with plan header Task 3 backlog (NR 0x82–0x85 gating absent, missing handlers, soft-reset reload missing, overlapping handler collision check missing).
- **All 5 skip rows**: Properly justified (IORQ-01: CPU interrupt-ack signal not exposed; CTN-01/02: timing-only features; AMAP-01: expansion-bus freeze state debug API absent).
- **Count reconciliation**: 72 (GOOD pass) + 2 (FALSE-PASS) = 74. 18 (GOOD-FAIL) = 18. 5 (GOOD-SKIP) = 5. Total: 97 rows.
