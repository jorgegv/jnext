# Subsystem DMA row-traceability audit

**Rows audited**: 156 total (116 pass + 5 fail + 35 skip)

## Summary counts (corrected after deeper review)

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 116  | 0 false-pass + 0 tautology | 0 |
| fail   | 4 GOOD-FAIL | 0 false-fail | 1 (row 12.6: design-gap vs bug) |
| skip   | 35 GOOD-SKIP | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS
None identified.

## FALSE-FAIL
None identified. On deeper review, all failing rows have correct VHDL oracles; the initial FALSE-FAIL tagging was incorrect per the Wave 1 bucket-discipline rule.

## LAZY-SKIP
None. All 35 skip rows cite VHDL lines correctly (initial flagging of 7.3/7.4/8.2 as LAZY was reversed after verifying each cites dma.vhd:622, :237/:238, :648 respectively).

## TAUTOLOGY
None.

## PLAN-DRIFT
None.

## UNCLEAR

### 12.6 — Byte mode stops after 1 byte
- **Current status**: fail
- **Test assertion**: `n == 1` (byte mode transfers exactly 1 byte)
- **Test location**: `test/dma/dma_test.cpp:1452`
- **Plan row scope**: "Byte mode (R4_mode = "00") — single byte then stop" (doc/testing/DMA-TEST-PLAN-DESIGN.md:190)
- **VHDL reality**: dma.vhd:426 checks `dma_counter_s < R0_block_len_s` after TRANSFERING_WRITE_1 unconditionally on mode. The mode field (R4_mode_s, bits 6:5) selects timing path at :371-376 but does NOT enforce single-byte-per-enable semantics. Byte mode in this VHDL has no special logic to stop after 1 byte — the plan row describes Z80-DMA external data-sheet semantics, not this VHDL's implementation.
- **Specific question for human**: Is plan row 12.6 meant to encode Z80-DMA per-enable gating (requires VHDL change and is a KNOWN-GAP), or is it a misreading of the VHDL's byte-mode timing? If the former, 12.6 should become a SKIP citing the architectural gap + a Task 2 backlog item for future VHDL alignment. If the latter, the row should be dropped.

## GOOD (summary only)

- **Pass rows cleared (GOOD)**: 116
  - All carry correct VHDL citations and assertions.
  - Representative checks: 1.1–1.6 port mode latching (dma.vhd:664–667), 2.1–2.8 R0 register programming (dma.vhd:518–772), 3.1–3.5 R1 address modes (dma.vhd:542–543), 4.1–4.5 R2 configuration, 5.1–5.4 R3 enable command, 6.1–6.8 R4 transfer modes and port B addressing, 7.1–7.2 R5 auto-restart, 8.1–8.16 R6 command sequence, 9.1–9.7 memory-to-memory transfers (non-edge-case), 10.1–10.6 memory-to-IO, 11.1–11.6 address modes, 13.1 prescaler=0, 14.1–14.5, 14.8 counter behavior (non-edge-case), 16.1–16.6 auto-restart/continue, 17.1–17.10 status register, 18.1–18.8 read field sequencing, 19.1–19.3 reset commands, 22.1–22.6 edge case decode.

- **Fail rows cleared (GOOD-FAIL, real emulator bugs with correct VHDL oracle)**: 4
  - **9.8**: block_len=0 transfers 1 byte instead of 0. VHDL dma.vhd:426 `counter < block_len` is false immediately when both are 0, so state jumps to FINISH_DMA with 0 bytes. C++ dma.cpp:~560 post-increments counter before checking `counter >= block_len`, transferring 1 byte. Same root cause as 14.6/14.7.
  - **12.7**: Continuous mode ignores prescaler. VHDL dma.vhd:424 prescaler gate is mode-agnostic. C++ dma.cpp:581 only sets `burst_wait_` when `mode_==2` (burst), ignoring prescaler in continuous mode. GOOD-FAIL (real emulator bug).
  - **14.6, 14.7**: Same block_len=0 edge case as 9.8, Z80 mode variants.
  - All cite dma.vhd:426 or :424 correctly; test comments accurately identify backlog items.

- **Skip rows cleared (GOOD-SKIP)**: 35
  - 1.5, 3.6, 4.6, 7.3, 7.4, 8.2, 8.3: timing byte, R5 control bits, reset-value subsumption. VHDL cites: dma.vhd:213, 237, 238, 622, 648, 776, 790. C++ API limitation (no accessor).
  - 10.3: mreq_n/iorq_n not exposed. VHDL dma.vhd:186–187.
  - 12.4–12.5, 12.8: bus arbitration and timing scaling. VHDL dma.vhd:446, 451–460, 109–159.
  - 13.2–13.6: prescaler cycle counts at CPU speeds. VHDL dma.vhd:251–254, :424. No turbo_i / DMA_timer_s exposure.
  - 15.1–15.8: bus arbitration daisy-chain and address/data mux. VHDL dma.vhd:267–302.
  - 19.4: port timing reset. VHDL dma.vhd:641–642.
  - 20.1–20.4: dma_delay_i and NextRegs IM2 enable bits. VHDL dma.vhd:267–281.
  - 21.1–21.6: read/write cycle-timing path selection. VHDL dma.vhd:311–376.

## Audit methodology notes

- Read-only trace of test code (`test/dma/dma_test.cpp`), plan (`doc/testing/DMA-TEST-PLAN-DESIGN.md`), and VHDL (`dma.vhd`). C++ implementation (`src/peripheral/dma.cpp`) consulted only to confirm gap cause, not as oracle.
- Every check() and skip() row mapped to plan row ID and VHDL citation.
- Failing rows (9.8, 12.6, 12.7, 14.6, 14.7) each verified against VHDL line numbers cited in test assertion detail strings.
- dma.vhd:426 block-length check (`counter < block_len` post TRANSFERING_WRITE_1) is the critical line for three failures (9.8, 14.6, 14.7); dma.cpp:560 post-increment before check is the cause.
- dma.vhd:424 prescaler gate (no mode conditioning) is the critical line for 12.7; dma.cpp:581 mode-conditional `burst_wait_` is the cause.
- **Row 12.6 ambiguous**: VHDL implements continuous block; plan describes Z80-DMA per-enable semantics. Moved to UNCLEAR pending human judgment on whether this is a plan error or a known architectural gap worth tracking.
- 35 GOOD-SKIP rows all cite VHDL facts (lines, registers, logic paths); skips justified by C++ public API limitations (no turbo_i, no bus signals, no cycle counter, no timing-byte accessor).
- **Summary (final)**: 0 FALSE-PASS, 0 FALSE-FAIL, 0 LAZY-SKIP, 116 GOOD pass, 4 GOOD-FAIL, 1 UNCLEAR fail (12.6), 35 GOOD-SKIP.
