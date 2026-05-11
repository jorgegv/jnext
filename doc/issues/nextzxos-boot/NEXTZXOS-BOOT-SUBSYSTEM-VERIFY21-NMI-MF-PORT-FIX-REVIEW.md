# Pass-21 NMI + MF + Port + NextREG — Fix-of-Reviewer Independent Review

**Subject:** Verification of fix-of-reviewer commits `8c71ee39` (doc-only,
V21R-NMP-NIT-01), `6b3459ad` (source + tests, V21R-NMP-NIT-02) and
`b0494b33` (test rewrite, V21R-NMP-NIT-03) against the Pass-21 reviewer
(`21eedd7a`) APPROVE-WITH-NITS verdict.

**Reviewer:** Independent fix-reviewer agent (this document).

**Source under review:** `task2/verify21-nmi-mf-port-fix-review` worktree,
HEAD `b0494b33`.

**Verdict: APPROVE.**

All three NITs are VHDL-faithful:
- **NIT-01** is a doc-only correction (removes the incorrect NR 0x28
  citation from the V21-NMP-01 explanatory comment). Per
  `feedback_task2_skip_review_comment_only.md`, doc-only commits skip
  the fix-reviewer step — but I still independently verified the VHDL
  citation: VHDL line 6301-6303 is indeed the NR 0x28 keymap-select
  write handler, unrelated to `nr_palette_sub_idx`. NIT-01 is correct.
- **NIT-02** registers a no-op 0x00 read handler for the CTC alias range
  0x1C3B..0x1F3B (A10=1). Per the VHDL bus composition (zxnext.vhd:2690,
  :2796-2797, :2803-2806, :2833, :2837-2840, ctc.vhd:128-137, :164-176),
  VHDL drives 0x00 on cpu_di for these reads when CTC IO-enable is on,
  not the floating 0xFF. The mask/value choice (0xFCFF / 0x1C3B) is
  disjoint from the channel handler (0xFCFF / 0x183B) and from every
  other registered handler in the codebase. The IO-enable gate (NR 0x85
  bit 3) is correctly implemented. Test flips for NR85-03b and
  V21-NMP-02-B from 0xFF → 0x00 are legitimate VHDL-corrections, not
  test enshrinement (verified by sandwich revert).
- **NIT-03** rewrites V21-NMP-02-A from a non-discriminative
  control-word probe to a discriminative TC-write probe. The new
  sequence (control 0x07 → snapshot → TC alias write → re-read)
  correctly exercises the write-drop semantics: pre-fix yields
  `pre=0x00 post=0x42` (alias hits channel 0 in RESET_TC and mutates
  `counter_`), post-fix yields `pre=0x00 post=0x00` (alias dropped).
  Sandwich verify confirms this.

No pre-existing test or invariant regresses. The full regression suite
(33/0/0) PASSES in this pristine worktree, refuting the agent's
environmental concern from the fix-of-reviewer commit message.

One ADJACENT observation from the re-audit: no other peripheral in
the VHDL has the same 3-bit selector-vs-4-channel aliasing pattern
that CTC has (CTC is uniquely decoded with `cpu_a(15:11)="00011"` and
selector `cpu_a(10:8)`; other 3B-LSB peripherals use full 8-bit MSB
decode and exact-address handlers, so no aliasing to drop). No
follow-up needed.

---

## Methodology

1. Read `git show 8c71ee39`, `git show 6b3459ad`, `git show b0494b33`
   in full.
