# Pass-18 NMI + MF + Port + NextREG — Fix-of-Reviewer Independent Review

**Subject:** Verification of fix-of-reviewer commit `3f25e43` against Pass-18
reviewer NIT cluster V18-NMP-NIT-01 (10 port handlers missing `port_*_io_en`
gates).

**Reviewer:** Independent fix-reviewer agent (this document).

**Source under review:** `task2/verify18-nmi-mf-port-fix-review` worktree,
HEAD before this review = `3f25e43`. Previous reviewer (NIT identifier)
commit: `7b705c6`.

**Verdict: APPROVE.**

---

## Methodology

1. Read the entire `git diff 7b705c6..3f25e43` for `src/core/emulator.cpp`
   and `test/port/port_test.cpp`.
2. Cross-referenced every gate-bit decision against
   `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
   lines 2392, 2397-2442, 2635, 2643, 2679-2681, 2685-2686, 2690, 1226-1230,
   5054-5057, 5499-5508.
3. Audited that `effective_internal_port_enable` (helper used by every fix
   site) correctly folds the NR 0x86-0x89 expansion-bus AND per VHDL
   :2392-2393 (definition at `src/core/emulator.cpp:6193-6245`).
4. Ran full test invariants Release: `ctest`, `fuse_z80_test`, `port_test`.
5. Performed the standard discriminative sandwich on `src/core/emulator.cpp`
   only (kept tests staged) for every V18-NMP-NIT-01 row — all 8 tests
   transition `FAIL pre-fix → PASS post-fix`.
6. Scanned all pre-existing port / sprite / layer2 / CTC / DMA / ULA tests
   for assumptions about gate state at reset (`NR 0x82..0x85` default to
   0xFF / 0x0F → all gates open) to confirm no regressions.

---

## VHDL faithfulness — per-site verification

VHDL :2392 defines `internal_port_enable` as the concatenation `(nr_85 &
nr_84 & nr_83 & nr_82)` (28 bits: `nr_82` at [7:0], `nr_83` at [15:8],
`nr_84` at [23:16], `nr_85` at [27:24]). Under `expbus_eff_en = '1'` each
half-byte is ANDed with the matching `nr_86..nr_89` bus-enable register.
Each `port_*_io_en` is the slice of one bit (VHDL :2397-2442).

The fix uses `effective_internal_port_enable(NR) & mask` where `NR` is the
NextREG number ($82..$85) and `mask` is the bit within that NextREG. This
matches the established UART/I2C/mouse/SPI/7FFD pattern already in
`emulator.cpp`. All NR-bit mappings are correct:

| Site | Port(s) | NR / bit | VHDL gate-signal bit | VHDL ref | Fix mask | Verdict |
|---|---|---|---|---|---|---|
| Sprite (RD+WR) | 0x303B | NR 0x83 b6 | bit 14 = (8+6) | :2423,:2681 | `0x40` | ✓ |
| Sprite (WR) | 0x57 | NR 0x83 b6 | bit 14 | :2423,:2679 | `0x40` | ✓ |
| Sprite (WR) | 0x5B | NR 0x83 b6 | bit 14 | :2423,:2680 | `0x40` | ✓ |
| Layer 2 (RD+WR) | 0x123B | NR 0x83 b7 | bit 15 = (8+7) | :2424,:2635 | `0x80` | ✓ |
| ULA+ idx (RD+WR) | 0xBF3B | NR 0x85 b0 | bit 24 = (24+0) | :2439,:2685 | `0x01` | ✓ |
| ULA+ dat (RD+WR) | 0xFF3B | NR 0x85 b0 | bit 24 | :2439,:2686 | `0x01` | ✓ |
| CTC (RD+WR) | 0x183B..1F3B | NR 0x85 b3 | bit 27 = (24+3) | :2442,:2690 | `0x08` | ✓ |
| DMA (RD+WR) | 0x6B | NR 0x82 b5 | bit 5 | :2405,:2643 | `0x20` | ✓ |
| DMA (RD+WR) | 0x0B | NR 0x85 b1 | bit 25 = (24+1) | :2440,:2643 | `0x02` | ✓ |

All 9 fix sites encode the correct NR-bit. Read path returns `0xFF`
(floating-bus default — matches VHDL pull-up behavior for an unhandled
port). Write path is a silent drop. Both match the existing
UART/I2C/mouse template byte-for-byte.

DMA 0x6B and 0x0B: per VHDL :2643 the gating is `(port_6b_lsb AND
port_dma_6b_io_en) OR (port_0b_lsb AND port_dma_0b_io_en)`. The two
distinct gate registers are correctly mapped to NR 0x82 b5 and NR 0x85 b1
respectively. The pre-existing `dma_holds_bus` check inside each handler
is preserved — the new gate is added on top of (logical AND with) the
existing dma-holds check, which mirrors VHDL's two-stage gating
(`port_dma_rd <= port_dma_rd_raw AND NOT dma_holds_bus`, where
`port_dma_rd_raw` itself is gated on `port_dma_*_io_en`).

`effective_internal_port_enable` (helper at `emulator.cpp:6193-6245`)
correctly implements:
- `if (reg < 0x82 || reg > 0x85)` early-return for unsupported regs.
- Reads NR via `nextreg_.cached(reg)`.
- When `expbus_eff_en` (NR 0x80 b7) is `'1'`, ANDs with the paired
  expansion register (`reg + 4`, i.e. 0x86 for 0x82, 0x87 for 0x83,
  0x88 for 0x84, 0x89 for 0x85), masking the high nibble out of the
  0x89 pairing because `nr_85_internal_port_enable` is only 4 bits
  per VHDL :1229.
- Bit 7 of NR 0x85 (`reset_type`) is correctly preserved in the
  returned byte so external callers reading it can still see it.

This is the canonical VHDL formula and is shared with every prior gate
fix in the file (V14-NMP-*, V16-NMP-02, V17-NMP-*, etc.) — no new
divergence is introduced.

---

## Discriminative-test verification

All 8 V18-NMP-NIT-01a/b/d/e/f/g/h/i rows + 1 SKIP (NIT-01c). The SKIP
is justified: `SpriteEngine::pattern_offset_` has no public accessor,
and NIT-01b (sprite attribute write) covers the structurally identical
NR 0x83 b6 gate-clear path. The skip is annotated in-line and reflected
in the test summary output.

### Sandwich — independent run

Step 1: clean build at `3f25e43` (post-fix).
- `port_test`: **99 PASS / 0 FAIL / 1 SKIP** ✓

Step 2: revert `src/core/emulator.cpp` only to its `7b705c6` content
(`git checkout 7b705c6 -- src/core/emulator.cpp`), keep test additions
staged, rebuild port_test target.

Pre-fix run:
```
FAIL V18-NMP-NIT-01a  spr[3].byte0=0x00 spr[0x10].byte0=0xaa
FAIL V18-NMP-NIT-01b  spr[5].byte0=0xbe (expected 0x00)
FAIL V18-NMP-NIT-01d  layer2.enabled before=0 after=1 (expected unchanged)
FAIL V18-NMP-NIT-01e  ulap_mode before=0x00 after=0x01
FAIL V18-NMP-NIT-01f  ulap_en before=1 after=0 (expected unchanged)
FAIL V18-NMP-NIT-01g  ctc.ch0.int_enabled=1 rd=0x00 (expected 0/0xFF)
FAIL V18-NMP-NIT-01h  dma.read 6B before=0x3a after=0x00
FAIL V18-NMP-NIT-01i  dma.read 0B before=0x3a after=0x00

