# NextZXOS boot subsystem — Pass-14 verify-audit independent review (DivMMC + SD + SPI)

**Date**: 2026-05-10
**Reviewer worktree**: `.claude/worktrees/task2-verify14-divmmc-sd-spi-reviewer`
**Reviewer branch**: `task2/verify14-divmmc-sd-spi-reviewer`
**Audit branch**: `task2/verify14-divmmc-sd-spi`
**Audit head**: `debf004` ("doc(task2-verify14-divmmc-sd-spi): pass-14 audit report — 2 findings")
**Build**: Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`)

## Verdict

**APPROVE-WITH-NITS** — Both class-(c) findings (V14-DIVMMC-01, V14-DIVMMC-02)
are spec-faithful and correctly fixed. Discriminative regression tests
(SD-26, SD-27) confirmed isolating each fix individually. One comment-only
NIT on the V14-DIVMMC-01 bit-layout mnemonic.

## Mission scope honored

- Read only the audit report (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY14-DIVMMC-SD-SPI.md`)
  and the audit's commit `debf004^..debf004`.
- Did **not** read prior pass reports under `doc/issues/nextzxos-boot/VERIFY*`.
- Used SD Physical Layer Simplified Spec § 7.3.2.6 / § 7.3.3.3 + VHDL as
  the oracle.
- No probes added. No push, no merge to main, no PR.

## Spec verification

### V14-DIVMMC-01 — CMD18 mid-stream past-EOF data error token

**Audit claim**: per SD Phys Layer Spec § 7.3.3.3 "Data Error Token", a
1-byte error token replaces the 0xFE start-of-block token when the card
cannot deliver the requested data block. The OUT_OF_RANGE-only token is
0x08.

**Verification**: confirmed against the spec. The token bit layout is
"upper nibble zero (0000), lower nibble OoR/CC/CardECC/Error". The
OUT_OF_RANGE bit is bit 3, so the OOR-only token is binary 0000_1000 =
**0x08**. The fix value is correct.

**NIT**: the comment on the fix and the SD-26 docstring describe the
layout as `0bxxx0_xxxE_CCO_R` with `O = OUT_OF_RANGE, C = CC error,
E = ERROR, R = card_ECC_failed`. The mnemonic ordering reads R as the
LSB which is **inconsistent** with the conventional reading "Out of
Range = bit 3" used to derive the value 0x08 in the same paragraph. A
clearer rendering would be `0b0000_OCcE` with `O=OoR (bit 3), C=CardECC
(bit 2), c=CC (bit 1), E=Error (bit 0)`. The fix value (0x08) is
**unaffected** — this is a documentation-only NIT.

### V14-DIVMMC-02 — CMD8 R7 byte 0 (R1 prefix) ignores `initialized_`

**Audit claim**: per SD Phys Layer Spec § 7.3.2.6 (Format R7 — Card
Interface Condition), the R7 4-byte register is preceded by an R1-format
byte. R1 reflects the live card state.

**Verification**: confirmed. SD spec § 7.3.2.6 / Table 7-9 explicitly
describes R7 as `R1 + 4-byte R7 register (cmd version + voltage range +
check pattern)`. R1 bit 0 = "in idle state" (per § 7.3.2.1) which
toggles on ACMD41 init completion. The fix `initialized_ ? 0x00 : 0x01`
is symmetric with every other R1-producing handler in `sd_card.cpp`
(CMD12, CMD13, CMD16, CMD23, CMD55, CMD58, default-illegal branch).

## Audit's commit diff (`debf004`)

Reviewed in full. The fix consists of:

| File | Lines (post-fix) | Change |
|------|------------------|--------|
| `src/peripheral/sd_card.cpp:358` | `return 0xFF;` → `return 0x08;` (with explanatory comment) | V14-DIVMMC-01 |
| `src/peripheral/sd_card.cpp:517-518` | `0x01` → `initialized_ ? 0x00 : 0x01` (with explanatory comment) | V14-DIVMMC-02 |
| `test/sdcard/sdcard_test.cpp:1294-1419` | Two new tests (SD-26, SD-27) + main() entries | Discriminative regressions |

The fixes are **minimal**, **surgical**, and **purely additive** — no
behavioural change for any non-error code path. No interface change. No
header file touched.

## Discriminative test verification

Discriminative tests performed by reverting each fix individually
(rebuild, re-run, verify FAIL, restore, verify PASS):

### V14-DIVMMC-01 revert (line 358: `return 0x08;` → `return 0xFF;`)

```
SD-26: FAIL (signal=255 [= 0xFF, pre-fix silent abort])
SD-27: PASS (V14-02 unchanged)
Total: 27/28
```

→ confirms SD-26 isolates V14-DIVMMC-01 alone.

### V14-DIVMMC-02 revert (line 517: `initialized_ ? 0x00 : 0x01` → `0x01`)

```
SD-26: PASS (V14-01 unchanged)
SD-27: FAIL (r1_post_init=1 [= pre-fix hard-coded idle])
Total: 27/28
```

→ confirms SD-27 isolates V14-DIVMMC-02 alone.

### Restored to `debf004` HEAD

```
ctest:        38/38 PASS
FUSE Z80:     1356/1356 PASS
sdcard_test:  28/28 PASS (incl. SD-26 + SD-27)
```

## Missed-case hunt (per reviewer prompt)

### Token error cases beyond past-EOF