2. Cross-referenced every VHDL citation against
   `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
   and `.../device/ctc.vhd`.
3. Built Release (CMake, j$(nproc)).
4. Established baseline:
   - `port_test`: 103/102 PASS/0 FAIL/1 SKIP
   - `ctest --output-on-failure`: 38/38
   - `fuse_z80_test`: 1356/1356
   - `nextreg_integration_test`: 272/272
   - `bash test/00regression/regression.sh`: 33 PASS / 0 FAIL / 0 SKIP
5. Executed two sandwich reverts to confirm discriminative behaviour:
   - Sandwich-A (NIT-02 disabled, NIT-03 retained): NR85-03b + V21-NMP-02-B
     FAIL with `rd=0xFF, expected 0x00`. Confirms NIT-02's effect.
   - Sandwich-B (both NIT-02 alias handler AND NIT-03 mask reverted to
     0xF8FF): V21-NMP-02-A FAIL with `pre=0x00 post=0x42`. Confirms
     NIT-03's effect.
6. Adjacent re-audit: scanned zxnext.vhd port decoders for similar
   selector-aliasing patterns (none found).
7. Restored source. Confirmed working tree clean (`git diff` empty).

---

## VHDL faithfulness — per-NIT verification

### V21R-NMP-NIT-01 — doc-only NR 0x28 mis-citation removed

**VHDL line 6301-6303:**

The NR 0x28 write handler drives `nr_keymap_sel` and `nr_keymap_addr(8)`.
It does not touch `nr_palette_sub_idx`.

**Confirmation:** the C++ comment was previously listing NR 0x28 as a
resetter of `nine_bit_first_written_`. That citation was wrong. The
fix is a pure comment edit (no code change). Per workflow rule
`feedback_task2_skip_review_comment_only.md`, comment-only commits
skip the fix-reviewer step. I still verified the cited VHDL line is
indeed the keymap handler (not a palette reset), and the comment is
now accurate.

**Result: VHDL-faithful. No code change. Approve.**

---

### V21R-NMP-NIT-02 — CTC alias-range 0x00 readback handler

**VHDL composition** (zxnext.vhd):

```vhdl
:2690  port_ctc <= '1' when cpu_a(15 downto 11) = "00011"
                       and port_3b_lsb = '1'
                       and port_ctc_io_en = '1' else '0';
:2796  port_ctc_wr <= iowr and port_ctc;
:2797  port_ctc_rd <= iord and port_ctc;
:2803-2806  port_internal_rd_response <= ... or port_ctc_rd;
:2833  port_ctc_rd_dat <= port_ctc_dat when port_ctc_rd = '1' else X"00";
:2837-2840  port_rd_dat <= ... or port_ctc_rd_dat;
:1872-1873  elsif port_internal_rd_response = '1' ... then cpu_di <= port_rd_dat;
:4076  i_port_ctc_sel => cpu_a(10 downto 8);  -- 3-bit selector
:4067  NUM_CTC => 4;                          -- only 4 channels
```

**VHDL ctc.vhd:**

```vhdl
:128-137  sel(I) <= '1' when I = unsigned(i_port_ctc_sel) else '0';
          -- i_port_ctc_sel is 3 bits (0..7), NUM_CTC=4 (I in 0..3).
          -- When i_port_ctc_sel >= 4 (i.e. A10=1), NO I matches,
          -- so sel(0)=sel(1)=sel(2)=sel(3)='0'.
:141-146  iowr(I) <= i_port_ctc_wr and sel(I);
          -- For A10=1, iowr(0..3)=0 → channel writes are dropped.
:164-176  tmp_dout := dout(I) AND sel(I), OR-fold across I.
          -- For A10=1, every term is AND'd with 0 → tmp_dout=0x00.