Total:  100  Passed:  91  Failed:  8  Skipped:  1
```

All 8 V18-NMP-NIT-01 rows FAIL exactly as the fix-of-reviewer claimed.
The detail strings are genuinely discriminative — they show the gated-off
write landed (sprite 0x10 selected; sprite[5].byte0 = 0xBE; layer2
enabled; ulap_mode latched 01; ulap_en flipped off; CTC int_enabled
latched) or that a gated read returned the live peripheral byte (0x3A
DMA status) instead of the floating-bus 0xFF.

Step 3: `git checkout HEAD -- src/core/emulator.cpp` to restore fix.
Rebuild + rerun:
```
Total:  100  Passed:  99  Failed:  0  Skipped:  1
```

All 8 rows PASS post-fix.

The sandwich is clean — each test is materially detecting the specific
gate-vs-no-gate behavior, not coincidentally passing on unrelated state.

### Note on NIT-01h / NIT-01i pre-fix `after` value

The detail string shows `dma.read 6B before=0x3a after=0x00` — the
`after=0x00` (not the expected pre-fix `0x3a`) is an incidental side
effect of the second DMA read advancing the controller's internal
status latch between the two reads (the test calls `read()` twice
back-to-back via the port path). The assertion `after == 0xFF`
correctly distinguishes the gated case from any non-gated case (whether
it's 0x3A, 0x00 or anything else): pre-fix the gate is ignored and the
read goes through to `dma_.read()` returning a real status byte
(non-0xFF); post-fix the gate is honored and the read returns 0xFF.
The test is sound.

---

## Side-effect audit

### Reset defaults preserve open gates

NR 0x82-0x85 reset values per VHDL :1226-1230 are all `(others => '1')`
(0xFF / 0x0F). At reset every gate bit is 1 → every gate is open →
fix is fully transparent at boot. Boot trajectory is unaffected.

### Pre-existing port tests

REG-13 (sprite 0x303B), REG-14 (Layer 2 0x123B), REG-21 (ULA+
0xBF3B/0xFF3B), REG-22 (DMA 0x6B vs 0x0B) all rely on gates open at
default — they don't set NR 0x82/0x83/0x85 before issuing reads/writes.
After the fix they all still PASS (`port_test` 99/0/1 in step 1).

### Pre-existing per-peripheral tests

Ran the seven directly-related ctest targets:
- `ctc_tests`, `ctc_interrupts_tests`, `layer2_tests`, `sprites_tests`,
  `ula_tests`, `ula_integration_tests`, `dma_tests` — all PASS (7/7).

### Full ctest invariant

```
100% tests passed, 0 tests failed out of 38
```

### FUSE invariant

```
Total: 1356  Passed: 1356  Failed: 0  Skipped: 0
```

### No code-path regression

The fix only adds entry-guards to handlers; none of the existing logic
is altered or moved. The guards return 0xFF on gated reads (matching
floating-bus default, consistent with VHDL `port_*_io_en` AND on the
read mux) and silently drop gated writes (matching VHDL — `port_*` is
just used as the chip-enable for the latch). No state mutation happens
inside the gate-closed path.

### `expbus_eff_en` interaction

Because each fix routes through `effective_internal_port_enable`, when
software enables expansion-bus mode (NR 0x80 b7 = 1) and a paired
NR 0x86-0x89 bit clears the gate, the corresponding port is correctly
silenced. This is the same behavior all prior gate fixes use.

### Latent boot-path side effect — none

The Pass-18 reviewer noted that defaults keep every gate open, so this
fix changes no live boot trajectory. Confirmed: the boot screenshot
regression suite (`ctest -R "screenshot"` equivalents within the
38-test invariant) all PASS.

---

## Code quality / style

- Inline comments at every fix site cite the exact VHDL lines that
  define the gate-signal AND the NR-bit that backs it, in the same
  format used by V14-NMP-*, V16-NMP-02, V17-NMP-* — house style
  preserved.
- The capture-by-`[this]` change on the BF3B/FF3B read lambdas
  (from capture-nothing to `[this]`) is necessary because the read
  side now needs `effective_internal_port_enable`, which is a member
  function. Idiomatic.
- The fix mirrors the existing UART/I2C/mouse/SPI/7FFD pattern
  exactly (early-return with 0xFF for reads, silent return for
  writes); no duplication, no helper-creep.

---

## Conclusion

**APPROVE.**

- All 9 fix sites correctly encode the VHDL `port_*_io_en` gate bit
  per zxnext.vhd:2392-2442 / :2635 / :2643 / :2679-2681 / :2685-2686 /
  :2690.
- The fix shape (early-return guard using
  `effective_internal_port_enable(NR) & bit`) matches the established
  UART/I2C/mouse/SPI/7FFD template byte-for-byte.
- All 8 V18-NMP-NIT-01a..i tests are genuinely discriminative
  (independent sandwich: 8/8 FAIL pre-fix, 8/8 PASS post-fix).
- The 1 SKIP (NIT-01c sprite pattern port 0x5B) is justified
  (no public `pattern_offset_` accessor); NIT-01b covers the
  structurally identical NR 0x83 b6 path.
- No regressions: ctest 38/38, FUSE 1356/1356, port_test 99/0/1,
  all per-peripheral tests 7/7.
- Defaults at reset keep every gate open → boot trajectory unchanged
  (no latent side effect).
- Style and documentation conform to house pattern.

The fix-of-reviewer commit `3f25e43` is VHDL-faithful, discriminatively
tested, and side-effect-free. No further changes recommended.