Audit closed the past-EOF token family with V14-DIVMMC-01:
- CMD17 past-EOF: queues R1=0x40 + emits 0x08 (Pass-12 V12-DIVMMC-04). ✓
- CMD18 past-EOF on initial block: same as CMD17 (Pass-12 V12-DIVMMC-04). ✓
- CMD18 past-EOF mid-stream: now emits 0x08 (this pass V14-DIVMMC-01). ✓
- CMD24 past-EOF: queued R1=0x40, no data phase (Pass-13 V13-DIVMMC-01),
  AND mid-data-phase past-EOF emits 0x0D write-error response token
  (Pass-12 V12-DIVMMC-02). ✓

**Other token error cases checked**:
- CMD9 (SEND_CSD): single-shot, gated by `if (!initialized_) queue_r1(0x01); return;`; success path emits 0xFE + 16 CSD bytes + CRC. No sector addressing → no past-EOF. **No missed case.**
- CMD10 (SEND_CID): same shape as CMD9. **No missed case.**
- `file_.read()` failure (rare I/O fault): code does not check `file_.good()` after read — would emit 0xFE + stale data_block_. Class-(d) latent edge case (host I/O fault), out of normal-operation scope. Not a finding for this pass.

### Multi-format response tokens with R1 prefix

Audit closed the R7 R1-prefix dynamic-state issue with V14-DIVMMC-02.
Other multi-byte response tokens with R1 prefix:
- **CMD58 R3** (R1 + OCR, line 855): `static_cast<uint8_t>(initialized_ ? 0x00 : 0x01)`. Already dynamic. ✓
- **CMD13 R2** (R1 + status, line 556): `initialized_ ? 0x00 : 0x01`. Already dynamic. ✓
- **CMD12** (8 stuff bytes + NCR + R1, line 535): dynamic. ✓
- **CMD16 SET_BLOCKLEN** (R1, lines 570/580): dynamic. ✓
- **CMD23 SET_BLOCK_COUNT** (R1, line 590): dynamic. ✓
- **CMD55 APP_CMD** (R1, line 741): dynamic. ✓
- **default/illegal CMD branch** (R1, line 449): `(initialized_ ? 0x00 : 0x01) | 0x04`. Dynamic. ✓
- **CMD9 / CMD10** (R1=0x00 in success path, lines 797, 832): hard-coded
  but **correctly gated** — `if (!initialized_) queue_r1(0x01); return;`
  precedes the success path, so R1=0x00 is only reached when
  `initialized_=true`. Functionally equivalent to dynamic. ✓
- **CMD17 / CMD18** (R1=0x00 in success path, lines 628, 672): same as
  CMD9/10 — gated. ✓
- **CMD24** (R1=0x00 in success path, line 731): same — gated. ✓

**No missed case** in the R1-prefix family. CMD8 was the genuine outlier
because it was the only handler that **didn't** gate by `initialized_`
before the response (it always responds with R7 regardless of state) and
hard-coded the R1 byte. V14-DIVMMC-02 closes this.

## Test impact

### Release-mode test suite

Final state (post-discriminative-revert restore):

```
ctest --test-dir build:    38/38 PASS, 0 FAIL
fuse_z80_test:              1356/1356 PASS
sdcard_test (28 rows):      28/28 PASS, 0 FAIL, 0 SKIP
```

### Regression test note (NOT V14 related)

`bash test/00regression/regression.sh` shows `parallax-demo` failing
(44636 pixels differ). Verified by reverting V14 entirely and re-running
the same regression: parallax-demo **still fails** (32 PASS / 1 FAIL,
identical). The regression failure is **pre-existing in the integration
trunk** (introduced upstream of `73c3146`), NOT caused by V14. Out of
scope for this review.

## Code style and conformance

- Comment quality: very high. Each fix has a multi-paragraph rationale
  citing the spec section, pre-fix behaviour, asymmetry-with-other-fixes
  reasoning, and class-(c) latency justification. Excellent for future
  maintainers.
- Symmetric phrasing with prior pass fixes (V12-DIVMMC-04, V13-DIVMMC-01).
- Logging: `sd_log()->warn()` updated to mention the new error token
  emission, helping debug.
- One NIT on the `0bxxx0_xxxE_CCO_R` bit-mnemonic ordering (see above).
  This appears in two places: the V14-DIVMMC-01 fix comment in
  `sd_card.cpp:331` and the SD-26 test docstring in `sdcard_test.cpp:1301`.
  **Comment-only NIT** — does not affect behaviour or correctness.

## Findings (reviewer-promoted)

| ID                | Class | Subsystem | Disposition |
|-------------------|-------|-----------|-------------|
| V14-DIVMMC-01-NIT | NIT (comment-only) | sd_card.cpp:331 + sdcard_test.cpp:1301 | Mnemonic `0bxxx0_xxxE_CCO_R` is awkwardly ordered (R as LSB conflicts with "OoR=bit 3 → 0x08" derivation in the same paragraph). Suggest re-rendering. **Per the comment-only-skip rule (Pass-13 aggregate), this is catalogued but does NOT require a fix-reviewer roundtrip.** |

No class-(a)/(b)/(c)/(d) findings to add or contest. Audit's two
class-(c) findings stand verified.

## Summary

| Verdict | APPROVE-WITH-NITS |
|---------|-------------------|
| Findings verified | 2 (V14-DIVMMC-01, V14-DIVMMC-02) |
| Discriminative tests | 2 (SD-26 isolates V14-01, SD-27 isolates V14-02) |
| Reviewer-promoted findings | 1 NIT (comment-only) |
| Tests passed | YES (38/38 ctest, 1356/1356 FUSE, 28/28 sdcard) |
| Pre-existing regressions | parallax-demo (NOT V14-caused, verified by revert) |
| Audit head | debf004 |

Both fixes are **spec-faithful**, **discriminative-test-verified**, and
**non-regressive**. Approve.