:175      o_cpu_d <= tmp_dout;  -- = 0x00.
```

So for an IN at 0x1C3B..0x1F3B with `port_ctc_io_en='1'`:
- `port_ctc='1'` → `port_ctc_rd='1'` → `port_internal_rd_response='1'`
- `port_ctc_dat = ctc_do = 0x00` → `port_ctc_rd_dat = 0x00`
- `port_rd_dat = 0x00` → `cpu_di = 0x00`

When `port_ctc_io_en='0'`: `port_ctc='0'` → no read-response →
cpu_di='0xFF' (floating bus per :1877).

**Pre-fix divergence:** with V21-NMP-02's mask change 0xF8FF→0xFCFF
the alias range was unhooked from the channel handler entirely and
fell through to the floating-bus default 0xFF, which is wrong when
CTC IO-enable is on. NR85-03b expected 0xFF (the Pass-21 audit's
"no-decode" interpretation) but VHDL drives 0x00.

**Post-fix:** new handler at mask 0xFCFF / value 0x1C3B returns 0x00
when NR 0x85 bit 3 is set, 0xFF otherwise. Writes are silently
dropped (IO-enable check kept for symmetry).

**Handler-overlap check:** I enumerated all 50+ `port_.register_handler`
calls in `src/core/emulator.cpp`. The new handler matches exactly
0x1C3B, 0x1D3B, 0x1E3B, 0x1F3B. No other registered handler matches
any of these four addresses:
- LSB-only handlers (mask 0x00FF) use LSB values {0x3F, 0xBF, 0x9F,
  0xFF, 0xDF, 0x6B, 0x0B, 0xE7, 0xEB, 0xE3, 0x1F, 0x37, etc.} — none
  is 0x3B.
- Exact-port handlers (mask 0xFFFF) use values {0x123B, 0x243B, 0x253B,
  0x303B, 0x103B, 0x113B, 0x133B, 0x143B, 0x153B, 0x163B, 0xBF3B,
  0xFF3B} — none in 0x1C3B..0x1F3B.
- Channel handler (mask 0xFCFF / value 0x183B) is mutually exclusive
  with mask 0xFCFF / value 0x1C3B (differ in bit 10).
- Both handlers have 10 mask bits set — same specificity, but match
  conditions are mutually exclusive, so no dispatch ambiguity.

**Test-flip legitimacy:** the flipped expectations (NR85-03b and
V21-NMP-02-B both 0xFF → 0x00) align with the VHDL OR-fold result.
The pre-V21-NMP-02 audit assumed "no-decode = floating bus 0xFF"
which is wrong for A10=1 in the CTC range. The reviewer caught this
and the fix corrects to VHDL truth. Not test enshrinement.

**Sandwich-A verify** (alias handler removed, mask kept at 0xFCFF):

```
NR85-03b: FAIL [ctc_top=0xff expected 0x00]
V21-NMP-02-B: FAIL [rd_alias=0xff expected 0x00]
Total:  103  Passed:  100  Failed:    2  Skipped:    1
```

Exactly matches commit message claim. Discriminative.

**Result: VHDL-faithful. Discriminative test. Approve.**

---

### V21R-NMP-NIT-03 — V21-NMP-02-A rewritten as TC-write probe

**Reviewer's observation:** original V21-NMP-02-A wrote 0x87 (a control
word with bit 0 set) to alias 0x1C3B, then read `counter_` at 0x183B
pre/post. Per `src/peripheral/ctc.cpp:33-72`, a control word write
only updates the channel state machine bits and never mutates
`counter_`. `CtcChannel::read()` returns `counter_` (:142-144). So
pre == post == 0 regardless of whether the alias write reached
channel 0 — the test passed pre-fix for the wrong reason.

**Rewritten sequence:**

1. `OUT 0x183B, 0x07` → channel 0 control word (soft_reset +
   tc_follows) → state→RESET_TC, counter_=0.
2. `IN 0x183B` → snapshot counter_=0 (pre).
3. `OUT 0x1C3B, 0x42` → alias write.
   - Pre-V21-NMP-02 (mask 0xF8FF): alias hits channel 0, channel is
     in RESET_TC, write is taken as TC: `counter_=0x42`, state→RUN.
   - Post-V21R-NMP-NIT-02 (mask 0xFCFF + alias handler): write
     dropped, channel 0 stays in RESET_TC with counter_=0.
4. `IN 0x183B` → post.
   - Pre-fix: post=0x42 (FAIL: pre != post)
   - Post-fix: post=0x00 (PASS: pre == post)

**VHDL ctc_chan.vhd basis:** confirmed via `ctc.cpp` mirror that
RESET_TC + TC write sets `counter_=val` (line 38-40). The control
word 0x07 indeed transitions state→RESET_TC (line 96-107, soft_reset
+ tc_follows path). State machine model is VHDL-faithful (per G120
comment in ctc.cpp citing ctc_chan.vhd:117, :131-141).

**Sandwich-B verify** (alias handler removed + mask reverted to 0xF8FF
— "pre-fix" state from the commit message):

```
V21-NMP-02-A: FAIL [pre=0x00 post=0x42 (must be equal)]
Total:  103  Passed:  101  Failed:    1  Skipped:    1
```

Exactly matches commit message claim (`103/101/1/1`). Discriminative.

**Result: VHDL-faithful, test genuinely discriminative. Approve.**

---

## Test invariants — full Release suite

| Suite                          | Result            |
| ------------------------------ | ----------------- |
| `ctest --output-on-failure`    | 38/38 PASS        |
| `fuse_z80_test`                | 1356/1356 PASS    |
| `port_test`                    | 103/102 PASS / 0 FAIL / 1 SKIP |
| `nextreg_integration_test`     | 272/272 PASS      |
| `test/00regression/regression.sh` | 33 PASS / 0 FAIL / 0 SKIP |

The regression-suite result in this pristine worktree (33/0/0)
refutes the agent's environmental observation in the commit message
("environmental fails but invariants held"). In this fix-reviewer's
environment, the regression suite is fully green.

---

## Adjacent re-audit

The CTC-alias issue is rooted in VHDL's asymmetry: the CTC port
decoder uses `cpu_a(15:11)="00011"` (5-bit MSB) but only `NUM_CTC=4`
channels — so `cpu_a(10:8)` values 4..7 select no channel while
still asserting `port_ctc='1'` for the bus composition.

I enumerated every `port_3b_lsb`-based decoder in zxnext.vhd (lines
2625-2690):

| Port     | Decoder                                                            | Aliasing risk? |
| -------- | ------------------------------------------------------------------ | -------------- |
| 0x243B   | `port_24xx_msb AND port_3b_lsb`                                    | None (exact)   |
| 0x253B   | `port_25xx_msb AND port_3b_lsb`                                    | None (exact)   |
| 0x103B   | `port_10xx_msb AND port_3b_lsb AND port_i2c_io_en`                 | None (exact)   |
| 0x113B   | `port_11xx_msb AND port_3b_lsb AND port_i2c_io_en`                 | None (exact)   |
| 0x123B   | `port_12xx_msb AND port_3b_lsb AND port_layer2_io_en`              | None (exact)   |
| UART     | `cpu_a(15:11)="00010" AND (a10 XOR (a9 AND a8))='1' AND port_3b_lsb AND port_uart_io_en` | Range, but discrete addresses (0x133B/0x143B/0x153B/0x163B) registered exactly in emulator.cpp:4586/4595/4604/4613 |
| 0x303B   | `port_30xx_msb AND port_3b_lsb AND port_sprite_io_en`              | None (exact)   |
| 0xBF3B   | `port_bfxx_msb AND port_3b_lsb AND port_ulap_io_en`                | None (exact)   |
| 0xFF3B   | `port_ffxx_msb AND port_3b_lsb AND port_ulap_io_en`                | None (exact)   |
| **CTC**  | `cpu_a(15:11)="00011" AND port_3b_lsb AND port_ctc_io_en`          | **Yes — A10:8 aliasing for I>=4. Now correctly handled by NIT-02.** |

UART deserved a closer look: its decode covers cpu_a(10:8) values
{011, 100, 101, 110} = 0x13xx, 0x14xx, 0x15xx, 0x16xx. The remaining
in-range MSBs are 0x103B, 0x113B, 0x123B (handled by I2C/layer2 above)
and 0x173B (decoded `port_uart='0'` per the XOR formula). For 0x173B,
the bus stays floating (no decoder asserts). No analog of the CTC
alias issue.

**Conclusion: no other VHDL peripheral has the CTC's selector-vs-channel-
count asymmetry. The NIT-02 fix scope is complete. No follow-up needed.**

---

## Side-effect inspection

- **Handler dispatch:** PortDispatch uses most-specific-match-wins
  (`src/port/port_dispatch.cpp:35-66`). Channel handler (mask 0xFCFF,
  10 bits) and alias handler (mask 0xFCFF, 10 bits) have identical
  specificity but mutually exclusive match conditions. No ambiguity.
- **IO-enable gating:** alias handler reads `effective_internal_port_enable(0x85) & 0x08`,
  identical to the channel handler's gate. Symmetric and VHDL-faithful.
- **Power-on default:** NR 0x85 bit 3 defaults to 1 (per VHDL :2442
  `internal_port_enable(27)` and its reset value), so the read returns
  0x00 in a freshly-initialised emulator. Confirmed by V21R-NMP-NIT-02-A
  test (clears bit 3 → reads 0xFF).
- **Write-drop semantics:** alias handler's write callback is no-op
  when IO-enable is on (symmetric with read gate), and no-op when off.
  Per VHDL ctc.vhd:141-146 (`iowr(I)=0` when `sel(I)=0`), this is
  faithful — the four would-be channels never see the byte regardless
  of IO-enable.
- **Save/load state:** alias handler stores no state. No save/load
  surface to consider.
- **Memory safety / lambdas:** captures `[this]` only. No new fields.

No regressions or side effects detected.

---

## Verdict

**APPROVE.**

All three V21R NITs are VHDL-faithful, the test flips (NR85-03b and
V21-NMP-02-B from 0xFF→0x00) are legitimate corrections to a wrong
audit assumption (not test enshrinement), the rewritten V21-NMP-02-A
is genuinely discriminative, and no adjacent VHDL peripheral has the
same family of aliasing issue. The full regression suite passes
33/0/0 in this pristine worktree.

Final HEAD SHA: `b0494b33` (unchanged — this review adds documentation
only, no code or test diff).
